#include "../src/mcp_manager.hpp"
#include <iostream>
void require(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(...){rejected=true;}require(rejected,"Expected rejection");}
int wmain(int argc,wchar_t** argv){try{
    if(argc==3&&std::wstring(argv[1])==L"--connections"){std::cout<<McpManager(argv[2]).connections();return 0;}
    require(argc==2,"Provide isolated test folder");fs::path root=fs::absolute(argv[1]);
    require(sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 broken");
    require(validRepo("owner/zapret-mcp")&&!validRepo("../..")&&!validRepo("https://github.com/owner/repo"),"Repo validation broken");
    std::string binary="MZ fixture only, never executed";Json manifest={{"name","zapret-gui-mcp"},{"version","0.1.1"},{"api_version",1},{"sha256",sha256(binary)},{"json_license","test fixture"}};
    McpManager manager(root);require(!manager.installed(),"Unexpected existing installation");manager.setRepository("owner/zapret-mcp");
    require(manager.install(manifest,binary)=="0.1.1"&&manager.installed(),"Installation failed");
    auto config=manager.clientConfig();require(config["mcpServers"]["zapret-gui"]["command"]==utf8((root/L"versions"/L"0.1.1"/L"ZapretMCP.exe").wstring()),"Incorrect client path");
    rejects([&]{manager.install(manifest,binary+"changed");});require(manager.version()=="0.1.1","Bad package changed active version");
    auto second=manifest;second["api_version"]=2;rejects([&]{manager.install(second,binary);});
    second=manifest;second["version"]="../escape";rejects([&]{manager.install(second,binary);});
    second=manifest;second["version"]="0.1.2";manager.install(second,binary);
    require(fs::is_regular_file(root/L"versions"/L"0.1.1"/L"ZapretMCP.exe"),"Update removed previous version");
    require(manager.version()=="0.1.2"&&manager.repository()=="owner/zapret-mcp","Independent update state lost");
    require(manager.connections()==0,"False client connection");
    std::cout<<"PASS: hashes, manifest/API compatibility, installation, independent update, previous version preservation, client config\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
