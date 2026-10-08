#pragma once
#include "mcp_manager.hpp"
#include <shellapi.h>
#include <sstream>
#include <sddl.h>
#include <aclapi.h>

inline fs::path ownExecutable(){std::vector<wchar_t> s(32768);auto n=GetModuleFileNameW(nullptr,s.data(),(DWORD)s.size());if(!n||n>=s.size())throw std::runtime_error("Не удалось определить путь приложения.");return fs::path(std::wstring(s.data(),n));}
inline std::wstring quoteArg(const std::wstring& s){std::wstring out=L"\"";unsigned slashes=0;for(auto c:s){if(c==L'\\'){slashes++;continue;}if(c==L'\"'){out.append(slashes*2+1,L'\\');out+=c;}else{out.append(slashes,L'\\');out+=c;}slashes=0;}out.append(slashes*2,L'\\');return out+L"\"";}
inline DWORD runProcess(const fs::path& exe,const std::vector<std::wstring>& args,const fs::path& cwd={},DWORD timeout=120000){
    std::wstring cmd=quoteArg(exe.wstring());for(const auto& a:args)cmd+=L" "+quoteArg(a);STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,cwd.empty()?nullptr:cwd.c_str(),&si,&pi))throw std::runtime_error("Не удалось запустить компонент Windows.");CloseHandle(pi.hThread);
    auto wait=WaitForSingleObject(pi.hProcess,timeout);if(wait!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,1);WaitForSingleObject(pi.hProcess,5000);CloseHandle(pi.hProcess);throw std::runtime_error("Установка заняла слишком много времени.");}
    DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);return code;
}
inline void writeBytes(const fs::path& path,const std::string& data){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(data.data(),data.size());out.close();if(!out)throw std::runtime_error("Не удалось сохранить файл.");}
inline void writeJsonAtomic(const fs::path& path,const Json& j){auto tmp=path;tmp+=L".tmp";writeBytes(tmp,j.dump(2));if(!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Не удалось сохранить состояние установки.");}
inline std::wstring systemExe(const wchar_t* name){wchar_t sys[MAX_PATH];if(!GetSystemDirectoryW(sys,MAX_PATH))throw std::runtime_error("Windows system directory unavailable");return (fs::path(sys)/name).wstring();}
inline bool safeTag(const std::string& s){return !s.empty()&&s.size()<65&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c<128&&(std::isalnum(c)||c=='.'||c=='-'||c=='_');})&&s!="."&&s!="..";}
inline std::string lowerAscii(std::string s){for(auto& c:s)c=(char)std::tolower((unsigned char)c);return s;}
inline std::string checkedAsset(const Json& release,const std::string& repo,const std::string& name){
    for(const auto& a:release.at("assets"))if(a.at("name")==name){auto url=a.at("browser_download_url").get<std::string>();auto prefix="https://github.com/"+repo+"/releases/download/";
        if(lowerAscii(url.substr(0,prefix.size()))!=lowerAscii(prefix))throw std::runtime_error("Файл находится вне официального репозитория.");
        auto bytes=httpsGet(wide(url),80*1024*1024);auto digest=a.contains("digest")&&a["digest"].is_string()?a["digest"].get<std::string>():"";if(!digest.empty()&&(digest.rfind("sha256:",0)!=0||digest.substr(7)!=sha256(bytes)))throw std::runtime_error("Контрольная сумма файла не совпала с GitHub.");return bytes;
    }throw std::runtime_error("В релизе нет подходящего файла для Windows.");
}
inline Json getRelease(const std::string& repo,const std::string& tag=""){
    if(!tag.empty()&&!safeTag(tag))throw std::runtime_error("Некорректная версия.");auto j=Json::parse(httpsGet(wide("https://api.github.com/repos/"+repo+"/releases/"+(tag.empty()?"latest":"tags/"+tag)),4*1024*1024));
    if(j.value("draft",false)||j.value("prerelease",false)||!safeTag(j.at("tag_name").get<std::string>()))throw std::runtime_error("Ожидается стабильный релиз.");return j;
}
inline void extractZip(const fs::path& zip,const fs::path& destination){
    const char* script=R"PS(param([string]$Archive,[string]$Destination)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root=[IO.Path]::GetFullPath($Destination)+[IO.Path]::DirectorySeparatorChar
[IO.Directory]::CreateDirectory($root)|Out-Null
$bundle=[IO.Compression.ZipFile]::OpenRead($Archive)
try {
 if($bundle.Entries.Count -gt 3000){throw 'Too many files'}
 [long]$total=0
 $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 foreach($entry in $bundle.Entries){
  $name=$entry.FullName.Replace('/', '\')
  if($name.Contains(':') -or $name.StartsWith('\') -or (($entry.ExternalAttributes -shr 16) -band 61440) -eq 40960){throw 'Unsafe ZIP entry'}
  $target=[IO.Path]::GetFullPath([IO.Path]::Combine($root,$name))
  if(!$target.StartsWith($root,[StringComparison]::OrdinalIgnoreCase) -or !$seen.Add($target)){throw 'Unsafe ZIP path'}
  $total+=$entry.Length
  if($total -gt 100MB){throw 'Unpacked size limit'}
  if(!$entry.Name){[IO.Directory]::CreateDirectory($target)|Out-Null;continue}
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))|Out-Null
  [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$false)
 }
} finally {$bundle.Dispose()}
)PS";
    auto file=zip.parent_path()/L"extract.ps1";writeBytes(file,script);
    if(runProcess(systemExe(L"WindowsPowerShell\\v1.0\\powershell.exe"),{L"-NoProfile",L"-NonInteractive",L"-ExecutionPolicy",L"Bypass",L"-File",file.wstring(),L"-Archive",zip.wstring(),L"-Destination",destination.wstring()})!=0)throw std::runtime_error("Не удалось распаковать релиз. Предыдущие версии сохранены.");
}
inline std::string strategyArguments(const Install& install,const Profile& profile,const fs::path& runtimeRoot={}){
    if(std::find(install.strategies.begin(),install.strategies.end(),profile.strategy)==install.strategies.end())throw std::runtime_error("Выберите существующую стратегию.");
    auto text=fileBytes(install.path/wide(profile.strategy),256*1024);std::istringstream lines(text);std::string line,args;bool captured=false;
    while(std::getline(lines,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(!captured){auto at=line.find("winws.exe\"");if(at==std::string::npos)continue;line=line.substr(at+10);captured=true;}
        while(!line.empty()&&(line.back()==' '||line.back()=='\t'))line.pop_back();bool continued=!line.empty()&&line.back()=='^';if(continued)line.pop_back();args+=line+" ";if(!continued)break;}
    if(!captured)throw std::runtime_error("Не удалось прочитать стратегию этой версии.");auto root=runtimeRoot.empty()?install.path:runtimeRoot;
    auto replace=[&](const std::string& key,const std::string& value){size_t at=0;while((at=args.find(key,at))!=std::string::npos){args.replace(at,key.size(),value);at+=value.size();}};
    replace("%BIN%",utf8((root/L"bin").wstring())+"\\");replace("%LISTS%",utf8((root/L"lists").wstring())+"\\");
    replace("^!","!");
    auto mode=gameMode(profile);if(!validGameMode(mode)||!validPorts(profile.tcpPorts)||!validPorts(profile.udpPorts))throw std::runtime_error("Некорректный диапазон портов.");replace("%GameFilterTCP%",mode=="all"||mode=="tcp"?profile.tcpPorts:"12");replace("%GameFilterUDP%",mode=="all"||mode=="udp"?profile.udpPorts:"12");replace("%GameFilter%",mode!="disabled"?profile.tcpPorts:"12");
    if(args.find('%')!=std::string::npos||args.find('^')!=std::string::npos||args.find('\n')!=std::string::npos)throw std::runtime_error("Эта стратегия использует неподдерживаемые переменные.");
    int count=0;auto tokens=CommandLineToArgvW((L"winws "+wide(args)).c_str(),&count);if(!tokens)throw std::runtime_error("Не удалось прочитать параметры.");
    std::string result;try{for(int i=1;i<count;i++){std::wstring t=tokens[i];if(t.rfind(L"--",0)!=0)throw std::runtime_error("Некорректный параметр стратегии.");result+=utf8(quoteArg(t))+" ";}}catch(...){LocalFree(tokens);throw;}LocalFree(tokens);return result;
}
inline void prepareLists(const fs::path& root,const Profile& p){
    auto lists=root/L"lists";for(auto name:{L"list-general-user.txt",L"list-exclude-user.txt"})if(!fs::exists(lists/name))writeBytes(lists/name,"domain.example.abc\n");
    if(!fs::exists(lists/L"ipset-exclude-user.txt"))writeBytes(lists/L"ipset-exclude-user.txt","203.0.113.113/32\n");
    auto ip=lists/L"ipset-all.txt",saved=lists/L"ipset-all.txt.backup";
    auto data=fs::exists(ip)?fileBytes(ip,4*1024*1024):"";bool emptyMode=data.empty()||data=="203.0.113.113/32\n"||data=="203.0.113.113/32\r\n";
    if(p.ipset=="loaded"){if(emptyMode&&fs::exists(saved))fs::copy_file(saved,ip,fs::copy_options::overwrite_existing);}
    else{if(!emptyMode)fs::copy_file(ip,saved,fs::copy_options::overwrite_existing);writeBytes(ip,p.ipset=="any"?"":"203.0.113.113/32\n");}
}
class Backend {
    fs::path root;
public:
    explicit Backend(fs::path folder=defaultConfig().parent_path()):root(fs::absolute(folder)){}
    fs::path folder()const{return root;}
    std::vector<std::string> releases(){std::vector<std::string> out;for(unsigned page=1;page<=50;page++){auto j=Json::parse(httpsGet(L"https://api.github.com/repos/Flowseal/zapret-discord-youtube/releases?per_page=100&page="+std::to_wstring(page),8*1024*1024));if(!j.is_array())throw std::runtime_error("Не удалось получить список релизов.");for(auto& r:j)if(!r.value("draft",false)&&!r.value("prerelease",false)&&safeTag(r.value("tag_name","")))out.push_back(r["tag_name"]);if(j.size()<100)break;}return out;}
    Install downloadZapret(const std::string& tag,const fs::path& previous={}){
        auto release=getRelease("Flowseal/zapret-discord-youtube",tag);auto version=release.at("tag_name").get<std::string>();auto target=root/L"versions"/wide(version);if(fs::exists(target))return inspect(target);
        auto bytes=checkedAsset(release,"Flowseal/zapret-discord-youtube","zapret-discord-youtube-"+version+".zip");
        auto stage=root/L"staging"/(L"zapret-"+std::to_wstring(GetTickCount64()));fs::create_directories(stage);writeBytes(stage/L"release.zip",bytes);extractZip(stage/L"release.zip",stage/L"unpacked");
        fs::path source=stage/L"unpacked";try{inspect(source);}catch(...){bool found=false;for(auto& e:fs::directory_iterator(source))if(e.is_directory())try{inspect(e.path());source=e.path();found=true;break;}catch(...){}if(!found)throw std::runtime_error("В релизе не найдены файлы Zapret.");}
        if(!previous.empty()&&fs::is_directory(previous/L"lists"))for(auto& e:fs::directory_iterator(previous/L"lists"))if(e.is_regular_file()&&(e.path().extension()==L".txt"||e.path().extension()==L".backup"))fs::copy_file(e.path(),source/L"lists"/e.path().filename(),fs::copy_options::overwrite_existing);
        if(!previous.empty()&&fs::is_directory(previous))for(auto& e:fs::directory_iterator(previous)){auto name=e.path().filename().wstring();if(e.is_regular_file()&&name.rfind(L"general (user-",0)==0&&e.path().extension()==L".bat")fs::copy_file(e.path(),source/e.path().filename(),fs::copy_options::overwrite_existing);}
        fs::create_directories(target.parent_path());if(!MoveFileExW(source.c_str(),target.c_str(),0))throw std::runtime_error("Не удалось установить Zapret.");writeJsonAtomic(target/L"gui-release.json",{{"tag",version},{"repository","Flowseal/zapret-discord-youtube"},{"sha256",sha256(bytes)}});return inspect(target);
    }
    void removeVersion(const Install& v){auto allowed=fs::weakly_canonical(root/L"versions"),target=fs::weakly_canonical(v.path);auto relative=target.lexically_relative(allowed);if(relative.empty()||relative==L"."||*relative.begin()==L".."||relative.has_parent_path())throw std::runtime_error("Импортированную папку можно только убрать из списка.");fs::remove_all(target);}
    Json tgState()const{auto p=root/L"telegram"/L"current.json";return fs::exists(p)?Json::parse(fileBytes(p,8192)):Json::object();}
    fs::path tgExecutable()const{auto tag=tgState().value("version","");if(!safeTag(tag))throw std::runtime_error("Telegram-прокси ещё не установлен.");return root/L"telegram"/L"versions"/wide(tag)/L"TgWsProxy_windows.exe";}
    std::string installTelegram(){auto release=getRelease("Flowseal/tg-ws-proxy");auto tag=release.at("tag_name").get<std::string>();auto target=root/L"telegram"/L"versions"/wide(tag);if(!fs::exists(target/L"TgWsProxy_windows.exe")){
        auto bytes=checkedAsset(release,"Flowseal/tg-ws-proxy","TgWsProxy_windows.exe");if(bytes.substr(0,2)!="MZ")throw std::runtime_error("Неверный формат Telegram-прокси.");writeBytes(target/L"TgWsProxy_windows.exe",bytes);
    }
    auto data=target/L"TgWsProxy_data";fs::create_directories(data);auto current=tgState();if(!current.empty()){auto previous=tgExecutable().parent_path()/L"TgWsProxy_data";if(previous!=data&&fs::exists(previous/L"config.json"))fs::copy_file(previous/L"config.json",data/L"config.json",fs::copy_options::overwrite_existing);}
    if(!fs::exists(data/L"config.json")){unsigned char random[16];if(BCryptGenRandom(nullptr,random,16,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Не удалось создать ключ прокси.");std::string secret;for(auto c:random){secret+="0123456789abcdef"[c>>4];secret+="0123456789abcdef"[c&15];}writeJsonAtomic(data/L"config.json",{{"host","127.0.0.1"},{"port",1443},{"secret",secret},{"check_updates",false},{"language","ru"}});}
    writeBytes(data/L".first_run_done_mtproto","");writeJsonAtomic(root/L"telegram"/L"current.json",{{"version",tag}});return tag;
    }
    void launchTelegram(){auto exe=tgExecutable();SHELLEXECUTEINFOW s{sizeof(s)};s.fMask=SEE_MASK_NOCLOSEPROCESS;s.lpFile=exe.c_str();s.lpParameters=L"--portable";s.nShow=SW_HIDE;if(!ShellExecuteExW(&s))throw std::runtime_error("Не удалось запустить Telegram-прокси.");if(s.hProcess)CloseHandle(s.hProcess);}
    void connectTelegram(){auto exe=tgExecutable();auto cfg=Json::parse(fileBytes(exe.parent_path()/L"TgWsProxy_data"/L"config.json",65536));auto secret=cfg.at("secret").get<std::string>();if(secret.size()!=32||secret.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos||cfg.value("host","")!="127.0.0.1")throw std::runtime_error("Некорректная конфигурация локального прокси.");auto port=cfg.value("port",1443);if(port<1||port>65535)throw std::runtime_error("Некорректный порт.");auto link=wide("tg://proxy?server=127.0.0.1&port="+std::to_string(port)+"&secret="+secret);if((INT_PTR)ShellExecuteW(nullptr,L"open",link.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)throw std::runtime_error("Установите Telegram Desktop, затем нажмите «Подключить Telegram».");}
};

struct ServiceState {bool installed=false,running=false,owned=false;std::wstring binary;};
struct EngineOperation {HANDLE handle=nullptr;explicit EngineOperation(const wchar_t* name=L"Global\\ZapretGUIEngineOperation"){handle=CreateMutexW(nullptr,FALSE,name);if(!handle)throw std::runtime_error("Не удалось открыть управление обходом.");auto wait=WaitForSingleObject(handle,0);if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED){CloseHandle(handle);handle=nullptr;throw std::runtime_error("Другая операция ещё выполняется.");}}~EngineOperation(){if(handle){ReleaseMutex(handle);CloseHandle(handle);}}};
inline ServiceState serviceState(){ServiceState s;auto scm=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);if(!scm)return s;auto service=OpenServiceW(scm,L"zapret",SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG);CloseServiceHandle(scm);if(!service)return s;s.installed=true;SERVICE_STATUS_PROCESS status{};DWORD needed=0;if(QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&status,sizeof(status),&needed))s.running=status.dwCurrentState==SERVICE_RUNNING;
    QueryServiceConfigW(service,nullptr,0,&needed);std::vector<BYTE> config(needed);if(needed&&QueryServiceConfigW(service,(QUERY_SERVICE_CONFIGW*)config.data(),needed,&needed))s.binary=((QUERY_SERVICE_CONFIGW*)config.data())->lpBinaryPathName;CloseServiceHandle(service);
    HKEY key=nullptr;if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\zapret",0,KEY_READ,&key)==ERROR_SUCCESS){DWORD owner=0,size=sizeof(owner),type=0;if(RegQueryValueExW(key,L"ZapretGUIOwner",nullptr,&type,(BYTE*)&owner,&size)==ERROR_SUCCESS&&type==REG_DWORD&&owner==1)s.owned=true;RegCloseKey(key);}return s;
}
inline fs::path serviceFolder(){PWSTR path=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles,0,nullptr,&path)))throw std::runtime_error("Program Files unavailable");fs::path p=fs::path(path)/L"ZapretGUI"/L"engine";CoTaskMemFree(path);return p;}
inline void stopService(SC_HANDLE service){SERVICE_STATUS status{};if(!ControlService(service,SERVICE_CONTROL_STOP,&status)&&GetLastError()!=ERROR_SERVICE_NOT_ACTIVE)throw std::runtime_error("Не удалось остановить службу.");auto end=GetTickCount64()+20000;do{if(!QueryServiceStatus(service,&status))throw std::runtime_error("Не удалось проверить службу.");if(status.dwCurrentState==SERVICE_STOPPED)return;Sleep(200);}while(GetTickCount64()<end);throw std::runtime_error("Служба не остановилась. Версия сохранена.");}
inline void serviceAction(const std::string& action,const fs::path& config){
    EngineOperation operation;
    auto before=serviceState();if(before.installed&&!before.owned)throw std::runtime_error("Уже есть служба Zapret, установленная другой программой. Удалите её через прежний менеджер.");
    auto scm=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_ALL_ACCESS);if(!scm)throw std::runtime_error("Windows не предоставила права администратора.");struct Cleanup{SC_HANDLE h;~Cleanup(){if(h)CloseServiceHandle(h);}}scmCleanup{scm};
    SC_HANDLE service=before.installed?OpenServiceW(scm,L"zapret",SERVICE_ALL_ACCESS):nullptr;Cleanup cleanup{service};
    if(action=="stop"||action=="remove"){if(service){stopService(service);if(action=="remove"&&!DeleteService(service))throw std::runtime_error("Не удалось удалить службу.");}return;}
    if(action!="install"&&action!="start")throw std::runtime_error("Неизвестное действие службы.");
    Store store(config);auto state=store.load();auto selected=std::find_if(state.installs.begin(),state.installs.end(),[&](auto& v){return v.id==state.profile.version;});if(selected==state.installs.end())throw std::runtime_error("Сначала установите Zapret.");
    auto root=serviceFolder();fs::create_directories(root);PSECURITY_DESCRIPTOR sd=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)",SDDL_REVISION_1,&sd,nullptr))throw std::runtime_error("Не удалось защитить папку службы.");
    BOOL present=FALSE,defaults=FALSE;PACL acl=nullptr;GetSecurityDescriptorDacl(sd,&present,&acl,&defaults);DWORD security=SetNamedSecurityInfoW((LPWSTR)root.c_str(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,acl,nullptr);LocalFree(sd);if(security!=ERROR_SUCCESS)throw std::runtime_error("Не удалось защитить файлы службы.");
    auto folderTag=safeTag(selected->id)?selected->id:"import-"+sha256(utf8(selected->path.wstring())).substr(0,12);auto destination=root/wide(folderTag+"-"+sha256(profileJson(state.profile).dump()).substr(0,12));
    auto args=strategyArguments(*selected,state.profile,destination);
    struct RestoreService {SC_HANDLE& service;const ServiceState& before;bool armed=false,committed=false;~RestoreService(){if(!armed||committed||!service)return;try{stopService(service);}catch(...){}if(before.installed&&!before.binary.empty()){if(ChangeServiceConfigW(service,SERVICE_NO_CHANGE,SERVICE_NO_CHANGE,SERVICE_NO_CHANGE,before.binary.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr,nullptr)&&before.running)StartServiceW(service,0,nullptr);}else DeleteService(service);}}restore{service,before};
    if(service)stopService(service);restore.armed=true;
    if(!fs::exists(destination)){fs::copy(selected->path,destination,fs::copy_options::recursive);}
    else{fs::copy(selected->path/L"lists",destination/L"lists",fs::copy_options::recursive|fs::copy_options::overwrite_existing);for(auto name:{L"ACTIVE_DISCORD_UDP.bin",L"ACTIVE_GAME_UDP.bin"})if(fs::exists(selected->path/L"bin"/name))fs::copy_file(selected->path/L"bin"/name,destination/L"bin"/name,fs::copy_options::overwrite_existing);}
    prepareLists(destination,state.profile);
    auto binary=quoteArg((destination/L"bin"/L"winws.exe").wstring())+L" "+wide(args);
    if(service){if(!ChangeServiceConfigW(service,SERVICE_NO_CHANGE,SERVICE_AUTO_START,SERVICE_NO_CHANGE,binary.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr,L"Zapret GUI"))throw std::runtime_error("Не удалось обновить службу.");}
    else{service=CreateServiceW(scm,L"zapret",L"Zapret GUI",SERVICE_ALL_ACCESS,SERVICE_WIN32_OWN_PROCESS,SERVICE_AUTO_START,SERVICE_ERROR_NORMAL,binary.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr);cleanup.h=service;if(!service)throw std::runtime_error("Не удалось создать службу.");}
    HKEY key=nullptr;if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\zapret",0,KEY_SET_VALUE,&key)!=ERROR_SUCCESS)throw std::runtime_error("Не удалось отметить службу GUI.");DWORD owner=1;auto marked=RegSetValueExW(key,L"ZapretGUIOwner",0,REG_DWORD,(BYTE*)&owner,sizeof(owner));RegCloseKey(key);if(marked!=ERROR_SUCCESS)throw std::runtime_error("Не удалось сохранить владельца службы.");
    bool started=StartServiceW(service,0,nullptr)||GetLastError()==ERROR_SERVICE_ALREADY_RUNNING;
    auto end=GetTickCount64()+15000;SERVICE_STATUS status{};while(started&&GetTickCount64()<end){if(!QueryServiceStatus(service,&status))break;if(status.dwCurrentState==SERVICE_RUNNING){restore.committed=true;return;}if(status.dwCurrentState==SERVICE_STOPPED)break;Sleep(200);}
    throw std::runtime_error("Новая стратегия не запустилась. GUI попытался вернуть прежнюю службу; проверьте её статус и выберите другую стратегию.");
}
