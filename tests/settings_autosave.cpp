#include "../src/main.cpp"
#include <iostream>

static unsigned checks=0;
void require(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
void wait_for_edits(App& app) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(app.pending_edits && std::chrono::steady_clock::now()<deadline) {
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        Sleep(5);
    }
    require(app.pending_edits==0,"Automatic edit timer did not finish");
}
void save_settings_preview(HWND window,const std::filesystem::path& path) {
    RECT rect{};GetClientRect(window,&rect);
    Image image;image.width=rect.right;image.height=rect.bottom;image.bgra.resize(checked_pixels(image.width,image.height)*4);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=rect.right;info.bmiHeader.biHeight=-rect.bottom;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(nullptr);void* pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!dc || !bitmap) throw std::runtime_error("Cannot render owned settings preview.");
    HGDIOBJ old=SelectObject(dc,bitmap);
    SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
    GdiFlush();std::memcpy(image.bgra.data(),pixels,image.bgra.size());
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    for(std::size_t i=3;i<image.bgra.size();i+=4) image.bgra[i]=255;
    std::filesystem::create_directories(path.parent_path());save_image(path,image);
}
int wmain(int argc,wchar_t** argv) try {
    if(argc!=2)throw std::runtime_error("Specify an owned automatic-settings evidence directory.");
    check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Cannot initialize automatic-settings fixture");
    struct Finish {~Finish(){CoUninitialize();}} finish;
    const std::wstring key=L"Software\\LumenShotAutosave-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    HKEY owned=nullptr;DWORD created=0;
    check_registry(RegCreateKeyExW(HKEY_CURRENT_USER,key.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&owned,&created),"Cannot create isolated automatic-settings key");
    require(created==REG_CREATED_NEW_KEY,"Refusing to reuse a registry fixture key");
    struct Remove {HKEY value;std::wstring key;~Remove(){RegCloseKey(value);RegDeleteKeyW(HKEY_CURRENT_USER,key.c_str());}} remove{owned,key};
    App app;app.startup=StartupRegistration(key);app.settings=Settings{};
    app.settings.folder=std::filesystem::path(argv[1]);app.settings.key=VK_F9;app.settings.modifiers=MOD_CONTROL|MOD_SHIFT|MOD_ALT;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"HdrAutosaveFixture";RegisterClassW(&wc);
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"Owned automatic settings",WS_OVERLAPPEDWINDOW,0,0,714,750,nullptr,nullptr,wc.hInstance,&app);
    require(window!=nullptr && app.registered_key==VK_F9,"Cannot create owned settings with its fixture shortcut");
    struct Close {HWND value;~Close(){if(IsWindow(value))DestroyWindow(value);}} close{window};
    const auto changed=[&](int id,int code=BN_CLICKED){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,code),reinterpret_cast<LPARAM>(GetDlgItem(window,id)));};
    const auto choose=[&](int id,int index){SendDlgItemMessageW(window,id,CB_SETCURSEL,index,0);changed(id,CBN_SELCHANGE);};
    const auto toggle=[&](int id,bool value){SendDlgItemMessageW(window,id,BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);changed(id);};
    ShowWindow(window,SW_SHOWNOACTIVATE);app.show_page(2);
    toggle(DarkMode,false);
    require(!app.settings.dark_mode && !load_settings().dark_mode && !app.theme.dark,"Dark mode required manual saving");
    require(IsWindowVisible(window),"Automatic checkbox save hid settings");
    toggle(DarkMode,true);require(load_settings().dark_mode && app.theme.dark,"Dark mode did not restore automatically");
    app.show_page(0);
    choose(BackendChoice,2);require(app.settings.backend==CaptureBackend::Desktop && load_settings().backend==CaptureBackend::Desktop,"Backend choice did not apply automatically");
    choose(BackendChoice,0);
    if(!app.displays.empty()) {
        choose(MonitorChoice,1);require(load_settings().monitor==app.displays.front().device_name,"Monitor choice did not persist automatically");
        choose(MonitorChoice,0);require(load_settings().monitor.empty(),"Active-monitor choice did not persist");
    }
    app.show_page(1);
    HWND sharpness=GetDlgItem(window,Sharpness);SetFocus(sharpness);
    require(Settings{}.sharpness==0 && SendMessageW(sharpness,TBM_GETPOS,0,0)==0,"Sharpness must default to off");
    require(text(GetDlgItem(window,SharpnessValue))==L"Off","Zero sharpness label is unclear");
    const auto slide=[&](unsigned value,UINT event) {
        SendMessageW(sharpness,TBM_SETPOS,TRUE,value);
        SendMessageW(window,WM_HSCROLL,MAKEWPARAM(event,value),reinterpret_cast<LPARAM>(sharpness));
    };
    for(unsigned value:{10u,20u,35u}) slide(value,TB_THUMBTRACK);
    require(app.settings.sharpness==0 && load_settings().sharpness==0,"Dragging sharpness saved before debounce");
    require(app.pending_edits && text(GetDlgItem(window,SharpnessValue))==L"35%","Dragging did not update sharpness feedback");
    slide(35,TB_ENDTRACK);
    require(app.settings.sharpness==35 && load_settings().sharpness==35 && !app.pending_edits,"Releasing sharpness did not persist its final value");
    require(GetFocus()==sharpness && IsWindowVisible(window),"Saving sharpness moved focus or hid settings");
    SendMessageW(sharpness,WM_KEYDOWN,VK_RIGHT,0);SendMessageW(sharpness,WM_KEYUP,VK_RIGHT,0);
    wait_for_edits(app);
    require(app.settings.sharpness==36 && load_settings().sharpness==36,"Keyboard sharpness adjustment did not save");
    app.capture_settings=app.settings;app.busy=true;app.set_status(L"Capturing...");
    slide(15,TB_ENDTRACK);
    require(app.settings.sharpness==15 && app.capture_settings.sharpness==36,"Sharpness edit changed an in-flight capture");
    require(text(app.status)==L"Capturing...","Sharpness saving obscured capture status");app.busy=false;
    HWND image_size=GetDlgItem(window,ImageSize);SetFocus(image_size);
    require(Settings{}.scale==100,"Image size must default to Original");
    for(unsigned index=1;index<image_scales.size();++index) {
        choose(ImageSize,index);
        require(app.settings.scale==image_scales[index] && load_settings().scale==image_scales[index],"Image size did not save automatically");
        require(GetFocus()==image_size && IsWindowVisible(window),"Saving image size moved focus or hid settings");
    }
    app.capture_settings=app.settings;app.busy=true;app.set_status(L"Capturing...");
    choose(ImageSize,2);
    require(app.settings.scale==150 && app.capture_settings.scale==200,"Image size edit changed an in-flight capture");
    require(text(app.status)==L"Capturing...","Image size saving obscured capture status");app.busy=false;
    save_settings_preview(window,std::filesystem::path(argv[1])/L"sharpness-dark.png");
    toggle(DarkMode,false);save_settings_preview(window,std::filesystem::path(argv[1])/L"sharpness-light.png");toggle(DarkMode,true);
    slide(100,TB_ENDTRACK);require(load_settings().sharpness==100,"Maximum sharpness failed to persist");
    slide(0,TB_ENDTRACK);require(load_settings().sharpness==0 && text(GetDlgItem(window,SharpnessValue))==L"Off","Sharpness did not return to an exact bypass");
    for(const wchar_t* invalid:{L"-1",L"101",L"nan"}) {
        require(WritePrivateProfileStringW(L"Screenshot",L"Sharpness",invalid,config_path().c_str()),"Cannot seed invalid fixture sharpness");
        require(load_settings().sharpness==0,"Invalid saved sharpness enabled enhancement");
    }
    choose(ImageSize,0);require(load_settings().scale==100,"Original image size did not restore");
    for(const wchar_t* invalid:{L"-1",L"0",L"110",L"201",L"nan"}) {
        require(WritePrivateProfileStringW(L"Screenshot",L"Scale",invalid,config_path().c_str()),"Cannot seed invalid fixture image size");
        require(load_settings().scale==100,"Invalid saved image size changed output dimensions");
    }
    save_settings(app.settings);
    HWND content=GetDlgItem(window,ContentChoice);SetForegroundWindow(window);SetFocus(content);
    SendMessageW(content,CB_SHOWDROPDOWN,TRUE,0);
    require(SendMessageW(content,CB_GETDROPPEDSTATE,0,0),"Cannot open owned settings dropdown");
    choose(ContentChoice,2);
    require(load_settings().color.mode==ContentMode::Sdr && app.settings.color.mode==ContentMode::Sdr,"Content choice did not persist automatically");
    require(SendMessageW(content,CB_GETDROPPEDSTATE,0,0),"Automatic saving closed dropdown");
    require(GetFocus()==content && IsWindowVisible(window),"Automatic saving moved focus or hid settings");
    SendMessageW(content,CB_SHOWDROPDOWN,FALSE,0);
    choose(ContentChoice,1);
    choose(HdrFormatChoice,1);require(load_settings().hdr_format==HdrFormat::JpegXr,"HDR format required manual saving");
    toggle(KeepOriginal,false);require(!load_settings().keep_original && !IsWindowVisible(GetDlgItem(window,HdrFormatChoice)),"HDR retention required manual saving");
    toggle(KeepOriginal,true);choose(HdrFormatChoice,0);
    SetFocus(GetDlgItem(window,PaperWhite));SetDlgItemTextW(window,PaperWhite,L"250");
    require(app.settings.color.paper_white==203,"Text edit saved before debounce");
    wait_for_edits(app);
    require(app.settings.color.paper_white==250 && load_settings().color.paper_white==250,"Valid reference white did not save automatically");
    SetDlgItemTextW(window,PaperWhite,L"2");wait_for_edits(app);
    require(app.settings.color.paper_white==250 && load_settings().color.paper_white==250,"Partial numeric edit replaced valid reference white");
    require(text(app.status).find(L"40 and 2000")!=std::wstring::npos,"Invalid reference-white status is unhelpful");
    toggle(KeepOriginal,false);require(!load_settings().keep_original,"Invalid numeric draft blocked unrelated checkbox");
    SendMessageW(window,WM_COMMAND,CaptureButton,0);
    require(!app.busy && app.countdown==0,"Invalid numeric draft started capture");
    SetDlgItemTextW(window,PaperWhite,L"nan");wait_for_edits(app);
    require(app.settings.color.paper_white==250,"Nonfinite reference white was accepted");
    SetDlgItemTextW(window,PaperWhite,L"203");wait_for_edits(app);
    require(!app.invalid_edits && load_settings().color.paper_white==203,"Corrected reference white did not clear validation error");
    SetFocus(GetDlgItem(window,Exposure));SetDlgItemTextW(window,Exposure,L"-");wait_for_edits(app);
    require(load_settings().color.exposure==0 && app.invalid_edits,"Partial exposure was persisted");
    SetDlgItemTextW(window,Exposure,L"1junk");wait_for_edits(app);
    require(load_settings().color.exposure==0,"Trailing exposure junk was accepted");
    SetDlgItemTextW(window,Exposure,L"1.25");wait_for_edits(app);
    require(load_settings().color.exposure==1.25 && !app.invalid_edits,"Valid exposure did not apply automatically");
    app.capture_settings=app.settings;app.busy=true;app.set_status(L"Capturing...");
    SetDlgItemTextW(window,Exposure,L"2");wait_for_edits(app);
    require(app.settings.color.exposure==2 && app.capture_settings.color.exposure==1.25,"Automatic edit changed in-flight capture settings");
    require(text(app.status)==L"Capturing...","Automatic saving obscured active capture status");app.busy=false;
    HWND collision=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Owned shortcut collision",WS_POPUP,0,0,0,0,nullptr,nullptr,wc.hInstance,nullptr);
    require(collision && RegisterHotKey(collision,777,MOD_CONTROL|MOD_SHIFT|MOD_ALT|MOD_NOREPEAT,VK_F11),"Cannot reserve owned collision shortcut");
    struct Release {HWND value;~Release(){UnregisterHotKey(value,777);DestroyWindow(value);}} release{collision};
    choose(KeyChoice,11);
    require(app.settings.key==VK_F9 && app.registered_key==VK_F9 && load_settings().key==VK_F9,"Shortcut conflict lost previous working shortcut");
    require(SendDlgItemMessageW(window,KeyChoice,CB_GETCURSEL,0,0)==9 && text(app.status).find(L"already in use")!=std::wstring::npos,"Shortcut conflict did not restore UI or explain failure");
    UnregisterHotKey(collision,777);choose(KeyChoice,11);
    require(app.registered_key==VK_F11 && load_settings().key==VK_F11,"Valid shortcut change did not apply automatically");
    choose(KeyChoice,9);
    UnregisterHotKey(window,hotkey_id);app.registered_key=0;
    toggle(KeepOriginal,true);
    require(load_settings().keep_original && !app.registered_key,"Unavailable shortcut blocked unrelated setting");
    require(text(app.status).find(L"shortcut is unavailable")!=std::wstring::npos,"Automatic save hid unavailable-shortcut warning");
    changed(KeyChoice,CBN_SELCHANGE);require(app.registered_key==VK_F9,"Shortcut change did not retry registration");
    SetDlgItemTextW(window,ControlId::Folder,L"relative-folder");wait_for_edits(app);
    require(app.settings.folder==std::filesystem::path(argv[1]),"Relative folder replaced valid output path");
    const auto output=std::filesystem::path(argv[1])/L"new-output";
    SetDlgItemTextW(window,ControlId::Folder,output.c_str());SendMessageW(window,WM_CLOSE,0,0);
    require(load_settings().folder==output && !IsWindowVisible(window),"Closing settings lost pending folder edit");
    require(text(GetDlgItem(window,SaveButton))==L"Hide to tray","Obsolete manual-save label remains");
    SetDlgItemTextW(window,Exposure,L"0");SendMessageW(window,WM_COMMAND,ExitButton,0);
    require(!IsWindow(window) && load_settings().color.exposure==0,"Exit lost pending edit");
    std::cout<<checks<<" automatic settings checks passed. Isolated INI and startup key only; no desktop capture, clipboard, game, or launcher was used.\n";
    return 0;
} catch(const std::exception& error){std::cerr<<"Automatic settings fixture failed after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}