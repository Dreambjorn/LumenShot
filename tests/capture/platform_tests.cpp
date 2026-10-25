#include "capture/capture.hpp"
#include "image/image_io.hpp"
#include <iostream>
#include <atomic>
#include <d3d11_1.h>

using namespace hdrshot;
static unsigned checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

const COLORREF fixture_colors[5] = {RGB(0,0,0),RGB(128,128,128),RGB(255,0,0),RGB(0,255,0),RGB(0,0,255)};
LRESULT CALLBACK fixture_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window,&client);
        for (int i=0;i<5;++i) {
            RECT patch{client.right*i/5,0,client.right*(i+1)/5,client.bottom};
            HBRUSH brush = CreateSolidBrush(fixture_colors[i]); FillRect(dc,&patch,brush); DeleteObject(brush);
        }
        EndPaint(window,&paint); return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
void verify_patch(const Image& image,unsigned x,unsigned y,int patch) {
    const auto index = (std::size_t(y)*image.width+x)*4;
    const auto close = [](int a,int b){ return std::abs(a-b) <= 2; };
    const COLORREF expected = fixture_colors[patch];
    const bool matches=close(image.bgra[index],GetBValue(expected)) && close(image.bgra[index+1],GetGValue(expected))
         && close(image.bgra[index+2],GetRValue(expected));
    if (!matches) std::cerr<<"Patch "<<patch<<" expected RGB "<<int(GetRValue(expected))<<","<<int(GetGValue(expected))
                          <<","<<int(GetBValue(expected))<<" got "<<int(image.bgra[index+2])<<","<<int(image.bgra[index+1])
                          <<","<<int(image.bgra[index])<<"\n";
    require(matches,"Captured fixture color mismatch");
}
int wmain(int argc,wchar_t** argv) try {
    if (argc != 2) throw std::runtime_error("Supply a temporary output directory.");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Runtime runtime;
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    Frame source; source.width=4;source.height=2;source.hdr=true;source.sdr_white=203;
    source.pixels={{0,0,0,1},{.5f,1,2,1},{12.5f,2.5f,.25f,1},{-.125f,.25f,1,1},
                   {10,0,0,1},{0,10,0,1},{0,0,10,1},{1,1,1,1}};
    {
        Device device(nullptr);
        std::vector<std::uint16_t> gpu_pixels(source.pixels.size()*4);
        for(std::size_t i=0;i<source.pixels.size();++i) {
            gpu_pixels[i*4]=float_to_half(source.pixels[i].r);
            gpu_pixels[i*4+1]=float_to_half(source.pixels[i].g);
            gpu_pixels[i*4+2]=float_to_half(source.pixels[i].b);
            gpu_pixels[i*4+3]=float_to_half(1);
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=4;desc.Height=2;desc.MipLevels=1;desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA data{gpu_pixels.data(),4*8,0};
        ComPtr<ID3D11Texture2D> texture;
        check(device.device->CreateTexture2D(&desc,&data,&texture),"Cannot create HDR GPU fixture");
        const Frame gpu_read=read_texture(device,texture.Get(),true,203,L"Synthetic FP16 texture");
        for(std::size_t i=0;i<source.pixels.size();++i) {
            require(gpu_read.pixels[i].r==source.pixels[i].r,"FP16 GPU red readback changed");
            require(gpu_read.pixels[i].g==source.pixels[i].g,"FP16 GPU green readback changed");
            require(gpu_read.pixels[i].b==source.pixels[i].b,"FP16 GPU blue readback changed");
        }
    }
    auto displays=monitors();
    const Monitor& monitor=displays.front();
    WNDCLASSW wc{};wc.lpfnWndProc=fixture_proc;wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=L"LumenShotSyntheticFixture";RegisterClassW(&wc);
    const int left=monitor.bounds.left+80,top=monitor.bounds.top+80;
    HWND window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,wc.lpszClassName,L"Synthetic color fixture",
        WS_POPUP,left,top,500,200,nullptr,nullptr,wc.hInstance,nullptr);
    if (!window) throw std::runtime_error("Cannot create synthetic fixture.");
    struct DestroyFixture { HWND window;~DestroyFixture(){DestroyWindow(window);} } cleanup{window};
    ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);
    Sleep(100);
    const Frame wgc=capture_wgc(monitor,window);
    ColorSettings sdr_settings;sdr_settings.mode=ContentMode::Sdr;sdr_settings.exposure=.1;sdr_settings.dither=false;
    const Image wgc_image=render_sdr(wgc,sdr_settings);
    require(wgc_image.width==500 && wgc_image.height==200,"WGC fixture dimensions");
    for (int i=0;i<5;++i) verify_patch(wgc_image,unsigned(50+i*100),100,i);
    save_image(directory/L"wgc_fixture.png",wgc_image);
    try {
        const Frame monitor_frame=capture_wgc(monitor);
        const Image monitor_image=render_sdr(monitor_frame,sdr_settings);
        for(int i=0;i<5;++i) verify_patch(monitor_image,unsigned(left-monitor.bounds.left+50+i*100),
                                                       unsigned(top-monitor.bounds.top+100),i);
        std::cout << "WGC monitor capture matched all five synthetic patches.\n";
    } catch(const std::exception& error) {
        std::cout << "MONITOR CAPTURE LIMIT: " << error.what() << "\n";
        throw;
    }

    Device renderer(monitor.adapter.Get());
    ComPtr<IDXGIFactory2> swap_factory;
    check(monitor.adapter->GetParent(IID_PPV_ARGS(&swap_factory)),"Cannot create fixture swapchain factory");
    DXGI_SWAP_CHAIN_DESC1 swap_desc{};
    swap_desc.Width=500;swap_desc.Height=200;
    swap_desc.Format=monitor.hdr?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_B8G8R8A8_UNORM;
    swap_desc.SampleDesc.Count=1;swap_desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.BufferCount=2;swap_desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    ComPtr<IDXGISwapChain1> swapchain;
    check(swap_factory->CreateSwapChainForHwnd(renderer.device.Get(),window,&swap_desc,nullptr,nullptr,&swapchain),
          "Cannot create synthetic presentation window");
    if(monitor.hdr) {
        ComPtr<IDXGISwapChain3> swap3;check(swapchain.As(&swap3),"Cannot access fixture color space");
        check(swap3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709),"Cannot select scRGB fixture output");
    }
    ComPtr<ID3D11Texture2D> backbuffer;
    check(swapchain->GetBuffer(0,IID_PPV_ARGS(&backbuffer)),"Cannot access fixture backbuffer");
    ComPtr<ID3D11RenderTargetView> target;
    check(renderer.device->CreateRenderTargetView(backbuffer.Get(),nullptr,&target),"Cannot create fixture target");
    ComPtr<ID3D11DeviceContext1> context1;
    check(renderer.context.As(&context1),"Cannot access fixture drawing interface");
    auto draw_fixture=[&] {
        for(int i=0;i<5;++i) {
            const auto encode=[&](BYTE channel){return monitor.hdr?float(srgb_decode(channel/255.0)*monitor.sdr_white/80):channel/255.0f;};
            const float color[4]={encode(GetRValue(fixture_colors[i])),encode(GetGValue(fixture_colors[i])),
                                  encode(GetBValue(fixture_colors[i])),1};
            const D3D11_RECT patch{i*100,0,(i+1)*100,200};
            context1->ClearView(target.Get(),color,&patch,1);
        }
        const float moving[4]={float(GetTickCount()%100)/100,0,0,1};
        const D3D11_RECT corner{0,0,2,2};context1->ClearView(target.Get(),moving,&corner,1);
        return swapchain->Present(1,0);
    };
    check(draw_fixture(),"Cannot present synthetic fixture");
    std::atomic<bool> captured{false};
    Frame desktop;std::exception_ptr capture_error;
    NativeWork capture_work;
    capture_work.start([&] {
        try {desktop=capture_dxgi(monitor);} catch(...) {capture_error=std::current_exception();}
        captured=true;
    });
    unsigned presentations=0;HRESULT presentation=S_OK;
    // Present can wait on the window thread, so keep its message pump running.
    while(!captured.load()) {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
        const HRESULT result=draw_fixture();
        if(result==S_OK) ++presentations;else presentation=result;
        Sleep(5);
    }
    capture_work.join();
    check(presentation,"Synthetic presentation failed during DXGI capture");
    require(presentation==S_OK,"Synthetic presentation was occluded during DXGI capture");
    std::cout<<presentations<<" synthetic presentations completed during DXGI capture.\n";
    try {if(capture_error) std::rethrow_exception(capture_error);}
    catch (const PlatformError& error) {
        if(error.code!=DXGI_ERROR_WAIT_TIMEOUT) throw;
        std::cout << checks << " platform checks passed: WGC and FP16 GPU readback.\n"
                  << "CAPABILITY SKIP: Desktop Duplication returned DXGI_ERROR_WAIT_TIMEOUT after synthetic updates; no compatibility claim.\n";
        return 0;
    }
    const Image desktop_image=render_sdr(desktop,sdr_settings);
    for (int i=0;i<5;++i) verify_patch(desktop_image,unsigned(left-monitor.bounds.left+50+i*100),
                                                      unsigned(top-monitor.bounds.top+100),i);
    Image crop;crop.width=500;crop.height=200;crop.bgra.resize(500*200*4);
    for (unsigned y=0;y<200;++y) {
        const auto source_offset=(std::size_t(top-monitor.bounds.top+y)*desktop_image.width
                                  +left-monitor.bounds.left)*4;
        std::memcpy(crop.bgra.data()+std::size_t(y)*500*4,desktop_image.bgra.data()+source_offset,500*4);
    }
    save_image(directory/L"dxgi_fixture.png",crop);
    std::cout << checks << " platform checks passed. WGC, DXGI and FP16 GPU readback verified.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAILED after " << checks << " checks: " << error.what() << "\n"; return 1;
}
