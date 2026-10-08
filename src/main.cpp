#include "client_setup.hpp"
#include "app_install.hpp"
#include "maintenance.hpp"
#include <functional>
#include <thread>
#include <atomic>
#include <commctrl.h>
#include <uxtheme.h>
#include <shellapi.h>

constexpr COLORREF BG=RGB(245,247,246), PANEL=RGB(255,255,255), INK=RGB(29,42,37), MUTED=RGB(104,119,111), GREEN=RGB(28,100,72), LINE=RGB(222,230,225), SIDE=RGB(23,39,32);
enum { NAV=100, VERSION=200, STRATEGY, IMPORT, RELEASES, START, AUTOSTART, GAME, IPSET, SAVE, FORGET, TRANSCRIPT, COPY, DIAG, MCP_DOWNLOAD };
enum { MCP_REPO=402,MCP_REPO_SAVE,MCP_LOCAL,MCP_CONFIG };
enum Page {HOME_PAGE,VERSIONS_PAGE,TELEGRAM_PAGE,TESTS_PAGE,SETTINGS_PAGE,MCP_PAGE,DIAGNOSTICS_PAGE};
constexpr int NAV_COUNT=7;
enum {DOWNLOAD_LATEST=500,RELEASE_PICK,LOAD_RELEASES,DOWNLOAD_PICKED,DELETE_VERSION,TG_INSTALL,TG_CONNECT,MCP_CLIENT,MCP_CONNECT,SERVICE_REMOVE,GUI_UPDATE,IPSET_UPDATE};
enum {TEST_CURRENT=600,TEST_SELECTED,TEST_ALL,TEST_CANCEL,TEST_PICK,TEST_EXPORT,FAKE_PICK,FAKE_DISCORD,FAKE_GAME,DISCORD_CACHE,USER_DOMAINS,USER_EXCLUDES,TEST_PARALLEL};
struct JobResult {std::wstring message;bool start=false,telegram=false,client=false;int clientId=0;std::vector<std::string> releases;Json report;};
LRESULT CALLBACK buttonProc(HWND h,UINT msg,WPARAM w,LPARAM l) {
    auto original=(WNDPROC)GetPropW(h,L"ZapretButtonProc");
    bool hovered=GetPropW(h,L"ZapretButtonHover")!=nullptr;
    if(msg==WM_ERASEBKGND)return 1; // The buffered owner draw paints the entire button.
    if(msg==WM_MOUSEMOVE&&!hovered) {
        SetPropW(h,L"ZapretButtonHover",(HANDLE)1);
        TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,h,0};TrackMouseEvent(&tracking);
        InvalidateRect(h,nullptr,FALSE);
    } else if(msg==WM_MOUSELEAVE) {
        RemovePropW(h,L"ZapretButtonHover");InvalidateRect(h,nullptr,FALSE);
    } else if(msg==WM_SETCURSOR&&IsWindowEnabled(h)) {
        SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;
    } else if(msg==WM_NCDESTROY) {RemovePropW(h,L"ZapretButtonHover");RemovePropW(h,L"ZapretButtonProc");}
    return CallWindowProcW(original,h,msg,w,l);
}
constexpr UINT MCP_DONE=WM_APP+1;
struct Control { HWND h; int x,y,w,hgt; bool stretch=false,bottom=false,grow=false; };
class App {
public:
    HWND window=nullptr; HINSTANCE instance=nullptr; int page=0; float scale=1; int width=1120,height=760;
    HFONT font=nullptr,bold=nullptr,title=nullptr,smallFont=nullptr; HBRUSH bgBrush=CreateSolidBrush(BG),panelBrush=CreateSolidBrush(PANEL),sideBrush=CreateSolidBrush(SIDE);
    std::vector<Control> controls; std::vector<HWND> nav; std::vector<Install> installs; Profile profile;
    Store store; uint64_t revision=0;
    Backend backend;ServiceState engine;std::vector<std::string> remoteReleases;
    McpManager mcp;int clientCount=0;bool mcpBusy=false;std::string repoDraft;
    std::thread mcpWorker;std::atomic<bool> closing=false;
    std::atomic<bool> cancelTests=false;bool testBusy=false,strategyTesting=false,testParallel=true;Json testReport=Json::object();std::wstring reportText;
    std::wstring notice=L"Готово к настройке";
    ~App(){ closing=true;if(mcpWorker.joinable())mcpWorker.join();for(auto f:{font,bold,title,smallFont}) DeleteObject(f); DeleteObject(bgBrush);DeleteObject(panelBrush);DeleteObject(sideBrush); }
    int px(int v) const {return (int)(v*scale);}
    HWND control(int id) {for(auto& c:controls) if(GetDlgCtrlID(c.h)==id) return c.h; return nullptr;}
    std::wstring text(int id) {HWND h=control(id);int n=GetWindowTextLengthW(h);std::wstring s(n+1,L'\0');GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
    void setText(int id,const std::wstring& s) {if(auto h=control(id)) SetWindowTextW(h,s.c_str());}
    void fonts() {for(auto f:{font,bold,title,smallFont}) if(f) DeleteObject(f);
        auto make=[&](int size,int weight){return CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");};
        font=make(15,FW_NORMAL);bold=make(15,FW_SEMIBOLD);title=make(30,FW_SEMIBOLD);smallFont=make(13,FW_NORMAL);
    }
    void reload() { auto state=store.load();installs=state.installs;profile=state.profile;revision=state.revision; }
    void init() { reload();repoDraft=mcp.repository();engine=serviceState();try{auto file=defaultConfig().parent_path()/L"test-report.json";if(fs::exists(file))testReport=Json::parse(fileBytes(file,2*1024*1024));}catch(...){}fonts();mount();SetTimer(window,1,1000,nullptr); }
    void save() {
        try { auto state=store.save({installs,profile,revision},revision);revision=state.revision; }
        catch(...) { reload();mount();throw; }
    }
    void poll() {
        if(strategyTesting)try{auto file=defaultConfig().parent_path()/L"test-report.json";if(fs::exists(file)){auto next=Json::parse(fileBytes(file,2*1024*1024));auto nextText=testReportText(next);testReport=next;if(nextText!=reportText){reportText=nextText;notice=wide(next.value("message",""));if(page==TESTS_PAGE)setText(TRANSCRIPT,reportText);InvalidateRect(window,nullptr,FALSE);}}}catch(...){}
        try { if(store.revision()!=revision) { reload();notice=L"Настройки обновлены извне";mount(); } }
        catch(...) { notice=L"Не удалось прочитать настройки. Исходный файл сохранён."; }
        try{int next=mcp.connections();if(next!=clientCount){clientCount=next;InvalidateRect(window,nullptr,FALSE);}}catch(...){}
        try{auto next=serviceState();if(next.installed!=engine.installed||next.running!=engine.running){engine=next;mount();}}catch(...){}
    }
    Install* selected() {for(auto& v:installs) if(v.id==profile.version) return &v;return nullptr;}
    void normalize() {if(!selected()) {profile.version=installs.empty()?"":installs.front().id;profile.strategy="";}if(auto v=selected()) if(std::find(v->strategies.begin(),v->strategies.end(),profile.strategy)==v->strategies.end()) profile.strategy=v->strategies.empty()?"":v->strategies.front();}
    HWND add(int id,const wchar_t* cls,const std::wstring& label,int x,int y,int w,int h,DWORD style=0,bool stretch=false,bool bottom=false,bool grow=false) {
        HWND child=CreateWindowExW((wcscmp(cls,L"EDIT")==0 || wcscmp(cls,L"LISTBOX")==0)?WS_EX_CLIENTEDGE:0,cls,label.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,0,0,window,(HMENU)(INT_PTR)id,instance,nullptr);
        SendMessageW(child,WM_SETFONT,(WPARAM)font,TRUE);
        if(wcscmp(cls,L"BUTTON")==0) {SetWindowTheme(child,L"",L"");
            SetPropW(child,L"ZapretButtonProc",(HANDLE)GetWindowLongPtrW(child,GWLP_WNDPROC));
            SetWindowLongPtrW(child,GWLP_WNDPROC,(LONG_PTR)buttonProc);}
        if(wcscmp(cls,L"EDIT")==0) SendMessageW(child,EM_SETLIMITTEXT,100000,0);
        controls.push_back({child,x,y,w,h,stretch,bottom,grow}); return child;
    }
    HWND button(int id,const std::wstring& label,int x,int y,int w,int h=42,bool stretch=false,bool bottom=false) {return add(id,L"BUTTON",label,x,y,w,h,BS_OWNERDRAW,stretch,bottom);}
    HWND combo(int id,int x,int y,int w,bool stretch=false) {return add(id,L"COMBOBOX",L"",x,y,w,220,CBS_DROPDOWNLIST|WS_VSCROLL,stretch);}
    void item(HWND h,const std::string& s){SendMessageW(h,CB_ADDSTRING,0,(LPARAM)wide(s).c_str());}
    void comboSelect(HWND h,int index) {SendMessageW(h,CB_SETCURSEL,index,0);}
    void choices() {
        HWND versions=control(VERSION),strategy=control(STRATEGY);
        SendMessageW(versions,CB_RESETCONTENT,0,0);SendMessageW(strategy,CB_RESETCONTENT,0,0);
        int index=0,current=0; for(auto& v:installs){item(versions,v.id);if(v.id==profile.version)current=index;index++;}
        if(installs.empty()) item(versions,"Нет импортированных версий");comboSelect(versions,current);
        index=0;current=0;if(auto v=selected()) for(auto& s:v->strategies){item(strategy,s);if(s==profile.strategy)current=index;index++;}
        else item(strategy,"Сначала добавьте версию");comboSelect(strategy,current);
        EnableWindow(versions,!mcpBusy&&!installs.empty());EnableWindow(strategy,!mcpBusy&&!installs.empty());
    }
    void mount() {
        if(control(MCP_REPO))repoDraft=utf8(text(MCP_REPO));
        // Keep navigation windows alive and expose a completed page in one redraw.
        struct PageUpdate {
            HWND h;bool visible;
            explicit PageUpdate(HWND value):h(value),visible(IsWindowVisible(value)!=FALSE){if(visible)SendMessageW(h,WM_SETREDRAW,FALSE,0);}
            ~PageUpdate(){if(visible)SendMessageW(h,WM_SETREDRAW,TRUE,0);RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);}
        } update(window);
        controls.erase(std::remove_if(controls.begin(),controls.end(),[](const Control& c){
            int id=GetDlgCtrlID(c.h);if(id>=NAV&&id<NAV+NAV_COUNT)return false;DestroyWindow(c.h);return true;
        }),controls.end());
        const wchar_t* labels[]={L"Главная",L"Версии",L"Telegram",L"Тесты",L"Настройки",L"MCP",L"Диагностика"};
        static_assert(sizeof(labels)/sizeof(labels[0])==NAV_COUNT);
        if(nav.empty())for(int i=0;i<NAV_COUNT;i++)nav.push_back(button(NAV+i,labels[i],20,143+i*54,192,44));
        for(auto h:nav)SendMessageW(h,WM_SETFONT,(WPARAM)font,FALSE);
        if(page==HOME_PAGE) {
            combo(VERSION,286,313,315,true);combo(STRATEGY,286,395,315,true);choices();
            button(IMPORT,L"Добавить папку",286,454,180);button(START,mcpBusy?L"Настраиваем…":engine.running?L"Выключить":installs.empty()?L"Установить и включить":L"Включить",480,454,240);EnableWindow(control(START),!mcpBusy&&(!engine.installed||engine.owned));
            button(AUTOSTART,L"Применить настройки",286,566,230);EnableWindow(control(AUTOSTART),!mcpBusy&&!installs.empty()&&(!engine.installed||engine.owned));
            button(SERVICE_REMOVE,L"Убрать автозапуск",530,566,210);EnableWindow(control(SERVICE_REMOVE),!mcpBusy&&engine.owned);
        } else if(page==VERSIONS_PAGE) {
            auto list=add(VERSION,L"LISTBOX",L"",286,194,315,198,LBS_NOTIFY|WS_VSCROLL,true);
            for(const auto& v:installs)SendMessageW(list,LB_ADDSTRING,0,(LPARAM)wide(v.id+"  •  "+std::to_string(v.strategies.size())+" стратегий").c_str());
            for(size_t i=0;i<installs.size();i++)if(installs[i].id==profile.version)SendMessageW(list,LB_SETCURSEL,i,0);
            button(DOWNLOAD_LATEST,mcpBusy?L"Загрузка…":L"Установить последнюю",286,412,230);button(LOAD_RELEASES,L"Другие версии",530,412,190);
            auto release=combo(RELEASE_PICK,286,474,220,true);for(auto& r:remoteReleases)item(release,r);if(remoteReleases.empty())item(release,"Нажмите «Другие версии»");comboSelect(release,0);
            button(DOWNLOAD_PICKED,L"Установить выбранную",286,518,230);EnableWindow(control(DOWNLOAD_PICKED),!mcpBusy&&!remoteReleases.empty());
            button(DELETE_VERSION,L"Удалить версию",530,518,190);EnableWindow(control(DELETE_VERSION),!mcpBusy&&!installs.empty());
            button(IMPORT,L"Добавить папку",286,576,180);button(FORGET,L"Убрать из списка",480,576,190);EnableWindow(control(FORGET),!installs.empty());
            EnableWindow(control(DOWNLOAD_LATEST),!mcpBusy);EnableWindow(control(LOAD_RELEASES),!mcpBusy);
        } else if(page==TELEGRAM_PAGE) {
            button(TG_INSTALL,mcpBusy?L"Устанавливаем…":L"Установить и подключить",286,306,270);button(TG_CONNECT,L"Подключить Telegram",286,370,270);
            EnableWindow(control(TG_INSTALL),!mcpBusy);EnableWindow(control(TG_CONNECT),!mcpBusy&&!backend.tgState().empty());
        } else if(page==TESTS_PAGE) {
            button(TEST_CURRENT,L"Проверить доступность",286,158,238);button(TEST_SELECTED,L"Тест выбранной",538,158,200);button(TEST_ALL,L"Подобрать стратегию",752,158,232);
            EnableWindow(control(TEST_CURRENT),!mcpBusy);for(auto id:{TEST_SELECTED,TEST_ALL})EnableWindow(control(id),!mcpBusy&&!installs.empty()&&(!engine.installed||engine.owned));
            auto parallel=combo(TEST_PARALLEL,538,210,446);item(parallel,"По одной стратегии");item(parallel,"Все одновременно (экспериментально)");comboSelect(parallel,testParallel?1:0);EnableWindow(parallel,!mcpBusy);
            reportText=testReportText(testReport);add(TRANSCRIPT,L"EDIT",reportText,286,254,315,290,ES_MULTILINE|ES_READONLY|WS_VSCROLL,true);
            button(TEST_CANCEL,L"Остановить тесты",286,558,210);EnableWindow(control(TEST_CANCEL),testBusy);
            button(TEST_PICK,L"Выбрать рекомендацию",510,558,250);EnableWindow(control(TEST_PICK),!mcpBusy&&testReport.value("done",false)&&!testReport.value("cancelled",false)&&testReport.contains("best")&&testReport["best"].is_string());
            button(TEST_EXPORT,L"Сохранить отчёт",286,618,210);EnableWindow(control(TEST_EXPORT),!testReport.empty()&&!mcpBusy);
        } else if(page==SETTINGS_PAGE) {
            button(GAME,profile.game?L"Game Filter: включён":L"Game Filter: выключен",286,212,315,46,true);
            auto ip=combo(IPSET,286,320,315,true);for(auto s:{"loaded","none","any"})item(ip,s);comboSelect(ip,profile.ipset=="loaded"?0:profile.ipset=="none"?1:2);
            button(SAVE,L"Сохранить профиль",286,414,220);
            button(IPSET_UPDATE,L"Обновить список IP",286,614,230);EnableWindow(control(IPSET_UPDATE),!mcpBusy&&!installs.empty());
            button(USER_DOMAINS,L"Мои сайты",286,472,180);button(USER_EXCLUDES,L"Исключения",480,472,180);
            auto fake=combo(FAKE_PICK,286,540,315,true);if(auto v=selected())for(auto& f:fakeFiles(*v))item(fake,f);comboSelect(fake,0);
            button(FAKE_DISCORD,L"Для Discord UDP",536,614,210);button(FAKE_GAME,L"Для игр UDP",760,614,210);
            for(auto id:{FAKE_PICK,FAKE_DISCORD,FAKE_GAME,USER_DOMAINS,USER_EXCLUDES})EnableWindow(control(id),!mcpBusy&&!installs.empty());
            for(auto id:{GAME,IPSET,SAVE})EnableWindow(control(id),!mcpBusy);
        } else if(page==MCP_PAGE) {
                auto client=combo(MCP_CLIENT,286,324,315,true);for(auto name:{"Codex","Claude Desktop","Claude Code"})item(client,name);comboSelect(client,0);
                button(MCP_CONNECT,mcpBusy?L"Настраиваем…":L"Подключить выбранный ИИ",286,372,280);EnableWindow(control(MCP_CONNECT),!mcpBusy);
                button(MCP_DOWNLOAD,mcpBusy?L"Загрузка…":mcp.installed()?L"Обновить MCP":L"Установить MCP",286,448,180);
                button(MCP_CONFIG,L"Копировать подключение",286,510,260);EnableWindow(control(MCP_CONFIG),mcp.installed());
                EnableWindow(control(MCP_DOWNLOAD),!mcpBusy);EnableWindow(control(MCP_LOCAL),!mcpBusy);EnableWindow(control(MCP_REPO_SAVE),!mcpBusy);
        } else if(page==DIAGNOSTICS_PAGE) {
            add(TRANSCRIPT,L"EDIT",diagnostics(),286,194,315,340,ES_MULTILINE|ES_READONLY|WS_VSCROLL,true);
            button(DIAG,L"Проверить файлы",286,558,200);button(COPY,L"Копировать контекст",500,558,220);
            button(GUI_UPDATE,L"Обновить приложение",286,620,240);EnableWindow(control(GUI_UPDATE),!mcpBusy);
            button(DISCORD_CACHE,L"Очистить кэш Discord",542,620,240);EnableWindow(control(DISCORD_CACHE),!mcpBusy);
        }
        for(auto id:{IMPORT,FORGET})if(control(id))EnableWindow(control(id),!mcpBusy&&(id!=FORGET||!installs.empty()));
        layout();
    }
    void layout() {
        RECT r;GetClientRect(window,&r);width=(int)(r.right/scale);height=(int)(r.bottom/scale);
        for(auto& c:controls) {
            int x=c.x,y=c.bottom?height-c.y:c.y,w=c.stretch?width-c.x-56:c.w,h=c.grow?height-525:c.hgt;
            MoveWindow(c.h,px(x),px(y),px(w),px(h),FALSE);
        }
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    }
    void rect(HDC dc,int x,int y,int w,int h,COLORREF color,int radius=14) {
        HBRUSH brush=CreateSolidBrush(color);auto oldB=SelectObject(dc,brush);auto oldP=SelectObject(dc,GetStockObject(NULL_PEN));
        RoundRect(dc,px(x),px(y),px(x+w),px(y+h),px(radius),px(radius));SelectObject(dc,oldB);SelectObject(dc,oldP);DeleteObject(brush);
    }
    void label(HDC dc,const std::wstring& s,int x,int y,int w,int h,COLORREF color=INK,HFONT f=nullptr,UINT flags=DT_LEFT|DT_WORDBREAK) {
        RECT r={px(x),px(y),px(x+w),px(y+h)};SelectObject(dc,f?f:font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,s.c_str(),-1,&r,flags);
    }
    void paint(HDC dc) {
        RECT r;GetClientRect(window,&r);FillRect(dc,&r,bgBrush);rect(dc,0,0,232,height,SIDE,0);
        rect(dc,22,29,38,38,RGB(192,232,186),12);label(dc,L"Z",30,30,30,38,SIDE,title);
        label(dc,L"zapret",72,29,145,38,RGB(241,249,242),title);label(dc,L"Легко управлять",24,86,188,25,RGB(161,185,170),smallFont);
        label(dc,L"GUI "+wide(GUI_VERSION),24,height-65,190,22,RGB(161,185,170),smallFont);
        label(dc,L"Flowseal / Windows",24,height-42,190,22,RGB(161,185,170),smallFont);
        const wchar_t* titles[]={L"Ваш обход",L"Версии Zapret",L"Telegram",L"Проверка обхода",L"Настройки профиля",L"Подключение ИИ через MCP",L"Диагностика"};
        const wchar_t* subtitles[]={L"Версия, стратегия и управление — в одном месте.",L"Рабочая версия всегда остаётся под рукой.",L"Локальный прокси Flowseal для Telegram Desktop.",L"Проверка сайтов и сравнение стратегий вашей версии.",L"Параметры сохраняются отдельно от версий.",L"Необязательное подключение вашего ИИ-клиента.",L"Проверка файлов, служб и возможных конфликтов."};
        static_assert(sizeof(titles)/sizeof(titles[0])==NAV_COUNT&&sizeof(subtitles)/sizeof(subtitles[0])==NAV_COUNT);
        label(dc,titles[page],274,33,width-330,43,INK,title);label(dc,subtitles[page],276,89,width-332,28,MUTED);
        if(page==HOME_PAGE) {
            rect(dc,274,142,width-318,106,PANEL);rect(dc,294,168,9,9,RGB(170,185,177),9);
            label(dc,engine.installed&&!engine.owned?L"Zapret установлен другим менеджером":engine.running?L"Обход включён":L"Обход выключен",315,161,width-375,28,INK,bold);
            label(dc,engine.installed&&!engine.owned?L"Чтобы перейти на GUI, удалите службу через прежний менеджер Zapret.":L"Нажмите «Включить». Файлы и служба устанавливаются автоматически.",294,202,width-355,30,MUTED,smallFont);
            rect(dc,274,267,width-318,246,PANEL);label(dc,L"Версия Zapret",286,285,300,24,MUTED,smallFont);
            label(dc,L"Стратегия обхода",286,367,300,24,MUTED,smallFont);
            rect(dc,274,533,width-318,110,PANEL);label(dc,L"Работа в фоне",286,546,300,24,INK,bold);
            label(dc,L"Служба работает после закрытия GUI и запускается с Windows.",286,624,width-350,40,MUTED,smallFont);
        } else if(page==VERSIONS_PAGE) {
            rect(dc,274,142,width-318,482,PANEL);label(dc,L"Установленные версии",286,161,400,24,INK,bold);
            if(installs.empty())label(dc,L"Список пуст. Нажмите «Установить последнюю».",300,214,width-370,60,MUTED);
            label(dc,L"Источник: Flowseal. При обновлении старые версии и ваши списки сохраняются.",286,654,width-342,54,MUTED,smallFont);
        } else if(page==TELEGRAM_PAGE) {
            rect(dc,274,142,width-318,126,PANEL);auto tg=backend.tgState();label(dc,tg.empty()?L"Telegram-прокси не установлен":L"Telegram-прокси установлен · "+wide(tg.value("version","")),286,166,width-350,28,INK,bold);
            label(dc,L"Официальный TG WS Proxy от Flowseal. Настройки создаются автоматически.",286,214,width-350,44,MUTED,smallFont);
            label(dc,L"После установки Telegram предложит включить локальный прокси. Подтвердите подключение в Telegram.",286,446,width-350,80,MUTED);
        } else if(page==TESTS_PAGE) {
            rect(dc,274,246,width-318,304,PANEL);label(dc,L"Скорость подбора",286,212,240,24,MUTED,smallFont);
            label(dc,L"Подбор временно приостанавливает службу GUI и возвращает её после тестов. TLS/HTTP не проверяет голос Discord, видео и QUIC. VPN может влиять на результаты.",286,674,width-350,55,MUTED,smallFont);
        } else if(page==SETTINGS_PAGE) {
            rect(dc,274,142,width-318,516,PANEL);label(dc,L"Игры и UDP",286,169,width-350,25,INK,bold);
            label(dc,L"IPSet Filter",286,282,width-350,25,INK,bold);
            label(dc,L"loaded — список IP · none — без IPSet · any — любые адреса",286,370,width-350,30,MUTED,smallFont);
            label(dc,L"Активные фейки UDP: выберите файл и назначьте его ниже",286,518,width-350,22,MUTED,smallFont);
            label(dc,L"После изменения настроек, списков и фейков нажмите «Применить настройки» на главной странице.",286,682,width-350,42,MUTED,smallFont);
        } else if(page==MCP_PAGE) {
                rect(dc,274,142,width-318,120,PANEL);
                label(dc,mcp.installed()?L"MCP установлен · версия "+wide(mcp.version()):L"MCP не установлен",286,164,width-350,28,INK,bold);
                label(dc,clientCount?L"Подключённых ИИ-клиентов: "+std::to_wstring(clientCount):L"ИИ-клиент не подключён",286,212,width-350,28,MUTED);
                label(dc,L"Выберите свой ИИ-клиент",286,286,width-350,26,MUTED,smallFont);
                label(dc,L"MCP устанавливается и добавляется в настройки клиента автоматически. Перезапустите ИИ-клиент после подключения.",286,578,width-350,70,MUTED,smallFont);
        } else if(page==DIAGNOSTICS_PAGE) {
            rect(dc,274,142,width-318,478,PANEL);label(dc,L"Локальная проверка",286,161,width-350,24,INK,bold);
            label(dc,L"Кэш Discord переносится в резервные папки. Обнаруженные сетевые программы могут влиять на тесты.",286,682,width-350,36,MUTED,smallFont);
        }
        label(dc,notice,276,height-43,width-332,25,MUTED,smallFont);
    }
    void drawButton(DRAWITEMSTRUCT* d) {
        HDC destination=d->hDC;
        int bitmapWidth=d->rcItem.right-d->rcItem.left,bitmapHeight=d->rcItem.bottom-d->rcItem.top;
        if(bitmapWidth<=0||bitmapHeight<=0)return;
        HDC buffer=CreateCompatibleDC(destination);HBITMAP bitmap=CreateCompatibleBitmap(destination,bitmapWidth,bitmapHeight);
        if(!buffer||!bitmap){if(buffer)DeleteDC(buffer);if(bitmap)DeleteObject(bitmap);return;}
        auto previousBitmap=SelectObject(buffer,bitmap);
        DRAWITEMSTRUCT buffered=*d;buffered.hDC=buffer;buffered.rcItem={0,0,bitmapWidth,bitmapHeight};
        int destinationX=d->rcItem.left,destinationY=d->rcItem.top;d=&buffered;
        bool disabled=(d->itemState&ODS_DISABLED),pressed=(d->itemState&ODS_SELECTED);int id=d->CtlID;
        bool isNav=id>=NAV&&id<NAV+NAV_COUNT,active=isNav&&id-NAV==page;
        bool hovered=GetPropW(d->hwndItem,L"ZapretButtonHover")!=nullptr;
        bool highlighted=!disabled&&(hovered||((d->itemState&ODS_FOCUS)&&!(d->itemState&ODS_NOFOCUSRECT)));
        bool primary=id==IMPORT || id==SAVE || id==MCP_DOWNLOAD;
        HBRUSH background=CreateSolidBrush(isNav?SIDE:(id==MCP_DOWNLOAD?BG:PANEL));
        FillRect(d->hDC,&d->rcItem,background);DeleteObject(background);
        COLORREF color=isNav?(active?RGB(48,71,57):SIDE):(disabled?RGB(234,239,235):primary?GREEN:RGB(231,238,233));
        if(highlighted)color=isNav?(active?RGB(61,88,70):RGB(38,59,47)):(primary?RGB(38,122,89):RGB(211,229,218));
        if(pressed&&!disabled)color=isNav?RGB(43,66,51):(primary?RGB(20,81,57):RGB(185,215,197));
        HBRUSH b=CreateSolidBrush(color);auto oldB=SelectObject(d->hDC,b);auto oldP=SelectObject(d->hDC,GetStockObject(NULL_PEN));
        RoundRect(d->hDC,d->rcItem.left,d->rcItem.top,d->rcItem.right,d->rcItem.bottom,px(10),px(10));
        SelectObject(d->hDC,oldB);SelectObject(d->hDC,oldP);DeleteObject(b);
        wchar_t t[160];GetWindowTextW(d->hwndItem,t,160);RECT r=d->rcItem;r.left+=px(isNav?16:8);r.right-=px(8);
        SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,disabled?RGB(150,161,154):isNav||primary?RGB(245,252,247):INK);SelectObject(d->hDC,bold);
        DrawTextW(d->hDC,t,-1,&r,(isNav?DT_LEFT:DT_CENTER)|DT_VCENTER|DT_SINGLELINE);
        BitBlt(destination,destinationX,destinationY,bitmapWidth,bitmapHeight,buffer,0,0,SRCCOPY);
        SelectObject(buffer,previousBitmap);DeleteObject(bitmap);DeleteDC(buffer);
    }
    std::wstring diagnostics() {
        std::wstring r=L"Zapret GUI "+wide(GUI_VERSION)+L"\r\n\r\n";r+=engine.running?L"Служба Zapret запущена.\r\n":engine.installed?L"Служба Zapret остановлена.\r\n":L"Служба Zapret не установлена.\r\n";
        if(engine.installed&&!engine.owned)r+=L"Служба управляется другой программой. GUI её не меняет.\r\n";r+=L"\r\n"+systemDiagnostics();
        if(installs.empty())return r+L"\r\nНет импортированных установок.\r\n";
        for(const auto& v:installs) {
            bool ok=fs::is_regular_file(v.path/L"bin"/L"winws.exe")&&fs::is_directory(v.path/L"lists");
            r+=L"\r\n"+wide(v.id)+(ok?L": файлы доступны":L": файлы отсутствуют")+L"\r\n";
            for(const auto& s:v.strategies)r+=L"  "+wide(s)+(fs::is_regular_file(v.path/wide(s))?L" — доступна":L" — отсутствует")+L"\r\n";
        }return r;
    }
    void importFolder() {
        IFileDialog* dialog=nullptr; if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))throw std::runtime_error("Не удалось открыть выбор папки.");
        DWORD flags=0;dialog->GetOptions(&flags);dialog->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);dialog->SetTitle(L"Выберите распакованную папку Flowseal");
        if(SUCCEEDED(dialog->Show(window))) {
            IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))) {PWSTR path=nullptr;
                if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) {
                    fs::path root(path);CoTaskMemFree(path);item->Release();dialog->Release();dialog=nullptr;
                    auto v=inspect(root);for(const auto& old:installs)if(old.path==v.path){notice=L"Эта папка уже добавлена";mount();return;}
                    auto original=v.id;int suffix=2;while(std::any_of(installs.begin(),installs.end(),[&](auto& x){return x.id==v.id;}))v.id=original+" ("+std::to_string(suffix++)+")";
                    profile.version=v.id;profile.strategy=v.strategies.front();installs.push_back(v);save();notice=L"Версия добавлена. Исходные файлы не изменены.";mount();return;
                }item->Release();
            }
        }if(dialog)dialog->Release();
    }
    void copyContext() {
        auto s=wide(context(installs,profile).dump(2));
        if(!OpenClipboard(window))throw std::runtime_error("Буфер обмена занят.");
        HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,(s.size()+1)*sizeof(wchar_t));if(!mem){CloseClipboard();throw std::runtime_error("Нет памяти.");}
        auto data=GlobalLock(mem);if(!data){GlobalFree(mem);CloseClipboard();throw std::runtime_error("Нет памяти.");}
        memcpy(data,s.c_str(),(s.size()+1)*sizeof(wchar_t));GlobalUnlock(mem);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,mem))GlobalFree(mem);CloseClipboard();
        notice=L"Состояние приложения скопировано";InvalidateRect(window,nullptr,FALSE);
    }
    void installMcp(bool local);
    void runTests(bool strategies,bool all);
    void exportTests(){IFileSaveDialog* dialog=nullptr;if(FAILED(CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))throw std::runtime_error("Не удалось открыть сохранение отчёта.");dialog->SetFileName(L"zapret-tests.json");dialog->SetDefaultExtension(L"json");if(SUCCEEDED(dialog->Show(window))){IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR p=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&p))){writeBytes(fs::path(p),testReport.dump(2));CoTaskMemFree(p);notice=L"Отчёт сохранён";}item->Release();}}dialog->Release();InvalidateRect(window,nullptr,FALSE);}
    void job(std::function<JobResult()> work);
    void engineAction(const std::string& action);
    void installVersion(const std::string& tag,bool start);
    void finishMcp(JobResult* result){std::unique_ptr<JobResult> value(result);if(mcpWorker.joinable())mcpWorker.join();mcpBusy=false;testBusy=false;strategyTesting=false;notice=value->message;if(!value->report.is_null())testReport=value->report;if(!value->releases.empty())remoteReleases=value->releases;reload();engine=serviceState();mount();if(value->start)engineAction("install");if(value->telegram){backend.launchTelegram();backend.connectTelegram();}if(value->client){configureClient(value->clientId,mcp.connectionExecutable());notice=L"ИИ подключён. Перезапустите выбранный ИИ-клиент.";InvalidateRect(window,nullptr,FALSE);}}
    void action(int id,int code) {
        if(id>=NAV&&id<NAV+NAV_COUNT){int next=id-NAV;if(next!=page){page=next;mount();}return;}
        if(id==VERSION&&code==CBN_SELCHANGE&&page==HOME_PAGE){int i=(int)SendMessageW(control(VERSION),CB_GETCURSEL,0,0);if(i>=0&&i<(int)installs.size()){profile.version=installs[i].id;normalize();choices();save();}return;}
        if(id==VERSION&&code==LBN_SELCHANGE&&page==VERSIONS_PAGE){int i=(int)SendMessageW(control(VERSION),LB_GETCURSEL,0,0);if(i>=0&&i<(int)installs.size()){profile.version=installs[i].id;normalize();save();notice=L"Выбрана версия "+wide(profile.version);InvalidateRect(window,nullptr,FALSE);}return;}
        if(id==STRATEGY&&code==CBN_SELCHANGE){if(auto v=selected()){int i=(int)SendMessageW(control(STRATEGY),CB_GETCURSEL,0,0);if(i>=0&&i<(int)v->strategies.size()){profile.strategy=v->strategies[i];save();}}return;}
        if(id==IPSET&&code==CBN_SELCHANGE){int i=(int)SendMessageW(control(IPSET),CB_GETCURSEL,0,0);profile.ipset=i==0?"loaded":i==1?"none":"any";save();return;}
        if(id==TEST_PARALLEL&&code==CBN_SELCHANGE){testParallel=SendMessageW(control(TEST_PARALLEL),CB_GETCURSEL,0,0)==1;return;}
        if(code!=BN_CLICKED)return;
        switch(id) {
            case TEST_CURRENT:runTests(false,false);break;
            case TEST_SELECTED:runTests(true,false);break;
            case TEST_ALL:runTests(true,true);break;
            case TEST_CANCEL:cancelTests=true;if(strategyTesting)writeBytes(defaultConfig().parent_path()/L"test-cancel.flag","");notice=L"Останавливаем тесты и возвращаем прежний обход…";InvalidateRect(window,nullptr,FALSE);break;
            case TEST_PICK:if(!testReport.contains("best")||!testReport["best"].is_string())break;if(testReport.value("revision",uint64_t{0})!=revision||testReport.value("version","")!=profile.version)throw std::runtime_error("Профиль изменился после теста. Повторите тест для текущих настроек.");profile=validateChanges({{"strategy",testReport["best"]}},profile,installs);save();notice=L"Рекомендация выбрана. Нажмите «Применить настройки» на главной.";mount();break;
            case TEST_EXPORT:exportTests();break;
            case DISCORD_CACHE:{auto count=backupDiscordCache();notice=count?L"Кэш очищен; резервные папки сохранены":L"Папки кэша не найдены";InvalidateRect(window,nullptr,FALSE);break;}
            case FAKE_DISCORD:case FAKE_GAME:if(auto v=selected()){replaceFake(*v,utf8(text(FAKE_PICK)),id==FAKE_DISCORD);notice=L"Фейк сохранён с резервной копией. Примените настройки на главной.";InvalidateRect(window,nullptr,FALSE);}break;
            case USER_DOMAINS:case USER_EXCLUDES:if(auto v=selected()){auto file=v->path/L"lists"/(id==USER_DOMAINS?L"list-general-user.txt":L"list-exclude-user.txt");if(!fs::exists(file))writeBytes(file,"");auto parameters=quoteArg(file.wstring());if((INT_PTR)ShellExecuteW(window,L"open",systemExe(L"notepad.exe").c_str(),parameters.c_str(),nullptr,SW_SHOWNORMAL)<=32)throw std::runtime_error("Не удалось открыть список сайтов.");notice=L"По одному домену в строке. Сохраните список и примените настройки.";InvalidateRect(window,nullptr,FALSE);}break;
            case IMPORT:importFolder();break;
            case START:if(engine.running)engineAction("stop");else if(installs.empty())installVersion("",true);else engineAction("install");break;
            case AUTOSTART:engineAction("install");break;
            case SERVICE_REMOVE:engineAction("remove");break;
            case DOWNLOAD_LATEST:installVersion("",false);break;
            case LOAD_RELEASES:job([this]{JobResult r;r.releases=backend.releases();r.message=L"Выберите версию для установки";return r;});break;
            case DOWNLOAD_PICKED:{int i=(int)SendMessageW(control(RELEASE_PICK),CB_GETCURSEL,0,0);if(i>=0&&i<(int)remoteReleases.size())installVersion(remoteReleases[i],false);break;}
            case DELETE_VERSION:{int i=(int)SendMessageW(control(VERSION),LB_GETCURSEL,0,0);if(i>=0&&i<(int)installs.size()){backend.removeVersion(installs[i]);installs.erase(installs.begin()+i);normalize();save();notice=L"Версия удалена";mount();}break;}
            case TG_INSTALL:job([this]{JobResult r;r.message=L"Установлен Telegram-прокси "+wide(backend.installTelegram());r.telegram=true;return r;});break;
            case TG_CONNECT:backend.launchTelegram();backend.connectTelegram();break;
            case MCP_CONNECT:{int client=(int)SendMessageW(control(MCP_CLIENT),CB_GETCURSEL,0,0);job([this,client]{JobResult r;r.message=L"MCP установлен";mcp.downloadLatest("vladimir-mezh/zapret-gui-mcp");r.client=true;r.clientId=client;return r;});break;}
            case IPSET_UPDATE:{if(auto v=selected()){auto path=v->path;job([path]{auto data=httpsGet(L"https://raw.githubusercontent.com/Flowseal/zapret-discord-youtube/main/lists/ipset-all.txt",4*1024*1024);auto file=path/L"lists"/L"ipset-all.txt";if(fs::exists(file))fs::copy_file(file,path/L"lists"/L"ipset-all.txt.before-update",fs::copy_options::overwrite_existing);writeBytes(file,data);writeBytes(path/L"lists"/L"ipset-all.txt.backup",data);return JobResult{L"Список IP обновлён. Примените настройки на главной странице."};});}break;}
            case GUI_UPDATE:job([]{auto release=getRelease("vladimir-mezh/zapret-gui");auto tag=release.at("tag_name").get<std::string>();if(tag==std::string("v")+GUI_VERSION||tag==GUI_VERSION)return JobResult{L"Установлена последняя версия GUI"};auto exe=defaultConfig().parent_path()/L"gui"/wide(tag)/L"ZapretGUI.exe";auto data=checkedAsset(release,"vladimir-mezh/zapret-gui","ZapretGUI.exe");if(data.substr(0,2)!="MZ")throw std::runtime_error("Некорректный файл приложения.");writeBytes(exe,data);if((INT_PTR)ShellExecuteW(nullptr,L"open",exe.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)throw std::runtime_error("Не удалось открыть обновление.");return JobResult{L"Открыта новая версия GUI. Старое окно можно закрыть."};});break;
            case RELEASES:ShellExecuteW(window,L"open",L"https://github.com/Flowseal/zapret-discord-youtube/releases",nullptr,nullptr,SW_SHOWNORMAL);break;
            case GAME:profile.game=!profile.game;save();setText(GAME,profile.game?L"Game Filter: включён":L"Game Filter: выключен");break;
            case SAVE:save();notice=L"Профиль сохранён";InvalidateRect(window,nullptr,FALSE);break;
            case FORGET: {
                int i=(int)SendMessageW(control(VERSION),LB_GETCURSEL,0,0);if(i>=0&&i<(int)installs.size()) {installs.erase(installs.begin()+i);normalize();save();notice=L"Установка убрана из списка. Файлы сохранены.";mount();}break;
            }
            case DIAG:setText(TRANSCRIPT,diagnostics());notice=L"Проверка файлов завершена";InvalidateRect(window,nullptr,FALSE);break;
            case COPY:copyContext();break;
            case MCP_REPO_SAVE:repoDraft=utf8(text(MCP_REPO));mcp.setRepository(repoDraft);notice=L"Репозиторий MCP сохранён";InvalidateRect(window,nullptr,FALSE);break;
            case MCP_DOWNLOAD:installMcp(false);break;
            case MCP_LOCAL:installMcp(true);break;
            case MCP_CONFIG:{auto s=wide(mcp.clientConfig().dump(2));if(!OpenClipboard(window))throw std::runtime_error("Буфер обмена занят.");
                auto memory=GlobalAlloc(GMEM_MOVEABLE,(s.size()+1)*sizeof(wchar_t));if(!memory){CloseClipboard();throw std::runtime_error("Нет памяти.");}
                auto ptr=GlobalLock(memory);if(!ptr){GlobalFree(memory);CloseClipboard();throw std::runtime_error("Нет памяти.");}
                memcpy(ptr,s.c_str(),(s.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,memory))GlobalFree(memory);CloseClipboard();notice=L"Конфигурация скопирована. Добавьте её в свой ИИ-клиент.";InvalidateRect(window,nullptr,FALSE);break;}
        }
    }
};
void App::installMcp(bool local) {
    std::vector<wchar_t> buffer(32768);DWORD n=GetModuleFileNameW(nullptr,buffer.data(),(DWORD)buffer.size());
    if(!n||n>=buffer.size())throw std::runtime_error("Не удалось определить папку приложения.");
    auto source=fs::path(std::wstring(buffer.data(),n)).parent_path()/L"mcp";
    if(local){notice=L"Установлен MCP "+wide(mcp.installFolder(source));mount();return;}
    job([this]{return JobResult{L"Установлен MCP "+wide(mcp.downloadLatest("vladimir-mezh/zapret-gui-mcp"))};});
}
void App::job(std::function<JobResult()> work){if(mcpBusy)return;if(mcpWorker.joinable())mcpWorker.join();mcpBusy=true;notice=L"Выполняем настройку…";mount();
    mcpWorker=std::thread([this,work]{auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);auto result=std::make_unique<JobResult>();try{*result=work();}catch(const std::exception& e){result->message=wide(e.what());}if(!closing){auto p=result.release();if(!PostMessageW(window,MCP_DONE,0,(LPARAM)p))delete p;}if(SUCCEEDED(initialized))CoUninitialize();});
}
void App::runTests(bool strategies,bool all){
    if(mcpBusy)return;save();auto config=defaultConfig(),reportPath=config.parent_path()/L"test-report.json";auto version=selected()?selected()->path:fs::path{};cancelTests=false;testBusy=true;strategyTesting=strategies;testReport={{"message","Проверяем доступность…"},{"done",false}};
    if(strategies){std::error_code error;fs::remove(config.parent_path()/L"test-cancel.flag",error);fs::remove(reportPath,error);auto exe=ownExecutable();auto parentId=GetCurrentProcessId();bool parallel=testParallel;job([this,exe,config,reportPath,all,parentId,parallel]{
        auto parameters=L"--test-action "+std::wstring(all?(parallel?L"all-parallel":L"all"):L"selected")+L" --config "+quoteArg(config.wstring())+L" --parent "+std::to_wstring(parentId);SHELLEXECUTEINFOW s{sizeof(s)};s.fMask=SEE_MASK_NOCLOSEPROCESS;s.lpVerb=L"runas";s.lpFile=exe.c_str();s.lpParameters=parameters.c_str();s.nShow=SW_HIDE;
        if(!ShellExecuteExW(&s))throw std::runtime_error(GetLastError()==ERROR_CANCELLED?"Запрос прав для тестов отменён.":"Не удалось запустить тесты.");if(!s.hProcess)throw std::runtime_error("Нет доступа к тестовому процессу.");
        while(WaitForSingleObject(s.hProcess,250)==WAIT_TIMEOUT)if(closing||cancelTests)try{writeBytes(config.parent_path()/L"test-cancel.flag","");}catch(...){}
        CloseHandle(s.hProcess);if(!fs::exists(reportPath))throw std::runtime_error("Тесты не записали отчёт. Прежний обход сохранён.");JobResult result;result.report=Json::parse(fileBytes(reportPath,2*1024*1024));result.message=wide(result.report.value("message","Тесты завершены"));if(result.report.contains("restore_error"))result.message=L"Не удалось вернуть прежнюю службу. Проверьте диагностику.";return result;
    });}else job([this,version,reportPath]{auto rows=probeTargets(testTargets(version),[this]{return closing||cancelTests;});JobResult result;result.report={{"schema",1},{"kind","connectivity"},{"done",true},{"cancelled",cancelTests.load()},{"targets",rows},{"score",testScore(rows)},{"message",cancelTests?"Проверка остановлена":"Проверка завершена. TLS/HTTP не подтверждает работу голоса или видео."}};writeJsonAtomic(reportPath,result.report);result.message=wide(result.report["message"]);return result;});
}
void App::engineAction(const std::string& action){save();auto exe=ownExecutable();auto config=defaultConfig();job([exe,config,action]{
    auto resultFile=config.parent_path()/L"service-result.json";std::error_code error;fs::remove(resultFile,error);
    auto parameters=L"--engine-action "+wide(action)+L" --config "+quoteArg(config.wstring());SHELLEXECUTEINFOW s{sizeof(s)};s.fMask=SEE_MASK_NOCLOSEPROCESS;s.lpVerb=L"runas";s.lpFile=exe.c_str();s.lpParameters=parameters.c_str();s.nShow=SW_HIDE;
    if(!ShellExecuteExW(&s))throw std::runtime_error(GetLastError()==ERROR_CANCELLED?"Запрос прав администратора отменён.":"Не удалось запросить права администратора.");
    if(!s.hProcess)throw std::runtime_error("Не удалось дождаться установки службы.");WaitForSingleObject(s.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(s.hProcess,&code);CloseHandle(s.hProcess);
    if(code!=0){if(fs::exists(resultFile))throw std::runtime_error(Json::parse(fileBytes(resultFile,16384)).value("error","Не удалось настроить службу."));throw std::runtime_error("Не удалось настроить службу.");}
    return JobResult{action=="stop"?L"Обход выключен":action=="remove"?L"Автозапуск удалён":L"Обход включён. Можно закрыть GUI."};
});}
void App::installVersion(const std::string& tag,bool start){auto previous=selected()?selected()->path:fs::path{};job([this,tag,start,previous]{
    auto version=backend.downloadZapret(tag,previous);auto state=store.load();state=store.importVersion(version.path,state.revision);
    auto updated=state.profile;updated.version=version.id;if(std::find(version.strategies.begin(),version.strategies.end(),updated.strategy)==version.strategies.end())updated.strategy=std::find(version.strategies.begin(),version.strategies.end(),"general.bat")!=version.strategies.end()?"general.bat":version.strategies.front();state.profile=updated;store.save(state,state.revision);
    JobResult r;r.message=L"Установлен Zapret "+wide(version.id)+L". Предыдущие версии сохранены.";r.start=start;return r;
});}
static App* app=nullptr;
LRESULT CALLBACK wndProc(HWND h,UINT msg,WPARAM w,LPARAM l) {
    try {
        switch(msg) {
            case WM_CREATE:app->window=h;app->scale=GetDpiForWindow(h)/96.f;app->init();return 0;
            case WM_SIZE:if(app&&app->window){app->layout();InvalidateRect(h,nullptr,TRUE);}return 0;
            case WM_GETMINMAXINFO:((MINMAXINFO*)l)->ptMinTrackSize={app?app->px(1000):1000,app?app->px(760):760};return 0;
            case WM_DPICHANGED: {auto r=(RECT*)l;app->scale=HIWORD(w)/96.f;app->fonts();SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);app->mount();return 0;}
            case WM_ERASEBKGND:return 1;
            case WM_PAINT: {PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);auto memory=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);auto old=SelectObject(memory,bitmap);app->paint(memory);BitBlt(dc,0,0,r.right,r.bottom,memory,0,0,SRCCOPY);SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);EndPaint(h,&ps);return 0;}
            case WM_DRAWITEM:app->drawButton((DRAWITEMSTRUCT*)l);return TRUE;
            case WM_CTLCOLORBTN: {int id=GetDlgCtrlID((HWND)l);SetBkColor((HDC)w,id>=NAV&&id<NAV+NAV_COUNT?SIDE:PANEL);return (LRESULT)(id>=NAV&&id<NAV+NAV_COUNT?app->sideBrush:app->panelBrush);}
            case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORSTATIC:SetTextColor((HDC)w,INK);SetBkColor((HDC)w,PANEL);return (LRESULT)app->panelBrush;
            case WM_COMMAND:app->action(LOWORD(w),HIWORD(w));return 0;
            case WM_TIMER:app->poll();return 0;
            case MCP_DONE:app->finishMcp((JobResult*)l);return 0;
            case WM_CLOSE:app->closing=true;app->cancelTests=true;if(app->strategyTesting)try{writeBytes(defaultConfig().parent_path()/L"test-cancel.flag","");}catch(...){}KillTimer(h,1);DestroyWindow(h);return 0;
            case WM_DESTROY:PostQuitMessage(0);return 0;
        }
    }catch(const std::exception& e){MessageBoxW(h,wide(e.what()).c_str(),L"Zapret GUI",MB_OK|MB_ICONINFORMATION);return msg==WM_CREATE?-1:0;}
    return DefWindowProcW(h,msg,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);
    int argumentCount=0;auto arguments=CommandLineToArgvW(GetCommandLineW(),&argumentCount);
    if(arguments&&argumentCount==7&&std::wstring(arguments[1])==L"--test-action"&&std::wstring(arguments[3])==L"--config"&&std::wstring(arguments[5])==L"--parent"){
        fs::path config(arguments[4]);auto mode=std::wstring(arguments[2]);DWORD parentId=wcstoul(arguments[6],nullptr,10);LocalFree(arguments);try{if(mode!=L"all"&&mode!=L"all-parallel"&&mode!=L"selected")throw std::runtime_error("Неизвестный режим теста.");runStrategyTests(config,mode!=L"selected",parentId,mode==L"all-parallel"?128:1);CoUninitialize();return 0;}catch(const std::exception& e){try{writeJsonAtomic(config.parent_path()/L"test-report.json",{{"done",true},{"error",e.what()},{"message",e.what()}});}catch(...){}CoUninitialize();return 1;}
    }
    if(arguments&&argumentCount==5&&std::wstring(arguments[1])==L"--engine-action"&&std::wstring(arguments[3])==L"--config"){
        fs::path config(arguments[4]);auto action=utf8(arguments[2]);LocalFree(arguments);try{serviceAction(action,config);writeJsonAtomic(config.parent_path()/L"service-result.json",{{"ok",true}});CoUninitialize();return 0;}catch(const std::exception& e){try{writeJsonAtomic(config.parent_path()/L"service-result.json",{{"error",e.what()}});}catch(...){}CoUninitialize();return 1;}
    }
    if(argumentCount==1)try{if(installApplication()){if(arguments)LocalFree(arguments);CoUninitialize();return 0;}}catch(const std::exception& e){if(arguments)LocalFree(arguments);MessageBoxW(nullptr,wide(e.what()).c_str(),L"Zapret GUI",MB_OK|MB_ICONINFORMATION);CoUninitialize();return 1;}
    App state;app=&state;state.instance=instance;
    bool settings=arguments&&argumentCount==2&&std::wstring(arguments[1])==L"--settings";
    if(arguments)LocalFree(arguments);if(settings)state.page=SETTINGS_PAGE;
    auto className=L"ZapretGUIWindow-"+wide(GUI_VERSION);WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.hInstance=instance;cls.lpfnWndProc=wndProc;cls.lpszClassName=className.c_str();cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassExW(&cls);
    UINT dpi=GetDpiForSystem();RECT r={0,0,MulDiv(1120,dpi,96),MulDiv(760,dpi,96)};AdjustWindowRectExForDpi(&r,WS_OVERLAPPEDWINDOW,FALSE,0,dpi);
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"Zapret GUI",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,nullptr);
    if(!window){CoUninitialize();return 1;}ShowWindow(window,settings?SW_SHOWNOACTIVATE:show);UpdateWindow(window);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    CoUninitialize();return 0;
}
