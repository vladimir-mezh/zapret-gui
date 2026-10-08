#include "../src/client_setup.hpp"
#include <iostream>
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action){bool rejected=false;try{action();}catch(...){rejected=true;}require(rejected,"Expected rejection");}
const char* testStep="start";
int wmain(int argc,wchar_t** argv){try{
    require(argc>=2,"Provide isolated folder");fs::path root=fs::absolute(argv[1]);fs::create_directories(root);
    require(!safeTag("../escape")&&!safeTag(".")&&safeTag("v1.10.3"),"Tag validation failed");
    auto home=root/L"profile";auto codex=home/L".codex"/L"config.toml";writeBytes(codex,"model = \"example\"\n[mcp_servers.other]\ncommand = \"preserve\"\n");
    testStep="codex configuration";auto command=root/L"mcp"/L"ZapretMcpLauncher.exe";configureClient(0,command,home);testStep="codex repeated configuration";configureClient(0,command,home);
    testStep="codex preservation";auto toml=fileBytes(codex,65536);require(toml.find("command = \"preserve\"")!=std::string::npos,"Lost other server");require(toml.find("[mcp_servers.zapret-gui]")==toml.rfind("[mcp_servers.zapret-gui]"),"Duplicate server");
    testStep="claude configuration";auto claude=home/L"AppData"/L"Roaming"/L"Claude"/L"claude_desktop_config.json";writeJsonAtomic(claude,{{"other","keep"},{"mcpServers",{{"other",{{"command","keep"}}}}}});configureClient(1,command,home);
    auto json=Json::parse(fileBytes(claude,65536));require(json["other"]=="keep"&&json["mcpServers"]["other"]["command"]=="keep","Lost Claude configuration");configureClient(2,command,home);
    writeBytes(claude,"{broken");rejects([&]{configureClient(1,command,home);});require(fileBytes(claude,65536)=="{broken","Overwrote corrupt configuration");
    testStep="strategy parser";auto fixture=root/L"fixture";writeBytes(fixture/L"bin"/L"winws.exe","MZ fixture never executed");fs::create_directories(fixture/L"lists");writeBytes(fixture/L"general.bat","start \"example\" \"%BIN%winws.exe\" --wf-tcp=443,%GameFilterTCP% --wf-udp=443,%GameFilterUDP% ^\n--filter-tcp=443 --hostlist=\"%LISTS%list-general.txt\"\n");auto v=inspect(fixture);Profile p{v.id,"general.bat",false,"loaded"};auto args=strategyArguments(v,p);require(args.find("443,12")!=std::string::npos&&args.find("%LISTS%")==std::string::npos,"Strategy conversion failed");
    p.gameMode="tcp";p.tcpPorts="27015,27020-27050";p.udpPorts="5000";args=strategyArguments(v,p);require(args.find("--wf-tcp=443,27015,27020-27050")!=std::string::npos&&args.find("--wf-udp=443,12")!=std::string::npos,"TCP game range leaked into UDP");p.gameMode="udp";args=strategyArguments(v,p);require(args.find("--wf-tcp=443,12")!=std::string::npos&&args.find("--wf-udp=443,5000")!=std::string::npos,"UDP game range leaked into TCP");
    writeBytes(fixture/L"lists"/L"ipset-all.txt","192.0.2.0/24\n");p.ipset="none";prepareLists(fixture,p);require(fileBytes(fixture/L"lists"/L"ipset-all.txt",65536)=="203.0.113.113/32\n","IPSet none failed");p.ipset="any";prepareLists(fixture,p);require(fileBytes(fixture/L"lists"/L"ipset-all.txt",65536).empty(),"IPSet any failed");p.ipset="loaded";prepareLists(fixture,p);require(fileBytes(fixture/L"lists"/L"ipset-all.txt",65536)=="192.0.2.0/24\n","IPSet restore failed");
    Backend manager(root/L"managed");rejects([&]{manager.removeVersion(v);});
    if(argc==3&&std::wstring(argv[2])==L"--live"){
        auto versions=manager.releases();require(!versions.empty(),"No releases");auto installed=manager.downloadZapret("");std::cout<<"Installed Zapret "<<installed.id<<", strategies: "<<installed.strategies.size()<<"\n";
        p.version=installed.id;p.ipset="loaded";for(auto& strategy:installed.strategies){p.strategy=strategy;require(!strategyArguments(installed,p).empty(),"Empty strategy");}
        writeBytes(installed.path/L"lists"/L"list-general-user.txt","user.example\n");writeBytes(installed.path/L"lists"/L"ipset-all.txt","192.0.2.0/24\n");
        require(versions.size()>1,"Need previous release");auto old=manager.downloadZapret(versions[1],installed.path);require(fileBytes(old.path/L"lists"/L"list-general-user.txt",65536)=="user.example\n","User list not preserved");require(fileBytes(old.path/L"lists"/L"ipset-all.txt",65536)=="192.0.2.0/24\n","IPSet not preserved");require(fs::exists(installed.path),"Old version deleted");
        auto telegram=manager.installTelegram();require(fs::exists(manager.tgExecutable()),"TG not installed");manager.installTelegram();std::cout<<"Installed Telegram proxy "<<telegram<<" (not executed)\n";
        manager.removeVersion(old);require(!fs::exists(old.path)&&fs::exists(installed.path),"Version removal failed");
        McpManager mcp(root/L"mcp");auto version=mcp.downloadLatest("vladimir-mezh/zapret-gui-mcp");require(fs::exists(mcp.connectionExecutable()),"MCP launcher missing");std::cout<<"Installed published MCP "<<version<<"\n";
    }
    std::cout<<"PASS: client config preservation, repeat setup, corrupt config, strategy args, IPSet modes, managed paths\n";return 0;
}catch(const std::exception& e){std::cerr<<testStep<<": "<<e.what()<<"\n";return 1;}}
