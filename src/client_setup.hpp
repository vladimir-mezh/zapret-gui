#pragma once
#include "backend.hpp"
inline fs::path profileFolder(){PWSTR p=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_Profile,0,nullptr,&p)))throw std::runtime_error("Папка пользователя недоступна.");fs::path folder(p);CoTaskMemFree(p);return folder;}
inline void backupAndWrite(const fs::path& path,const std::string& text){
    fs::create_directories(path.parent_path());if(fs::exists(path)){unsigned index=0;for(;;){auto backup=path;backup+=L".zapret-backup-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(index++);if(CopyFileW(path.c_str(),backup.c_str(),TRUE))break;if(GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS)throw std::runtime_error("Не удалось сохранить резервную копию настроек ИИ.");}}
    auto tmp=path;tmp+=L".zapret-tmp";writeBytes(tmp,text);if(!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Не удалось сохранить подключение ИИ.");
}
inline void configureClient(int client,const fs::path& executable,const fs::path& testHome={}){
    auto home=testHome.empty()?profileFolder():testHome;
    if(client==0){auto configHome=home/L".codex";if(testHome.empty())if(auto environment=_wgetenv(L"CODEX_HOME"))configHome=environment;auto path=configHome/L"config.toml";
        std::string text=fs::exists(path)?fileBytes(path,2*1024*1024):"";auto begin=text.find("# BEGIN ZAPRET GUI MCP"),end=text.find("# END ZAPRET GUI MCP");
        if(begin!=std::string::npos){if(end==std::string::npos||end<begin)throw std::runtime_error("Незавершённая запись MCP в настройках Codex.");text=text.substr(0,begin)+text.substr(end+std::string("# END ZAPRET GUI MCP").size());}
        if(text.find("mcp_servers.zapret-gui")!=std::string::npos||text.find("mcp_servers.\"zapret-gui\"")!=std::string::npos)throw std::runtime_error("В Codex уже есть другая запись zapret-gui. Она сохранена.");
        text+="\n# BEGIN ZAPRET GUI MCP\n[mcp_servers.zapret-gui]\ncommand = "+Json(utf8(executable.wstring())).dump()+"\nargs = []\n# END ZAPRET GUI MCP\n";backupAndWrite(path,text);return;
    }
    fs::path path;if(client==1){if(!testHome.empty())path=home/L"AppData"/L"Roaming"/L"Claude"/L"claude_desktop_config.json";else{PWSTR p=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&p)))throw std::runtime_error("AppData unavailable");path=fs::path(p)/L"Claude"/L"claude_desktop_config.json";CoTaskMemFree(p);}}
    else if(client==2)path=home/L".claude.json";else throw std::runtime_error("Выберите ИИ-клиент.");
    auto j=fs::exists(path)?Json::parse(fileBytes(path,4*1024*1024)):Json::object();if(!j.is_object())throw std::runtime_error("Некорректные настройки ИИ-клиента. Файл сохранён.");
    if(j.contains("mcpServers")&&!j["mcpServers"].is_object())throw std::runtime_error("Некорректный список MCP. Файл сохранён.");
    if(j.contains("mcpServers")&&j["mcpServers"].contains("zapret-gui")){auto command=j["mcpServers"]["zapret-gui"].value("command","");auto filename=fs::path(wide(command)).filename();if(filename!=L"ZapretMCP.exe"&&filename!=L"ZapretMcpLauncher.exe")throw std::runtime_error("В клиенте уже есть другая запись zapret-gui. Она сохранена.");}
    j["mcpServers"]["zapret-gui"]={{"command",utf8(executable.wstring())},{"args",Json::array()}};if(client==2)j["mcpServers"]["zapret-gui"]["type"]="stdio";backupAndWrite(path,j.dump(2));
}
