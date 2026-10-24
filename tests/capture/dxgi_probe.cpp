#define UNICODE
#define _UNICODE
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using Microsoft::WRL::ComPtr;

LRESULT CALLBACK paint_fixture(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_PAINT) {
        PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT rect{};GetClientRect(window,&rect);
        HBRUSH brush=CreateSolidBrush(COLORREF(GetWindowLongPtrW(window,GWLP_USERDATA)));
        FillRect(dc,&rect,brush);DeleteObject(brush);EndPaint(window,&paint);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
void report_desktop() {
    const auto report=[](HANDLE object,const char* label) {
        wchar_t name[256]{};DWORD needed=0;
        if(object && GetUserObjectInformationW(object,UOI_NAME,name,sizeof(name),&needed))
            std::wcout<<label<<L'='<<name<<L'\n';
        else std::cout<<label<<" error="<<GetLastError()<<'\n';
    };
    report(GetProcessWindowStation(),"window_station");report(GetThreadDesktop(GetCurrentThreadId()),"thread_desktop");
    HDESK input=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);report(input,"input_desktop");if(input) CloseDesktop(input);
}

// Raw Windows APIs keep this probe independent of the application's capture wrapper.
int main(int argc,char**) {
    if(argc!=1) {
        std::cout<<"Usage: dxgi_probe.exe\n"
                   "Animates owned color windows on each connected monitor.\n"
                   "Compares DuplicateOutput, DuplicateOutput1, input-desktop attachment and longer waits.\n"
                   "Reports frame metadata only; no screenshot pixels are mapped or saved.\n"
                   "Run from an ordinary interactive terminal to compare with the test host.\n";
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    DWORD session=0;ProcessIdToSessionId(GetCurrentProcessId(),&session);
    std::cout<<"session="<<session<<" remote_session="<<GetSystemMetrics(SM_REMOTESESSION)
             <<" dpi_v2="<<AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)<<'\n';
    HANDLE token=nullptr;
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) {
        DWORD needed=0,container=0;TOKEN_ELEVATION elevation{};
        const BOOL queried=GetTokenInformation(token,TokenIsAppContainer,&container,sizeof(container),&needed);
        GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&needed);
        std::cout<<"app_container="<<(queried?int(container):-1)<<" elevated="<<elevation.TokenIsElevated
                 <<" restricted_token="<<IsTokenRestricted(token)<<'\n';CloseHandle(token);
    }
    report_desktop();
    WNDCLASSW wc{};wc.lpfnWndProc=paint_fixture;wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=L"LumenShotIndependentDxgiProbe";if(!RegisterClassW(&wc)) return 1;
    ComPtr<IDXGIFactory1> factory;HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));if(FAILED(hr)) return 1;
    unsigned tested=0,successful=0;
    for(UINT a=0;;++a) {
        ComPtr<IDXGIAdapter1> adapter;hr=factory->EnumAdapters1(a,&adapter);
        if(hr==DXGI_ERROR_NOT_FOUND) break;
        if(FAILED(hr)) return 1;
        DXGI_ADAPTER_DESC1 adapter_desc{};adapter->GetDesc1(&adapter_desc);
        for(UINT o=0;;++o) {
            ComPtr<IDXGIOutput> output;hr=adapter->EnumOutputs(o,&output);
            if(hr==DXGI_ERROR_NOT_FOUND) break;
            if(FAILED(hr)) return 1;
            DXGI_OUTPUT_DESC desc{};hr=output->GetDesc(&desc);if(FAILED(hr)) return 1;
            if(!desc.AttachedToDesktop) continue;
            ComPtr<IDXGIOutput6> output6;DXGI_OUTPUT_DESC1 color{};
            if(SUCCEEDED(output.As(&output6))) output6->GetDesc1(&color);
            std::wcout<<L"adapter="<<adapter_desc.Description<<L" output="<<o<<L" colorspace="<<color.ColorSpace<<L'\n';
            HWND window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,
                L"Owned DXGI test colors",WS_POPUP,desc.DesktopCoordinates.left+80,desc.DesktopCoordinates.top+80,
                500,200,nullptr,nullptr,wc.hInstance,nullptr);
            if(!window) return 1;
            struct CloseWindow {HWND window;~CloseWindow(){DestroyWindow(window);}} close_window{window};
            ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);DwmFlush();
            for(unsigned mode=0;mode<4;++mode) {
                std::atomic<bool> done=false;unsigned waits=0,pointers=0,frames=0;
                HRESULT error=S_OK;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
                std::thread capture([&] {
                    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
                    HDESK original=GetThreadDesktop(GetCurrentThreadId()),input=nullptr;
                    if(mode==2) {
                        input=OpenInputDesktop(0,FALSE,GENERIC_ALL);
                        if(!input || !SetThreadDesktop(input)) error=HRESULT_FROM_WIN32(GetLastError());
                    }
                    struct RestoreDesktop {HDESK original,input;~RestoreDesktop(){if(input){SetThreadDesktop(original);CloseDesktop(input);}}}
                        restore{original,input};
                    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
                    if(SUCCEEDED(error)) error=D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,
                        D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
                    ComPtr<IDXGIOutputDuplication> duplication;
                    if(SUCCEEDED(error)) {
                        if(mode==1) {
                            ComPtr<IDXGIOutput1> out;error=output.As(&out);
                            if(SUCCEEDED(error)) error=out->DuplicateOutput(device.Get(),&duplication);
                        } else {
                            ComPtr<IDXGIOutput5> out;error=output.As(&out);
                            constexpr DXGI_FORMAT formats[]={DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_B8G8R8A8_UNORM};
                            if(SUCCEEDED(error)) error=out->DuplicateOutput1(device.Get(),0,2,formats,&duplication);
                        }
                    }
                    if(SUCCEEDED(error)) {
                        DXGI_OUTDUPL_DESC description{};duplication->GetDesc(&description);format=description.ModeDesc.Format;
                        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(mode==3?5:3);
                        while(std::chrono::steady_clock::now()<deadline) {
                            DXGI_OUTDUPL_FRAME_INFO info{};ComPtr<IDXGIResource> resource;
                            error=duplication->AcquireNextFrame(mode==3?1000:100,&info,&resource);
                            if(error==DXGI_ERROR_WAIT_TIMEOUT) {++waits;continue;}
                            if(FAILED(error)) break;
                            if(info.LastPresentTime.QuadPart) ++frames;else ++pointers;
                            error=duplication->ReleaseFrame();if(FAILED(error) || frames>=3) break;
                        }
                    }
                    done=true;
                });
                unsigned redraws=0;HRESULT composition=S_OK;
                while(!done.load()) {
                    MSG message{};
                    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
                    SetWindowLongPtrW(window,GWLP_USERDATA,RGB(redraws%256,(redraws*7)%256,128));
                    InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);
                    const HRESULT flushed=DwmFlush();if(FAILED(flushed)) composition=flushed;
                    ++redraws;Sleep(16);
                }
                capture.join();++tested;successful+=frames>0 && SUCCEEDED(composition);
                static constexpr const char* names[]={"DuplicateOutput1","DuplicateOutput","DuplicateOutput1_attached","DuplicateOutput1_long_wait"};
                std::cout<<names[mode]<<" format="<<format<<" frames="<<frames<<" pointer_only="<<pointers
                         <<" waits="<<waits<<" redraws="<<redraws<<" last_hresult=0x"<<std::hex<<unsigned(error)
                         <<" composition_hresult=0x"<<unsigned(composition)<<std::dec<<std::endl;
            }
        }
    }
    std::cout<<successful<<'/'<<tested<<" cases delivered desktop frames. No driver or display settings changed.\n";
    return tested && successful==tested?0:2;
}
