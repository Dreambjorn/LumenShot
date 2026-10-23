#include "capture.hpp"
#include "hdr_output.hpp"
#include "startup.hpp"
#include "theme.hpp"
#include "notices.hpp"
#include "selector.hpp"
#include "scaling.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <memory>

using namespace hdrshot;

namespace {
constexpr wchar_t window_class[] = L"LumenShotAppWindow";
constexpr UINT done_message = WM_APP + 1, tray_message = WM_APP + 2;
constexpr int hotkey_id = 1, cancel_hotkey_id = 2, tray_id = 1, countdown_timer = 1, settings_timer = 2;
bool command_line_mode=false;
enum ControlId { Folder=100, Browse, MonitorChoice, BackendChoice, ContentChoice, PaperWhite,
                 Exposure, KeepOriginal, ModifierCtrl, ModifierShift, ModifierAlt, KeyChoice,
                 CaptureButton, SaveButton=114, FolderButton, ExitButton, Status, HdrFormatChoice, StartWithWindows, DarkMode, SettingsTabs, Sharpness, SharpnessValue, ImageSize, LicensesButton };
constexpr int settings_width=714,settings_height=650;
struct Settings {
    std::filesystem::path folder = executable_directory()/L"Screenshots";
    std::wstring monitor;
    CaptureBackend backend = CaptureBackend::Auto;
    ColorSettings color{ContentMode::Hdr,203,0,true};
    bool keep_original = true, dark_mode = true;
    HdrFormat hdr_format = HdrFormat::UltraJpeg;
    UINT modifiers = 0, key = VK_SNAPSHOT;
    unsigned sharpness = 0, scale = 100;
};
struct WorkResult {
    std::wstring status, details;
    bool success = false, canceled = false;
    Image preview, clipboard;
    Frame frame;
    RECT bounds{};
    bool needs_selection=false;
};

std::wstring text(HWND control) {
    const int length=GetWindowTextLengthW(control);
    std::wstring result(std::size_t(length)+1,L'\0');
    GetWindowTextW(control,result.data(),length+1);result.resize(length);return result;
}
std::filesystem::path config_path() { return executable_directory()/L"LumenShot.ini"; }
std::wstring ini_string(const wchar_t* key,const std::wstring& fallback) {
    std::wstring buffer(32768,L'\0');
    const DWORD length=GetPrivateProfileStringW(L"Screenshot",key,fallback.c_str(),buffer.data(),
                                               DWORD(buffer.size()),config_path().c_str());
    buffer.resize(length);return buffer;
}
Settings load_settings() {
    Settings settings;
    settings.folder=ini_string(L"Folder",settings.folder.wstring());
    settings.monitor=ini_string(L"Monitor",L"");
    const auto value=[&](const wchar_t* key,int fallback){
        return GetPrivateProfileIntW(L"Screenshot",key,fallback,config_path().c_str());
    };
    settings.backend=CaptureBackend(std::min(value(L"Backend",0),2u));
    settings.color.mode=ContentMode(std::min(value(L"Content",1),2u));
    try { settings.color.paper_white=std::stod(ini_string(L"PaperWhite",L"203")); }
    catch (...) { settings.color.paper_white=203; }
    try { settings.color.exposure=std::stod(ini_string(L"Exposure",L"0")); }
    catch (...) { settings.color.exposure=0; }
    if (!std::isfinite(settings.color.paper_white) || settings.color.paper_white<40 || settings.color.paper_white>2000)
        settings.color.paper_white=203;
    if (!std::isfinite(settings.color.exposure) || std::abs(settings.color.exposure)>4) settings.color.exposure=0;
    const auto sharpness=value(L"Sharpness",0);
    settings.sharpness=sharpness<=100?sharpness:0;
    const auto scale=value(L"Scale",100);settings.scale=valid_scale(scale)?scale:100;
    settings.keep_original=value(L"KeepOriginal",1)!=0;
    settings.dark_mode=value(L"DarkMode",1)!=0;
    settings.hdr_format=value(L"HdrFormat",0)==1?HdrFormat::JpegXr:HdrFormat::UltraJpeg;
    settings.modifiers=value(L"Modifiers",0)&(MOD_CONTROL|MOD_SHIFT|MOD_ALT);
    settings.key=value(L"Key",VK_SNAPSHOT);
    if (settings.key!=VK_SNAPSHOT && (settings.key<VK_F1 || settings.key>VK_F11)) settings.key=VK_SNAPSHOT;
    return settings;
}
void save_settings(const Settings& settings) {
    const auto write=[&](const wchar_t* key,const std::wstring& value){
        if (!WritePrivateProfileStringW(L"Screenshot",key,value.c_str(),config_path().c_str()))
            throw std::runtime_error("Cannot save settings beside the application. Move it to a writable folder.");
    };
    write(L"Folder",settings.folder.wstring());write(L"Monitor",settings.monitor);
    write(L"Backend",std::to_wstring(int(settings.backend)));
    write(L"Content",std::to_wstring(int(settings.color.mode)));
    write(L"PaperWhite",std::to_wstring(settings.color.paper_white));
    write(L"Exposure",std::to_wstring(settings.color.exposure));
    write(L"Sharpness",std::to_wstring(settings.sharpness));
    write(L"Scale",std::to_wstring(settings.scale));
    write(L"KeepOriginal",settings.keep_original?L"1":L"0");
    write(L"HdrFormat",std::to_wstring(int(settings.hdr_format)));
    write(L"DarkMode",settings.dark_mode?L"1":L"0");
    write(L"Modifiers",std::to_wstring(settings.modifiers));write(L"Key",std::to_wstring(settings.key));
}
std::wstring hotkey_label(const Settings& settings) {
    std::wstring label;
    if (settings.modifiers&MOD_CONTROL) label+=L"Ctrl + ";
    if (settings.modifiers&MOD_SHIFT) label+=L"Shift + ";
    if (settings.modifiers&MOD_ALT) label+=L"Alt + ";
    return label+(settings.key==VK_SNAPSHOT?L"Print Screen":L"F"+std::to_wstring(settings.key-VK_F1+1));
}
CapturedInput capture_input(const Settings& settings,HMONITOR foreground) {
    const auto displays=monitors();
    if(settings.monitor==L"*") return capture_desktop(displays,settings.backend);
    auto found=displays.end();
    if(!settings.monitor.empty()) found=std::find_if(displays.begin(),displays.end(),
        [&](const Monitor& monitor){return monitor.device_name==settings.monitor;});
    else found=std::find_if(displays.begin(),displays.end(),
        [&](const Monitor& monitor){return monitor.handle==foreground;});
    if(found==displays.end()) throw std::runtime_error("Selected monitor is no longer connected. Restart settings.");
    CapturedInput input;input.bounds=found->bounds;
    input.frame=capture(*found,settings.backend,&input.sdr);
    return input;
}
// Sharpening waits for the crop; a full-screen pass delayed the selector by over 100 ms.
void prepare_preview(WorkResult& result,const Settings& settings,Image sdr={}) {
    result.preview=sdr.bgra.empty()?render_sdr(result.frame,settings.color):std::move(sdr);
}
// Sharpening reads each neighbour, so a one-pixel margin keeps a crop equal to a full-image pass.
RECT sharpen_margin(RECT rect,const Image& image,const Settings& settings) {
    if(settings.scale!=100 || !settings.sharpness) return rect;
    return {std::max(0l,rect.left-1),std::max(0l,rect.top-1),
            std::min(LONG(image.width),rect.right+1),std::min(LONG(image.height),rect.bottom+1)};
}
WorkResult process_output(const Settings& settings,const Frame& frame,const Image& converted,
                          const std::filesystem::path& imported={},bool clipboard=false,const Image* hdr_base=nullptr) {
    std::filesystem::create_directories(settings.folder);
    const auto name=screenshot_name();
    const auto output=settings.folder/(name+L".png");
    if(!clipboard) save_image(output,converted);
    std::wstring warning;
    std::filesystem::path original;
    bool jpeg=false;
    if(settings.keep_original && frame.hdr) {
        try {
            jpeg=settings.hdr_format==HdrFormat::UltraJpeg && frame.width>=8 && frame.height>=8;
            const auto originals=settings.folder/L"HDR images";
            std::filesystem::create_directories(originals);
            const auto destination=originals/(name+(jpeg?L".jpg":L".jxr"));
            if(jpeg) save_hdr_jpeg(destination,frame,hdr_base?*hdr_base:converted);
            else if(imported.empty() || settings.sharpness || settings.scale!=100) save_master(destination,frame);
            else std::filesystem::copy_file(imported,destination,std::filesystem::copy_options::none);
            original=destination;
            if(!jpeg && settings.hdr_format==HdrFormat::UltraJpeg)
                warning=L"HDR JPEG requires at least 8 x 8 pixels. Saved JPEG XR instead.";
        } catch(const std::exception& error) {
            warning=L"HDR image could not be saved: "+widen(error.what());
        }
    }
    WorkResult result;
    result.success=true;
    if(clipboard) result.clipboard=converted;
    result.status=clipboard?L"Copied screenshot to clipboard":L"Saved screenshot";
    std::wostringstream details;
    details<<L"Size: "<<converted.width<<L" x "<<converted.height
           <<(clipboard?L"\nClipboard color: ":L"\nPNG color: ")<<converted.description;
    if(frame.hdr && settings.color.exposure!=0)
        details<<L"\nHDR exposure setting: "<<std::showpos<<std::fixed<<std::setprecision(2)<<settings.color.exposure<<L" stops";
    if(settings.sharpness) details<<L"\nSharpness: "<<settings.sharpness<<L"%";
    if(settings.scale!=100) details<<L"\nImage size: "<<settings.scale<<L"%";
    if(!clipboard) details<<L"\n\nPNG: "<<output.wstring();
    if(!original.empty()) details<<(jpeg?L"\n\nHDR JPEG: ":L"\n\nHDR JPEG XR: ")<<original.wstring();
    if(!warning.empty()) details<<L"\n\n"<<warning;
    result.details=details.str();
    return result;
}
WorkResult process_capture(const Settings& settings,HMONITOR foreground,const std::filesystem::path& imported) {
    Runtime runtime;
    WorkResult input;
    Image sdr;
    if(imported.empty()) {auto captured=capture_input(settings,foreground);input.frame=std::move(captured.frame);sdr=std::move(captured.sdr);}
    else input.frame=load_master(imported);
    Settings snapshot=settings;
    if(!imported.empty() && snapshot.color.mode==ContentMode::Auto) snapshot.color.mode=ContentMode::Hdr;
    prepare_preview(input,snapshot,std::move(sdr));
    Image converted=std::move(input.preview);
    if(settings.scale!=100) scale_image(converted,settings.scale,settings.sharpness);
    else sharpen_image(converted,settings.sharpness);
    if(settings.keep_original && input.frame.hdr) enhance_hdr(input.frame,settings.scale,settings.sharpness);
    return process_output(settings,input.frame,converted,imported);
}

struct App {
    HWND window=nullptr,status=nullptr,menu_window=nullptr;
    HFONT body_font=nullptr,title_font=nullptr,small_font=nullptr;
    NativeTheme theme;
    LicenseViewer licenses;
    int active_page=0,creating_page=-1;
    std::vector<std::pair<HWND,int>> page_controls;
    Settings settings=load_settings();
    Settings capture_settings=settings;
    std::vector<Monitor> displays;
    NativeWork worker,warmup;
    StartupRegistration startup;
    bool busy=false,exit_after_work=false,tray_added=false;
    bool preparing=false,cancel_requested=false,escape_registered=false;
    RegionSelector* active_selector=nullptr;
    int countdown=0;
    bool controls_ready=false;
    UINT pending_edits=0,invalid_edits=0;
    UINT registered_modifiers=0,registered_key=0;
    WorkResult result;
    std::wstring subtitle;
    ~App() {
        if (worker.joinable()) worker.join();
        warmup.join();
        release_escape();
        if (menu_window) DestroyWindow(menu_window);
        if (body_font) DeleteObject(body_font);
        if (title_font) DeleteObject(title_font);
        if (small_font) DeleteObject(small_font);

    }
    HWND control(const wchar_t* type,const wchar_t* label,DWORD style,int id,int x,int y,int w,int h) {
        // A client edge draws a white bevel in dark mode; the theme paints a flat border instead.
        HWND child=CreateWindowExW(0,type,label,
            WS_CHILD|WS_VISIBLE|style|(wcscmp(type,L"EDIT")==0?WS_BORDER:0),x,y,w,h,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        if (!child) throw std::runtime_error("Cannot create application controls.");
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(body_font),TRUE);
        if(wcscmp(type,L"EDIT")==0) SendMessageW(child,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));
        if(creating_page>=0) page_controls.emplace_back(child,creating_page);
        return child;
    }
    void label(const wchar_t* value,int x,int y,int width) { control(L"STATIC",value,SS_LEFT,0,x,y,width,22); }
    void add_combo(int id,const std::vector<std::wstring>& entries,int selection,int x,int y,int width) {
        HWND combo=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_TABSTOP|WS_VSCROLL,id,x,y,width,280);
        for (const auto& entry:entries) SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(entry.c_str()));
        SendMessageW(combo,CB_SETCURSEL,selection,0);
    }
    void notify(const std::wstring& title,const std::wstring& message,DWORD flags=NIIF_INFO) {
        NOTIFYICONDATAW data{};data.cbSize=sizeof(data);data.hWnd=window;data.uID=tray_id;
        data.uFlags=NIF_INFO;data.dwInfoFlags=flags|NIIF_NOSOUND;
        wcsncpy_s(data.szInfoTitle,title.c_str(),_TRUNCATE);wcsncpy_s(data.szInfo,message.c_str(),_TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY,&data);
    }
    void paint(HDC dc) {
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
    }
    void set_status(const std::wstring& message) {
        std::wstring display;
        for(std::size_t i=0;i<message.size();++i) {
            if(message[i]==L'\n' && (i==0 || message[i-1]!=L'\r')) display+=L'\r';
            display+=message[i];
        }
        SetWindowTextW(status,display.c_str());
    }
    void register_hotkey() {
        if(registered_key==settings.key && registered_modifiers==settings.modifiers) return;
        UnregisterHotKey(window,hotkey_id);
        registered_key=0;
        if (!RegisterHotKey(window,hotkey_id,settings.modifiers|MOD_NOREPEAT,settings.key))
            throw std::runtime_error("Shortcut is already in use. Close the other capture app or choose a different key. Windows screen snipping may reserve Print Screen.");
        registered_key=settings.key;registered_modifiers=settings.modifiers;
        subtitle=L"Ready  |  "+hotkey_label(settings);
        InvalidateRect(window,nullptr,TRUE);
    }
    void create_controls() {
        body_font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        small_font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        title_font=CreateFontW(-30,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        INITCOMMONCONTROLSEX tab_classes{sizeof(tab_classes),ICC_TAB_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&tab_classes);
        constexpr int left=28,right=370,column_width=316,content_width=658,field_height=30;
        HWND tabs=control(WC_TABCONTROLW,L"",WS_TABSTOP|TCS_FIXEDWIDTH|TCS_SINGLELINE,SettingsTabs,left,104,content_width,40);
        const wchar_t* pages[]={L"Capture",L"Output",L"General"};
        for(int i=0;i<3;++i){TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(pages[i]);TabCtrl_InsertItem(tabs,i,&item);}
        TabCtrl_SetItemSize(tabs,(content_width-4)/3,36);
        creating_page=0;
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
        add_combo(BackendChoice,{L"Automatic (Windows, then desktop)",L"Windows Graphics Capture",L"Desktop Duplication"},int(settings.backend),right,194,column_width);
        label(L"Screenshot shortcut",left,236,column_width);
        const struct {int id;const wchar_t* text;UINT modifier;int x;} modifiers[]={
            {ModifierCtrl,L"Ctrl",MOD_CONTROL,left},{ModifierShift,L"Shift",MOD_SHIFT,left+108},{ModifierAlt,L"Alt",MOD_ALT,left+216}};
        for(const auto& modifier:modifiers) {
            HWND check=control(L"BUTTON",modifier.text,BS_AUTOCHECKBOX|WS_TABSTOP,modifier.id,modifier.x,262,100,field_height);
            SendMessageW(check,BM_SETCHECK,(settings.modifiers&modifier.modifier)?BST_CHECKED:BST_UNCHECKED,0);
        }
        std::vector<std::wstring> keys{L"Print Screen"};for(int i=1;i<=11;++i)keys.push_back(L"F"+std::to_wstring(i));
        add_combo(KeyChoice,keys,settings.key==VK_SNAPSHOT?0:int(settings.key-VK_F1)+1,right,262,column_width);
        creating_page=1;
        label(L"Save folder",left,168,column_width);
        control(L"EDIT",settings.folder.c_str(),ES_AUTOHSCROLL|WS_TABSTOP,Folder,left,194,542,field_height);
        control(L"BUTTON",L"Browse...",BS_PUSHBUTTON|WS_TABSTOP,Browse,580,194,106,field_height);
        label(L"Content",left,236,column_width);label(L"HDR reference white (nits)",right,236,column_width);
        add_combo(ContentChoice,{L"Automatic",L"HDR content",L"SDR content on HDR display"},int(settings.color.mode),left,262,column_width);
        control(L"EDIT",std::to_wstring(int(settings.color.paper_white)).c_str(),ES_NUMBER|WS_TABSTOP,PaperWhite,right,262,column_width,field_height);
        label(L"HDR exposure (stops)",left,304,170);
        label(L"Image size",210,304,134);
        std::wostringstream exposure_text;exposure_text<<std::fixed<<std::setprecision(2)<<settings.color.exposure;
        control(L"EDIT",exposure_text.str().c_str(),ES_AUTOHSCROLL|WS_TABSTOP,Exposure,left,330,170,field_height);
        const auto size_index=std::find(image_scales.begin(),image_scales.end(),settings.scale)-image_scales.begin();
        add_combo(ImageSize,{L"Original",L"125%",L"150%",L"200%"},int(size_index),210,330,134);
        label(L"Sharpness",right,304,column_width);
        HWND sharpness=control(TRACKBAR_CLASSW,L"",TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,Sharpness,right,330,260,field_height);
        SendMessageW(sharpness,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));
        SendMessageW(sharpness,TBM_SETPAGESIZE,0,10);
        SendMessageW(sharpness,TBM_SETPOS,TRUE,settings.sharpness);
        control(L"STATIC",L"Off",SS_LEFT|SS_CENTERIMAGE,SharpnessValue,640,330,46,field_height);
        update_sharpness_label();
        HWND keep=control(L"BUTTON",L"Save HDR image",BS_AUTOCHECKBOX|WS_TABSTOP,KeepOriginal,left,376,column_width,field_height);
        SendMessageW(keep,BM_SETCHECK,settings.keep_original?BST_CHECKED:BST_UNCHECKED,0);
        add_combo(HdrFormatChoice,{L"Ultra HDR JPEG (.jpg) - recommended",L"JPEG XR (.jxr) - lossless HDR"},int(settings.hdr_format),right,376,column_width);
        creating_page=2;
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
        theme.apply(window,settings.dark_mode);show_page(0);
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
    void show_page(int page) {
        active_page=std::clamp(page,0,2);TabCtrl_SetCurSel(GetDlgItem(window,SettingsTabs),active_page);
        for(const auto& item:page_controls)
            ShowWindow(item.first,item.second==active_page?SW_SHOWNA:SW_HIDE);
        update_hdr_choice();InvalidateRect(window,nullptr,TRUE);
    }
    void update_hdr_choice() {
        const bool keep=SendDlgItemMessageW(window,KeepOriginal,BM_GETCHECK,0,0)==BST_CHECKED;
        ShowWindow(GetDlgItem(window,HdrFormatChoice),keep && active_page==1?SW_SHOWNA:SW_HIDE);
    }
    void update_sharpness_label() {
        const auto value=SendDlgItemMessageW(window,Sharpness,TBM_GETPOS,0,0);
        const auto label=value?std::to_wstring(value)+L"%":L"Off";
        SetDlgItemTextW(window,SharpnessValue,label.c_str());
    }
    void read_controls(int changed=0) {
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
            const char* message="Reference white must be between 40 and 2000 nits.";
            next.color.paper_white=number(PaperWhite,message);
            if(next.color.paper_white<40 || next.color.paper_white>2000) throw std::runtime_error(message);
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
        if(!changed || changed==DarkMode)
            next.dark_mode=SendDlgItemMessageW(window,DarkMode,BM_GETCHECK,0,0)==BST_CHECKED;
        if(!changed || changed==KeepOriginal)
            next.keep_original=SendDlgItemMessageW(window,KeepOriginal,BM_GETCHECK,0,0)==BST_CHECKED;
        if(!changed || changed==HdrFormatChoice)
            next.hdr_format=SendDlgItemMessageW(window,HdrFormatChoice,CB_GETCURSEL,0,0)==1?HdrFormat::JpegXr:HdrFormat::UltraJpeg;
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
    bool apply_setting(int id) {
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
    void queue_edit(int id) {
        pending_edits|=1u<<(id-Folder);SetTimer(window,settings_timer,400,nullptr);
    }
    bool flush_edits() {
        KillTimer(window,settings_timer);
        const UINT edits=pending_edits|invalid_edits;pending_edits=0;std::wstring error;
        for(int id:{Folder,PaperWhite,Exposure,Sharpness}) {
            const UINT flag=1u<<(id-Folder);
            if(edits&flag) {
                if(apply_setting(id)) invalid_edits&=~flag;
                else {invalid_edits|=flag;error=text(status);}
            }
        }
        if(!error.empty()) set_status(error);
        return invalid_edits==0;
    }
    void listen_for_escape() {
        if(!escape_registered)
            escape_registered=RegisterHotKey(window,cancel_hotkey_id,MOD_NOREPEAT,VK_ESCAPE)!=FALSE;
    }
    void release_escape() {
        if(escape_registered) UnregisterHotKey(window,cancel_hotkey_id);
        escape_registered=false;
    }
    void cancel_capture() {
        if(active_selector) {active_selector->cancel();return;}
        if(countdown>0) {
            KillTimer(window,countdown_timer);countdown=0;release_escape();
            set_status(L"Capture canceled");
        } else if(preparing) {
            cancel_requested=true;release_escape();set_status(L"Capture canceled");
        }
    }
    void begin_work() {
        if (busy || !flush_edits()) return;
        KillTimer(window,countdown_timer); countdown=0;
        const HMONITOR foreground=MonitorFromWindow(GetForegroundWindow(),MONITOR_DEFAULTTOPRIMARY);
        busy=true;preparing=true;cancel_requested=false;capture_settings=settings;listen_for_escape();
        set_status(L"Capturing...");
        const Settings snapshot=capture_settings;
        worker.start([this,snapshot,foreground] {
            try {
                Runtime runtime;
                auto captured=capture_input(snapshot,foreground);
                result={};result.frame=std::move(captured.frame);result.bounds=captured.bounds;
                prepare_preview(result,snapshot,std::move(captured.sdr));result.needs_selection=true;
            }
            catch (const std::exception& error) { result={};result.status=L"Screenshot failed";result.details=widen(error.what()); }
            PostMessageW(window,done_message,0,0);
        });
    }
    void finish_work() {
        if(!busy || !worker.joinable()) return;
        worker.join();busy=false;preparing=false;
        if(cancel_requested) {
            result={};result.success=true;result.canceled=true;result.status=L"Capture canceled";
            cancel_requested=false;
        }
        if(result.needs_selection) {
            busy=true;
            SelectionResult selected;
            Image converted;
            Frame cropped;
            RECT margin{};
            {
                RegionSelector selector(std::move(result.preview));
                active_selector=&selector;
                struct ClearSelector {RegionSelector*& value;~ClearSelector(){value=nullptr;}} clear{active_selector};
                selected=selector.run(window,result.bounds);
                if(selected.action!=SelectionAction::Cancel) {
                    margin=sharpen_margin(selected.rect,selector.image(),capture_settings);
                    converted=crop_image(selector.image(),margin);
                    cropped=crop_frame(result.frame,selected.rect);
                }
            }
            result={};busy=false;release_escape();
            if(selected.action!=SelectionAction::Cancel) {
                const Settings snapshot=capture_settings;
                busy=true;
                worker.start([this,snapshot,selected,margin,cropped=std::move(cropped),converted=std::move(converted)]() mutable {
                    try {
                        Runtime runtime;
                        if(snapshot.scale!=100) scale_image(converted,snapshot.scale,snapshot.sharpness);
                        else if(snapshot.sharpness) {
                            sharpen_image(converted,snapshot.sharpness);
                            converted=crop_image(converted,{selected.rect.left-margin.left,selected.rect.top-margin.top,
                                                            selected.rect.right-margin.left,selected.rect.bottom-margin.top});
                        }
                        if(snapshot.keep_original && cropped.hdr) enhance_hdr(cropped,snapshot.scale,snapshot.sharpness);
                        Image hdr_base;
                        if(snapshot.keep_original && cropped.hdr && snapshot.hdr_format==HdrFormat::UltraJpeg && !selected.marks.empty())
                            hdr_base=converted;
                        annotate(converted,selected.marks,{selected.rect.left,selected.rect.top},
                                 double(converted.width)/(selected.rect.right-selected.rect.left),
                                 double(converted.height)/(selected.rect.bottom-selected.rect.top));
                        result=process_output(snapshot,cropped,converted,{},selected.action==SelectionAction::Copy,
                                              hdr_base.bgra.empty()?nullptr:&hdr_base);
                    }
                    catch(const std::exception& error) {result={};result.status=L"Screenshot failed";result.details=widen(error.what());}
                    PostMessageW(window,done_message,0,0);
                });
                return;
            }
            result.success=true;result.canceled=true;result.status=L"Capture canceled";
        }
        release_escape();
        if(!result.clipboard.bgra.empty()) {
            try {copy_image(window,result.clipboard);}
            catch(const std::exception& error) {result.success=false;result.status=L"Copy failed";result.details=widen(error.what())+L"\n\n"+result.details;}
        }
        set_status(result.status+(result.details.empty()?L"":L"\n\n"+result.details));
        if(!result.canceled)
            notify(result.success?L"Screenshot ready":L"Screenshot failed",result.success?result.status:result.details,
                   result.success?NIIF_INFO:NIIF_ERROR);
        if(!result.success) {ShowWindow(window,SW_SHOW);SetForegroundWindow(window);}
        if(exit_after_work) DestroyWindow(window);
    }
    void browse() {
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
    void tray_menu() {
        POINT point{};GetCursorPos(&point);
        HMENU menu=CreatePopupMenu();
        AppendMenuW(menu,MF_STRING,1,L"Capture screenshot");
        AppendMenuW(menu,MF_STRING,2,L"Settings");
        AppendMenuW(menu,MF_STRING,3,L"Open screenshot folder");
        AppendMenuW(menu,MF_STRING,5,L"Licenses");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,4,L"Exit");
        SetForegroundWindow(menu_window);
        const UINT selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,menu_window,nullptr);
        PostMessageW(menu_window,WM_NULL,0,0);
        DestroyMenu(menu);
        if (selected==1) begin_work();
        if (selected==2) {ShowWindow(window,SW_SHOW);SetForegroundWindow(window);}
        if (selected==3) ShellExecuteW(window,L"explore",settings.folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        if (selected==5) licenses.show(window,settings.dark_mode);
        if (selected==4) {flush_edits();if (busy){exit_after_work=true;ShowWindow(window,SW_HIDE);}else DestroyWindow(window);}
    }
};
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
            case Folder: case PaperWhite: case Exposure:
                if(app->controls_ready && HIWORD(wp)==EN_CHANGE) app->queue_edit(LOWORD(wp));
                if(app->controls_ready && HIWORD(wp)==EN_KILLFOCUS) app->flush_edits();
                break;
            case MonitorChoice: case BackendChoice: case ContentChoice: case HdrFormatChoice: case KeyChoice: case ImageSize:
                if(app->controls_ready && HIWORD(wp)==CBN_SELCHANGE) app->apply_setting(LOWORD(wp));
                break;
            case KeepOriginal: case DarkMode: case StartWithWindows: case ModifierCtrl: case ModifierShift: case ModifierAlt:
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
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
        struct FreeArgs{LPWSTR* value;~FreeArgs(){LocalFree(value);}} free_args{argv};
        if (argc>1 && wcscmp(argv[1],L"--licenses")==0) {
            command_line_mode=true;std::cout<<embedded_notices();return 0;
        }
        if (argc>1 && (wcscmp(argv[1],L"--help")==0 || wcscmp(argv[1],L"--version")==0)) {
            std::cout<<"LumenShot 0.1.2\nUsage: LumenShot.exe [--settings | --tray | --capture | --import <file.jxr> | --licenses]\n"
                     <<"External Windows capture only. No game injection or game-process access.\n";return 0;
        }
        const bool open_settings=argc>1 && wcscmp(argv[1],L"--settings")==0;
        command_line_mode=argc>1 && wcscmp(argv[1],L"--tray")!=0 && !open_settings;
        if (argc>1 && (wcscmp(argv[1],L"--capture")==0 || wcscmp(argv[1],L"--import")==0)) {
            Settings settings=load_settings();
            std::filesystem::path imported;
            if(wcscmp(argv[1],L"--import")==0) {
                if(argc<3) throw std::runtime_error("Specify a floating-point .jxr file to import.");
                imported=argv[2];
            }
            const auto result=process_capture(settings,MonitorFromWindow(GetForegroundWindow(),MONITOR_DEFAULTTOPRIMARY),imported);
            std::wcout<<result.status<<L"\n\n"<<result.details<<L"\n";return 0;
        }
        HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\LumenShotApp.0.1");
        if(!mutex) throw std::runtime_error("Cannot initialize application instance.");
        struct CloseMutex{HANDLE value;~CloseMutex(){CloseHandle(value);}} close_mutex{mutex};
        if(GetLastError()==ERROR_ALREADY_EXISTS) {
            HWND existing=FindWindowW(window_class,nullptr);
            if(existing && open_settings){ShowWindow(existing,SW_SHOW);SetForegroundWindow(existing);}return 0;
        }
        check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Cannot initialize application UI");
        struct Uninit{~Uninit(){CoUninitialize();}} uninit;
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
        App app;
        WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=instance;wc.lpszClassName=window_class;
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));
        RegisterClassW(&wc);
        RECT bounds{0,0,settings_width,settings_height};
        AdjustWindowRectEx(&bounds,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,0);
        const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
        HWND window=CreateWindowExW(0,window_class,L"LumenShot",
            WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
            CW_USEDEFAULT,CW_USEDEFAULT,width,height,nullptr,nullptr,instance,&app);
        if(!window) throw std::runtime_error("Cannot create settings window.");
        ShowWindow(window,open_settings?SW_SHOW:SW_HIDE);UpdateWindow(window);
        MSG message{};
        while(GetMessageW(&message,nullptr,0,0)>0) {
            if(!app.licenses.translate(message) && !IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<"\n";
        if(!command_line_mode) MessageBoxW(nullptr,widen(error.what()).c_str(),L"LumenShot",MB_OK|MB_ICONERROR);
        return 1;
    }
}
