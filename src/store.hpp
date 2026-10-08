#pragma once
#include "core.hpp"
struct Snapshot {std::vector<Install> installs;Profile profile;uint64_t revision=0;};
inline fs::path defaultConfig() {
    PWSTR folder=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&folder)))throw std::runtime_error("AppData unavailable");
    fs::path p=fs::path(folder)/L"ZapretGUI"/L"settings.json";CoTaskMemFree(folder);return p;
}
class Store {
    fs::path path;
    struct Lock {
        HANDLE h=INVALID_HANDLE_VALUE;
        explicit Lock(const fs::path& file) {
            fs::create_directories(file.parent_path());auto lock=file;lock+=L".lock";auto deadline=GetTickCount64()+5000;
            do {h=CreateFileW(lock.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN,nullptr);
                if(h!=INVALID_HANDLE_VALUE)return;
                if(GetLastError()!=ERROR_SHARING_VIOLATION)throw std::runtime_error("Cannot lock settings file");Sleep(10);
            }while(GetTickCount64()<deadline);throw std::runtime_error("Settings busy; retry");
        }
        ~Lock(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    };
    Json read() {
        if(!fs::exists(path))return {{"schema",1},{"revision",0},{"installs",Json::array()},{"profile",profileJson(Profile{})}};
        if(fs::file_size(path)>1024*1024)throw std::runtime_error("Settings file too large");
        std::ifstream input(path,std::ios::binary);Json j;input>>j;
        if(!j.is_object()||(j.value("schema",1)!=1&&j.value("schema",1)!=2)||!j.contains("installs")||!j["installs"].is_array()||!j.contains("profile")||!j["profile"].is_object())throw std::runtime_error("Invalid settings file; original file was preserved");return j;
    }
    Snapshot decode(const Json& j) {
        Snapshot s;s.revision=j.value("revision",uint64_t{0});auto p=j.at("profile");
        s.profile={p.value("version_id",""),p.value("strategy",""),p.value("game_filter",false),p.value("ipset_mode","loaded")};
        s.profile.gameMode=p.value("game_mode",s.profile.game?"all":"disabled");s.profile.tcpPorts=p.value("tcp_ports","1024-65535");s.profile.udpPorts=p.value("udp_ports","1024-65535");s.profile.game=gameMode(s.profile)!="disabled";
        if(!validMode(s.profile.ipset))throw std::runtime_error("Invalid saved IPSet mode");
        if(!validGameMode(gameMode(s.profile))||!validPorts(s.profile.tcpPorts)||!validPorts(s.profile.udpPorts))throw std::runtime_error("Invalid saved game filter");
        for(const auto& entry:j.at("installs")) {
            auto root=fs::path(wide(entry.at("path").get<std::string>()));Install v;
            try{v=inspect(root);}catch(...){v.path=root;}v.id=entry.at("id").get<std::string>();s.installs.push_back(v);
        }return s;
    }
    void write(const Snapshot& s) {
        Json j={{"schema",2},{"revision",s.revision},{"installs",Json::array()},{"profile",profileJson(s.profile)}};
        if(fs::exists(path)&&read().value("schema",1)==1){auto backup=path;backup+=L".before-schema2";if(!fs::exists(backup)&&!CopyFileW(path.c_str(),backup.c_str(),TRUE))throw std::runtime_error("Cannot backup settings before migration");}
        for(const auto& v:s.installs)j["installs"].push_back({{"id",v.id},{"path",utf8(v.path.wstring())}});
        auto temp=path;temp+=L".tmp";{std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<j.dump(2);out.close();if(!out)throw std::runtime_error("Cannot write settings");}
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot commit settings");
    }
public:
    explicit Store(fs::path config=defaultConfig()):path(fs::absolute(config)){}
    Snapshot load(){Lock lock(path);return decode(read());}
    uint64_t revision(){Lock lock(path);return read().value("revision",uint64_t{0});}
    Snapshot save(Snapshot s,uint64_t expected) {
        Lock lock(path);auto current=read();if(current.value("revision",uint64_t{0})!=expected)throw std::runtime_error("Settings changed elsewhere. Read latest state before saving.");
        s.revision=expected+1;write(s);return s;
    }
    Snapshot setProfile(const Json& changes,uint64_t expected,bool dryRun) {
        Lock lock(path);auto s=decode(read());if(s.revision!=expected)throw std::runtime_error("Stale revision. Read state again before changing profile.");
        auto next=validateChanges(changes,s.profile,s.installs);if(dryRun){s.profile=next;return s;}
        if(profileJson(next)!=profileJson(s.profile)){s.profile=next;s.revision++;write(s);}return s;
    }
    Snapshot importVersion(const fs::path& root,uint64_t expected) {
        auto v=inspect(root);Lock lock(path);auto s=decode(read());if(s.revision!=expected)throw std::runtime_error("Stale revision. Read state again.");
        for(const auto& old:s.installs)if(old.path==v.path)return s;
        auto base=v.id;int suffix=2;while(std::any_of(s.installs.begin(),s.installs.end(),[&](auto& old){return old.id==v.id;}))v.id=base+" ("+std::to_string(suffix++)+")";
        if(s.profile.version.empty()){s.profile.version=v.id;s.profile.strategy=v.strategies.front();}
        s.installs.push_back(v);s.revision++;write(s);return s;
    }
};
inline Json stateJson(const Snapshot& s){auto j=context(s.installs,s.profile);j["revision"]=s.revision;return j;}
