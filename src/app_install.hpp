#pragma once
#include "backend.hpp"
#include "version.hpp"
#include "../resources/licenses.hpp"
inline bool installApplication(){auto className=L"ZapretGUIWindow-"+wide(GUI_VERSION);if(auto window=FindWindowW(className.c_str(),nullptr)){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);return true;}auto source=ownExecutable();auto destination=defaultConfig().parent_path()/L"app"/wide(GUI_VERSION)/L"ZapretGUI.exe";
    if(fs::weakly_canonical(source)==fs::weakly_canonical(destination))return false;fs::create_directories(destination.parent_path());
    if(!CopyFileW(source.c_str(),destination.c_str(),FALSE))throw std::runtime_error("Не удалось установить GUI. Закройте открытую копию этой версии.");
    writeBytes(destination.parent_path()/L"LICENSE",appLicense);writeBytes(destination.parent_path()/L"LICENSE-json.txt",jsonLicense);
    PWSTR programs=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_Programs,0,nullptr,&programs)))throw std::runtime_error("Не удалось открыть меню Пуск.");auto shortcut=fs::path(programs)/L"Zapret GUI.lnk";CoTaskMemFree(programs);
    IShellLinkW* link=nullptr;if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))))throw std::runtime_error("Не удалось создать ярлык.");
    link->SetPath(destination.c_str());link->SetWorkingDirectory(destination.parent_path().c_str());link->SetDescription(L"Zapret GUI — простой запуск обхода и Telegram");IPersistFile* file=nullptr;auto queried=link->QueryInterface(IID_PPV_ARGS(&file));link->Release();if(FAILED(queried))throw std::runtime_error("Не удалось сохранить ярлык.");auto saved=file->Save(shortcut.c_str(),TRUE);file->Release();if(FAILED(saved))throw std::runtime_error("Не удалось сохранить ярлык.");
    if((INT_PTR)ShellExecuteW(nullptr,L"open",destination.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)throw std::runtime_error("Не удалось открыть установленный GUI.");return true;
}
