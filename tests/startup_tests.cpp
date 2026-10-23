#include "../src/main.cpp"
#include <iostream>

static unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
int wmain(int argc,wchar_t** argv) try {
    if(argc!=2)throw std::runtime_error("Specify an owned startup evidence directory.");
    check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Cannot start startup fixture");
    struct Finish {~Finish(){CoUninitialize();}} finish;
    const std::wstring key=L"Software\\LumenShotFixture-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    HKEY owned=nullptr;DWORD created=0;
    check_registry(RegCreateKeyExW(HKEY_CURRENT_USER,key.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&owned,&created),"Cannot create isolated fixture key");
    require(created==REG_CREATED_NEW_KEY,"Refusing to reuse an existing registry key");
    struct Remove {HKEY value;std::wstring key;~Remove(){RegCloseKey(value);RegDeleteKeyW(HKEY_CURRENT_USER,key.c_str());}} remove{owned,key};
    const wchar_t other[]=L"Owned unrelated-value fixture";
    check_registry(RegSetValueExW(owned,L"OtherApp",0,REG_SZ,reinterpret_cast<const BYTE*>(other),sizeof(other)),"Cannot seed unrelated fixture value");
    const std::wstring command=L"\"G:\\App Folder\\\u622a\u56fe.exe\" --tray";
    StartupRegistration registration(key,command);
    require(!registration.enabled(),"Startup must be opt-in");
    registration.set_enabled(true);
    require(registration.enabled() && registration.read()==command,"Startup lost quoted path, Unicode, or tray flag");
    registration.set_enabled(true);
    require(registration.read()==command,"Repeated enable changed registration");
    StartupRegistration elsewhere(key,L"\"G:\\Other Folder\\LumenShot.exe\" --tray");
    require(!elsewhere.enabled(),"Another portable copy was mistaken for this one");
    elsewhere.set_enabled(false);
    require(registration.read()==command,"Disabling another copy deleted this registration");
    registration.set_enabled(false);
    require(!registration.enabled() && registration.read().empty(),"Disable left startup registration behind");
    registration.set_enabled(false);
    wchar_t value[256]{};DWORD bytes=sizeof(value);
    require(RegGetValueW(HKEY_CURRENT_USER,key.c_str(),L"OtherApp",RRF_RT_REG_SZ,nullptr,value,&bytes)==ERROR_SUCCESS
            && std::wstring(value)==other,"Startup changed another value");
    bool rejected=false;
    try{StartupRegistration too_long(key,std::wstring(261,L'x'));too_long.set_enabled(true);}catch(const std::exception&){rejected=true;}
    require(rejected && registration.read().empty(),"Overlong startup path was accepted or modified registry");
    App app;app.startup=StartupRegistration(key);
    app.settings.folder=std::filesystem::path(argv[1]);
    app.settings.key=VK_F9;app.settings.modifiers=MOD_CONTROL|MOD_SHIFT|MOD_ALT;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"HdrStartupFixture";RegisterClassW(&wc);
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"Owned startup settings",WS_OVERLAPPEDWINDOW,0,0,714,750,nullptr,nullptr,wc.hInstance,&app);
    require(window!=nullptr,"Startup control window missing");
    struct Close {HWND value;~Close(){DestroyWindow(value);}} close{window};
    require(SendDlgItemMessageW(window,StartWithWindows,BM_GETCHECK,0,0)==BST_UNCHECKED,"Startup checkbox must begin unchecked");
    SendDlgItemMessageW(window,StartWithWindows,BM_SETCHECK,BST_CHECKED,0);
    SendMessageW(window,WM_COMMAND,StartWithWindows,0);
    require(app.startup.enabled(),"Checkbox did not automatically enable isolated startup");
    require(app.startup.read()==startup_command(),"Startup did not use the actual executable and tray flag");
    require(!IsWindowVisible(window),"Automatic startup change unexpectedly showed settings");
    SendDlgItemMessageW(window,StartWithWindows,BM_SETCHECK,BST_UNCHECKED,0);
    SendMessageW(window,WM_COMMAND,StartWithWindows,0);
    require(!app.startup.enabled(),"Checkbox did not automatically remove isolated entry");
    std::cout<<checks<<" startup checks passed using one isolated fixture key. Real Windows startup registration was not modified.\n";
    return 0;
} catch(const std::exception& error){std::cerr<<"Startup fixture failed after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
