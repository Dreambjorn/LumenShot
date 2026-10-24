#include "image/image_io.hpp"
#include "ui/selector.hpp"
#include <iostream>
#include <functional>

using namespace hdrshot;
static unsigned checks=0;
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}

struct OwnedWindow {
    HWND value=nullptr;
    ~OwnedWindow(){if(value) DestroyWindow(value);}
};
std::wstring event_name(const wchar_t* kind,DWORD process) {
    return std::wstring(L"Local\\LumenShotSnap")+kind+std::to_wstring(process);
}
RECT capture_bounds() {
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
    require(GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor)!=FALSE,"Cannot locate fixture monitor");
    require(monitor.rcWork.right-monitor.rcWork.left>=940 && monitor.rcWork.bottom-monitor.rcWork.top>=640,
            "Window snapping fixture needs a 940 x 640 work area");
    return {monitor.rcWork.left+20,monitor.rcWork.top+20,monitor.rcWork.left+920,monitor.rcWork.top+620};
}
RECT visible_bounds(HWND window) {
    RECT bounds{};
    if(FAILED(DwmGetWindowAttribute(window,DWMWA_EXTENDED_FRAME_BOUNDS,&bounds,sizeof(bounds))))
        require(GetWindowRect(window,&bounds)!=FALSE,"Cannot inspect owned fixture bounds");
    return bounds;
}
int fixture(DWORD parent) {
    HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,event_name(L"Ready",parent).c_str());
    HANDLE stop=OpenEventW(SYNCHRONIZE,FALSE,event_name(L"Stop",parent).c_str());
    if(!ready || !stop) return 2;
    struct Events{HANDLE ready,stop;~Events(){CloseHandle(ready);CloseHandle(stop);}} events{ready,stop};
    const RECT area=capture_bounds();
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);
    wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH));wc.lpszClassName=L"LumenShotSnapFixture";
    require(RegisterClassW(&wc)!=0,"Cannot register owned snap fixture");
    const auto create=[&](const wchar_t* title,int x,int y,int width,int height,DWORD style=WS_POPUP,DWORD extra=WS_EX_TOOLWINDOW) {
        HWND window=CreateWindowExW(extra|WS_EX_TOPMOST,wc.lpszClassName,title,style,
            area.left+x,area.top+y,width,height,nullptr,nullptr,wc.hInstance,nullptr);
        require(window!=nullptr,"Cannot create owned snap fixture");return window;
    };
    OwnedWindow back{create(L"Back",-20,-20,940,640)};ShowWindow(back.value,SW_SHOWNOACTIVATE);
    OwnedWindow front{create(L"Front",180,120,460,300,WS_OVERLAPPEDWINDOW)};ShowWindow(front.value,SW_SHOWNOACTIVATE);
    OwnedWindow hidden{create(L"Hidden",650,50,80,60)};
    OwnedWindow minimized{create(L"Minimized",650,120,80,60)};ShowWindow(minimized.value,SW_SHOWMINNOACTIVE);
    OwnedWindow overlay{create(L"Overlay",300,180,160,120,WS_POPUP,WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE)};
    ShowWindow(overlay.value,SW_SHOWNOACTIVATE);
    OwnedWindow cloaked{create(L"Cloaked",650,200,80,60)};ShowWindow(cloaked.value,SW_SHOWNOACTIVATE);
    const BOOL cloak=TRUE;
    if(FAILED(DwmSetWindowAttribute(cloaked.value,DWMWA_CLOAK,&cloak,sizeof(cloak)))) ShowWindow(cloaked.value,SW_HIDE);
    SetEvent(ready);
    const auto deadline=GetTickCount64()+30000;
    while(GetTickCount64()<deadline && WaitForSingleObject(stop,0)==WAIT_TIMEOUT) {
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
        MsgWaitForMultipleObjects(1,&stop,FALSE,50,QS_ALLINPUT);
    }
    return 0;
}
class FixtureProcess {
    HANDLE ready_=nullptr,stop_=nullptr;
    PROCESS_INFORMATION process_{};
public:
    FixtureProcess() {
        const DWORD parent=GetCurrentProcessId();
        ready_=CreateEventW(nullptr,TRUE,FALSE,event_name(L"Ready",parent).c_str());
        stop_=CreateEventW(nullptr,TRUE,FALSE,event_name(L"Stop",parent).c_str());
        require(ready_ && stop_,"Cannot allocate fixture events");
        wchar_t executable[32768]{};
        require(GetModuleFileNameW(nullptr,executable,32768)!=0,"Cannot locate fixture executable");
        std::wstring command=L"\""+std::wstring(executable)+L"\" --fixture "+std::to_wstring(parent);
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process_)!=FALSE,
                "Cannot launch owned fixture process");
    }
    ~FixtureProcess() {
        if(stop_) SetEvent(stop_);
        if(process_.hProcess) {WaitForSingleObject(process_.hProcess,5000);CloseHandle(process_.hThread);CloseHandle(process_.hProcess);}
        if(ready_) CloseHandle(ready_);
        if(stop_) CloseHandle(stop_);
    }
    void wait() {require(WaitForSingleObject(ready_,5000)==WAIT_OBJECT_0,"Owned windows did not become ready");}
    DWORD id() const {return process_.dwProcessId;}
    HWND window(const wchar_t* title) const {
        HWND window=FindWindowW(L"LumenShotSnapFixture",title);DWORD process=0;
        if(window) GetWindowThreadProcessId(window,&process);
        require(window && process==id(),"Owned fixture window missing");return window;
    }
};
Image render_window(HWND window) {
    RECT bounds{};GetClientRect(window,&bounds);
    Image image;image.width=bounds.right;image.height=bounds.bottom;
    image.bgra.resize(checked_pixels(image.width,image.height)*4);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=bounds.right;
    info.bmiHeader.biHeight=-bounds.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(nullptr);void* pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!dc || !bitmap) throw std::runtime_error("Cannot allocate window snapping preview");
    HGDIOBJ old=SelectObject(dc,bitmap);
    SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);GdiFlush();
    std::memcpy(image.bgra.data(),pixels,image.bgra.size());ValidateRect(window,nullptr);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    for(std::size_t i=3;i<image.bgra.size();i+=4) image.bgra[i]=255;
    return image;
}
constexpr UINT probe_message=WM_APP+18;
struct WindowProbe {std::function<void(HWND)> body;std::exception_ptr failure;};
LRESULT CALLBACK probe_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==probe_message) {
        auto& probe=*reinterpret_cast<WindowProbe*>(lp);
        try {probe.body(window);} catch(...) {probe.failure=std::current_exception();}
        return 0;
    }
    return CallWindowProcW(reinterpret_cast<WNDPROC>(GetClassLongPtrW(window,GCLP_WNDPROC)),window,message,wp,lp);
}
void probe_selector(HWND window,const std::function<void(HWND)>& body) {
    WindowProbe probe{body,{}};
    const auto previous=SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(probe_proc));
    require(previous!=0,"Cannot attach owned window probe");
    SendMessageW(window,probe_message,0,reinterpret_cast<LPARAM>(&probe));
    SetWindowLongPtrW(window,GWLP_WNDPROC,previous);
    if(probe.failure) std::rethrow_exception(probe.failure);
}
HWND wait_selector() {
    for(unsigned i=0;i<500;++i) {
        HWND window=FindWindowW(L"LumenShotRegionSelector",nullptr);DWORD process=0;
        if(window) GetWindowThreadProcessId(window,&process);
        if(window && process==GetCurrentProcessId() && IsWindowVisible(window)) return window;
        Sleep(5);
    }
    throw std::runtime_error("Owned selector did not open");
}
int wmain(int argc,wchar_t** argv) try {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(argc==3 && wcscmp(argv[1],L"--fixture")==0) return fixture(DWORD(std::stoul(argv[2])));
    if(argc!=2) throw std::runtime_error("Specify a window snapping evidence directory.");
    Runtime runtime;
    const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
    for(LONG scale:{100l,125l,150l}) {
        const RECT capture{-1920*scale/100,-120*scale/100,640*scale/100,1320*scale/100};
        const RECT window{capture.left-80,capture.top+30,capture.left+500*scale/100,capture.bottom+70};
        const RECT actual=window_snap_rect(window,capture),expected{0,30,500*scale/100,capture.bottom-capture.top};
        require(EqualRect(&actual,&expected),"Negative-origin physical-pixel bounds were scaled or clipped incorrectly");
        const std::vector<RECT> windows={{40,50,220,190},{0,0,800,600}};
        const RECT front=window_snap_at(windows,{100,100}),back=window_snap_at(windows,{300,100});
        require(EqualRect(&front,&windows[0]) && EqualRect(&back,&windows[1]),"Stacked window hit testing ignored the frontmost match");
    }
    require(!valid_rect(window_snap_rect({0,0,10,10},{10,10,100,100})),"Off-capture window became selectable");
    require(!valid_rect(window_snap_at({{0,0,10,10}},{10,5})),"Exclusive window edge became selectable");
    FixtureProcess fixture;fixture.wait();const RECT bounds=capture_bounds();
    const RECT front=window_snap_rect(visible_bounds(fixture.window(L"Front")),bounds);
    const RECT back=window_snap_rect(visible_bounds(fixture.window(L"Back")),bounds);
    DWORD cloaked=0;
    const bool cloak_supported=SUCCEEDED(DwmGetWindowAttribute(fixture.window(L"Cloaked"),DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) && cloaked;
    if(!cloak_supported) std::cout<<"CAPABILITY SKIP: OS did not cloak the owned window; hidden fallback was checked.\n";
    std::cout<<"Native fixture DPI: "<<GetDpiForWindow(fixture.window(L"Front"))<<"; cloaked filter exercised: "<<cloak_supported<<"\n";
    const auto windows=window_snap_rectangles(bounds,GetCurrentProcessId());
    const auto contains=[&](RECT rect) {return std::any_of(windows.begin(),windows.end(),[&](RECT candidate){return EqualRect(&rect,&candidate);});};
    require(contains(front) && contains(back),"Native enumeration missed visible window bounds");
    for(const auto* title:{L"Hidden",L"Minimized",L"Overlay",L"Cloaked"})
        require(!contains(window_snap_rect(visible_bounds(fixture.window(title)),bounds)),"Native enumeration included a hidden or auxiliary window");
    const POINT click{(front.left+front.right)/2,(front.top+front.bottom)/2};
    const RECT picked=window_snap_at(windows,click);
    require(EqualRect(&picked,&front),"Native stacking order selected the covered window");
    const auto ignored=window_snap_rectangles(bounds,fixture.id());
    require(std::none_of(ignored.begin(),ignored.end(),[&](RECT rect){return EqualRect(&rect,&front);}),"Application process exclusion failed");
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LumenShotSnapOwnWindow";
    require(RegisterClassW(&wc)!=0,"Cannot register own exclusion window");
    OwnedWindow own{CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,wc.lpszClassName,L"Owned exclusion",WS_POPUP|WS_VISIBLE,
        bounds.left+250,bounds.top+150,260,220,nullptr,nullptr,wc.hInstance,nullptr)};
    require(own.value!=nullptr,"Cannot create own exclusion window");
    Image image;image.width=900;image.height=600;image.bgra.resize(900*600*4);
    for(unsigned y=0;y<600;++y) for(unsigned x=0;x<900;++x) {
        const auto index=(std::size_t(y)*900+x)*4;const bool inside=PtInRect(&front,{LONG(x),LONG(y)})!=FALSE;
        image.bgra[index]=inside?200:110;image.bgra[index+1]=inside?180:90;image.bgra[index+2]=inside?160:70;image.bgra[index+3]=255;
    }
    BYTE saved[256]{};GetKeyboardState(saved);
    struct Keys{BYTE* saved;~Keys(){SetKeyboardState(saved);}} keys{saved};
    for(unsigned mode=0;mode<4;++mode) {
        BYTE neutral[256]{};if(mode==3) neutral[VK_CONTROL]=0x80;SetKeyboardState(neutral);
        RegionSelector selector(image);std::exception_ptr failure;
        const POINT end=mode==1?POINT{click.x+90,click.y+60}:mode==2?POINT{click.x+1,click.y+1}:click;
        RECT expected=mode==1?normalize_rect(click,end):front;
        NativeWork driver;driver.start([&] {
            HWND target=nullptr;
            try {
                target=wait_selector();
                probe_selector(target,[&](HWND target) {
                    const auto send=[&](UINT message,POINT point,WPARAM keys=0) {SendMessageW(target,message,keys,MAKELPARAM(point.x,point.y));};
                    if(mode==0) {
                        SendMessageW(target,WM_MOUSELEAVE,0,0);const auto baseline=render_window(target);
                        send(WM_MOUSEMOVE,click);const auto hovered=render_window(target);
                        require(hovered.bgra!=baseline.bgra,"Hover produced no window highlight");
                        const auto center=(std::size_t(click.y)*900+click.x)*4;
                        require(std::memcmp(hovered.bgra.data()+center,image.bgra.data()+center,4)==0,"Hovered window did not reveal frozen image pixels");
                        save_image(directory/L"window-hover.png",hovered);
                        send(WM_MOUSEMOVE,{20,570});render_window(target);
                        SendMessageW(target,WM_MOUSELEAVE,0,0);
                        require(render_window(target).bgra==baseline.bgra,"Hover transitions left stale window pixels");
                    }
                    send(WM_MOUSEMOVE,click);send(WM_LBUTTONDOWN,click,MK_LBUTTON);
                    if(mode==1 || mode==2) send(WM_MOUSEMOVE,end,MK_LBUTTON);
                    send(WM_LBUTTONUP,end);
                    if(mode!=3) {
                        if(mode==0) save_image(directory/L"window-selected.png",render_window(target));
                        const POINT save{std::clamp(expected.right-134,0l,766l)+67,
                            (expected.bottom+45<=600?expected.bottom+7:std::max(0l,expected.top-45))+19};
                        send(WM_LBUTTONDOWN,save);
                    }
                });
            } catch(...) {failure=std::current_exception();if(target) PostMessageW(target,WM_CLOSE,0,0);}
        });
        const auto result=selector.run(nullptr,bounds);driver.join();if(failure) std::rethrow_exception(failure);
        require(result.action==(mode==3?SelectionAction::Copy:SelectionAction::Save),"Snapped selection did not reach the requested output action");
        require(EqualRect(&result.rect,&expected),"Click, click jitter, or free drag selected incorrect pixels");
        require(result.marks.empty() && selector.image().bgra==image.bgra,"Window selection modified source pixels or annotations");
        const auto cropped=crop_image(selector.image(),result.rect);
        require(cropped.width==unsigned(expected.right-expected.left) && cropped.height==unsigned(expected.bottom-expected.top),
                "Snapped export dimensions disagree with visible bounds");
    }
    std::cout<<checks<<" window snapping checks passed: native stacking/filtering, clipped bounds, hover cleanup, click, jitter, free drag, Ctrl-copy.\n";
    std::cout<<"100/125/150 percent coordinate cases are synthetic; physical display scaling was not changed.\n";
    std::cout<<"Only owned windows were driven; system clipboard and display settings were not changed.\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"FAILED after "<<checks<<" window snapping checks: "<<error.what()<<"\n";return 1;}
