#pragma once
#include "platform/platform.hpp"
#include <dwmapi.h>
#include <vector>

namespace hdrshot {

inline RECT window_snap_rect(RECT window,RECT capture) {
    RECT visible{};
    if(!IntersectRect(&visible,&window,&capture)) return {};
    OffsetRect(&visible,-capture.left,-capture.top);
    return visible;
}
inline RECT window_snap_at(const std::vector<RECT>& windows,POINT point) {
    for(const auto& window:windows) if(PtInRect(&window,point)) return window;
    return {};
}
inline std::vector<RECT> window_snap_rectangles(RECT capture,DWORD ignored_process) {
    struct Windows {RECT capture;DWORD ignored_process;std::vector<RECT> rectangles;};
    Windows windows{capture,ignored_process,{}};
    // Snapshot stacking order without messaging other applications.
    const auto collect=[](HWND window,LPARAM parameter) noexcept -> BOOL {
        auto& result=*reinterpret_cast<Windows*>(parameter);
        if(!IsWindowVisible(window) || IsIconic(window) || window==GetShellWindow() || window==GetDesktopWindow()) return TRUE;
        DWORD process=0;GetWindowThreadProcessId(window,&process);
        if(process==result.ignored_process) return TRUE;
        const auto style=GetWindowLongPtrW(window,GWL_EXSTYLE);
        if(style&(WS_EX_NOACTIVATE|WS_EX_TRANSPARENT)) return TRUE;
        DWORD cloaked=0;
        if(SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) && cloaked) return TRUE;
        wchar_t name[128]{};GetClassNameW(window,name,128);
        for(const auto* excluded:{L"Progman",L"WorkerW",L"Shell_TrayWnd",L"Shell_SecondaryTrayWnd",L"CEF-OSC-WIDGET"})
            if(wcscmp(name,excluded)==0) return TRUE;
        RECT bounds{};
        if(FAILED(DwmGetWindowAttribute(window,DWMWA_EXTENDED_FRAME_BOUNDS,&bounds,sizeof(bounds)))
           && !GetWindowRect(window,&bounds)) return TRUE;
        const RECT clipped=window_snap_rect(bounds,result.capture);
        if(clipped.right<=clipped.left || clipped.bottom<=clipped.top) return TRUE;
        try {result.rectangles.push_back(clipped);} catch(...) {return FALSE;}
        return TRUE;
    };
    if(!EnumWindows(collect,reinterpret_cast<LPARAM>(&windows))) return {};
    return windows.rectangles;
}

} // namespace hdrshot
