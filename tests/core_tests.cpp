#include "../src/core.hpp"
#include <iostream>
void require(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char* message){bool rejected=false;try{f();}catch(...){rejected=true;}require(rejected,message);}
int main(){try{
    std::vector<Install> all={{"old",L"C:\\private-user\\old",{"general.bat","general (ALT).bat"}},{"new",L"C:\\private-user\\new",{"general.bat"}}};
    Profile p{"old","general.bat",false,"loaded"};auto next=validateChanges({{"version_id","new"},{"strategy","general.bat"},{"game_filter",true}},p,all);
    require(next.version=="new"&&next.game,"valid change rejected");
    rejects([&]{validateChanges({{"command","powershell.exe"}},p,all);},"arbitrary command accepted");
    rejects([&]{validateChanges({{"version_id","missing"}},p,all);},"uninstalled version accepted");
    rejects([&]{validateChanges({{"version_id","new"},{"strategy","general (ALT).bat"}},p,all);},"strategy from other version accepted");
    rejects([&]{validateChanges({{"game_filter","true"}},p,all);},"incorrect boolean accepted");
    rejects([&]{validateChanges({{"ipset_mode","invalid"}},p,all);},"invalid mode accepted");
    require(validateChanges({{"game_filter",true}},Profile{},{}).game,"empty installation profile cannot be configured");
    require(context(all,p).dump().find("private-user")==std::string::npos,"local path exposed");
    require(wide(utf8(L"Версия обхода"))==L"Версия обхода","UTF-8 broken");
    std::cout<<"PASS: profile validation, unknown command rejection, exact strategies, context privacy, UTF-8\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
