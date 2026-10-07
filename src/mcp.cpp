#include "store.hpp"
#include <iostream>
#include <fcntl.h>
#include <io.h>
#include <memory>
#ifndef MCP_VERSION
#define MCP_VERSION "0.2.0"
#endif
class SessionMarker {
    fs::path file;
public:
    explicit SessionMarker(const fs::path& config){auto folder=config.parent_path()/L"mcp"/L"sessions";file=folder/(std::to_wstring(GetCurrentProcessId())+L".json");}
    void connected(){fs::create_directories(file.parent_path());FILETIME created,exit,kernel,user;GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user);
        uint64_t timestamp=((uint64_t)created.dwHighDateTime<<32)|created.dwLowDateTime;
        std::ofstream out(file,std::ios::binary);out<<Json{{"pid",GetCurrentProcessId()},{"created",timestamp},{"version",MCP_VERSION}}.dump();}
    ~SessionMarker(){std::error_code error;fs::remove(file,error);}
};

Json tools() {
    Json empty={{"type","object"},{"properties",Json::object()},{"additionalProperties",false}};
    Json changes={{"type","object"},{"properties",{{"version_id",{{"type","string"}}},{"strategy",{{"type","string"}}},{"game_filter",{{"type","boolean"}}},{"ipset_mode",{{"type","string"},{"enum",{"loaded","none","any"}}}}}},{"additionalProperties",false}};
    Json update={{"type","object"},{"properties",{{"changes",changes},{"expected_revision",{{"type","integer"},{"minimum",0}}},{"dry_run",{{"type","boolean"},{"default",true}}}}},{"required",{"changes","expected_revision"}},{"additionalProperties",false}};
    Json import={{"type","object"},{"properties",{{"path",{{"type","string"},{"description","Absolute path to an existing unpacked Flowseal folder. Files are only read."}}},{"expected_revision",{{"type","integer"},{"minimum",0}}}}},{"required",{"path","expected_revision"}},{"additionalProperties",false}};
    auto tool=[](const char* name,const char* description,const Json& schema,bool readOnly){return Json{{"name",name},{"description",description},{"inputSchema",schema},{"annotations",{{"readOnlyHint",readOnly},{"destructiveHint",false},{"openWorldHint",false}}}};};
    return Json::array({
        tool("zapret_get_state","Read local GUI profile, revision, available versions and exact strategy names. Paths and credentials are omitted.",empty,true),
        tool("zapret_set_profile","Validate and preview profile changes (dry_run defaults to true). Set dry_run=false only for changes the user requested. Requires current revision. Saves GUI profile only; cannot start Zapret or install services.",update,false),
        tool("zapret_import_version","Register an existing unpacked Flowseal folder after validating files. Does not copy, edit, download or execute installation files. Requires current revision.",import,false),
        tool("zapret_check_files","Check presence of registered installation files and strategies. Does not check live network access or modify Windows.",empty,true)
    });
}
void argumentsOnly(const Json& a,const std::vector<std::string>& keys) {
    if(!a.is_object())throw std::runtime_error("Arguments must be an object");
    for(auto it=a.begin();it!=a.end();++it)if(std::find(keys.begin(),keys.end(),it.key())==keys.end())throw std::runtime_error("Unknown argument: "+it.key());
}
uint64_t expectedRevision(const Json& a) {
    if(!a.contains("expected_revision")||!a["expected_revision"].is_number_integer()||(!a["expected_revision"].is_number_unsigned()&&a["expected_revision"].get<int64_t>()<0))throw std::runtime_error("expected_revision must be a nonnegative integer");
    return a["expected_revision"].get<uint64_t>();
}
Json invoke(Store& store,const std::string& name,const Json& a) {
    if(name=="zapret_get_state"){argumentsOnly(a,{});return stateJson(store.load());}
    if(name=="zapret_set_profile") {
        argumentsOnly(a,{"changes","expected_revision","dry_run"});auto rev=expectedRevision(a);
        if(!a.contains("changes")||!a["changes"].is_object())throw std::runtime_error("changes must be an object");
        if(a.contains("dry_run")&&!a["dry_run"].is_boolean())throw std::runtime_error("dry_run must be boolean");
        bool dry=a.value("dry_run",true);auto result=stateJson(store.setProfile(a["changes"],rev,dry));result["dry_run"]=dry;result["applied_to_engine"]=false;return result;
    }
    if(name=="zapret_import_version") {
        argumentsOnly(a,{"path","expected_revision"});auto rev=expectedRevision(a);
        if(!a.contains("path")||!a["path"].is_string())throw std::runtime_error("path must be a string");
        fs::path root=wide(a["path"].get<std::string>());if(!root.is_absolute())throw std::runtime_error("path must be absolute");return stateJson(store.importVersion(root,rev));
    }
    if(name=="zapret_check_files") {
        argumentsOnly(a,{});auto state=store.load();Json checks=Json::array();
        for(const auto& v:state.installs){Json strategies=Json::array();for(const auto& s:v.strategies)strategies.push_back({{"name",s},{"exists",fs::is_regular_file(v.path/wide(s))}});
            checks.push_back({{"version_id",v.id},{"winws_exists",fs::is_regular_file(v.path/L"bin"/L"winws.exe")},{"lists_exist",fs::is_directory(v.path/L"lists")},{"strategies",strategies}});}
        return {{"revision",state.revision},{"checks",checks},{"network_tested",false}};
    }throw std::runtime_error("Unknown tool");
}
Json rpcError(const Json& id,int code,const std::string& message){return {{"jsonrpc","2.0"},{"id",id},{"error",{{"code",code},{"message",message}}}};}
class Protocol {
    Store store;SessionMarker session;bool initialized=false,ready=false;
public:
    explicit Protocol(fs::path path):store(path),session(fs::absolute(path)){}
    Json handle(const Json& request) {
        Json id=request.is_object()?request.value("id",Json()):Json();
        if(!request.is_object()||request.value("jsonrpc",Json())!="2.0"||!request.contains("method")||!request["method"].is_string()||(!id.is_null()&&!id.is_string()&&!id.is_number_integer()))return rpcError(nullptr,-32600,"Invalid request");
        auto method=request["method"].get<std::string>();
        if(!request.contains("id")){if(method=="notifications/initialized"&&initialized){ready=true;session.connected();}return nullptr;}
        auto result=[&](const Json& data){return Json{{"jsonrpc","2.0"},{"id",id},{"result",data}};};
        Json params=request.value("params",Json::object());if(!params.is_object())return rpcError(id,-32602,"params must be an object");
        if(method=="initialize") {
            if(!params.contains("protocolVersion")||!params["protocolVersion"].is_string())return rpcError(id,-32602,"protocolVersion required");
            auto version=params["protocolVersion"].get<std::string>();
            if(version!="2025-11-25"&&version!="2025-06-18"&&version!="2025-03-26"&&version!="2024-11-05")version="2025-11-25";
            initialized=true;ready=false;return result({{"protocolVersion",version},{"capabilities",{{"tools",{{"listChanged",false}}}}},{"serverInfo",{{"name","zapret-gui"},{"version",MCP_VERSION}}},{"instructions","Use zapret_get_state first. Preview profile changes before committing user-requested changes. This server only configures the GUI profile, not the bypass engine."}});
        }
        if(method=="ping")return result(Json::object());
        if(method!="tools/list"&&method!="tools/call")return rpcError(id,-32601,"Method not found");
        if(!ready)return rpcError(id,-32000,"Initialize server first");
        if(method=="tools/list")return result({{"tools",tools()}});
        if(!params.contains("name")||!params["name"].is_string())return rpcError(id,-32602,"Tool name required");
        auto name=params["name"].get<std::string>();bool found=false;for(const auto& t:tools())if(t["name"]==name)found=true;
        if(!found)return rpcError(id,-32602,"Unknown tool");
        try {auto data=invoke(store,name,params.value("arguments",Json::object()));return result({{"content",Json::array({{{"type","text"},{"text",data.dump()}}})},{"structuredContent",data},{"isError",false}});}
        catch(const std::exception& e){return result({{"content",Json::array({{{"type","text"},{"text",e.what()}}})},{"isError",true}});}
    }
};
int wmain(int argc,wchar_t** argv) {
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
    try {
        fs::path config=defaultConfig();
        if(argc==2&&std::wstring(argv[1])==L"--client-config") {
            std::vector<wchar_t> buffer(32768);auto n=GetModuleFileNameW(nullptr,buffer.data(),(DWORD)buffer.size());
            if(!n||n>=buffer.size())throw std::runtime_error("Cannot resolve executable path");
            std::cout<<Json{{"mcpServers",{{"zapret-gui",{{"command",utf8(std::wstring(buffer.data(),n))},{"args",Json::array()}}}}}}.dump(2)<<"\n";return 0;
        }
        if(argc==3&&std::wstring(argv[1])==L"--config")config=argv[2];
        else if(argc!=1){std::cerr<<"ZapretMCP 0.1.1: stdio MCP server. Options: --client-config | --config <settings.json>\n";return 2;}
        Protocol protocol(config);std::string line;char c;
        while(std::cin.get(c)) {
            if(c!='\n'){line+=c;if(line.size()>1024*1024){std::cerr<<"MCP input exceeds 1 MiB\n";return 2;}continue;}
            if(line.empty())continue;Json response;
            try{response=protocol.handle(Json::parse(line));}
            catch(const Json::parse_error&){response=rpcError(nullptr,-32700,"Parse error");}
            catch(const std::exception&){response=rpcError(nullptr,-32600,"Invalid request");}
            if(!response.is_null())std::cout<<response.dump()<<"\n"<<std::flush;line.clear();
        }return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
