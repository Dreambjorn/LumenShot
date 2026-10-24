#include "app/app.hpp"
#include "image/scaling.hpp"
#include <shellapi.h>

namespace hdrshot {

App::~App() {
    if (worker.joinable()) worker.join();
    warmup.join();
    release_escape();
    if (menu_window) DestroyWindow(menu_window);
    if (body_font) DeleteObject(body_font);
    if (title_font) DeleteObject(title_font);
    if (small_font) DeleteObject(small_font);

}

void App::notify(const std::wstring& title,const std::wstring& message,DWORD flags) {
    NOTIFYICONDATAW data{};data.cbSize=sizeof(data);data.hWnd=window;data.uID=tray_id;
    data.uFlags=NIF_INFO;data.dwInfoFlags=flags|NIIF_NOSOUND;
    wcsncpy_s(data.szInfoTitle,title.c_str(),_TRUNCATE);wcsncpy_s(data.szInfo,message.c_str(),_TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY,&data);
}

void App::register_hotkey() {
    if(registered_key==settings.key && registered_modifiers==settings.modifiers) return;
    UnregisterHotKey(window,hotkey_id);
    registered_key=0;
    if (!RegisterHotKey(window,hotkey_id,settings.modifiers|MOD_NOREPEAT,settings.key)) {
        SetTimer(window,hotkey_retry_timer,1000,nullptr);
        subtitle=L"Shortcut unavailable  |  "+hotkey_label(settings);
        InvalidateRect(window,nullptr,TRUE);
        throw std::runtime_error("Shortcut is already in use. Close the other capture app or choose a different key. Windows screen snipping may reserve Print Screen.");
    }
    KillTimer(window,hotkey_retry_timer);
    registered_key=settings.key;registered_modifiers=settings.modifiers;
    subtitle=L"Ready  |  "+hotkey_label(settings);
    InvalidateRect(window,nullptr,TRUE);
}

void App::retry_hotkey() {
    if(registered_key) {KillTimer(window,hotkey_retry_timer);return;}
    try {register_hotkey();}
    catch(const std::exception&) {return;}
    const auto message=text(status);
    if(!busy && !countdown && !invalid_edits
       && (message.find(L"Shortcut is already in use.")==0
           || message.find(L"Screenshot shortcut is unavailable.")!=std::wstring::npos))
        set_status(L"Ready. Press "+hotkey_label(settings)+L" to select an area.");
}

void App::listen_for_escape() {
    if(!escape_registered)
        escape_registered=RegisterHotKey(window,cancel_hotkey_id,MOD_NOREPEAT,VK_ESCAPE)!=FALSE;
}

void App::release_escape() {
    if(escape_registered) UnregisterHotKey(window,cancel_hotkey_id);
    escape_registered=false;
}

void App::cancel_capture() {
    if(active_selector) {active_selector->cancel();return;}
    if(countdown>0) {
        KillTimer(window,countdown_timer);countdown=0;release_escape();
        set_status(L"Capture canceled");
    } else if(preparing) {
        cancel_requested=true;release_escape();set_status(L"Capture canceled");
    }
}

void App::begin_work() {
    if (busy) return;
    const auto requested=diagnostics_enabled?DiagnosticRecord::Clock::now():DiagnosticRecord::Clock::time_point{};
    if(!flush_edits()) return;
    capture_diagnostics=diagnostics_enabled?std::make_shared<DiagnosticRecord>(DiagnosticSource::Live,requested):nullptr;
    DiagnosticContext context(capture_diagnostics);
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

void App::finish_work() {
    if(!busy || !worker.joinable()) return;
    const auto diagnostics=capture_diagnostics;
    DiagnosticContext context(diagnostics);
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
            RegionSelector selector(std::move(result.preview),capture_settings.show_magnifier);
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
                    const POINT origin{selected.rect.left,selected.rect.top};
                    // Remove secrets before resampling or sharpening can spread them into neighbouring pixels.
                    apply_privacy_marks(converted,selected.marks,{margin.left,margin.top},1,1,true);
                    if(snapshot.keep_original && cropped.hdr) apply_privacy_marks(cropped,selected.marks,origin,1,1,true);
                    if(snapshot.scale!=100) scale_image(converted,snapshot.scale,snapshot.sharpness);
                    else if(snapshot.sharpness) {
                        sharpen_image(converted,snapshot.sharpness);
                        converted=crop_image(converted,{selected.rect.left-margin.left,selected.rect.top-margin.top,
                                                        selected.rect.right-margin.left,selected.rect.bottom-margin.top});
                    }
                    if(snapshot.keep_original && cropped.hdr) enhance_hdr(cropped,snapshot.scale,snapshot.sharpness);
                    const double scale_x=double(converted.width)/(selected.rect.right-selected.rect.left);
                    const double scale_y=double(converted.height)/(selected.rect.bottom-selected.rect.top);
                    Image hdr_base;
                    if(snapshot.keep_original && cropped.hdr) {
                        apply_privacy_marks(cropped,selected.marks,origin,scale_x,scale_y);
                        if(snapshot.hdr_format==HdrFormat::UltraJpeg && !selected.marks.empty()) {
                            hdr_base=converted;apply_privacy_marks(hdr_base,selected.marks,origin,scale_x,scale_y);
                        }
                    }
                    annotate(converted,selected.marks,origin,scale_x,scale_y);
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
    if(!save_capture_diagnostics(diagnostics,result.success,result.canceled))
        result.details+=L"\nDiagnostics could not be saved.";
    set_status(result.status+(result.details.empty()?L"":L"\n\n"+result.details));
    if(!result.canceled)
        notify(result.success?L"Screenshot ready":L"Screenshot failed",result.success?result.status:result.details,
               result.success?NIIF_INFO:NIIF_ERROR);
    if(!result.success) {ShowWindow(window,SW_SHOW);SetForegroundWindow(window);}
    if(exit_after_work) DestroyWindow(window);
}

void App::tray_menu() {
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

} // namespace hdrshot
