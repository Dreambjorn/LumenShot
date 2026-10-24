#include "app/app.hpp"
#include "image/scaling.hpp"
#include <shlobj.h>
#include <shobjidl.h>

namespace hdrshot {

std::wstring text(HWND control) {
    const int length=GetWindowTextLengthW(control);
    std::wstring result(std::size_t(length)+1,L'\0');
    GetWindowTextW(control,result.data(),length+1);result.resize(length);return result;
}

HWND App::control(const wchar_t* type,const wchar_t* label,DWORD style,int id,int x,int y,int w,int h) {
    // A client edge draws a white bevel in dark mode; the theme paints a flat border instead.
    HWND child=CreateWindowExW(0,type,label,
        WS_CHILD|WS_VISIBLE|style|(wcscmp(type,L"EDIT")==0?WS_BORDER:0),x,y,w,h,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
    if (!child) throw std::runtime_error("Cannot create application controls.");
    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(body_font),TRUE);
    if(wcscmp(type,L"EDIT")==0) SendMessageW(child,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));
    if(creating_page>=0) page_controls.emplace_back(child,creating_page);
    return child;
}

void App::label(const wchar_t* value,int x,int y,int width) { control(L"STATIC",value,SS_LEFT,0,x,y,width,22); }

void App::add_combo(int id,const std::vector<std::wstring>& entries,int selection,int x,int y,int width) {
    HWND combo=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_TABSTOP|WS_VSCROLL,id,x,y,width,280);
    for (const auto& entry:entries) SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(entry.c_str()));
    SendMessageW(combo,CB_SETCURSEL,selection,0);
}

void App::paint(HDC dc) {
    RECT client{}; GetClientRect(window,&client); FillRect(dc,&client,theme.background);
    if(status) {
        RECT console{};GetWindowRect(status,&console);
        MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&console),2);
        FillRect(dc,&console,theme.input);
    }
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,theme.colors.text);SelectObject(dc,title_font);
    TextOutW(dc,28,23,L"LumenShot",9);
    SelectObject(dc,small_font);SetTextColor(dc,theme.colors.muted);
    TextOutW(dc,28,66,subtitle.c_str(),int(subtitle.size()));
    RECT footer{198,592,516,622};
    DrawTextW(dc,L"Version " LUMENSHOT_VERSION,-1,&footer,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
}

void App::set_status(const std::wstring& message) {
    std::wstring display;
    for(std::size_t i=0;i<message.size();++i) {
        if(message[i]==L'\n' && (i==0 || message[i-1]!=L'\r')) display+=L'\r';
        display+=message[i];
    }
    SetWindowTextW(status,display.c_str());
}

void App::create_controls() {
    body_font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    small_font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    title_font=CreateFontW(-30,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    INITCOMMONCONTROLSEX tab_classes{sizeof(tab_classes),ICC_TAB_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&tab_classes);
    constexpr int left=28,right=370,column_width=316,content_width=658,field_height=30;
    HWND tabs=control(WC_TABCONTROLW,L"",WS_TABSTOP|TCS_FIXEDWIDTH|TCS_SINGLELINE,SettingsTabs,left,104,content_width,40);
    const wchar_t* pages[]={L"Capture",L"Image",L"Output",L"General"};
    for(int i=0;i<SettingsPageCount;++i){TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(pages[i]);TabCtrl_InsertItem(tabs,i,&item);}
    TabCtrl_SetItemSize(tabs,(content_width-4)/SettingsPageCount,36);
    creating_page=CapturePage;
    label(L"Capture monitor",left,168,column_width);label(L"Capture method",right,168,column_width);
    displays=monitors();
    std::vector<std::wstring> monitor_names{L"Monitor with active window"};
    int selected=0;
    for(std::size_t i=0;i<displays.size();++i) {
        const auto& monitor=displays[i];monitor_names.push_back(monitor_label(monitor));
        if(monitor.device_name==settings.monitor)selected=int(i)+1;
    }
    monitor_names.push_back(L"All monitors");
    if(settings.monitor==L"*") selected=int(displays.size())+1;
    add_combo(MonitorChoice,monitor_names,selected,left,194,column_width);
    add_combo(BackendChoice,{L"Automatic (WGC, then DXGI)",L"WGC",L"DXGI"},int(settings.backend),right,194,column_width);
    label(L"Screenshot shortcut",left,236,column_width);
    const struct {int id;const wchar_t* text;UINT modifier;int x;} modifiers[]={
        {ModifierCtrl,L"Ctrl",MOD_CONTROL,left},{ModifierShift,L"Shift",MOD_SHIFT,left+108},{ModifierAlt,L"Alt",MOD_ALT,left+216}};
    for(const auto& modifier:modifiers) {
        HWND check=control(L"BUTTON",modifier.text,BS_AUTOCHECKBOX|WS_TABSTOP,modifier.id,modifier.x,262,100,field_height);
        SendMessageW(check,BM_SETCHECK,(settings.modifiers&modifier.modifier)?BST_CHECKED:BST_UNCHECKED,0);
    }
    std::vector<std::wstring> keys{L"Print Screen"};for(int i=1;i<=11;++i)keys.push_back(L"F"+std::to_wstring(i));
    add_combo(KeyChoice,keys,settings.key==VK_SNAPSHOT?0:int(settings.key-VK_F1)+1,right,262,column_width);
    HWND magnifier=control(L"BUTTON",L"Show magnifier",BS_AUTOCHECKBOX|WS_TABSTOP,ShowMagnifier,left,316,column_width,field_height);
    SendMessageW(magnifier,BM_SETCHECK,settings.show_magnifier?BST_CHECKED:BST_UNCHECKED,0);
    creating_page=ImagePage;
    label(L"Content",left,168,column_width);label(L"Peak nits (0=Auto)",right,168,148);label(L"White nits (0=Auto)",538,168,148);
    add_combo(ContentChoice,{L"Automatic",L"HDR content",L"SDR content on HDR display"},int(settings.color.mode),left,194,column_width);
    control(L"EDIT",std::to_wstring(int(settings.color.peak_nits)).c_str(),ES_NUMBER|WS_TABSTOP,PeakNits,right,194,148,field_height);
    control(L"EDIT",std::to_wstring(int(settings.color.paper_white)).c_str(),ES_NUMBER|WS_TABSTOP,PaperWhite,538,194,148,field_height);
    label(L"HDR exposure (stops)",left,236,170);
    label(L"Image size",210,236,134);
    std::wostringstream exposure_text;exposure_text<<std::fixed<<std::setprecision(2)<<settings.color.exposure;
    control(L"EDIT",exposure_text.str().c_str(),ES_AUTOHSCROLL|WS_TABSTOP,Exposure,left,262,170,field_height);
    const auto size_index=std::find(image_scales.begin(),image_scales.end(),settings.scale)-image_scales.begin();
    add_combo(ImageSize,{L"Original",L"125%",L"150%",L"200%"},int(size_index),210,262,134);
    label(L"Sharpness",right,236,column_width);
    HWND sharpness=control(TRACKBAR_CLASSW,L"",TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,Sharpness,right,262,260,field_height);
    SendMessageW(sharpness,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));
    SendMessageW(sharpness,TBM_SETPAGESIZE,0,10);
    SendMessageW(sharpness,TBM_SETPOS,TRUE,settings.sharpness);
    control(L"STATIC",L"Off",SS_LEFT|SS_CENTERIMAGE,SharpnessValue,640,262,46,field_height);
    update_sharpness_label();
    creating_page=OutputPage;
    label(L"Save folder",left,168,column_width);
    control(L"EDIT",settings.folder.c_str(),ES_AUTOHSCROLL|WS_TABSTOP,Folder,left,194,542,field_height);
    control(L"BUTTON",L"Browse...",BS_PUSHBUTTON|WS_TABSTOP,Browse,580,194,106,field_height);
    HWND keep=control(L"BUTTON",L"Save HDR image",BS_AUTOCHECKBOX|WS_TABSTOP,KeepOriginal,left,236,column_width,field_height);
    SendMessageW(keep,BM_SETCHECK,settings.keep_original?BST_CHECKED:BST_UNCHECKED,0);
    add_combo(HdrFormatChoice,{L"Ultra HDR JPEG (.jpg) - recommended",L"JPEG XR (.jxr) - lossless HDR"},int(settings.hdr_format),right,236,column_width);
    control(L"STATIC",L"HDR content",SS_LEFT,HdrAnnotationLabel,left,286,column_width,22);
    add_combo(HdrAnnotationChoice,{L"Original HDR (no drawings)",L"Annotated HDR (include drawings)"},settings.annotate_hdr?1:0,left,312,content_width);
    control(L"STATIC",L"Redaction, blur and pixelation apply to both HDR options.",SS_LEFT,HdrPrivacyLabel,left,352,content_width,22);
    creating_page=GeneralPage;
    HWND dark=control(L"BUTTON",L"Dark mode",BS_AUTOCHECKBOX|WS_TABSTOP,DarkMode,left,168,column_width,field_height);
    SendMessageW(dark,BM_SETCHECK,settings.dark_mode?BST_CHECKED:BST_UNCHECKED,0);
    control(L"BUTTON",L"Licenses...",BS_PUSHBUTTON|WS_TABSTOP,LicensesButton,right,168,column_width,field_height);
    HWND autostart=control(L"BUTTON",L"Start with Windows",BS_AUTOCHECKBOX|WS_TABSTOP,StartWithWindows,left,214,column_width,field_height);
    SendMessageW(autostart,BM_SETCHECK,startup.enabled()?BST_CHECKED:BST_UNCHECKED,0);
    creating_page=-1;
    control(L"BUTTON",L"Capture in 3 seconds",BS_DEFPUSHBUTTON|WS_TABSTOP,CaptureButton,left,432,column_width,36);
    control(L"BUTTON",L"Hide to tray",BS_PUSHBUTTON|WS_TABSTOP,SaveButton,right,432,column_width,36);
    status=control(L"EDIT",L"Preparing screenshot shortcut...",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,Status,left,488,content_width,80);
    control(L"BUTTON",L"Open screenshots",BS_PUSHBUTTON|WS_TABSTOP,FolderButton,left,592,170,field_height);
    control(L"BUTTON",L"Exit",BS_PUSHBUTTON|WS_TABSTOP,ExitButton,516,592,170,field_height);
    theme.apply(window,settings.dark_mode);show_page(CapturePage);
    menu_window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"",WS_POPUP,0,0,0,0,
                               nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if (!menu_window) throw std::runtime_error("Cannot create tray menu owner.");
    NOTIFYICONDATAW icon{};icon.cbSize=sizeof(icon);icon.hWnd=window;icon.uID=tray_id;
    icon.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;icon.uCallbackMessage=tray_message;
    icon.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1));
    wcscpy_s(icon.szTip,L"LumenShot");
    tray_added=Shell_NotifyIconW(NIM_ADD,&icon)!=FALSE;
    warmup.start([]{try{warm_capture();}catch(...){}});
    controls_ready=true;
    try { register_hotkey();set_status(L"Ready. Press "+hotkey_label(settings)+L" to select an area."); }
    catch (const std::exception& error) { set_status(widen(error.what())); }
}

void App::show_page(int page) {
    active_page=std::clamp(page,0,int(SettingsPageCount)-1);TabCtrl_SetCurSel(GetDlgItem(window,SettingsTabs),active_page);
    for(const auto& item:page_controls)
        ShowWindow(item.first,item.second==active_page?SW_SHOWNA:SW_HIDE);
    update_hdr_choice();InvalidateRect(window,nullptr,TRUE);
}

void App::update_hdr_choice() {
    const bool keep=SendDlgItemMessageW(window,KeepOriginal,BM_GETCHECK,0,0)==BST_CHECKED;
    for(const int id:{HdrFormatChoice,HdrAnnotationChoice,HdrAnnotationLabel,HdrPrivacyLabel})
        ShowWindow(GetDlgItem(window,id),keep && active_page==OutputPage?SW_SHOWNA:SW_HIDE);
}

void App::update_sharpness_label() {
    const auto value=SendDlgItemMessageW(window,Sharpness,TBM_GETPOS,0,0);
    const auto label=value?std::to_wstring(value)+L"%":L"Off";
    SetDlgItemTextW(window,SharpnessValue,label.c_str());
}

void App::read_controls(int changed) {
    if(changed==StartWithWindows) {
        startup.set_enabled(SendDlgItemMessageW(window,StartWithWindows,BM_GETCHECK,0,0)==BST_CHECKED);
        return;
    }
    Settings next=settings;
    if(!changed || changed==Folder) {
        next.folder=text(GetDlgItem(window,Folder));
        if(next.folder.empty() || !next.folder.is_absolute()) throw std::runtime_error("Choose an absolute screenshot folder.");
    }
    if(!changed || changed==MonitorChoice) {
        const LRESULT index=SendDlgItemMessageW(window,MonitorChoice,CB_GETCURSEL,0,0);
        next.monitor=index==LRESULT(displays.size()+1)?L"*"
            :index>0 && std::size_t(index)<=displays.size()?displays[std::size_t(index)-1].device_name:L"";
    }
    if(!changed || changed==BackendChoice)
        next.backend=CaptureBackend(SendDlgItemMessageW(window,BackendChoice,CB_GETCURSEL,0,0));
    if(!changed || changed==ContentChoice)
        next.color.mode=ContentMode(SendDlgItemMessageW(window,ContentChoice,CB_GETCURSEL,0,0));
    const auto number=[&](int id,const char* message) {
        const auto value=text(GetDlgItem(window,id));std::size_t used=0;double parsed=0;
        try {parsed=std::stod(value,&used);} catch(const std::exception&) {throw std::runtime_error(message);}
        if(used!=value.size() || !std::isfinite(parsed)) throw std::runtime_error(message);
        return parsed;
    };
    if(!changed || changed==PaperWhite) {
        const char* message="Reference white must be zero (automatic) or between 40 and 2000 nits.";
        next.color.paper_white=number(PaperWhite,message);
        if((next.color.paper_white!=0 && next.color.paper_white<40) || next.color.paper_white<0 || next.color.paper_white>2000) throw std::runtime_error(message);
    }
    if(!changed || changed==PeakNits) {
        const char* message="HDR peak must be zero (automatic) or between 40 and 10000 nits.";
        next.color.peak_nits=number(PeakNits,message);
        if(next.color.peak_nits<0 || next.color.peak_nits>10000
           || (next.color.peak_nits>0 && next.color.peak_nits<40)) throw std::runtime_error(message);
    }
    if(!changed || changed==Exposure) {
        const char* message="Exposure must be between -4 and +4 stops.";
        next.color.exposure=number(Exposure,message);
        if(std::abs(next.color.exposure)>4) throw std::runtime_error(message);
    }
    if(!changed || changed==Sharpness)
        next.sharpness=unsigned(SendDlgItemMessageW(window,Sharpness,TBM_GETPOS,0,0));
    if(!changed || changed==ImageSize) {
        const auto index=SendDlgItemMessageW(window,ImageSize,CB_GETCURSEL,0,0);
        next.scale=index>=0 && std::size_t(index)<image_scales.size()?image_scales[std::size_t(index)]:100;
    }
    if(!changed || changed==ShowMagnifier)
        next.show_magnifier=SendDlgItemMessageW(window,ShowMagnifier,BM_GETCHECK,0,0)==BST_CHECKED;
    if(!changed || changed==DarkMode)
        next.dark_mode=SendDlgItemMessageW(window,DarkMode,BM_GETCHECK,0,0)==BST_CHECKED;
    if(!changed || changed==KeepOriginal)
        next.keep_original=SendDlgItemMessageW(window,KeepOriginal,BM_GETCHECK,0,0)==BST_CHECKED;
    if(!changed || changed==HdrFormatChoice)
        next.hdr_format=SendDlgItemMessageW(window,HdrFormatChoice,CB_GETCURSEL,0,0)==1?HdrFormat::JpegXr:HdrFormat::UltraJpeg;
    if(!changed || changed==HdrAnnotationChoice)
        next.annotate_hdr=SendDlgItemMessageW(window,HdrAnnotationChoice,CB_GETCURSEL,0,0)==1;
    const bool shortcut=!changed || (changed>=ModifierCtrl && changed<=KeyChoice);
    if(shortcut) {
        next.modifiers=0;
        if(SendDlgItemMessageW(window,ModifierCtrl,BM_GETCHECK,0,0)==BST_CHECKED) next.modifiers|=MOD_CONTROL;
        if(SendDlgItemMessageW(window,ModifierShift,BM_GETCHECK,0,0)==BST_CHECKED) next.modifiers|=MOD_SHIFT;
        if(SendDlgItemMessageW(window,ModifierAlt,BM_GETCHECK,0,0)==BST_CHECKED) next.modifiers|=MOD_ALT;
        const auto index=UINT(SendDlgItemMessageW(window,KeyChoice,CB_GETCURSEL,0,0));
        next.key=index==0?VK_SNAPSHOT:VK_F1+index-1;
    }
    const Settings previous=settings;
    settings=next;
    try {
        if(shortcut) register_hotkey();
        save_settings(settings);
    }
    catch(...) {settings=previous;if(shortcut) {try {register_hotkey();}catch(...){}}throw;}
}

bool App::apply_setting(int id) {
    try {
        read_controls(id);
        if(id==DarkMode) {theme.apply(window,settings.dark_mode);licenses.apply_theme(settings.dark_mode);}
        if(!busy && !countdown)
            set_status(registered_key?L"Settings saved automatically.":L"Settings saved automatically.\n\nScreenshot shortcut is unavailable. Close the other capture app or choose a different key.");
        return true;
    } catch(const std::exception& error) {
        if(id>=ModifierCtrl && id<=KeyChoice) {
            SendDlgItemMessageW(window,ModifierCtrl,BM_SETCHECK,(settings.modifiers&MOD_CONTROL)?BST_CHECKED:BST_UNCHECKED,0);
            SendDlgItemMessageW(window,ModifierShift,BM_SETCHECK,(settings.modifiers&MOD_SHIFT)?BST_CHECKED:BST_UNCHECKED,0);
            SendDlgItemMessageW(window,ModifierAlt,BM_SETCHECK,(settings.modifiers&MOD_ALT)?BST_CHECKED:BST_UNCHECKED,0);
            SendDlgItemMessageW(window,KeyChoice,CB_SETCURSEL,settings.key==VK_SNAPSHOT?0:settings.key-VK_F1+1,0);
        }
        if(id==StartWithWindows)
            SendDlgItemMessageW(window,StartWithWindows,BM_SETCHECK,startup.enabled()?BST_CHECKED:BST_UNCHECKED,0);
        set_status(widen(error.what()));return false;
    }
}

void App::queue_edit(int id) {
    pending_edits|=1u<<(id-Folder);SetTimer(window,settings_timer,400,nullptr);
}

bool App::flush_edits() {
    KillTimer(window,settings_timer);
    const UINT edits=pending_edits|invalid_edits;pending_edits=0;std::wstring error;
    for(int id:{Folder,PaperWhite,PeakNits,Exposure,Sharpness}) {
        const UINT flag=1u<<(id-Folder);
        if(edits&flag) {
            if(apply_setting(id)) invalid_edits&=~flag;
            else {invalid_edits|=flag;error=text(status);}
        }
    }
    if(!error.empty()) set_status(error);
    return invalid_edits==0;
}

void App::browse() {
    ComPtr<IFileOpenDialog> dialog;
    check(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)),
          "Cannot create screenshot folder picker");
    FILEOPENDIALOGOPTIONS options{};
    check(dialog->GetOptions(&options),"Cannot read folder picker options");
    check(dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR),
          "Cannot configure screenshot folder picker");
    check(dialog->SetTitle(L"Choose screenshot folder"),"Cannot set folder picker title");
    check(dialog->SetOkButtonLabel(L"Select folder"),"Cannot set folder picker button");
    std::filesystem::path initial=text(GetDlgItem(window,Folder));std::error_code error;
    while(initial.has_relative_path() && !std::filesystem::is_directory(initial,error)) initial=initial.parent_path();
    ComPtr<IShellItem> start;
    if(initial.is_absolute() && SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(),nullptr,IID_PPV_ARGS(&start))))
        check(dialog->SetFolder(start.Get()),"Cannot open current screenshot folder");
    const HRESULT shown=dialog->Show(window);
    if(shown==HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
    check(shown,"Cannot show screenshot folder picker");
    ComPtr<IShellItem> item;
    check(dialog->GetResult(&item),"Cannot read selected screenshot folder");
    PWSTR path=nullptr;
    check(item->GetDisplayName(SIGDN_FILESYSPATH,&path),"Cannot read selected folder path");
    struct FreePath {PWSTR value;~FreePath(){CoTaskMemFree(value);}} free_path{path};
    SetDlgItemTextW(window,Folder,path);
}

LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    App* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message==WM_NCCREATE) {
        app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window,message,wp,lp);
    try {
        switch (message) {
        case WM_CREATE: app->create_controls();return 0;
        case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX:
            return reinterpret_cast<LRESULT>(app->theme.color_control(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp)));
        case WM_MEASUREITEM:
            if(reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->CtlType==ODT_COMBOBOX){reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight=24;return TRUE;}break;
        case WM_DRAWITEM:
            if(reinterpret_cast<DRAWITEMSTRUCT*>(lp)->CtlType==ODT_COMBOBOX){app->theme.combo_item(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;}break;
        case WM_NOTIFY:
            if(reinterpret_cast<NMHDR*>(lp)->idFrom==SettingsTabs && reinterpret_cast<NMHDR*>(lp)->code==TCN_SELCHANGE)
                app->show_page(TabCtrl_GetCurSel(GetDlgItem(window,SettingsTabs)));
            return 0;
        case WM_ERASEBKGND: {
            RECT rect{};GetClientRect(window,&rect);FillRect(reinterpret_cast<HDC>(wp),&rect,app->theme.background);return 1;
        }
        case WM_PRINTCLIENT: app->paint(reinterpret_cast<HDC>(wp));return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);
            app->paint(dc);
            EndPaint(window,&paint);return 0;
        }
        case WM_HSCROLL:
            if(app->controls_ready && reinterpret_cast<HWND>(lp)==GetDlgItem(window,Sharpness)) {
                app->update_sharpness_label();app->queue_edit(Sharpness);
                if(LOWORD(wp)==TB_ENDTRACK) app->flush_edits();
                return 0;
            }
            break;
        case WM_COMMAND:
            switch(LOWORD(wp)) {
            case Folder: case PaperWhite: case PeakNits: case Exposure:
                if(app->controls_ready && HIWORD(wp)==EN_CHANGE) app->queue_edit(LOWORD(wp));
                if(app->controls_ready && HIWORD(wp)==EN_KILLFOCUS) app->flush_edits();
                break;
            case MonitorChoice: case BackendChoice: case ContentChoice: case HdrFormatChoice: case HdrAnnotationChoice: case KeyChoice: case ImageSize:
                if(app->controls_ready && HIWORD(wp)==CBN_SELCHANGE) app->apply_setting(LOWORD(wp));
                break;
            case KeepOriginal: case DarkMode: case ShowMagnifier: case StartWithWindows: case ModifierCtrl: case ModifierShift: case ModifierAlt:
                if(app->controls_ready && HIWORD(wp)==BN_CLICKED) app->apply_setting(LOWORD(wp));
                if(LOWORD(wp)==KeepOriginal) app->update_hdr_choice();
                break;
            case IDCANCEL: app->cancel_capture();break;
            case Browse: app->browse();break;
            case LicensesButton: app->licenses.show(window,app->settings.dark_mode);break;
            case SaveButton: app->flush_edits();ShowWindow(window,SW_HIDE);break;
            case CaptureButton:
                if(app->busy || !app->flush_edits()) break;
                app->countdown=3;app->listen_for_escape();app->set_status(L"Capture in 3 seconds. Switch to the desired window.");
                SetTimer(window,countdown_timer,1000,nullptr);break;
            case FolderButton: app->flush_edits();ShellExecuteW(window,L"explore",app->settings.folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;
            case ExitButton: app->flush_edits();if(app->busy){app->exit_after_work=true;ShowWindow(window,SW_HIDE);}else DestroyWindow(window);break;
            }
            return 0;
        case WM_TIMER:
            if(wp==settings_timer) app->flush_edits();
            if(wp==hotkey_retry_timer) app->retry_hotkey();
            if (wp==countdown_timer && app->countdown>0 && --app->countdown<=0) {KillTimer(window,countdown_timer);app->begin_work();}
            return 0;
        case WM_HOTKEY:
            if(wp==hotkey_id) app->begin_work();
            if(wp==cancel_hotkey_id) app->cancel_capture();
            return 0;
        case WM_KEYDOWN: if(wp==VK_ESCAPE) {app->cancel_capture();return 0;}break;
        case WM_DISPLAYCHANGE: forget_capture_items();break;
        case done_message: app->finish_work();return 0;
        case tray_message:
            if (lp==WM_RBUTTONUP) app->tray_menu();
            if (lp==WM_LBUTTONDBLCLK) {ShowWindow(window,SW_SHOW);SetForegroundWindow(window);}
            return 0;
        case WM_CLOSE: app->flush_edits();ShowWindow(window,SW_HIDE);return 0;
        case WM_DESTROY: {
            KillTimer(window,hotkey_retry_timer);
            UnregisterHotKey(window,hotkey_id);app->release_escape();
            NOTIFYICONDATAW icon{};icon.cbSize=sizeof(icon);icon.hWnd=window;icon.uID=tray_id;
            Shell_NotifyIconW(NIM_DELETE,&icon);PostQuitMessage(0);return 0;
        }
        }
    } catch (const std::exception& error) {
        if (app->status) app->set_status(widen(error.what()));
        MessageBoxW(window,widen(error.what()).c_str(),L"LumenShot",MB_OK|MB_ICONERROR);
        if (message==WM_CREATE) return -1;
    }
    return DefWindowProcW(window,message,wp,lp);
}

} // namespace hdrshot
