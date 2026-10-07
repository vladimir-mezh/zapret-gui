#include "store.hpp"
int wmain(int argc,wchar_t** argv){try{
    std::vector<wchar_t> buffer(32768);auto n=GetModuleFileNameW(nullptr,buffer.data(),(DWORD)buffer.size());if(!n||n>=buffer.size())return 2;
    auto root=fs::path(std::wstring(buffer.data(),n)).parent_path();std::ifstream input(root/L"manager.json",std::ios::binary);Json metadata;input>>metadata;
    auto version=metadata.at("active_version").get<std::string>();if(version.empty()||version.size()>64||version[0]<'0'||version[0]>'9'||version.find_first_not_of("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.-_")!=std::string::npos)return 3;
    auto exe=root/L"versions"/wide(version)/L"ZapretMCP.exe";if(!fs::is_regular_file(exe))return 4;
    auto quote=[](const std::wstring& s){std::wstring result=L"\"";unsigned slashes=0;for(auto c:s){if(c==L'\\'){slashes++;continue;}result.append(c==L'\"'?slashes*2+1:slashes,L'\\');result+=c;slashes=0;}result.append(slashes*2,L'\\');return result+L"\"";};
    std::wstring cmd=quote(exe.wstring());for(int i=1;i<argc;i++)cmd+=L" "+quote(argv[i]);STARTUPINFOW si{sizeof(si)};si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);si.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);si.hStdError=GetStdHandle(STD_ERROR_HANDLE);PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,root.c_str(),&si,&pi))return 5;CloseHandle(pi.hThread);WaitForSingleObject(pi.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);return (int)code;
}catch(...){return 1;}}
