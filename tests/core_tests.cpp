#include "../src/core.hpp"
#include <iostream>
void require(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char* message){bool rejected=false;try{f();}catch(...){rejected=true;}require(rejected,message);}
int main(){try{
    std::vector<Install> all={{"old",L"C:\\private-user\\old",{"general.bat","general (ALT).bat"}},{"new",L"C:\\private-user\\new",{"general.bat"}}};
    Profile p{"old","general.bat",false,"loaded"};auto next=validateChanges({{"version_id","new"},{"strategy","general.bat"},{"game_filter",true}},p,all);
    require(next.version=="new"&&next.game,"valid change rejected");
    auto tcp=validateChanges({{"game_mode","tcp"},{"tcp_ports","27015,27020-27050"},{"udp_ports","5000"}},p,all);require(tcp.game&&gameMode(tcp)=="tcp"&&tcp.tcpPorts=="27015,27020-27050","TCP mode/ranges failed");require(gameMode(validateChanges({{"game_filter",false}},tcp,all))=="disabled","Legacy toggle failed");
    for(auto ports:{"",",443","443,","0","65536","400-100","1--2","443; calc.exe","1, 2","999999999999"})require(!validPorts(ports),"Invalid port range accepted");require(validPorts("1,65535,27015-27050"),"Valid ports rejected");rejects([&]{validateChanges({{"game_mode","both"}},p,all);},"Bad game mode accepted");
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
