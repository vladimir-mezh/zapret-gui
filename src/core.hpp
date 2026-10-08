#pragma once
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <memory>
#include "../third_party/json.hpp"
using Json = nlohmann::json;
namespace fs = std::filesystem;
inline std::string utf8(const std::wstring& s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    std::string r(n,'\0'); WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),r.data(),n,nullptr,nullptr); return r;
}
inline std::wstring wide(const std::string& s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);
    if(!n) throw std::runtime_error("Invalid UTF-8");
    std::wstring r(n,L'\0'); MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),r.data(),n); return r;
}
struct Install { std::string id; fs::path path; std::vector<std::string> strategies; };
struct Profile { std::string version, strategy; bool game=false; std::string ipset="loaded",gameMode="",tcpPorts="1024-65535",udpPorts="1024-65535"; };
inline std::string gameMode(const Profile& p){return p.gameMode.empty()?(p.game?"all":"disabled"):p.gameMode;}
inline bool validGameMode(const std::string& s){return s=="disabled"||s=="all"||s=="tcp"||s=="udp";}
inline bool validPorts(const std::string& s){if(s.empty()||s.size()>255)return false;size_t start=0;while(start<s.size()){auto end=s.find(',',start);auto item=s.substr(start,end==std::string::npos?end:end-start);auto dash=item.find('-');auto number=[](const std::string& n){if(n.empty()||n.size()>5||!std::all_of(n.begin(),n.end(),[](unsigned char c){return c>='0'&&c<='9';}))return 0u;auto v=std::stoul(n);return v>=1&&v<=65535?(unsigned)v:0u;};auto first=number(item.substr(0,dash));if(!first)return false;if(dash!=std::string::npos){auto last=number(item.substr(dash+1));if(!last||last<first)return false;}if(end==std::string::npos)return true;start=end+1;}return false;}
inline Json profileJson(const Profile& p) {return {{"version_id",p.version},{"strategy",p.strategy},{"game_filter",gameMode(p)!="disabled"},{"ipset_mode",p.ipset},{"game_mode",gameMode(p)},{"tcp_ports",p.tcpPorts},{"udp_ports",p.udpPorts}};}
inline bool validMode(const std::string& s) { return s=="loaded" || s=="none" || s=="any"; }
inline Install inspect(const fs::path& root) {
    if(!fs::is_regular_file(root/L"bin"/L"winws.exe") || !fs::is_directory(root/L"lists"))
        throw std::runtime_error("Нужна распакованная папка Flowseal с bin/winws.exe и lists.");
    Install v; v.path=fs::canonical(root); v.id=utf8(v.path.filename().wstring());
    for(const auto& entry:fs::directory_iterator(root)) {
        auto name=entry.path().filename().wstring();
        if(entry.is_regular_file() && entry.path().extension()==L".bat" && name.rfind(L"general",0)==0)
            v.strategies.push_back(utf8(name));
    }
    std::sort(v.strategies.begin(),v.strategies.end());
    if(v.strategies.empty()) throw std::runtime_error("Не найдены стратегии general*.bat.");
    return v;
}
inline Json context(const std::vector<Install>& all, const Profile& p) {
    Json versions=Json::array();
    for(const auto& v:all) versions.push_back({{"id",v.id},{"strategies",v.strategies},{"files_available",!v.strategies.empty()}});
    return {{"app","Zapret GUI"},{"settings_schema",2},{"stage","Profile changes must be applied through GUI to restart the service."},
        {"available_versions",versions},{"profile",profileJson(p)}};
}
inline Profile validateChanges(const Json& changes, const Profile& current, const std::vector<Install>& all) {
    if(!changes.is_object()) throw std::runtime_error("changes must be an object");
    Profile p=current;
    for(auto it=changes.begin(); it!=changes.end();++it) {
        if(it.key()=="version_id" && it->is_string()) p.version=it->get<std::string>();
        else if(it.key()=="strategy" && it->is_string()) p.strategy=it->get<std::string>();
        else if(it.key()=="game_filter" && it->is_boolean()) {p.game=it->get<bool>();if(!changes.contains("game_mode"))p.gameMode=p.game?"all":"disabled";}
        else if(it.key()=="game_mode" && it->is_string())p.gameMode=it->get<std::string>();
        else if(it.key()=="tcp_ports" && it->is_string())p.tcpPorts=it->get<std::string>();
        else if(it.key()=="udp_ports" && it->is_string())p.udpPorts=it->get<std::string>();
        else if(it.key()=="ipset_mode" && it->is_string()) p.ipset=it->get<std::string>();
        else throw std::runtime_error("Unknown action or invalid field type");
    }
    if(!validMode(p.ipset)) throw std::runtime_error("Invalid IPSet mode");
    if(!validGameMode(gameMode(p))||!validPorts(p.tcpPorts)||!validPorts(p.udpPorts))throw std::runtime_error("Invalid game mode or port range");p.game=gameMode(p)!="disabled";
    if(changes.empty()) return p;
    if(p.version.empty() && p.strategy.empty() && !changes.contains("version_id") && !changes.contains("strategy")) return p;
    auto v=std::find_if(all.begin(),all.end(),[&](const Install& x){return x.id==p.version;});
    if(v==all.end() || std::find(v->strategies.begin(),v->strategies.end(),p.strategy)==v->strategies.end())
        throw std::runtime_error("Version or strategy is not imported");
    return p;
}
