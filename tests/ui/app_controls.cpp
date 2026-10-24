#include "app/app.hpp"
#include <iostream>
#include <atomic>

using namespace hdrshot;

static unsigned checks=0;
void require(bool condition,const char* message) {++checks;if(!condition) throw std::runtime_error(message);}
static void record_license_window(HWND window,const wchar_t* name) {
    RECT rect{};GetClientRect(window,&rect);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=rect.right;info.bmiHeader.biHeight=-rect.bottom;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;HDC dc=GetDC(window),memory=CreateCompatibleDC(dc);
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);ReleaseDC(window,dc);
    require(memory && bitmap && pixels,"Cannot allocate the owned license preview");
    HGDIOBJ previous=SelectObject(memory,bitmap);
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME|RDW_UPDATENOW);
    const BOOL printed=PrintWindow(window,memory,PW_CLIENTONLY);
    GdiFlush();
    Image image;image.width=unsigned(rect.right);image.height=unsigned(rect.bottom);
    image.bgra.assign(static_cast<const std::uint8_t*>(pixels),static_cast<const std::uint8_t*>(pixels)+std::size_t(rect.right)*rect.bottom*4);
    SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);
    require(printed!=FALSE,"Cannot render the owned license window");
    for(std::size_t i=3;i<image.bgra.size();i+=4) image.bgra[i]=255;
    const auto path=std::filesystem::temp_directory_path()/(std::wstring(name)+L"-"+screenshot_name()+L".png");
    save_image(path,image);std::wcout<<L"LICENSE_PREVIEW "<<path.wstring()<<L"\n";
}
int wmain() try {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Cannot initialize owned controls fixture");
    struct FinishCom {~FinishCom(){CoUninitialize();}} finish;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    App app;app.settings.monitor=L"*";app.settings.key=VK_F9;app.settings.modifiers=MOD_CONTROL|MOD_SHIFT|MOD_ALT;
    WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=L"LumenShotControlsFixture";RegisterClassW(&wc);
    HWND settings=CreateWindowExW(0,wc.lpszClassName,L"Owned screenshot controls",WS_OVERLAPPEDWINDOW,
        100,100,714,750,nullptr,nullptr,wc.hInstance,&app);
    require(settings!=nullptr,"Cannot create owned settings");
    struct CloseSettings {HWND value;~CloseSettings(){DestroyWindow(value);}} close{settings};
    // Match the application's initial hidden ShowWindow call.
    ShowWindow(settings,SW_HIDE);
    require(!IsWindowVisible(settings),"Creating settings must not show the window");
    for(std::size_t i=0;i<app.displays.size();++i) {
        const auto& display=app.displays[i];
        require(!display.friendly_name.empty(),"Monitor name is empty");
        const auto expected=monitor_label(display);
        const LRESULT length=SendDlgItemMessageW(settings,MonitorChoice,CB_GETLBTEXTLEN,i+1,0);
        require(length>=0,"Monitor option is missing");
        std::wstring actual(std::size_t(length)+1,L'\0');
        SendDlgItemMessageW(settings,MonitorChoice,CB_GETLBTEXT,i+1,reinterpret_cast<LPARAM>(actual.data()));
        actual.resize(std::size_t(length));
        require(actual==expected,"Monitor option lost its Windows name or identity");
        require(actual.find(L"\\\\.\\")==std::wstring::npos,"Monitor option exposes a raw device path");
        std::wcout<<L"MONITOR_LABEL "<<actual<<L"\n";
    }
    const LRESULT all_index=LRESULT(app.displays.size()+1);
    require(SendDlgItemMessageW(settings,MonitorChoice,CB_GETCURSEL,0,0)==all_index,"Saved all-monitor choice was not restored in settings");
    require(SendDlgItemMessageW(settings,MonitorChoice,CB_GETCOUNT,0,0)==all_index+1,"All-monitor option is missing");
    wchar_t all_label[64]{};
    SendDlgItemMessageW(settings,MonitorChoice,CB_GETLBTEXT,all_index,reinterpret_cast<LPARAM>(all_label));
    require(std::wstring(all_label)==L"All monitors","All-monitor label is unclear");
    SendDlgItemMessageW(settings,MonitorChoice,CB_SETCURSEL,all_index,0);
    SendMessageW(settings,WM_COMMAND,MAKEWPARAM(MonitorChoice,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(settings,MonitorChoice)));
    require(app.settings.monitor==L"*" && load_settings().monitor==L"*","All-monitor selection did not save automatically");
    SendMessageW(settings,tray_message,0,WM_LBUTTONDBLCLK);
    require(IsWindowVisible(settings),"Tray activation must show settings");
    require(app.settings.dark_mode && app.theme.dark,"Dark mode must default to enabled");
    require(TabCtrl_GetItemCount(GetDlgItem(settings,SettingsTabs))==3,"Settings categories are missing");
    require(IsWindowVisible(GetDlgItem(settings,MonitorChoice)) && !IsWindowVisible(GetDlgItem(settings,ContentChoice)),"Capture tab shows incorrect controls");
    HWND tabs=GetDlgItem(settings,SettingsTabs);SetFocus(tabs);
    SendMessageW(tabs,WM_KEYDOWN,VK_RIGHT,0);SendMessageW(tabs,WM_KEYUP,VK_RIGHT,0);
    require(app.active_page==1,"Native tab keyboard navigation did not select Output");
    app.show_page(2);
    require(IsWindowVisible(GetDlgItem(settings,DarkMode)) && IsWindowVisible(GetDlgItem(settings,StartWithWindows)),"General tab is incomplete");
    HWND licenses_button=GetDlgItem(settings,LicensesButton);
    require(licenses_button && IsWindowVisible(licenses_button),"General tab has no license viewer entry");
    require(text(licenses_button)==L"Licenses...","License viewer entry is unclear");
    const auto saved_settings=load_settings();
    SendMessageW(settings,WM_COMMAND,LicensesButton,0);
    HWND licenses=app.licenses.window(),license_text=GetDlgItem(licenses,HDRSHOT_LICENSE_TEXT);
    require(licenses && IsWindowVisible(licenses),"License viewer did not open from settings");
    require(IsWindowEnabled(settings),"License viewer unnecessarily blocks settings");
    require(app.licenses.dark(),"License viewer did not inherit dark mode");
    const auto notice_text=text(license_text);
    require(notice_text.size()>65535,"License text was truncated to a legacy edit-control limit");
    require(notice_text.find(L"Copyright (c) 2026 Dreambjorn")!=std::wstring::npos,"Application license is absent");
    require(notice_text.find(L"This product includes Gain Map technology under license by Adobe.")!=std::wstring::npos,
            "Adobe attribution is absent from the license viewer");
    require(notice_text.find(L"================ licenses/winpthreads.txt ================")!=std::wstring::npos,
            "Final runtime notice was truncated");
    require(GetWindowLongPtrW(license_text,GWL_STYLE)&ES_READONLY,"License text is editable");
    SendMessageW(license_text,WM_CHAR,L'x',0);
    require(text(license_text)==notice_text,"Read-only license text changed after typing");
    SendMessageW(license_text,EM_SETSEL,0,-1);DWORD selection_start=0,selection_end=0;
    SendMessageW(license_text,EM_GETSEL,reinterpret_cast<WPARAM>(&selection_start),reinterpret_cast<LPARAM>(&selection_end));
    require(selection_start==0 && selection_end==notice_text.size(),"Full license text cannot be selected for copying");
    SendMessageW(license_text,EM_SETSEL,0,0);record_license_window(licenses,L"licenses-dark");
    RECT original_bounds{},before_text{},after_text{},viewer_client{},close_button{};
    GetWindowRect(licenses,&original_bounds);GetWindowRect(license_text,&before_text);
    SetWindowPos(licenses,nullptr,0,0,original_bounds.right-original_bounds.left+80,
                 original_bounds.bottom-original_bounds.top+60,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    GetWindowRect(license_text,&after_text);GetClientRect(licenses,&viewer_client);
    GetWindowRect(GetDlgItem(licenses,IDCANCEL),&close_button);
    MapWindowPoints(nullptr,licenses,reinterpret_cast<POINT*>(&close_button),2);
    require(after_text.right-after_text.left>before_text.right-before_text.left
            && after_text.bottom-after_text.top>before_text.bottom-before_text.top,"License text did not follow window resizing");
    require(close_button.right<viewer_client.right && close_button.bottom<viewer_client.bottom,
            "License viewer close button leaves the client area");
    SendMessageW(settings,WM_COMMAND,LicensesButton,0);
    require(app.licenses.window()==licenses,"Repeated license commands opened duplicate viewers");
    SendDlgItemMessageW(settings,DarkMode,BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(settings,WM_COMMAND,DarkMode,0);
    require(!app.theme.dark,"Light mode did not apply immediately");
    require(!load_settings().dark_mode,"Light mode did not persist automatically");
    require(!app.licenses.dark(),"Open license viewer did not follow the theme change");
    record_license_window(licenses,L"licenses-light");
    MSG escape{};escape.hwnd=license_text;escape.message=WM_KEYDOWN;escape.wParam=VK_ESCAPE;
    require(app.licenses.translate(escape),"License viewer did not handle Escape");
    require(!app.licenses.window() && !IsWindow(licenses),"Escape left the license viewer open");
    require(IsWindowVisible(settings),"Closing licenses hid settings");
    SendMessageW(settings,WM_COMMAND,LicensesButton,0);licenses=app.licenses.window();
    require(licenses && !app.licenses.dark(),"License viewer did not reopen in the current theme");
    SendMessageW(licenses,WM_COMMAND,IDCANCEL,0);
    require(!app.licenses.window(),"License viewer Close button did not destroy its window");
    const auto after_licenses=load_settings();
    require(after_licenses.folder==saved_settings.folder && after_licenses.key==saved_settings.key
            && after_licenses.modifiers==saved_settings.modifiers && after_licenses.scale==saved_settings.scale,
            "Viewing licenses changed unrelated capture settings");
    SendDlgItemMessageW(settings,DarkMode,BM_SETCHECK,BST_CHECKED,0);SendMessageW(settings,WM_COMMAND,DarkMode,0);
    require(app.theme.dark,"Dark mode did not restore immediately");
    app.show_page(1);
    require(!IsWindowVisible(GetDlgItem(settings,MonitorChoice)) && IsWindowVisible(GetDlgItem(settings,ContentChoice)),"Output tab shows incorrect controls");
    HWND sharpness=GetDlgItem(settings,Sharpness);
    require(sharpness && IsWindowVisible(sharpness) && IsWindowEnabled(sharpness),"Output sharpness slider is missing or disabled");
    require(SendMessageW(sharpness,TBM_GETRANGEMIN,0,0)==0 && SendMessageW(sharpness,TBM_GETRANGEMAX,0,0)==100,
            "Sharpness slider has an incorrect range");
    require(GetWindowLongPtrW(sharpness,GWL_STYLE)&WS_TABSTOP,"Sharpness slider is inaccessible by keyboard");
    HWND image_size=GetDlgItem(settings,ImageSize);
    require(image_size && IsWindowVisible(image_size) && IsWindowEnabled(image_size),"Output image size is missing or disabled");
    require(SendMessageW(image_size,CB_GETCOUNT,0,0)==4 && SendMessageW(image_size,CB_GETCURSEL,0,0)==0,
            "Output image sizes or Original default are incorrect");
    require(text(GetDlgItem(settings,KeepOriginal))==L"Save HDR image","HDR save label is inconsistent");
    HWND format=GetDlgItem(settings,HdrFormatChoice);
    require(format && SendMessageW(format,CB_GETCOUNT,0,0)==2,"HDR format choices are missing");
    require(SendMessageW(format,CB_GETCURSEL,0,0)==0,"Recommended HDR JPEG is not the default");
    require(IsWindowVisible(format),"Enabled HDR copy must show its format dropdown");
    SendDlgItemMessageW(settings,KeepOriginal,BM_SETCHECK,BST_UNCHECKED,0);
    SendMessageW(settings,WM_COMMAND,KeepOriginal,0);
    require(!IsWindowVisible(format),"Disabled HDR copy must hide its format dropdown");
    require(IsWindowEnabled(format),"Hidden HDR format was unnecessarily grayed out");
    SendDlgItemMessageW(settings,KeepOriginal,BM_SETCHECK,BST_CHECKED,0);
    SendMessageW(settings,WM_COMMAND,KeepOriginal,0);
    require(IsWindowVisible(format),"Restoring HDR copy did not restore its dropdown");
    DWORD affinity=0;
    require(GetWindowDisplayAffinity(settings,&affinity) && affinity==WDA_NONE,
            "Open settings must be included in screenshots");
    app.set_status(L"Copied screenshot\n\nSize: 2 x 2\nColor: SDR preserved\n\nPNG: fixture.png");
    require(text(app.status).find(L"\r\n\r\n")!=std::wstring::npos,"Native status lost blank line separation");
    require(SendMessageW(app.status,EM_GETLINECOUNT,0,0)==6,"Native status rendered incorrect line breaks");
    SendMessageW(settings,WM_COMMAND,CaptureButton,0);
    require(app.countdown==3,"Capture button did not start its countdown");
    require(IsWindowVisible(settings),"Delayed capture hid open settings");
    require(app.escape_registered,"Countdown did not reserve Escape");
    SendMessageW(settings,WM_HOTKEY,cancel_hotkey_id,0);
    require(app.countdown==0 && !app.escape_registered,"Escape did not cancel countdown and release its shortcut");
    SendMessageW(settings,WM_TIMER,countdown_timer,0);
    require(!app.busy && app.countdown==0,"A stale timer restarted a canceled capture");
    require(!GetDlgItem(settings,113),"Settings must not expose the retired JXR import button");
    HWND content=GetDlgItem(settings,ContentChoice);
    SetForegroundWindow(settings);SetFocus(content);
    SendMessageW(content,CB_SHOWDROPDOWN,TRUE,0);
    require(SendMessageW(content,CB_GETDROPPEDSTATE,0,0),"Cannot open the owned content dropdown");
    app.settings.monitor=L"owned-unavailable-display";
    SendMessageW(settings,WM_HOTKEY,hotkey_id,0);
    require(app.busy,"Hotkey did not start a screenshot request");
    require(IsWindowVisible(settings),"Hotkey hid the open settings window");
    for(int id: {ControlId::Folder,Browse,MonitorChoice,BackendChoice,ContentChoice,PaperWhite,Exposure,KeepOriginal,
                 ModifierCtrl,ModifierShift,ModifierAlt,KeyChoice,CaptureButton,SaveButton,FolderButton,ExitButton,HdrFormatChoice,StartWithWindows,DarkMode,SettingsTabs,Sharpness,SharpnessValue,ImageSize,LicensesButton})
        require(IsWindowEnabled(GetDlgItem(settings,id)),"Capture disabled a settings control");
    require(GetFocus()==content,"Capture moved focus away from the open dropdown");
    require(SendMessageW(content,CB_GETDROPPEDSTATE,0,0),"Capture closed the open dropdown before freezing it");
    SendMessageW(settings,WM_COMMAND,CaptureButton,0);
    require(app.countdown==0,"Busy capture queued a second delayed screenshot");
    SendMessageW(content,CB_SHOWDROPDOWN,FALSE,0);
    require(app.preparing && app.escape_registered,"Capture preparation did not reserve Escape");
    SendMessageW(settings,WM_COMMAND,IDCANCEL,0);
    require(app.cancel_requested && !app.escape_registered,"Escape did not cancel pending capture");
    app.finish_work();
    require(app.result.canceled && app.result.success && !app.result.needs_selection,
            "Canceled background capture opened selection or reported failure");
    require(!app.preparing && !app.busy,"Canceled capture stayed busy after completion");
    require(RegisterHotKey(settings,777,MOD_NOREPEAT,VK_ESCAPE)!=FALSE,"Escape remained reserved after capture");
    UnregisterHotKey(settings,777);
    require(IsWindowVisible(settings),"Screenshot completion hid open settings");
    HWND foreground=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Owned foreground fixture",WS_POPUP|WS_VISIBLE,
        850,100,120,80,nullptr,nullptr,wc.hInstance,nullptr);
    require(foreground!=nullptr,"Cannot create owned foreground fixture");
    struct CloseForeground {HWND value;~CloseForeground(){DestroyWindow(value);}} close_foreground{foreground};
    SetForegroundWindow(foreground);
    const auto foreground_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    while(GetForegroundWindow()!=foreground && std::chrono::steady_clock::now()<foreground_deadline) {
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
        Sleep(10);
    }
    if(GetForegroundWindow()!=foreground)
        std::cerr<<"Owned focus state: active="<<GetActiveWindow()<<" focus="<<GetFocus()
                 <<" capture="<<GetCapture()<<" foreground="<<GetForegroundWindow()<<" expected="<<foreground<<"\n";
    require(GetForegroundWindow()!=settings,"Cannot establish background settings fixture");
    std::atomic<bool> menu_seen{false},settings_activated{false};
    NativeWork driver;
    driver.start([&] {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(std::chrono::steady_clock::now()<deadline) {
            HWND menu=FindWindowW(L"#32768",nullptr);
            DWORD pid=0;if(menu) GetWindowThreadProcessId(menu,&pid);
            if(menu && pid==GetCurrentProcessId()) {
                menu_seen=true;settings_activated=GetForegroundWindow()==settings;
                PostMessageW(app.menu_window,WM_CANCELMODE,0,0);return;
            }
            Sleep(10);
        }
        if(app.menu_window) PostMessageW(app.menu_window,WM_CANCELMODE,0,0);
    });
    app.tray_menu();driver.join();
    require(menu_seen,"Tray menu did not appear");
    require(!settings_activated,"Opening tray menu activated settings");
    require(IsWindowVisible(settings),"Tray menu hid open settings");
    require(GetForegroundWindow()!=settings,"Dismissing tray menu activated settings");
    SendMessageW(settings,WM_CLOSE,0,0);
    require(!IsWindowVisible(settings),"Closing settings must hide it to the tray");
    std::cout<<checks<<" app controls checks passed. No desktop capture, clipboard, game, or launcher was used.\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"Controls fixture failed after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
