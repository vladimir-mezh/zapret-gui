#pragma once
#include "backend.hpp"
#include <atomic>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <tlhelp32.h>
#include <ipexport.h>
#include <icmpapi.h>

struct TestTarget {std::string name,url,group,protocol="auto";bool transfer=false;fs::path payload;};
inline bool ipv4Address(const std::string& ip,ULONG& address){std::istringstream input(ip);std::string part;unsigned count=0;address=0;while(std::getline(input,part,'.')){if(count==4||part.empty()||part.size()>3||part.find_first_not_of("0123456789")!=std::string::npos||std::stoul(part)>255)return false;address|=(ULONG)std::stoul(part)<<(8*count++);}return count==4&&!ip.empty()&&ip.back()!='.';}
inline Json probePing(const TestTarget& target){Json row={{"name",target.name},{"url",target.url},{"group","Ping"},{"protocol","icmp"},{"reachable",false},{"error",0},{"milliseconds",0}};ULONG ip=0;if(!ipv4Address(target.url,ip)){row["error"]=ERROR_INVALID_PARAMETER;return row;}auto library=LoadLibraryExW(L"iphlpapi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!library){row["error"]=GetLastError();return row;}using Open=HANDLE(WINAPI*)();using Close=BOOL(WINAPI*)(HANDLE);using Send=DWORD(WINAPI*)(HANDLE,IPAddr,LPVOID,WORD,PIP_OPTION_INFORMATION,LPVOID,DWORD,DWORD);auto open=(Open)GetProcAddress(library,"IcmpCreateFile");auto close=(Close)GetProcAddress(library,"IcmpCloseHandle");auto send=(Send)GetProcAddress(library,"IcmpSendEcho");if(open&&close&&send){auto handle=open();if(handle!=INVALID_HANDLE_VALUE){char data[]="ZapretGUI";std::vector<BYTE> reply(sizeof(ICMP_ECHO_REPLY)+sizeof(data)+8);if(send(handle,ip,data,sizeof(data),nullptr,reply.data(),(DWORD)reply.size(),1500)){auto r=(PICMP_ECHO_REPLY)reply.data();row["error"]=r->Status;row["reachable"]=r->Status==IP_SUCCESS;row["milliseconds"]=r->RoundTripTime;}else row["error"]=GetLastError();close(handle);}else row["error"]=GetLastError();}else row["error"]=ERROR_PROC_NOT_FOUND;FreeLibrary(library);return row;}
inline std::string trim(std::string s){auto first=s.find_first_not_of(" \t\r\n");return first==std::string::npos?"":s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);}
inline std::vector<TestTarget> testTargets(const fs::path& installation){
    std::vector<TestTarget> out;auto file=installation/L"utils"/L"targets.txt";
    auto add=[&](const std::string& name,const std::string& url){
        if(url.rfind("https://",0)!=0||url.size()>2048||!std::all_of(url.begin(),url.end(),[](unsigned char c){return c>32&&c<127;}))return;
        auto host=url.substr(8,url.find('/',8)==std::string::npos?std::string::npos:url.find('/',8)-8);
        if(host.empty()||host.find_first_of("@:#?\\")!=std::string::npos)return;
        auto group=host=="discord.com"||host=="gateway.discord.gg"||host=="cdn.discordapp.com"||host=="updates.discord.com"?"Discord":host=="www.youtube.com"||host=="youtube.com"||host=="youtu.be"||host=="i.ytimg.com"||host=="redirector.googlevideo.com"?"YouTube":"Other";
        if(out.size()<32&&std::none_of(out.begin(),out.end(),[&](auto& t){return t.url==url;}))out.push_back({name,url,group});
    };
    if(fs::exists(file)){std::istringstream input(fileBytes(file,65536));std::string line;while(std::getline(input,line)){line=trim(line);if(line.empty()||line[0]=='#')continue;auto eq=line.find('=');if(eq==std::string::npos)continue;auto value=trim(line.substr(eq+1));if(value.size()>=2&&value.front()=='"'&&value.back()=='"')add(trim(line.substr(0,eq)),value.substr(1,value.size()-2));}}
    if(out.empty())for(auto t:std::vector<TestTarget>{{"Discord","https://discord.com","Discord"},{"Discord Gateway","https://gateway.discord.gg","Discord"},{"YouTube","https://www.youtube.com","YouTube"},{"YouTube Images","https://i.ytimg.com","YouTube"},{"Google","https://www.google.com","Other"},{"Cloudflare","https://www.cloudflare.com","Other"}})out.push_back(t);
    return out;
}
struct HttpHandle {HINTERNET h=nullptr;~HttpHandle(){if(h)WinHttpCloseHandle(h);}};
inline Json probeTarget(const TestTarget& target){
    Json result={{"name",target.name},{"url",target.url},{"group",target.group},{"reachable",false},{"status",0},{"milliseconds",0},{"error",0}};
    auto started=GetTickCount64();DWORD error=0;auto url=wide(target.url);URL_COMPONENTS parts{sizeof(parts)};parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=(DWORD)-1;
    if(!WinHttpCrackUrl(url.c_str(),0,0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS){result["error"]=ERROR_INVALID_PARAMETER;return result;}
    HttpHandle session{WinHttpOpen(L"ZapretGUI connectivity test",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.h){result["error"]=GetLastError();return result;}
    WinHttpSetTimeouts(session.h,2000,3000,5000,5000);
    DWORD retries=0;WinHttpSetOption(session.h,WINHTTP_OPTION_CONNECT_RETRIES,&retries,sizeof(retries));
    std::wstring host(parts.lpszHostName,parts.dwHostNameLength),path(parts.lpszUrlPath,parts.dwUrlPathLength);if(path.empty())path=L"/";if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    HttpHandle connection{WinHttpConnect(session.h,host.c_str(),parts.nPort,0)};if(!connection.h){result["error"]=GetLastError();return result;}
    HttpHandle request{WinHttpOpenRequest(connection.h,L"HEAD",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE)};
    if(!request.h){result["error"]=GetLastError();return result;}
    DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects));
    DWORD disable=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;WinHttpSetOption(request.h,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof(disable));
    if(WinHttpSendRequest(request.h,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request.h,nullptr)){
        DWORD status=0,length=sizeof(status);if(WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&length,WINHTTP_NO_HEADER_INDEX)){result["status"]=status;result["reachable"]=status>=100&&status<=599;}else error=GetLastError();
    }else error=GetLastError();result["error"]=error;result["milliseconds"]=GetTickCount64()-started;return result;
}
inline Json probeTargets(const std::vector<TestTarget>& targets,const std::function<bool()>& cancelled){
    std::vector<Json> rows(targets.size());std::atomic<size_t> index{0};std::vector<std::thread> workers;
    for(unsigned n=0;n<std::min<size_t>(4,targets.size());n++)workers.emplace_back([&]{for(;;){try{if(cancelled())return;}catch(...){return;}auto i=index.fetch_add(1);if(i>=targets.size())return;try{rows[i]=probeTarget(targets[i]);}catch(...){rows[i]={{"name",targets[i].name},{"url",targets[i].url},{"group",targets[i].group},{"reachable",false},{"error",ERROR_INVALID_DATA}};}}});
    for(auto& worker:workers)worker.join();Json out=Json::array();for(auto& row:rows)if(!row.is_null())out.push_back(row);return out;
}
inline Json testScore(const Json& rows){int primary=0,ok=0,total=0;uint64_t ms=0;for(auto& row:rows){if(row.value("group","")=="Ping")continue;total++;if(row.value("reachable",false)){ok++;if(row.value("group","")!="Other")primary++;ms+=row.value("milliseconds",uint64_t{0});}}return {{"primary",primary},{"reachable",ok},{"total",total},{"milliseconds",ok?ms/ok:UINT64_MAX}};}
inline bool betterTest(const Json& candidate,const Json& previous){auto a=candidate.at("score"),b=previous.at("score");if(a.at("primary")!=b.at("primary"))return a.at("primary").get<int>()>b.at("primary").get<int>();if(a.at("reachable")!=b.at("reachable"))return a.at("reachable").get<int>()>b.at("reachable").get<int>();return a.at("milliseconds").get<uint64_t>()<b.at("milliseconds").get<uint64_t>();}
inline bool processNamed(const wchar_t* name){auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("Не удалось проверить запущенные программы.");PROCESSENTRY32W p{sizeof(p)};bool found=false;if(Process32FirstW(snapshot,&p))do{if(_wcsicmp(p.szExeFile,name)==0){found=true;break;}}while(Process32NextW(snapshot,&p));CloseHandle(snapshot);return found;}
struct PortBand {unsigned first,last;};
inline PortBand strategyPorts(size_t index){if(index>=128)throw std::runtime_error("Слишком много стратегий для одного теста.");unsigned first=32000+(unsigned)index*128;return {first,first+127};}
inline std::string isolatedFilter(PortBand ports){return "!impostor and !loopback and tcp and ((outbound and tcp.DstPort == 443 and tcp.SrcPort >= "+std::to_string(ports.first)+" and tcp.SrcPort <= "+std::to_string(ports.last)+") or (inbound and tcp.SrcPort == 443 and tcp.DstPort >= "+std::to_string(ports.first)+" and tcp.DstPort <= "+std::to_string(ports.last)+"))";}
inline std::wstring isolatedArguments(const std::string& original,PortBand ports){int count=0;auto args=CommandLineToArgvW((L"winws "+wide(original)).c_str(),&count);if(!args)throw std::runtime_error("Не удалось разобрать фильтр теста.");std::wstring out;for(int i=1;i<count;i++){std::wstring arg=args[i];if(arg.rfind(L"--wf-",0)!=0)out+=quoteArg(arg)+L" ";}LocalFree(args);return out+quoteArg(L"--wf-raw="+wide(isolatedFilter(ports)));}
struct CapturedResult {DWORD exitCode=1;std::string text;};
inline CapturedResult captureProcess(const fs::path& exe,const std::vector<std::wstring>& args,const std::function<bool()>& cancelled){
    struct Handle{HANDLE h=nullptr;~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}}read,write,nul,job,process,thread;
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};if(!CreatePipe(&read.h,&write.h,&security,0))throw std::runtime_error("Не удалось создать вывод теста.");SetHandleInformation(read.h,HANDLE_FLAG_INHERIT,0);
    nul.h=CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);if(nul.h==INVALID_HANDLE_VALUE)throw std::runtime_error("Не удалось открыть вывод теста.");
    job.h=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;if(!job.h||!SetInformationJobObject(job.h,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("Не удалось изолировать сетевой тест.");
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<BYTE> memory(bytes);auto attributes=(LPPROC_THREAD_ATTRIBUTE_LIST)memory.data();if(!InitializeProcThreadAttributeList(attributes,1,0,&bytes))throw std::runtime_error("Не удалось подготовить сетевой тест.");struct AttributeCleanup{LPPROC_THREAD_ATTRIBUTE_LIST p;~AttributeCleanup(){DeleteProcThreadAttributeList(p);}}cleanup{attributes};
    HANDLE handles[]={write.h,nul.h};if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr))throw std::runtime_error("Не удалось подготовить вывод теста.");
    STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;si.StartupInfo.hStdInput=si.StartupInfo.hStdError=nul.h;si.StartupInfo.hStdOutput=write.h;si.lpAttributeList=attributes;PROCESS_INFORMATION pi{};
    auto command=quoteArg(exe.wstring());for(auto& arg:args)command+=L" "+quoteArg(arg);
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,exe.parent_path().c_str(),&si.StartupInfo,&pi))throw std::runtime_error("Не удалось запустить системный curl.");process.h=pi.hProcess;thread.h=pi.hThread;
    if(!AssignProcessToJobObject(job.h,process.h)){TerminateProcess(process.h,1);throw std::runtime_error("Не удалось изолировать curl.");}if(ResumeThread(thread.h)==(DWORD)-1){TerminateJobObject(job.h,1);throw std::runtime_error("Не удалось начать сетевой тест.");}
    CloseHandle(write.h);write.h=nullptr;CapturedResult result;auto deadline=GetTickCount64()+10000;
    for(;;){DWORD available=0;if(PeekNamedPipe(read.h,nullptr,0,nullptr,&available,nullptr)&&available){char buffer[1024];DWORD received=0;if(ReadFile(read.h,buffer,std::min<DWORD>(available,sizeof(buffer)),&received,nullptr))result.text.append(buffer,received);if(result.text.size()>4096){TerminateJobObject(job.h,1);throw std::runtime_error("Слишком большой вывод теста.");}}
        if(WaitForSingleObject(process.h,50)==WAIT_OBJECT_0){char buffer[1024];DWORD received=0;while(ReadFile(read.h,buffer,sizeof(buffer),&received,nullptr)&&received){result.text.append(buffer,received);if(result.text.size()>4096)throw std::runtime_error("Слишком большой вывод теста.");}break;}if(cancelled()||GetTickCount64()>deadline){TerminateJobObject(job.h,1);WaitForSingleObject(process.h,5000);break;}}
    GetExitCodeProcess(process.h,&result.exitCode);return result;
}
inline std::string transferVerdict(uint64_t uploaded,uint64_t downloaded,DWORD error){return downloaded>=32768?"passed":error==28&&uploaded>0&&downloaded==0?"suspected_block":error==2||error==4?"unsupported":"inconclusive";}
inline Json probeBoundTarget(const TestTarget& target,PortBand ports,const std::function<bool()>& cancelled){
    if(target.protocol=="icmp")return probePing(target);
    auto started=GetTickCount64();Json row={{"name",target.name},{"url",target.url},{"group",target.group},{"reachable",false},{"status",0},{"milliseconds",0},{"error",0},{"local_port",0}};
    std::vector<std::wstring> args={L"--disable",L"--noproxy",L"*",L"--globoff",L"--silent",L"--http1.1",L"--connect-timeout",L"2",L"--max-time",target.transfer?L"8":L"5",L"--output",L"NUL",L"--write-out",L"%{http_code} %{local_port} %{size_upload} %{size_download}",L"--url",wide(target.url)};
    if(ports.first){args.push_back(L"--local-port");args.push_back(std::to_wstring(ports.first)+L"-"+std::to_wstring(ports.last));}if(target.protocol=="tls12"){args.push_back(L"--tlsv1.2");args.push_back(L"--tls-max");args.push_back(L"1.2");}else if(target.protocol=="tls13"){args.push_back(L"--tlsv1.3");args.push_back(L"--tls-max");args.push_back(L"1.3");}
    if(target.transfer){args.push_back(L"--request");args.push_back(L"POST");args.push_back(L"--range");args.push_back(L"0-65535");args.push_back(L"--data-binary");args.push_back(L"@"+target.payload.wstring());}else args.push_back(L"--head");
    auto capture=captureProcess(systemExe(L"curl.exe"),args,cancelled);
    unsigned status=0,local=0;uint64_t uploaded=0,downloaded=0;std::istringstream(capture.text)>>status>>local>>uploaded>>downloaded;row["status"]=status;row["local_port"]=local;row["curl_error"]=capture.exitCode;row["milliseconds"]=GetTickCount64()-started;row["protocol"]=target.protocol;
    bool isolated=!ports.first||(local>=ports.first&&local<=ports.last);row["reachable"]=capture.exitCode==0&&status>=100&&status<=599&&isolated;
    if(target.transfer){row["uploaded"]=uploaded;row["downloaded"]=downloaded;row["transfer"]=true;row["reachable"]=isolated&&downloaded>=32768;row["classification"]=isolated?transferVerdict(uploaded,downloaded,capture.exitCode):"isolation_error";}
    row["error"]=capture.exitCode==60?ERROR_WINHTTP_SECURE_FAILURE:capture.exitCode==6?ERROR_WINHTTP_NAME_NOT_RESOLVED:capture.exitCode==28?ERROR_WINHTTP_TIMEOUT:capture.exitCode==0?0:ERROR_WINHTTP_CANNOT_CONNECT;
    if(capture.exitCode==0&&!isolated)row["isolation_error"]=true;return row;
}
inline Json probeBoundTargets(const std::vector<TestTarget>& targets,PortBand ports,const std::function<bool()>& cancelled){std::vector<Json> rows(targets.size());std::atomic<size_t> index{0};std::vector<std::thread> workers;for(unsigned n=0;n<std::min<size_t>(4,targets.size());n++)workers.emplace_back([&]{for(;;){try{if(cancelled())return;}catch(...){return;}auto i=index.fetch_add(1);if(i>=targets.size())return;try{rows[i]=probeBoundTarget(targets[i],ports,cancelled);}catch(const std::exception& e){rows[i]={{"name",targets[i].name},{"url",targets[i].url},{"group",targets[i].group},{"reachable",false},{"error",ERROR_INVALID_DATA},{"detail",e.what()}};}}});for(auto& worker:workers)worker.join();Json out=Json::array();for(auto& row:rows)if(!row.is_null())out.push_back(row);return out;}
inline std::vector<TestTarget> expandedTargets(const std::vector<TestTarget>& targets){std::vector<TestTarget> out;for(auto t:targets)for(auto protocol:{"auto","tls12","tls13"}){auto row=t;row.protocol=protocol;row.name+=protocol==std::string("auto")?" · HTTPS":protocol==std::string("tls12")?" · TLS 1.2":" · TLS 1.3";out.push_back(row);}return out;}
inline std::vector<TestTarget> dpiTargets(const Json& suite,const fs::path& payload){if(!suite.is_array()||suite.empty()||suite.size()>100)throw std::runtime_error("Некорректный набор DPI-проверок.");std::vector<TestTarget> out;for(auto& entry:suite){auto host=entry.at("host").get<std::string>();if(host.empty()||host.size()>253||host.front()=='.'||host.back()=='.'||host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-")!=std::string::npos)throw std::runtime_error("Некорректный адрес DPI-проверки.");if(std::any_of(out.begin(),out.end(),[&](auto& t){return t.url=="https://"+host;}))continue;out.push_back({host,"https://"+host,"DPI","auto",true,payload});}return expandedTargets(out);}
inline std::vector<TestTarget> prepareTestTargets(const fs::path& version,const std::string& mode,const fs::path& folder){if(mode=="quick")return testTargets(version);if(!fs::is_regular_file(systemExe(L"curl.exe")))throw std::runtime_error("Для расширенных тестов нужен системный curl (Windows 10/11).");if(mode=="tls"){auto targets=expandedTargets(testTargets(version));auto file=version/L"utils"/L"targets.txt";if(fs::exists(file)){std::istringstream input(fileBytes(file,65536));std::string line;while(std::getline(input,line)){auto at=line.find("PING:");if(at==std::string::npos)continue;auto ip=trim(line.substr(at+5));if(!ip.empty()&&ip.back()=='"')ip.pop_back();ULONG parsed=0;if(ipv4Address(ip,parsed)&&targets.size()<120&&std::none_of(targets.begin(),targets.end(),[&](auto& t){return t.protocol=="icmp"&&t.url==ip;}))targets.push_back({"Ping "+ip,ip,"Ping","icmp"});}}return targets;}if(mode!="dpi")throw std::runtime_error("Неизвестная проверка.");auto suite=Json::parse(httpsGet(L"https://hyperion-cs.github.io/dpi-checkers/ru/tcp-16-20/suite.v2.json",256*1024));std::string payload(65536,'\0');if(BCryptGenRandom(nullptr,(PUCHAR)payload.data(),(ULONG)payload.size(),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Не удалось подготовить тестовые данные.");auto file=folder/L"dpi-payload.bin";writeBytes(file,payload);return dpiTargets(suite,file);}
class TestProcess {
    HANDLE process=nullptr,job=nullptr;
public:
    TestProcess(const fs::path& exe,const std::wstring& args,const fs::path& log={}){
        job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){if(job)CloseHandle(job);throw std::runtime_error("Не удалось подготовить тестовый процесс.");}
        struct Output {HANDLE handles[2]{};HANDLE out=INVALID_HANDLE_VALUE,in=INVALID_HANDLE_VALUE;std::vector<BYTE> bytes;LPPROC_THREAD_ATTRIBUTE_LIST attributes=nullptr;~Output(){if(attributes)DeleteProcThreadAttributeList(attributes);if(out!=INVALID_HANDLE_VALUE)CloseHandle(out);if(in!=INVALID_HANDLE_VALUE)CloseHandle(in);}}output;
        auto command=quoteArg(exe.wstring())+L" "+args;STARTUPINFOEXW si{};si.StartupInfo.cb=log.empty()?sizeof(STARTUPINFOW):sizeof(si);PROCESS_INFORMATION pi{};
        if(!log.empty()){SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};output.out=CreateFileW(log.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);output.in=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);output.bytes.resize(bytes);auto attributes=(LPPROC_THREAD_ATTRIBUTE_LIST)output.bytes.data();if(output.out==INVALID_HANDLE_VALUE||output.in==INVALID_HANDLE_VALUE||!InitializeProcThreadAttributeList(attributes,1,0,&bytes)){CloseHandle(job);throw std::runtime_error("Не удалось открыть журнал обхода.");}output.attributes=attributes;output.handles[0]=output.out;output.handles[1]=output.in;if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,output.handles,sizeof(output.handles),nullptr,nullptr)){CloseHandle(job);throw std::runtime_error("Не удалось подготовить журнал обхода.");}si.lpAttributeList=attributes;si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;si.StartupInfo.hStdOutput=si.StartupInfo.hStdError=output.out;si.StartupInfo.hStdInput=output.in;}
        if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,!log.empty(),CREATE_NO_WINDOW|CREATE_SUSPENDED|(log.empty()?0:EXTENDED_STARTUPINFO_PRESENT),nullptr,exe.parent_path().c_str(),&si.StartupInfo,&pi)){auto error=GetLastError();CloseHandle(job);throw std::runtime_error("Не удалось запустить тестовую стратегию (Windows "+std::to_string(error)+").");}
        if(!AssignProcessToJobObject(job,pi.hProcess)){TerminateProcess(pi.hProcess,1);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CloseHandle(job);throw std::runtime_error("Не удалось изолировать тестовый процесс.");}
        process=pi.hProcess;if(ResumeThread(pi.hThread)==(DWORD)-1){CloseHandle(pi.hThread);CloseHandle(job);WaitForSingleObject(process,5000);CloseHandle(process);throw std::runtime_error("Не удалось начать тест стратегии.");}CloseHandle(pi.hThread);
    }
    bool running()const{return WaitForSingleObject(process,0)==WAIT_TIMEOUT;}
    ~TestProcess(){if(job)CloseHandle(job);if(process){WaitForSingleObject(process,5000);CloseHandle(process);}}
};
class TestStartupGate {
    std::mutex mutex;std::condition_variable condition;size_t ready=0,total;uint64_t started=0;
public:
    explicit TestStartupGate(size_t count):total(count){}
    uint64_t arrive(const std::function<bool()>& cancelled){std::unique_lock<std::mutex> lock(mutex);if(++ready==total){started=GetTickCount64();condition.notify_all();}while(ready<total&&!cancelled())condition.wait_for(lock,std::chrono::milliseconds(100));return started;}
};
inline bool reportFlag(const Json& report,const char* key){return report.is_object()&&report.contains(key)&&report[key].is_boolean()&&report[key].get<bool>();}
inline std::wstring testReportText(const Json& report){
    if(!report.is_object()||report.empty())return L"Результатов пока нет.\r\n";
    try{std::wstring text=wide(report.value("message",""))+L"\r\n\r\n";
    auto append=[&](const Json& rows){for(auto& r:rows){text+=wide(r.value("name",""))+L": ";if(r.value("protocol","")=="icmp"){text+=r.value("reachable",false)?L"ответ ICMP · "+std::to_wstring(r.value("milliseconds",uint64_t{0}))+L" мс":L"ответа ICMP нет (это не доказывает блокировку)";}else if(r.value("transfer",false)){auto kind=r.value("classification","");text+=kind=="passed"?L"передача 32 КБ подтверждена":kind=="suspected_block"?L"тайм-аут после отправки; возможная блокировка":kind=="unsupported"?L"протокол не поддерживается":L"результат передачи не определён";text+=L" · отправлено "+std::to_wstring(r.value("uploaded",uint64_t{0}))+L" · получено "+std::to_wstring(r.value("downloaded",uint64_t{0}));}else if(r.value("reachable",false))text+=L"TLS доступен · HTTP "+std::to_wstring(r.value("status",0))+L" · "+std::to_wstring(r.value("milliseconds",uint64_t{0}))+L" мс";else if(r.value("error",0)==ERROR_WINHTTP_SECURE_FAILURE)text+=L"ошибка сертификата/TLS";else if(r.value("error",0)==ERROR_WINHTTP_NAME_NOT_RESOLVED)text+=L"ошибка DNS";else if(r.value("error",0)==ERROR_WINHTTP_TIMEOUT)text+=L"тайм-аут";else text+=L"соединение не установлено ("+std::to_wstring(r.value("error",0))+L")";text+=L"\r\n";}};
    if(report.contains("targets"))append(report["targets"]);
    if(report.contains("baseline")){text+=L"Без тестовой стратегии:\r\n";append(report["baseline"]);text+=L"\r\n";}
    if(report.contains("results"))for(auto& r:report["results"]){auto score=r.at("score");text+=wide(r.value("strategy",""))+L" · "+std::to_wstring(score.value("reachable",0))+L"/"+std::to_wstring(score.value("total",0))+L" · приоритетные цели: "+std::to_wstring(score.value("primary",0))+L"\r\n";if(r.contains("error"))text+=L"  "+wide(r["error"])+L"\r\n";if(report["results"].size()==1&&r.contains("targets"))append(r["targets"]);}
    if(report.contains("best")&&!report["best"].is_null())text+=L"\r\nРекомендация: "+wide(report["best"].get<std::string>())+L"\r\n";
    if(report.contains("confirmation")){text+=L"\r\nПовторная проверка отдельно:\r\n";append(report["confirmation"]["targets"]);}
    if(report.contains("restore_error"))text+=L"\r\nНе удалось восстановить службу: "+wide(report["restore_error"].get<std::string>())+L"\r\n";
    return text;}catch(...){return L"Отчёт повреждён или имеет неизвестный формат. Повторите проверку.\r\n";}
}
inline void protectTestFolder(const fs::path& path){fs::create_directories(path);PSECURITY_DESCRIPTOR sd=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)",SDDL_REVISION_1,&sd,nullptr))throw std::runtime_error("Не удалось защитить тестовые файлы.");BOOL present=FALSE,defaults=FALSE;PACL acl=nullptr;GetSecurityDescriptorDacl(sd,&present,&acl,&defaults);auto error=SetNamedSecurityInfoW((LPWSTR)path.c_str(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,acl,nullptr);LocalFree(sd);if(error!=ERROR_SUCCESS)throw std::runtime_error("Не удалось защитить папку тестов.");}
inline void runStrategyTests(const fs::path& config,bool all,DWORD parentId,unsigned parallel=128,const std::string& mode="quick"){
    EngineOperation operation;
    auto reportPath=config.parent_path()/L"test-report.json",cancelPath=config.parent_path()/L"test-cancel.flag";
    Json report={{"schema",1},{"kind","strategies"},{"done",false},{"cancelled",false},{"results",Json::array()},{"best",nullptr},{"message","Готовим тесты…"}};
    auto parent=OpenProcess(SYNCHRONIZE,FALSE,parentId);struct Close{HANDLE h;~Close(){if(h)CloseHandle(h);}}parentClose{parent};
    auto cancelled=[&]{return !parent||WaitForSingleObject(parent,0)!=WAIT_TIMEOUT||fs::exists(cancelPath);};
    SC_HANDLE scm=nullptr,service=nullptr;auto before=serviceState();bool changed=false;
    auto restore=[&]{if(changed&&before.running){if(!StartServiceW(service,0,nullptr)&&GetLastError()!=ERROR_SERVICE_ALREADY_RUNNING)throw std::runtime_error("Не удалось запустить прежнюю службу.");auto deadline=GetTickCount64()+15000;SERVICE_STATUS status{};do{if(!QueryServiceStatus(service,&status))break;if(status.dwCurrentState==SERVICE_RUNNING){changed=false;return;}if(status.dwCurrentState==SERVICE_STOPPED)break;Sleep(100);}while(GetTickCount64()<deadline);throw std::runtime_error("Прежняя служба не перешла в состояние запуска.");}changed=false;};
    try{
        if(before.installed&&!before.owned)throw std::runtime_error("Тесты стратегий недоступны: служба установлена другим менеджером. Проверка доступности работает без её изменения.");
        if(cancelled())throw std::runtime_error("Тесты отменены.");Store store(config);auto state=store.load();auto selected=std::find_if(state.installs.begin(),state.installs.end(),[&](auto& v){return v.id==state.profile.version;});if(selected==state.installs.end())throw std::runtime_error("Сначала установите Zapret.");auto version=*selected;auto profile=state.profile;
        report["version"]=version.id;report["revision"]=state.revision;
        auto strategies=all?version.strategies:std::vector<std::string>{profile.strategy};if(strategies.size()>128)throw std::runtime_error("В версии слишком много стратегий для теста.");bool isolated=fs::is_regular_file(systemExe(L"curl.exe"));parallel=isolated?std::min<unsigned>((unsigned)strategies.size(),std::clamp(parallel,1u,128u)):1;report["parallel"]=parallel;report["isolated_ports"]=isolated;report["mode"]=mode;
        auto root=serviceFolder()/L"tests";protectTestFolder(root);auto runtime=root/(std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        fs::copy(version.path,runtime,fs::copy_options::recursive);profile.ipset="any";prepareLists(runtime,profile);
        auto targets=prepareTestTargets(version.path,mode,runtime);
        for(auto& strategy:strategies){profile.strategy=strategy;strategyArguments(version,profile,runtime);}
        if(before.installed){scm=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);if(!scm)throw std::runtime_error("Нет доступа к службе.");service=OpenServiceW(scm,L"zapret",SERVICE_STOP|SERVICE_START|SERVICE_QUERY_STATUS);if(!service)throw std::runtime_error("Не удалось открыть службу для теста.");if(before.running){changed=true;stopService(service);}}
        if(processNamed(L"winws.exe")||processNamed(L"winws2.exe"))throw std::runtime_error("Уже запущен другой обход. Закройте его перед подбором стратегий.");
        report["message"]="Проверяем соединения без тестовой стратегии…";writeJsonAtomic(reportPath,report);report["baseline"]=isolated?probeBoundTargets(targets,strategyPorts(127),cancelled):probeTargets(targets,cancelled);if(cancelled())throw std::runtime_error("Тесты отменены.");
        for(size_t batch=0;batch<strategies.size();batch+=parallel){
            if(cancelled())break;size_t count=std::min<size_t>(parallel,strategies.size()-batch);report["message"]="Проверяем стратегии "+std::to_string(batch+1)+"–"+std::to_string(batch+count)+" из "+std::to_string(strategies.size());writeJsonAtomic(reportPath,report);
            std::vector<Json> rows(count);std::vector<std::thread> workers;TestStartupGate gate(count);
            for(size_t slot=0;slot<count;slot++)workers.emplace_back([&,slot]{auto index=batch+slot;auto current=profile;current.strategy=strategies[index];auto ports=strategyPorts(index);Json row={{"strategy",current.strategy},{"targets",Json::array()},{"score",testScore(Json::array())}};
                bool arrived=false;try{auto args=strategyArguments(version,current,runtime);TestProcess process(runtime/L"bin"/L"winws.exe",parallel>1?isolatedArguments(args,ports):wide(args));arrived=true;auto until=gate.arrive(cancelled)+4000;while(process.running()&&!cancelled()&&GetTickCount64()<until)Sleep(100);if(!cancelled()){if(!process.running())throw std::runtime_error("Стратегия завершилась до начала проверки.");row["targets"]=isolated?probeBoundTargets(targets,ports,cancelled):probeTargets(targets,cancelled);row["score"]=testScore(row["targets"]);if(!process.running())throw std::runtime_error("Тестовый процесс завершился во время проверки.");}}
                catch(const std::exception& e){if(!arrived)try{gate.arrive(cancelled);}catch(...){}row["error"]=e.what();row["score"]=testScore(Json::array());}rows[slot]=row;
            });for(auto& worker:workers)worker.join();for(auto& row:rows)report["results"].push_back(row);writeJsonAtomic(reportPath,report);
        }
        report["cancelled"]=cancelled();if(!report["cancelled"].get<bool>()){
            Json best;for(auto& row:report["results"])if(row["score"].value("primary",0)>0&&(best.is_null()||betterTest(row,best)))best=row;
            if(!best.is_null()&&parallel>1){report["message"]="Повторно проверяем лучшую стратегию отдельно…";writeJsonAtomic(reportPath,report);auto current=profile;current.strategy=best["strategy"].get<std::string>();try{TestProcess process(runtime/L"bin"/L"winws.exe",wide(strategyArguments(version,current,runtime)));auto until=GetTickCount64()+4000;while(process.running()&&!cancelled()&&GetTickCount64()<until)Sleep(100);if(!process.running())throw std::runtime_error("Победитель не запустился при повторной проверке.");auto rows=probeBoundTargets(targets,strategyPorts(126),cancelled);report["confirmation"]={{"strategy",current.strategy},{"targets",rows},{"score",testScore(rows)}};if(!process.running()||testScore(rows).value("primary",0)<best["score"].value("primary",0))best=nullptr;}catch(const std::exception& e){report["confirmation_error"]=e.what();best=nullptr;}}
            if(cancelled()){report["cancelled"]=true;report["message"]="Тесты остановлены. Частичные результаты сохранены.";}else if(!best.is_null()){report["best"]=best["strategy"];report["message"]="Тесты завершены. Можно выбрать рекомендованную стратегию.";}else report["message"]="Рабочая стратегия не подтверждена. Попробуйте тест по одной стратегии.";
        }else report["message"]="Тесты остановлены. Частичные результаты сохранены.";
    }catch(const std::exception& e){report["message"]=e.what();report["cancelled"]=cancelled();report["error"]=e.what();}
    try{restore();report["restored"]=true;}catch(const std::exception& e){report["restore_error"]=e.what();report["restored"]=false;}
    if(service)CloseServiceHandle(service);if(scm)CloseServiceHandle(scm);report["done"]=true;writeJsonAtomic(reportPath,report);
}
