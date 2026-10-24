#include "app/bootstrap.hpp"
#include "app/app.hpp"
#include "app/command_line.hpp"
#include <shellapi.h>
#include <iostream>

namespace hdrshot {

int run_application(HINSTANCE instance) {
    LaunchOptions options;
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
        struct FreeArgs{LPWSTR* value;~FreeArgs(){LocalFree(value);}} free_args{argv};
        if(handle_command_line(argc,argv,options)) return 0;
        HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\LumenShotApp.0.1");
        if(!mutex) throw std::runtime_error("Cannot initialize application instance.");
        struct CloseMutex{HANDLE value;~CloseMutex(){CloseHandle(value);}} close_mutex{mutex};
        if(GetLastError()==ERROR_ALREADY_EXISTS) {
            HWND existing=FindWindowW(window_class,nullptr);
            if(existing && options.open_settings){ShowWindow(existing,SW_SHOW);SetForegroundWindow(existing);}return 0;
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
        ShowWindow(window,options.open_settings?SW_SHOW:SW_HIDE);UpdateWindow(window);
        MSG message{};
        while(GetMessageW(&message,nullptr,0,0)>0) {
            if(!app.licenses.translate(message) && !IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<"\n";
        if(!options.command_line_mode) MessageBoxW(nullptr,widen(error.what()).c_str(),L"LumenShot",MB_OK|MB_ICONERROR);
        return 1;
    }
}

} // namespace hdrshot
