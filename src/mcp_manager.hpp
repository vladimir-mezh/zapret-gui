#pragma once
#include "store.hpp"
#include <winhttp.h>
#include <bcrypt.h>
#include <cctype>
inline std::string sha256(const std::string& data) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA-256 unavailable");
    unsigned char digest[32];bool ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(ok)ok=BCryptHashData(hash,(PUCHAR)data.data(),(ULONG)data.size(),0)>=0&&BCryptFinishHash(hash,digest,32,0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);if(!ok)throw std::runtime_error("SHA-256 failed");
    std::string output;const char* hex="0123456789abcdef";for(auto c:digest){output+=hex[c>>4];output+=hex[c&15];}return output;
}
inline bool validRepo(const std::string& repo) {
    auto slash=repo.find('/');if(slash==std::string::npos||slash==0||slash==repo.size()-1||repo.find('/',slash+1)!=std::string::npos||repo.size()>200)return false;
    auto part=[](const std::string& s){return s!="."&&s!=".."&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c<128&&(std::isalnum(c)||c=='-'||c=='_'||c=='.');});};
    return part(repo.substr(0,slash))&&part(repo.substr(slash+1));
}
inline bool validMcpVersion(const std::string& v){return !v.empty()&&v.size()<=64&&v[0]>='0'&&v[0]<='9'&&std::all_of(v.begin(),v.end(),[](unsigned char c){return c<128&&(std::isalnum(c)||c=='.'||c=='-'||c=='_');});}
inline void validateManifest(const Json& j,const std::string& binary) {
    if(j.value("name","")!="zapret-gui-mcp"||j.value("api_version",0)!=1)throw std::runtime_error("Пакет MCP несовместим с этой версией GUI.");
    auto v=j.at("version").get<std::string>();
    if(!validMcpVersion(v))throw std::runtime_error("Некорректная версия пакета MCP.");
    if(binary.empty()||binary.size()>20*1024*1024||binary.size()<2||binary.substr(0,2)!="MZ"||j.at("sha256").get<std::string>()!=sha256(binary))throw std::runtime_error("Проверка файла MCP не пройдена.");
}
inline std::string httpsGet(const std::wstring& url,size_t limit) {
    struct Handle {HINTERNET h;explicit Handle(HINTERNET value):h(value){if(!h)throw std::runtime_error("Не удалось открыть соединение.");}~Handle(){WinHttpCloseHandle(h);}};
    URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=(DWORD)-1;
    if(!WinHttpCrackUrl(url.c_str(),0,0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS)throw std::runtime_error("Ожидается HTTPS-адрес.");
    std::wstring host(parts.lpszHostName,parts.dwHostNameLength),path(parts.lpszUrlPath,parts.dwUrlPathLength);if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    Handle session(WinHttpOpen(L"ZapretGUI-MCP-Updater/0.1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0));WinHttpSetTimeouts(session.h,10000,15000,30000,30000);
    Handle connection(WinHttpConnect(session.h,host.c_str(),parts.nPort,0));Handle request(WinHttpOpenRequest(connection.h,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    if(!WinHttpSendRequest(request.h,WINHTTP_NO_ADDITIONAL_HEADERS,0,nullptr,0,0,0)||!WinHttpReceiveResponse(request.h,nullptr))throw std::runtime_error("Нет ответа GitHub. Проверьте подключение.");
    DWORD status=0,len=sizeof(status);WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&len,nullptr);
    if(status!=200)throw std::runtime_error("GitHub HTTP "+std::to_string(status)+". Проверьте репозиторий, релизы и лимит запросов.");
    std::string result;DWORD available=0,read=0;auto deadline=GetTickCount64()+120000;
    for(;;){if(!WinHttpQueryDataAvailable(request.h,&available))throw std::runtime_error("Ответ GitHub прерван.");if(!available)break;
        if(result.size()+available>limit||GetTickCount64()>deadline)throw std::runtime_error("Превышен лимит загрузки MCP.");auto offset=result.size();result.resize(offset+available);
        if(!WinHttpReadData(request.h,result.data()+offset,available,&read))throw std::runtime_error("Файл MCP не загружен.");result.resize(offset+read);}
    return result;
}
inline std::string fileBytes(const fs::path& path,size_t limit) {
    if(fs::file_size(path)>limit)throw std::runtime_error("Файл слишком большой.");std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Файл не читается.");return std::string(std::istreambuf_iterator<char>(in),{});
}
inline uint64_t processCreated(HANDLE process) {FILETIME created,exit,kernel,user;if(!GetProcessTimes(process,&created,&exit,&kernel,&user))return 0;return ((uint64_t)created.dwHighDateTime<<32)|created.dwLowDateTime;}
class McpManager {
    fs::path root;
    Json metadata() const {auto p=root/L"manager.json";if(!fs::exists(p))return {{"repository",""},{"active_version",""}};return Json::parse(fileBytes(p,16384));}
    void writeMetadata(const Json& j) {fs::create_directories(root);auto temp=root/L"manager.json.tmp";{std::ofstream out(temp,std::ios::binary);out<<j.dump(2);out.close();if(!out)throw std::runtime_error("Не удалось сохранить настройки MCP.");}
        if(!MoveFileExW(temp.c_str(),(root/L"manager.json").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Не удалось сохранить настройки MCP.");}
public:
    explicit McpManager(fs::path folder=defaultConfig().parent_path()/L"mcp"):root(fs::absolute(folder)){}
    std::string repository() const {auto value=metadata().value("repository","");return value.empty()?"vladimir-mezh/zapret-gui-mcp":value;}
    void setRepository(std::string repo){if(!repo.empty()&&!validRepo(repo))throw std::runtime_error("Введите репозиторий в формате владелец/название.");auto j=metadata();j["repository"]=repo;writeMetadata(j);}
    std::string version() const {auto v=metadata().value("active_version","");if(!v.empty()&&!validMcpVersion(v))throw std::runtime_error("Некорректная установленная версия MCP.");return v;}
    fs::path activeExecutable() const {return root/L"versions"/wide(version())/L"ZapretMCP.exe";}
    bool installed() const {auto v=version();return !v.empty()&&fs::is_regular_file(activeExecutable());}
    std::string install(const Json& manifest,const std::string& binary,const std::string& launcher="") {
        validateManifest(manifest,binary);auto v=manifest.at("version").get<std::string>();auto target=root/L"versions"/wide(v);
        if(manifest.contains("launcher_sha256")&&(launcher.substr(0,2)!="MZ"||launcher.size()>20*1024*1024||sha256(launcher)!=manifest.at("launcher_sha256").get<std::string>()))throw std::runtime_error("Проверка запускателя MCP не пройдена.");
        if(fs::exists(target)) {if(fileBytes(target/L"ZapretMCP.exe",20*1024*1024)!=binary)throw std::runtime_error("Эта версия уже установлена с другим содержимым. Выпустите новый номер версии MCP.");}
        else {
            auto stage=root/L"staging"/(std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));fs::create_directories(stage);
            {std::ofstream out(stage/L"ZapretMCP.exe",std::ios::binary);out.write(binary.data(),binary.size());out.close();if(!out)throw std::runtime_error("Не удалось записать MCP.");}
            {std::ofstream out(stage/L"mcp-manifest.json",std::ios::binary);out<<manifest.dump(2);out.close();if(!out)throw std::runtime_error("Не удалось записать манифест MCP.");}
            {std::ofstream out(stage/L"LICENSE-json.txt",std::ios::binary);out<<manifest.value("json_license","");}
            auto underRoot=[&](const fs::path& p){auto relative=fs::weakly_canonical(p).lexically_relative(fs::weakly_canonical(root));return !relative.empty()&&*relative.begin()!=L"..";};
            if(!underRoot(stage)||!underRoot(target))throw std::runtime_error("Invalid MCP installation path");
            fs::create_directories(target.parent_path());if(!MoveFileExW(stage.c_str(),target.c_str(),0))throw std::runtime_error("Не удалось установить MCP. Старая версия сохранена.");
        }
        if(!launcher.empty()&&!fs::exists(root/L"ZapretMcpLauncher.exe")){auto file=root/L"ZapretMcpLauncher.exe";std::ofstream out(file,std::ios::binary);out.write(launcher.data(),launcher.size());out.close();if(!out)throw std::runtime_error("Не удалось установить запускатель MCP.");}
        auto j=metadata();j["active_version"]=v;writeMetadata(j);return v;
    }
    std::string installFolder(const fs::path& folder){auto manifest=Json::parse(fileBytes(folder/L"mcp-manifest.json",16384));return install(manifest,fileBytes(folder/L"ZapretMCP.exe",20*1024*1024),manifest.contains("launcher_sha256")?fileBytes(folder/L"ZapretMcpLauncher.exe",20*1024*1024):"");}
    std::string downloadLatest(const std::string& repo) {
        if(!validRepo(repo))throw std::runtime_error("Сначала укажите отдельный GitHub-репозиторий MCP.");
        auto release=Json::parse(httpsGet(wide("https://api.github.com/repos/"+repo+"/releases/latest"),2*1024*1024));
        auto asset=[&](const std::string& name){for(const auto& a:release.at("assets"))if(a.at("name")==name){auto u=a.at("browser_download_url").get<std::string>();
            auto prefix="https://github.com/"+repo+"/releases/download/";auto lower=[](std::string s){for(auto& c:s)c=(char)std::tolower((unsigned char)c);return s;};
            if(lower(u.substr(0,prefix.size()))!=lower(prefix))throw std::runtime_error("Адрес файла не принадлежит выбранному репозиторию.");return wide(u);}throw std::runtime_error("Релиз должен содержать ZapretMCP.exe и mcp-manifest.json.");};
        auto manifest=Json::parse(httpsGet(asset("mcp-manifest.json"),16384));auto binary=httpsGet(asset("ZapretMCP.exe"),20*1024*1024);auto launcher=manifest.contains("launcher_sha256")?httpsGet(asset("ZapretMcpLauncher.exe"),20*1024*1024):"";return install(manifest,binary,launcher);
    }
    fs::path connectionExecutable() const{return fs::is_regular_file(root/L"ZapretMcpLauncher.exe")?root/L"ZapretMcpLauncher.exe":activeExecutable();}
    Json clientConfig() const {if(!installed())throw std::runtime_error("Сначала установите MCP.");return {{"mcpServers",{{"zapret-gui",{{"command",utf8(connectionExecutable().wstring())},{"args",Json::array()}}}}}};}
    int connections() const {
        auto folder=root/L"sessions";if(!fs::exists(folder))return 0;int count=0;
        for(const auto& e:fs::directory_iterator(folder))try {auto j=Json::parse(fileBytes(e.path(),4096));DWORD pid=j.at("pid").get<DWORD>();
            auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!process)continue;
            bool alive=WaitForSingleObject(process,0)==WAIT_TIMEOUT&&processCreated(process)==j.at("created").get<uint64_t>();CloseHandle(process);if(alive)count++;
        }catch(...){}return count;
    }
};
