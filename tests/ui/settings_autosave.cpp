#include "app/app.hpp"
#include <iostream>
#include "image/scaling.hpp"

using namespace hdrshot;

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
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    require(PrintWindow(window,dc,PW_CLIENTONLY)!=FALSE,"Cannot print owned settings preview");
    GdiFlush();std::memcpy(image.bgra.data(),pixels,image.bgra.size());
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    for(std::size_t i=3;i<image.bgra.size();i+=4) image.bgra[i]=255;
    std::filesystem::create_directories(path.parent_path());save_image(path,image);
}
int wmain(int argc,wchar_t** argv) try {
    if(argc!=2)throw std::runtime_error("Specify an owned automatic-settings evidence directory.");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Cannot initialize automatic-settings fixture");
    struct Finish {~Finish(){CoUninitialize();}} finish;
    const std::wstring key=L"Software\\LumenShotAutosave-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    HKEY owned=nullptr;DWORD created=0;
    check_registry(RegCreateKeyExW(HKEY_CURRENT_USER,key.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&owned,&created),"Cannot create isolated automatic-settings key");
    require(created==REG_CREATED_NEW_KEY,"Refusing to reuse a registry fixture key");
    struct Remove {HKEY value;std::wstring key;~Remove(){RegCloseKey(value);RegDeleteKeyW(HKEY_CURRENT_USER,key.c_str());}} remove{owned,key};
    require(!std::filesystem::exists(config_path()),"Refusing to reuse an existing settings file");
    require(Settings{}.color.mode==ContentMode::Auto && load_settings().color.mode==ContentMode::Auto,
            "Fresh settings must use Automatic content detection");
    require(WritePrivateProfileStringW(L"Screenshot",L"Content",L"1",config_path().c_str())!=FALSE,"Cannot write owned HDR preference");
    require(load_settings().color.mode==ContentMode::Hdr,"Automatic default replaced a saved HDR preference");
    require(WritePrivateProfileStringW(L"Screenshot",L"Content",nullptr,config_path().c_str())!=FALSE,"Cannot remove owned content key");
    require(load_settings().color.mode==ContentMode::Auto,"Missing content preference did not use Automatic");
    require(Settings{}.color.peak_method==PeakMethod::Robust && load_settings().color.peak_method==PeakMethod::Robust,
            "Fresh settings must use the compared robust peak method");
    require(WritePrivateProfileStringW(L"Screenshot",L"PeakMethod",L"0",config_path().c_str())!=FALSE,"Cannot write owned observed-peak preference");
    require(load_settings().color.peak_method==PeakMethod::Observed,"Observed peak fallback was not restored");
    require(WritePrivateProfileStringW(L"Screenshot",L"PeakMethod",nullptr,config_path().c_str())!=FALSE,"Cannot remove owned peak key");
    require(load_settings().color.peak_method==PeakMethod::Robust,"Missing peak preference did not use the compared default");
    require(!Settings{}.show_magnifier && !load_settings().show_magnifier,"Magnifier must default to disabled");
    require(!Settings{}.window_snapping && !load_settings().window_snapping,"Window snapping must default to disabled");
    for(const wchar_t* preference:{L"ShowMagnifier",L"WindowSnapping"}) {
        for(const wchar_t* value:{L"1",L"0"}) {
            require(WritePrivateProfileStringW(L"Screenshot",preference,value,config_path().c_str())!=FALSE,"Cannot write owned selector preference");
            const auto saved=load_settings();
            require((wcscmp(preference,L"ShowMagnifier")==0?saved.show_magnifier:saved.window_snapping)==(value[0]==L'1'),
                    "Selector default replaced an explicit saved preference");
        }
        require(WritePrivateProfileStringW(L"Screenshot",preference,nullptr,config_path().c_str())!=FALSE,"Cannot remove owned selector preference");
    }
    require(!load_settings().show_magnifier && !load_settings().window_snapping,"Missing selector preferences did not restore disabled defaults");
    require(!Settings{}.annotate_hdr && !load_settings().annotate_hdr,"HDR drawings must require an explicit choice");
    for(const wchar_t* invalid:{L"-1",L"2",L"nan"}) {
        require(WritePrivateProfileStringW(L"Screenshot",L"AnnotateHdr",invalid,config_path().c_str()),"Cannot seed invalid HDR annotation preference");
        require(!load_settings().annotate_hdr,"Invalid preference enabled HDR drawings");
    }
    require(WritePrivateProfileStringW(L"Screenshot",L"AnnotateHdr",nullptr,config_path().c_str()),"Cannot remove owned HDR annotation preference");
    require(Settings{}.hdr_jpeg_quality==HdrJpegQuality::Full && load_settings().hdr_jpeg_quality==HdrJpegQuality::Full,
            "Existing and fresh settings must retain full JPEG quality");
    for(unsigned index=0;index<4;++index) {
        require(WritePrivateProfileStringW(L"Screenshot",L"HdrJpegQuality",std::to_wstring(index).c_str(),config_path().c_str()),"Cannot seed JPEG quality");
        require(load_settings().hdr_jpeg_quality==HdrJpegQuality(index),"Saved JPEG quality was not restored");
    }
    for(const wchar_t* invalid:{L"-1",L"4",L"4294967295",L"nan"}) {
        require(WritePrivateProfileStringW(L"Screenshot",L"HdrJpegQuality",invalid,config_path().c_str()),"Cannot seed invalid JPEG quality");
        require(load_settings().hdr_jpeg_quality==HdrJpegQuality::Full,"Invalid JPEG quality reduced fidelity");
    }
    require(WritePrivateProfileStringW(L"Screenshot",L"HdrJpegQuality",nullptr,config_path().c_str()),"Cannot remove owned JPEG quality");
    require(load_settings().hdr_jpeg_quality==HdrJpegQuality::Full,"Legacy settings reduced JPEG quality");
    require(Settings{}.clipboard_mode==ClipboardMode::SdrImage && load_settings().clipboard_mode==ClipboardMode::SdrImage,
            "Fresh clipboard settings must use SDR only");
    for(const wchar_t* invalid:{L"-1",L"2",L"4294967297",L"1junk",L"nan"}) {
        require(WritePrivateProfileStringW(L"Screenshot",L"CopyHdr",invalid,config_path().c_str()),"Cannot seed invalid legacy clipboard preference");
        require(load_settings().clipboard_mode==ClipboardMode::SdrImage,"Invalid legacy settings enabled HDR copying");
    }
    require(WritePrivateProfileStringW(L"Screenshot",L"CopyHdr",L"1",config_path().c_str()),"Cannot seed legacy HDR clipboard preference");
    require(load_settings().clipboard_mode==ClipboardMode::Both,"Legacy HDR inclusion did not restore both representations");
    for(unsigned index=0;index<3;++index) {
        require(WritePrivateProfileStringW(L"Screenshot",L"ClipboardMode",std::to_wstring(index).c_str(),config_path().c_str()),"Cannot seed clipboard mode");
        require(load_settings().clipboard_mode==ClipboardMode(index),"Clipboard mode was not restored independently of the legacy key");
    }
    for(const wchar_t* invalid:{L"-1",L"3",L"4294967297",L"1junk",L"nan"}) {
        require(WritePrivateProfileStringW(L"Screenshot",L"ClipboardMode",invalid,config_path().c_str()),"Cannot seed invalid clipboard mode");
        require(load_settings().clipboard_mode==ClipboardMode::SdrImage,"Invalid clipboard mode enabled HDR copying");
    }
    require(WritePrivateProfileStringW(L"Screenshot",L"ClipboardMode",nullptr,config_path().c_str()),"Cannot remove owned clipboard mode");
    require(load_settings().clipboard_mode==ClipboardMode::Both,"Legacy inclusion was lost when the new key is absent");
    require(WritePrivateProfileStringW(L"Screenshot",L"CopyHdr",nullptr,config_path().c_str()),"Cannot remove owned legacy clipboard preference");
    require(load_settings().clipboard_mode==ClipboardMode::SdrImage,"Missing clipboard preferences enabled HDR copying");
    App app;app.startup=StartupRegistration(key);app.settings=Settings{};
    app.settings.folder=std::filesystem::path(argv[1]);app.settings.key=VK_F9;app.settings.modifiers=MOD_CONTROL|MOD_SHIFT|MOD_ALT;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"HdrAutosaveFixture";RegisterClassW(&wc);
    HWND owner=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Owned startup shortcut collision",WS_POPUP,0,0,0,0,nullptr,nullptr,wc.hInstance,nullptr);
    require(owner && RegisterHotKey(owner,778,app.settings.modifiers|MOD_NOREPEAT,VK_F9),"Cannot reserve the owned startup shortcut");
    struct ReleaseOwner {HWND value;~ReleaseOwner(){UnregisterHotKey(value,778);DestroyWindow(value);}} release_owner{owner};
    constexpr DWORD settings_style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    RECT settings_bounds{0,0,settings_width,settings_height};
    AdjustWindowRectEx(&settings_bounds,settings_style,FALSE,0);
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"Owned automatic settings",settings_style,
        0,0,settings_bounds.right-settings_bounds.left,settings_bounds.bottom-settings_bounds.top,nullptr,nullptr,wc.hInstance,&app);
    require(window!=nullptr && !app.registered_key,"Startup shortcut conflict was not detected");
    struct Close {HWND value;~Close(){if(IsWindow(value))DestroyWindow(value);}} close{window};
    const auto pump_until=[&](const auto& done,unsigned milliseconds) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
        while(!done() && std::chrono::steady_clock::now()<deadline) {
            MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
            Sleep(5);
        }
    };
    pump_until([]{return false;},1200);
    require(!app.registered_key && text(app.status).find(L"already in use")!=std::wstring::npos,"Retry took a conflicting shortcut or hid its warning");
    UnregisterHotKey(owner,778);
    pump_until([&]{return app.registered_key==VK_F9;},3000);
    require(app.registered_key==VK_F9,"Closing the shortcut owner did not recover capture without restarting");
    require(text(app.status).find(L"Ready. Press ") == 0,"Shortcut recovery did not update hidden-tray readiness");
    const auto changed=[&](int id,int code=BN_CLICKED){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,code),reinterpret_cast<LPARAM>(GetDlgItem(window,id)));};
    const auto choose=[&](int id,int index){SendDlgItemMessageW(window,id,CB_SETCURSEL,index,0);changed(id,CBN_SELCHANGE);};
    const auto toggle=[&](int id,bool value){SendDlgItemMessageW(window,id,BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);changed(id);};
    ShowWindow(window,SW_SHOWNOACTIVATE);app.show_page(GeneralPage);
    app.settings.color.peak_method=PeakMethod::Observed;
    toggle(DarkMode,false);
    require(load_settings().color.peak_method==PeakMethod::Observed,"Automatic settings save replaced the selected peak fallback");
    require(!app.settings.dark_mode && !load_settings().dark_mode && !app.theme.dark,"Dark mode required manual saving");
    require(IsWindowVisible(window),"Automatic checkbox save hid settings");
    app.settings.color.peak_method=PeakMethod::Robust;
    toggle(DarkMode,true);require(load_settings().dark_mode && app.theme.dark,"Dark mode did not restore automatically");
    app.show_page(CapturePage);
    HWND magnifier=GetDlgItem(window,ShowMagnifier),snapping=GetDlgItem(window,WindowSnapping);
    require(IsWindowVisible(magnifier) && IsWindowEnabled(magnifier) && text(magnifier)==L"Show magnifier",
            "Capture tab must expose an enabled magnifier checkbox");
    require(IsWindowVisible(snapping) && IsWindowEnabled(snapping) && text(snapping)==L"Snap to windows",
            "Capture tab must expose an enabled window snapping checkbox");
    require(SendMessageW(magnifier,BM_GETCHECK,0,0)==BST_UNCHECKED && SendMessageW(snapping,BM_GETCHECK,0,0)==BST_UNCHECKED,
            "Fresh Capture tab must leave both optional selector features unchecked");
    for(bool enabled:{true,false}) {
        app.capture_settings=app.settings;app.busy=true;app.set_status(L"Capturing...");
        toggle(ShowMagnifier,enabled);
        require(app.settings.show_magnifier==enabled && app.settings.window_snapping!=enabled,
                "Magnifier checkbox changed the independent snapping preference");
        toggle(WindowSnapping,enabled);
        const auto saved=load_settings();
        require(app.settings.show_magnifier==enabled && saved.show_magnifier==enabled
                && app.settings.window_snapping==enabled && saved.window_snapping==enabled,
                "Selector checkboxes required manual saving");
        require(app.capture_settings.show_magnifier!=enabled && app.capture_settings.window_snapping!=enabled,
                "Selector checkboxes changed an in-flight capture");
        require(text(app.status)==L"Capturing...","Selector saving obscured active capture status");app.busy=false;
        save_settings_preview(window,std::filesystem::path(argv[1])/(enabled?L"capture-options-enabled-dark.png":L"capture-magnifier-dark.png"));
        toggle(DarkMode,false);
        save_settings_preview(window,std::filesystem::path(argv[1])/(enabled?L"capture-options-enabled-light.png":L"capture-magnifier-light.png"));
        toggle(DarkMode,true);
    }
    choose(BackendChoice,2);require(app.settings.backend==CaptureBackend::Desktop && load_settings().backend==CaptureBackend::Desktop,"Backend choice did not apply automatically");
    choose(BackendChoice,0);
    if(!app.displays.empty()) {
        choose(MonitorChoice,1);require(load_settings().monitor==app.displays.front().device_name,"Monitor choice did not persist automatically");
        choose(MonitorChoice,0);require(load_settings().monitor.empty(),"Active-monitor choice did not persist");
    }
    app.show_page(ImagePage);
    require(!IsWindowVisible(magnifier) && !IsWindowVisible(snapping),"Selector checkboxes leaked onto the Image tab");
    SetDlgItemTextW(window,PeakNits,L"600");wait_for_edits(app);
    require(app.settings.color.peak_nits==600 && load_settings().color.peak_nits==600 && app.settings.color.paper_white==0,
            "Game peak did not persist independently of automatic white");
    app.capture_settings=app.settings;app.busy=true;SetDlgItemTextW(window,PeakNits,L"650");wait_for_edits(app);
    require(app.capture_settings.color.peak_nits==600 && app.settings.color.peak_nits==650,
            "Peak edit changed an in-flight capture");app.busy=false;
    SetDlgItemTextW(window,PeakNits,L"10001");wait_for_edits(app);
    require(app.settings.color.peak_nits==650 && app.invalid_edits,"Invalid game peak was persisted");
    SetDlgItemTextW(window,PeakNits,L"600");wait_for_edits(app);
    require(!app.invalid_edits && load_settings().color.peak_nits==600,"Corrected game peak did not clear validation");
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
    require(load_settings().color.mode==ContentMode::Hdr,"Explicit HDR content choice was not preserved");
    app.show_page(OutputPage);
    HWND clipboard=GetDlgItem(window,ClipboardChoice);
    require(IsWindowVisible(clipboard) && SendMessageW(clipboard,CB_GETCOUNT,0,0)==3,
            "Output must expose three explicit clipboard choices");
    require(SendMessageW(clipboard,CB_GETCURSEL,0,0)==0,"Output clipboard must default to SDR only");
    for(unsigned index=0;index<3;++index) {
        choose(ClipboardChoice,index);
        require(app.settings.clipboard_mode==ClipboardMode(index) && load_settings().clipboard_mode==ClipboardMode(index),
                "Clipboard mode did not save automatically");
        const wchar_t* expected[]={L"SDR image",L"saved HDR file",L"receiving app chooses"};
        for(const int id:{ClipboardLabel,ClipboardChoice}) {
            TOOLINFOW tool{};tool.cbSize=TTTOOLINFOW_V2_SIZE;tool.hwnd=window;tool.uId=reinterpret_cast<UINT_PTR>(GetDlgItem(window,id));
            require(SendMessageW(app.settings_tooltip,TTM_GETTOOLINFOW,0,reinterpret_cast<LPARAM>(&tool))!=FALSE,
                    "Clipboard hover target was not registered");
            require((tool.uFlags&(TTF_IDISHWND|TTF_SUBCLASS))==(TTF_IDISHWND|TTF_SUBCLASS),"Clipboard tooltip cannot receive native hover messages");
            wchar_t hint[256]{};tool.lpszText=hint;SendMessageW(app.settings_tooltip,TTM_GETTEXTW,256,reinterpret_cast<LPARAM>(&tool));
            require(std::wstring(hint).find(expected[index])!=std::wstring::npos,"Clipboard tooltip did not follow the selected mode");
        }
    }
    app.capture_settings=app.settings;app.busy=true;app.set_status(L"Capturing...");choose(ClipboardChoice,1);
    require(app.settings.clipboard_mode==ClipboardMode::HdrFile && load_settings().clipboard_mode==ClipboardMode::HdrFile
            && app.capture_settings.clipboard_mode==ClipboardMode::Both,"Clipboard mode changed an in-flight capture");
    require(text(app.status)==L"Capturing...","Clipboard saving obscured capture status");app.busy=false;
    choose(ClipboardChoice,2);
    require(app.settings_tooltip && SendMessageW(app.settings_tooltip,TTM_GETTOOLCOUNT,0,0)==4,"Settings hover targets are missing");
    for(const int id:{HdrAnnotationChoice,HdrAnnotationLabel}) {
        TOOLINFOW tool{};tool.cbSize=TTTOOLINFOW_V2_SIZE;tool.hwnd=window;tool.uId=reinterpret_cast<UINT_PTR>(GetDlgItem(window,id));
        require(SendMessageW(app.settings_tooltip,TTM_GETTOOLINFOW,0,reinterpret_cast<LPARAM>(&tool))!=FALSE,
                "HDR content hover target was not registered");
        require((tool.uFlags&(TTF_IDISHWND|TTF_SUBCLASS))==(TTF_IDISHWND|TTF_SUBCLASS),"HDR content tooltip cannot receive native hover messages");
        wchar_t hint[256]{};tool.lpszText=hint;SendMessageW(app.settings_tooltip,TTM_GETTEXTW,256,reinterpret_cast<LPARAM>(&tool));
        require(std::wstring(hint)==L"Redaction, blur and pixelation are kept even when drawings are excluded.","HDR content tooltip lost privacy guidance");
    }
    HWND quality=GetDlgItem(window,HdrJpegQualityChoice),quality_label=GetDlgItem(window,HdrJpegQualityLabel);
    require(IsWindowVisible(quality) && IsWindowVisible(quality_label),"JPEG quality is missing from Output");
    for(unsigned index=0;index<4;++index) {
        choose(HdrJpegQualityChoice,index);
        require(app.settings.hdr_jpeg_quality==HdrJpegQuality(index) && load_settings().hdr_jpeg_quality==HdrJpegQuality(index),
                "JPEG quality required manual saving");
    }
    app.capture_settings=app.settings;app.busy=true;app.set_status(L"Capturing...");choose(HdrJpegQualityChoice,1);
    require(app.settings.hdr_jpeg_quality==HdrJpegQuality::High && load_settings().hdr_jpeg_quality==HdrJpegQuality::High
            && app.capture_settings.hdr_jpeg_quality==HdrJpegQuality::Compact,"JPEG quality changed an in-flight capture");
    require(text(app.status)==L"Capturing...","JPEG quality saving obscured capture status");app.busy=false;
    choose(HdrFormatChoice,1);require(load_settings().hdr_format==HdrFormat::JpegXr,"HDR format required manual saving");
    require(!IsWindowVisible(quality) && !IsWindowVisible(quality_label),"JPEG XR exposed JPEG quality controls");
    require(load_settings().hdr_jpeg_quality==HdrJpegQuality::High,"JPEG XR discarded the JPEG quality preference");
    toggle(KeepOriginal,false);require(!load_settings().keep_original && !IsWindowVisible(GetDlgItem(window,HdrFormatChoice)),"HDR retention required manual saving");
    require(!IsWindowVisible(quality) && !IsWindowVisible(quality_label),"Disabled HDR saving exposed JPEG quality");
    require(!IsWindowVisible(clipboard) && load_settings().clipboard_mode==ClipboardMode::Both,"Disabling HDR saving lost or exposed HDR clipboard preference");
    require(!IsWindowVisible(GetDlgItem(window,HdrAnnotationChoice)) && !IsWindowVisible(GetDlgItem(window,HdrFormatLabel)),
            "HDR annotation controls remained visible with HDR saving disabled");
    toggle(KeepOriginal,true);choose(HdrFormatChoice,0);
    require(IsWindowVisible(quality) && IsWindowVisible(quality_label) && SendMessageW(quality,CB_GETCURSEL,0,0)==1,
            "Returning to JPEG lost its quality preference");
    require(IsWindowVisible(clipboard) && SendMessageW(clipboard,CB_GETCURSEL,0,0)==int(ClipboardMode::Both),
            "HDR clipboard preference did not return with HDR saving");
    RECT copy_rect{},capture_rect{};GetWindowRect(clipboard,&copy_rect);GetWindowRect(GetDlgItem(window,CaptureButton),&capture_rect);
    require(copy_rect.bottom+16<=capture_rect.top,"Clipboard dropdown crowds the capture button");
    choose(HdrJpegQualityChoice,0);
    require(IsWindowVisible(GetDlgItem(window,HdrAnnotationChoice)),"HDR drawing choice did not return with HDR saving");
    choose(HdrAnnotationChoice,1);
    require(app.settings.annotate_hdr && load_settings().annotate_hdr,"Annotated HDR choice did not save automatically");
    app.capture_settings=app.settings;app.busy=true;choose(HdrAnnotationChoice,0);
    require(!app.settings.annotate_hdr && !load_settings().annotate_hdr && app.capture_settings.annotate_hdr,
            "HDR drawing choice changed an in-flight capture");app.busy=false;
    choose(HdrAnnotationChoice,1);
    save_settings_preview(window,std::filesystem::path(argv[1])/L"hdr-annotations-dark.png");
    toggle(DarkMode,false);save_settings_preview(window,std::filesystem::path(argv[1])/L"hdr-annotations-light.png");toggle(DarkMode,true);
    choose(HdrAnnotationChoice,0);
    app.show_page(ImagePage);
    require(!IsWindowVisible(clipboard),"HDR clipboard option leaked onto Image settings");
    SetFocus(GetDlgItem(window,PaperWhite));SetDlgItemTextW(window,PaperWhite,L"250");
    require(app.settings.color.paper_white==0,"Text edit saved before debounce");
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
    for(bool dark:{true,false}) {
        toggle(DarkMode,dark);
        for(int page=0;page<SettingsPageCount;++page) {
            app.show_page(page);
            save_settings_preview(window,std::filesystem::path(argv[1])/(L"settings-"+std::to_wstring(page)+(dark?L"-dark.png":L"-light.png")));
        }
        app.show_page(OutputPage);
        for(unsigned format=0;format<2;++format) for(unsigned mode=0;mode<3;++mode) {
            choose(HdrFormatChoice,format);choose(ClipboardChoice,mode);
            save_settings_preview(window,std::filesystem::path(argv[1])/(L"output-"+std::to_wstring(format)+L"-"+std::to_wstring(mode)+(dark?L"-dark.png":L"-light.png")));
        }
        toggle(KeepOriginal,false);
        save_settings_preview(window,std::filesystem::path(argv[1])/(dark?L"output-sdr-dark.png":L"output-sdr-light.png"));
        toggle(KeepOriginal,true);choose(HdrFormatChoice,0);
    }
    toggle(DarkMode,true);app.show_page(OutputPage);
    SetDlgItemTextW(window,ControlId::Folder,L"relative-folder");wait_for_edits(app);
    require(app.settings.folder==std::filesystem::path(argv[1]),"Relative folder replaced valid output path");
    const auto output=std::filesystem::path(argv[1])/L"new-output";
    SetDlgItemTextW(window,ControlId::Folder,output.c_str());SendMessageW(window,WM_CLOSE,0,0);
    require(load_settings().folder==output && !IsWindowVisible(window),"Closing settings lost pending folder edit");
    require(text(GetDlgItem(window,SaveButton))==L"Hide to tray","Obsolete manual-save label remains");
    SetDlgItemTextW(window,Exposure,L"0");SendMessageW(window,WM_COMMAND,ExitButton,0);
    require(!IsWindow(window) && load_settings().color.exposure==0,"Exit lost pending edit");
    require(!IsWindow(app.settings_tooltip),"Closing settings left an owned tooltip alive");
    std::cout<<checks<<" automatic settings checks passed. Isolated INI and startup key only; no desktop capture, clipboard, game, or launcher was used.\n";
    return 0;
} catch(const std::exception& error){std::cerr<<"Automatic settings fixture failed after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}