#pragma once
#include "theme.hpp"
#include "resource_ids.h"

namespace hdrshot {

inline std::string embedded_notices() {
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    HRSRC resource=FindResourceW(instance,MAKEINTRESOURCEW(HDRSHOT_NOTICES_RESOURCE),RT_RCDATA);
    const DWORD size=resource?SizeofResource(instance,resource):0;
    const auto* data=resource?static_cast<const char*>(LockResource(LoadResource(instance,resource))):nullptr;
    if(!data || !size) throw std::runtime_error("Embedded license texts are unavailable.");
    return {data,size};
}

class LicenseViewer {
    HWND window_=nullptr;
    NativeTheme theme_;
    std::wstring content_;
    static LRESULT CALLBACK edit_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
        if(message==WM_KEYDOWN && wp=='A' && GetKeyState(VK_CONTROL)<0) {
            SendMessageW(window,EM_SETSEL,0,-1);return 0;
        }
        if(message==WM_NCDESTROY) RemoveWindowSubclass(window,edit_proc,id);
        return DefSubclassProc(window,message,wp,lp);
    }
    void layout() const {
        RECT client{};GetClientRect(window_,&client);
        const int dpi=int(GetDpiForWindow(window_)),pad=MulDiv(16,dpi,96);
        const int button_width=MulDiv(110,dpi,96),button_height=MulDiv(30,dpi,96);
        MoveWindow(GetDlgItem(window_,HDRSHOT_LICENSE_TEXT),pad,pad,
                   std::max<LONG>(1,client.right-2*pad),std::max<LONG>(1,client.bottom-3*pad-button_height),TRUE);
        MoveWindow(GetDlgItem(window_,IDCANCEL),client.right-pad-button_width,
                   client.bottom-pad-button_height,button_width,button_height,TRUE);
    }
    static INT_PTR CALLBACK dialog_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
        auto* viewer=reinterpret_cast<LicenseViewer*>(GetWindowLongPtrW(window,DWLP_USER));
        if(message==WM_INITDIALOG) {
            viewer=reinterpret_cast<LicenseViewer*>(lp);viewer->window_=window;
            SetWindowLongPtrW(window,DWLP_USER,reinterpret_cast<LONG_PTR>(viewer));
            HWND edit=GetDlgItem(window,HDRSHOT_LICENSE_TEXT);
            SetWindowTextW(edit,viewer->content_.c_str());
            SendMessageW(edit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));
            SetWindowSubclass(edit,edit_proc,0x4c4943,0);
            viewer->theme_.apply(window,viewer->theme_.dark);viewer->layout();
            SetFocus(edit);return FALSE;
        }
        if(!viewer) return FALSE;
        switch(message) {
        case WM_SIZE: viewer->layout();return TRUE;
        case WM_GETMINMAXINFO: {
            auto* limits=reinterpret_cast<MINMAXINFO*>(lp);
            const int dpi=int(GetDpiForWindow(window));
            limits->ptMinTrackSize={MulDiv(420,dpi,96),MulDiv(300,dpi,96)};return TRUE;
        }
        case WM_CTLCOLORDLG: case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORBTN:
            return reinterpret_cast<INT_PTR>(viewer->theme_.color_control(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp)));
        case WM_ERASEBKGND: {
            RECT client{};GetClientRect(window,&client);
            FillRect(reinterpret_cast<HDC>(wp),&client,viewer->theme_.background);return TRUE;
        }
        case WM_COMMAND:
            if(LOWORD(wp)==IDCANCEL || LOWORD(wp)==IDOK) {DestroyWindow(window);return TRUE;}break;
        case WM_CLOSE: DestroyWindow(window);return TRUE;
        case WM_NCDESTROY: viewer->window_=nullptr;SetWindowLongPtrW(window,DWLP_USER,0);break;
        }
        return FALSE;
    }
public:
    LicenseViewer()=default;
    LicenseViewer(const LicenseViewer&)=delete;
    LicenseViewer& operator=(const LicenseViewer&)=delete;
    ~LicenseViewer(){if(window_) DestroyWindow(window_);}
    HWND window() const {return window_;}
    bool dark() const {return theme_.dark;}
    bool translate(MSG& message) const {
        if(!window_ || (message.hwnd!=window_ && !IsChild(window_,message.hwnd))) return false;
        if(message.message==WM_KEYDOWN && message.wParam==VK_ESCAPE) {
            SendMessageW(window_,WM_CLOSE,0,0);return true;
        }
        return IsDialogMessageW(window_,&message)!=FALSE;
    }
    void apply_theme(bool dark) {if(window_) theme_.apply(window_,dark);else theme_.set_colors(dark);}
    void show(HWND owner,bool dark) {
        if(!window_) {
            content_.clear();
            for(const wchar_t value:widen(embedded_notices())) {
                if(value==L'\n' && (content_.empty() || content_.back()!=L'\r')) content_+=L'\r';
                content_+=value;
            }
            theme_.set_colors(dark);
            HWND created=CreateDialogParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(HDRSHOT_LICENSE_DIALOG),
                                           owner,dialog_proc,reinterpret_cast<LPARAM>(this));
            if(!created) throw std::runtime_error("Cannot open the license viewer.");
            if(GetWindowTextLengthW(GetDlgItem(created,HDRSHOT_LICENSE_TEXT))!=int(content_.size())) {
                DestroyWindow(created);throw std::runtime_error("Cannot display the complete license texts.");
            }
            POINT point{};GetCursorPos(&point);
            const HMONITOR monitor=IsWindowVisible(owner)?MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST)
                :MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST);
            MONITORINFO info{};info.cbSize=sizeof(info);
            if(GetMonitorInfoW(monitor,&info)) {
                RECT bounds{};GetWindowRect(created,&bounds);
                const int width=std::min(bounds.right-bounds.left,info.rcWork.right-info.rcWork.left);
                const int height=std::min(bounds.bottom-bounds.top,info.rcWork.bottom-info.rcWork.top);
                SetWindowPos(created,nullptr,info.rcWork.left+(info.rcWork.right-info.rcWork.left-width)/2,
                             info.rcWork.top+(info.rcWork.bottom-info.rcWork.top-height)/2,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
            }
        } else apply_theme(dark);
        ShowWindow(window_,IsIconic(window_)?SW_RESTORE:SW_SHOW);SetForegroundWindow(window_);
        SetFocus(GetDlgItem(window_,HDRSHOT_LICENSE_TEXT));
    }
};

} // namespace hdrshot
