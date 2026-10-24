#pragma once
#include "platform/platform.hpp"
#include "image/color.hpp"
// mingw-w64 14 headers define IReference<boolean> twice because boolean is BYTE; it is unused here.
#define ____FIReference_1_boolean_INTERFACE_DEFINED__
#include <windows.graphics.capture.interop.h>
#include <d3d11_4.h>
#include <array>
#include <chrono>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <asyncinfo.h>

extern "C" HRESULT WINAPI CreateDirect3D11DeviceFromDXGIDevice(IDXGIDevice*, IInspectable**);

namespace hdrshot {

namespace CaptureAbi = ABI::Windows::Graphics::Capture;
namespace Direct3DAbi = ABI::Windows::Graphics::DirectX::Direct3D11;
namespace DirectXAbi = ABI::Windows::Graphics::DirectX;

struct DxgiAccess : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID iid, void** object) = 0;
};
inline constexpr GUID dxgi_access_iid =
    {0xa9b3d012,0x3df2,0x4ee3,{0xb8,0xd1,0x86,0x95,0xf4,0x57,0xd3,0xc1}};

struct Monitor {
    HMONITOR handle = nullptr;
    std::wstring device_name, friendly_name;
    RECT bounds{};
    bool primary = false, hdr = false;
    double sdr_white = 80;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput> output;
};

inline double read_sdr_white(HMONITOR monitor) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return 203;
    UINT32 path_count = 0, mode_count = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS)
        return 203;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count,
                           modes.data(), nullptr) != ERROR_SUCCESS) return 203;
    for (UINT32 i = 0; i < path_count; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS
            || wcscmp(source.viewGdiDeviceName, info.szDevice) != 0) continue;
        DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
        white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        white.header.size = sizeof(white);
        white.header.adapterId = paths[i].targetInfo.adapterId;
        white.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS && white.SDRWhiteLevel)
            return double(white.SDRWhiteLevel) * 80 / 1000;
    }
    return 203;
}

inline std::wstring read_monitor_name(HMONITOR monitor) {
    MONITORINFOEXW info{};info.cbSize=sizeof(info);
    if (!GetMonitorInfoW(monitor,&info)) return L"Monitor";
    DISPLAY_DEVICEW device{};device.cb=sizeof(device);
    std::wstring fallback=L"Monitor";
    if (EnumDisplayDevicesW(info.szDevice,0,&device,0) && device.DeviceString[0]) fallback=device.DeviceString;
    UINT32 path_count=0,mode_count=0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&path_count,&mode_count)!=ERROR_SUCCESS) return fallback;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&path_count,paths.data(),&mode_count,modes.data(),nullptr)!=ERROR_SUCCESS)
        return fallback;
    for (UINT32 i=0;i<path_count;++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),
                       paths[i].sourceInfo.adapterId,paths[i].sourceInfo.id};
        if (DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS || wcscmp(source.viewGdiDeviceName,info.szDevice)!=0)
            continue;
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof(target),
                       paths[i].targetInfo.adapterId,paths[i].targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&target.header)==ERROR_SUCCESS && target.monitorFriendlyDeviceName[0])
            return target.monitorFriendlyDeviceName;
    }
    return fallback;
}

inline std::wstring monitor_label(const Monitor& monitor) {
    const auto number=monitor.device_name.find_first_of(L"0123456789");
    const std::wstring prefix=number==std::wstring::npos?L"":monitor.device_name.substr(number)+L": ";
    return prefix+monitor.friendly_name+(monitor.primary?L" (primary)":L"");
}

inline std::vector<Monitor> monitors() {
    ComPtr<IDXGIFactory1> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Cannot enumerate display adapters");
    std::vector<Monitor> result;
    for (UINT a = 0;; ++a) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        if (!adapter) continue;
        for (UINT o = 0;; ++o) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND) break;
            if (!output) continue;
            DXGI_OUTPUT_DESC description{};
            check(output->GetDesc(&description), "Cannot read monitor information");
            if (!description.AttachedToDesktop) continue;
            Monitor monitor;
            monitor.handle = description.Monitor;
            monitor.device_name = description.DeviceName;
            monitor.friendly_name = read_monitor_name(monitor.handle);
            monitor.bounds = description.DesktopCoordinates;
            monitor.adapter = adapter;
            monitor.output = output;
            MONITORINFO info{}; info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor.handle, &info)) monitor.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
            ComPtr<IDXGIOutput6> output6;
            DXGI_OUTPUT_DESC1 desc1{};
            if (SUCCEEDED(output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&desc1)))
                monitor.hdr = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
                           || desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
            monitor.sdr_white = monitor.hdr ? read_sdr_white(monitor.handle) : 80;
            result.push_back(monitor);
        }
    }
    if (result.empty()) throw std::runtime_error("No active monitor was found.");
    return result;
}

struct Device {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    explicit Device(IDXGIAdapter* adapter) {
        check(D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
              nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
              &device, nullptr, &context), "Cannot create capture graphics device");
        // Monitors capture in parallel on one device, so its context must serialize calls itself.
        ComPtr<ID3D11Multithread> multithread;
        check(context.As(&multithread), "Cannot share capture graphics device");
        multithread->SetMultithreadProtected(TRUE);
    }
};

// Creating a device takes about 100 ms, so captures reuse one per adapter.
inline std::shared_ptr<Device> shared_device(IDXGIAdapter1* adapter) {
    DXGI_ADAPTER_DESC1 desc{};
    check(adapter->GetDesc1(&desc), "Cannot read display adapter");
    static std::mutex lock;
    static std::map<std::pair<DWORD, LONG>, std::shared_ptr<Device>> devices;
    std::lock_guard<std::mutex> guard(lock);
    auto& device = devices[{desc.AdapterLuid.LowPart, desc.AdapterLuid.HighPart}];
    if (!device || device->device->GetDeviceRemovedReason() != S_OK) device = std::make_shared<Device>(adapter);
    return device;
}

inline const std::vector<float>& half_lookup() {
    static const std::vector<float> table = [] {
        std::vector<float> values(65536);
        for (unsigned i = 0; i < values.size(); ++i) values[i] = half_to_float(std::uint16_t(i));
        return values;
    }();
    return table;
}

// With sdr set, 8-bit SDR pixels go there unconverted and the frame keeps no float pixels.
inline Frame read_texture(Device& device, ID3D11Texture2D* texture, bool hdr,
                          double sdr_white, const std::wstring& backend,
                          unsigned crop_width = 0, unsigned crop_height = 0, Image* sdr = nullptr) {
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    const bool fp16 = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    const bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM
                   || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    if (!fp16 && !bgra) throw std::runtime_error("Capture returned an unsupported color representation.");
    if (hdr && !fp16) throw std::runtime_error("HDR capture returned 8-bit pixels. Select Windows Graphics Capture.");
    Frame frame;
    frame.width = crop_width ? std::min(crop_width, desc.Width) : desc.Width;
    frame.height = crop_height ? std::min(crop_height, desc.Height) : desc.Height;
    frame.hdr = hdr; frame.sdr_white = sdr_white; frame.backend = backend;
    if (sdr) *sdr = {};
    if (!bgra || hdr) sdr = nullptr;
    if (sdr) {
        sdr->width = frame.width; sdr->height = frame.height; sdr->description = L"SDR preserved";
        sdr->bgra.resize(checked_pixels(frame.width, frame.height) * 4);
    }
    else frame.pixels.resize(checked_pixels(frame.width, frame.height));
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    check(device.device->CreateTexture2D(&desc, nullptr, &staging), "Cannot allocate capture readback");
    device.context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Cannot read captured pixels");
    const auto& halves = half_lookup();
    const auto& bytes = srgb_lookup();
    for (unsigned y = 0; y < frame.height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(mapped.pData) + std::size_t(y)*mapped.RowPitch;
        if (sdr) {
            auto* out = sdr->bgra.data() + std::size_t(y)*frame.width*4;
            std::memcpy(out, row, std::size_t(frame.width)*4);
            for (unsigned x = 0; x < frame.width; ++x) out[x*4+3] = 255;
            continue;
        }
        Pixel* out = frame.pixels.data() + std::size_t(y)*frame.width;
        if (fp16) {
            for (unsigned x = 0; x < frame.width; ++x) {
                std::uint16_t channels[4];
                std::memcpy(channels, row + x*8, sizeof(channels));
                out[x] = {halves[channels[0]], halves[channels[1]], halves[channels[2]], 1};
            }
        } else {
            for (unsigned x = 0; x < frame.width; ++x) {
                const auto* pixel = row + x*4;
                out[x] = {bytes[pixel[2]], bytes[pixel[1]], bytes[pixel[0]], 1};
            }
        }
    }
    device.context->Unmap(staging.Get(), 0);
    return frame;
}

inline void close_runtime_object(IUnknown* object) noexcept {
    if (!object) return;
    ComPtr<ABI::Windows::Foundation::IClosable> closable;
    if (SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&closable)))) closable->Close();
}

struct CaptureLifetime {
    ComPtr<CaptureAbi::IDirect3D11CaptureFramePool> pool;
    ComPtr<CaptureAbi::IGraphicsCaptureSession> session;
    ~CaptureLifetime() {
        close_runtime_object(session.Get());
        close_runtime_object(pool.Get());
    }
};

// Older SDK headers lack the borderless-access ABI.
struct CaptureAccessOperation : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(INT32*) = 0;
};
struct CaptureAccessStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE RequestAccessAsync(INT32, CaptureAccessOperation**) = 0;
};
inline constexpr GUID capture_access_iid =
    {0x743ed370,0x06ec,0x5040,{0xa5,0x8a,0x90,0x1f,0x0f,0x75,0x70,0x95}};

inline bool request_borderless_capture() noexcept {
    static const bool allowed=[]() noexcept {
        try {
            HString name(L"Windows.Graphics.Capture.GraphicsCaptureAccess");
            ComPtr<CaptureAccessStatics> access;
            check(RoGetActivationFactory(name.get(),capture_access_iid,
                  reinterpret_cast<void**>(access.GetAddressOf())),"Cannot request capture border preference");
            ComPtr<CaptureAccessOperation> operation;
            check(access->RequestAccessAsync(0,&operation),"Cannot request borderless capture");
            ComPtr<IAsyncInfo> info;
            check(operation.As(&info),"Cannot read capture permission request");
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            AsyncStatus status=Started;
            while (std::chrono::steady_clock::now()<deadline) {
                check(info->get_Status(&status),"Cannot read capture permission status");
                if (status!=Started) break;
                Sleep(5);
            }
            if (status!=Completed) {info->Cancel();return false;}
            INT32 result=0;
            check(operation->GetResults(&result),"Cannot read capture permission result");
            return result==4;
        } catch (...) {return false;}
    }();
    return allowed;
}

inline ComPtr<IGraphicsCaptureItemInterop> capture_item_factory() {
    HString item_class(L"Windows.Graphics.Capture.GraphicsCaptureItem");
    ComPtr<IGraphicsCaptureItemInterop> interop;
    check(RoGetActivationFactory(item_class.get(), IID_PPV_ARGS(&interop)), "Windows capture is unavailable");
    return interop;
}

struct CaptureItems {
    std::mutex lock;
    std::map<HMONITOR, std::pair<RECT, ComPtr<CaptureAbi::IGraphicsCaptureItem>>> items;
};

// Creating an item costs about 30 ms, so items are kept until the display layout changes.
inline CaptureItems& capture_items() {
    // Never destroyed: releasing runtime objects during process exit is unsafe.
    static CaptureItems* items = [] {
        // Items outlive the worker that created them, so the apartment must too.
        CO_MTA_USAGE_COOKIE cookie{};
        CoIncrementMTAUsage(&cookie);
        return new CaptureItems;
    }();
    return *items;
}

inline void forget_capture_items() {
    auto& cache = capture_items();
    std::lock_guard<std::mutex> guard(cache.lock);
    cache.items.clear();
}

inline ComPtr<CaptureAbi::IGraphicsCaptureItem> capture_item(IGraphicsCaptureItemInterop* interop,
                                                            const Monitor& monitor, bool& cached) {
    auto& cache = capture_items();
    {
        std::lock_guard<std::mutex> guard(cache.lock);
        const auto found = cache.items.find(monitor.handle);
        cached = found != cache.items.end() && EqualRect(&found->second.first, &monitor.bounds);
        if (cached) return found->second.second;
    }
    ComPtr<CaptureAbi::IGraphicsCaptureItem> item;
    check(interop->CreateForMonitor(monitor.handle, IID_PPV_ARGS(&item)), "Cannot select capture monitor");
    std::lock_guard<std::mutex> guard(cache.lock);
    cache.items[monitor.handle] = {monitor.bounds, item};
    return item;
}

// Runs at startup so the first screenshot skips device, permission and item setup.
inline void warm_capture() {
    Runtime runtime;
    const auto displays = monitors();
    for (const auto& monitor : displays) shared_device(monitor.adapter.Get());
    half_lookup();
    srgb_lookup();
    request_borderless_capture();
    const auto interop = capture_item_factory();
    bool cached = false;
    for (const auto& monitor : displays) capture_item(interop.Get(), monitor, cached);
}

struct CaptureTimeout : std::runtime_error { using std::runtime_error::runtime_error; };

inline Frame capture_item_frame(Device& device, Direct3DAbi::IDirect3DDevice* runtime_device,
                                CaptureAbi::IGraphicsCaptureItem* item, const Monitor& monitor, Image* sdr) {
    ABI::Windows::Graphics::SizeInt32 size{};
    check(item->get_Size(&size), "Cannot read capture dimensions");
    if (size.Width <= 0 || size.Height <= 0) throw std::runtime_error("Capture target has no visible size.");
    HString pool_class(L"Windows.Graphics.Capture.Direct3D11CaptureFramePool");
    ComPtr<CaptureAbi::IDirect3D11CaptureFramePoolStatics2> pool_factory;
    check(RoGetActivationFactory(pool_class.get(), IID_PPV_ARGS(&pool_factory)), "Free-threaded capture is unavailable");
    CaptureLifetime lifetime;
    const auto format = static_cast<DirectXAbi::DirectXPixelFormat>(
        monitor.hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM);
    check(pool_factory->CreateFreeThreaded(runtime_device, format, 2, size, &lifetime.pool),
          "Cannot initialize capture frame pool");
    check(lifetime.pool->CreateCaptureSession(item, &lifetime.session), "Cannot initialize capture session");
    ComPtr<CaptureAbi::IGraphicsCaptureSession2> cursor_control;
    if (SUCCEEDED(lifetime.session.As(&cursor_control)))
        check(cursor_control->put_IsCursorCaptureEnabled(false), "Cannot disable cursor capture");
    ComPtr<CaptureAbi::IGraphicsCaptureSession3> border_control;
    if (SUCCEEDED(lifetime.session.As(&border_control)) && request_borderless_capture())
        border_control->put_IsBorderRequired(false);
    check(lifetime.session->StartCapture(), "Cannot begin Windows capture");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    ComPtr<CaptureAbi::IDirect3D11CaptureFrame> frame;
    while (std::chrono::steady_clock::now() < deadline) {
        check(lifetime.pool->TryGetNextFrame(&frame), "Cannot receive capture frame");
        if (frame) break;
        Sleep(10);
    }
    if (!frame) throw CaptureTimeout("Capture timed out. The game or Windows may exclude this content.");
    ABI::Windows::Graphics::SizeInt32 content{};
    check(frame->get_ContentSize(&content), "Cannot read frame dimensions");
    if (content.Width != size.Width || content.Height != size.Height)
        throw std::runtime_error("Capture target resized. Take the screenshot again.");
    ComPtr<Direct3DAbi::IDirect3DSurface> surface;
    check(frame->get_Surface(&surface), "Cannot access captured surface");
    ComPtr<DxgiAccess> access;
    check(surface->QueryInterface(dxgi_access_iid, reinterpret_cast<void**>(access.GetAddressOf())),
          "Cannot access captured DirectX surface");
    ComPtr<ID3D11Texture2D> texture;
    check(access->GetInterface(IID_PPV_ARGS(&texture)), "Cannot access captured texture");
    Frame result = read_texture(device, texture.Get(), monitor.hdr, monitor.sdr_white,
                                L"Windows Graphics Capture", unsigned(content.Width), unsigned(content.Height), sdr);
    close_runtime_object(frame.Get());
    return result;
}

inline Frame capture_wgc(const Monitor& monitor, HWND own_test_window = nullptr, Image* sdr = nullptr) {
    Runtime runtime;
    const auto shared = shared_device(monitor.adapter.Get());
    ComPtr<IDXGIDevice> dxgi;
    check(shared->device.As(&dxgi), "Cannot access capture device");
    ComPtr<IInspectable> inspectable;
    check(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), &inspectable), "Cannot wrap capture device");
    ComPtr<Direct3DAbi::IDirect3DDevice> runtime_device;
    check(inspectable.As(&runtime_device), "Cannot create runtime capture device");
    const auto interop = capture_item_factory();
    if (own_test_window) {
        ComPtr<CaptureAbi::IGraphicsCaptureItem> item;
        check(interop->CreateForWindow(own_test_window, IID_PPV_ARGS(&item)), "Cannot select test window");
        return capture_item_frame(*shared, runtime_device.Get(), item.Get(), monitor, sdr);
    }
    bool cached = false;
    const auto item = capture_item(interop.Get(), monitor, cached);
    try { return capture_item_frame(*shared, runtime_device.Get(), item.Get(), monitor, sdr); }
    catch (const CaptureTimeout&) { throw; }
    catch (const std::exception&) {
        // A cached item can outlive its monitor; retry once with a fresh one.
        if (!cached) throw;
        forget_capture_items();
        return capture_item_frame(*shared, runtime_device.Get(), capture_item(interop.Get(), monitor, cached).Get(), monitor, sdr);
    }
}

inline Frame capture_dxgi(const Monitor& monitor, Image* sdr = nullptr) {
    const auto shared = shared_device(monitor.adapter.Get());
    Device& device = *shared;
    ComPtr<IDXGIOutput5> output;
    check(monitor.output.As(&output), "HDR-capable Desktop Duplication is unavailable");
    constexpr DXGI_FORMAT formats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM};
    ComPtr<IDXGIOutputDuplication> duplication;
    check(output->DuplicateOutput1(device.device.Get(), 0, 2, formats, &duplication),
          "Cannot start Desktop Duplication");
    DXGI_OUTDUPL_DESC desc{};
    duplication->GetDesc(&desc);
    if (desc.Rotation != DXGI_MODE_ROTATION_IDENTITY && desc.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED)
        throw std::runtime_error("Use Windows Graphics Capture for a rotated monitor.");
    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> resource;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    bool acquired=false;
    while (std::chrono::steady_clock::now()<deadline) {
        const HRESULT hr=duplication->AcquireNextFrame(100,&info,&resource);
        if (hr==DXGI_ERROR_WAIT_TIMEOUT) continue;
        check(hr,"Desktop capture unavailable");
        if (info.LastPresentTime.QuadPart) {acquired=true;break;}
        // A pointer-only notification cannot initialize a screenshot.
        duplication->ReleaseFrame();resource.Reset();info={};
    }
    if (!acquired) throw PlatformError(DXGI_ERROR_WAIT_TIMEOUT,"Desktop capture unavailable or timed out");
    struct ReleaseFrame {
        IDXGIOutputDuplication* duplication;
        ~ReleaseFrame() { duplication->ReleaseFrame(); }
    } release{duplication.Get()};
    if (info.ProtectedContentMaskedOut)
        throw std::runtime_error("Windows excluded protected content from this screenshot.");
    ComPtr<ID3D11Texture2D> texture;
    check(resource.As(&texture), "Cannot access duplicated texture");
    return read_texture(device, texture.Get(), monitor.hdr, monitor.sdr_white, L"Desktop Duplication", 0, 0, sdr);
}

// When every monitor is SDR, sdr holds the exact preview and the frame carries no float pixels.
struct CapturedInput { Frame frame; RECT bounds{}; Image sdr; };

inline std::vector<Pixel> decode_sdr(const Image& image) {
    const auto& bytes = srgb_lookup();
    std::vector<Pixel> pixels(checked_pixels(image.width, image.height));
    for (std::size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = {bytes[image.bgra[i*4+2]], bytes[image.bgra[i*4+1]], bytes[image.bgra[i*4]], 1};
    return pixels;
}

inline RECT desktop_bounds(const std::vector<Monitor>& displays) {
    if (displays.empty()) throw std::runtime_error("No active monitor was found.");
    RECT bounds = displays.front().bounds;
    for (const auto& monitor : displays) {
        const auto& rect = monitor.bounds;
        if (rect.right <= rect.left || rect.bottom <= rect.top)
            throw std::runtime_error("Monitor dimensions are invalid.");
        bounds.left = std::min(bounds.left, rect.left); bounds.top = std::min(bounds.top, rect.top);
        bounds.right = std::max(bounds.right, rect.right); bounds.bottom = std::max(bounds.bottom, rect.bottom);
    }
    checked_pixels(unsigned(std::int64_t(bounds.right) - bounds.left),
                   unsigned(std::int64_t(bounds.bottom) - bounds.top));
    return bounds;
}

inline CapturedInput compose_desktop(const std::vector<Monitor>& displays, const std::vector<Frame>& frames) {
    const RECT bounds = desktop_bounds(displays);
    if (displays.size() != frames.size()) throw std::runtime_error("A monitor capture is missing.");
    Frame result;
    result.width = unsigned(bounds.right - bounds.left); result.height = unsigned(bounds.bottom - bounds.top);
    result.backend = L"All monitors";
    result.pixels.assign(checked_pixels(result.width, result.height), Pixel{0,0,0,1});
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto& frame = frames[i]; const auto& rect = displays[i].bounds;
        if (frame.width != unsigned(rect.right - rect.left) || frame.height != unsigned(rect.bottom - rect.top)
            || frame.pixels.size() != checked_pixels(frame.width, frame.height))
            throw std::runtime_error("Monitor layout changed during capture. Try again.");
        const unsigned x = unsigned(rect.left - bounds.left), y = unsigned(rect.top - bounds.top);
        for (unsigned row = 0; row < frame.height; ++row)
            std::copy_n(frame.pixels.begin() + std::size_t(row) * frame.width, frame.width,
                        result.pixels.begin() + std::size_t(y + row) * result.width + x);
        result.regions.push_back({x,y,frame.width,frame.height,frame.hdr,frame.sdr_white});
        result.hdr = result.hdr || frame.hdr;
    }
    return {std::move(result),bounds,{}};
}

enum class CaptureBackend { Auto, Windows, Desktop };

inline Frame capture(const Monitor& monitor, CaptureBackend backend, Image* sdr = nullptr) {
    if (backend == CaptureBackend::Windows) return capture_wgc(monitor, nullptr, sdr);
    if (backend == CaptureBackend::Desktop) return capture_dxgi(monitor, sdr);
    try { return capture_wgc(monitor, nullptr, sdr); }
    catch (const std::exception& wgc_error) {
        try { return capture_dxgi(monitor, sdr); }
        catch (const std::exception& dxgi_error) {
            throw std::runtime_error(std::string("Windows capture: ") + wgc_error.what()
                + "\nDesktop capture: " + dxgi_error.what());
        }
    }
}

inline CapturedInput capture_desktop(const std::vector<Monitor>& displays, CaptureBackend backend) {
    const RECT bounds = desktop_bounds(displays);
    std::vector<Frame> frames(displays.size());
    std::vector<Image> sdr(displays.size());
    std::vector<std::exception_ptr> errors(displays.size());
    const auto run = [&](std::size_t i) {
        try { frames[i] = capture(displays[i], backend, &sdr[i]); }
        catch (...) { errors[i] = std::current_exception(); }
    };
    std::vector<NativeWork> jobs(displays.size() - 1);
    for (std::size_t i = 1; i < displays.size(); ++i) jobs[i-1].start([&run, i] { run(i); });
    run(0);
    for (auto& job : jobs) job.join();
    for (std::size_t i = 0; i < displays.size(); ++i) {
        if (!errors[i]) continue;
        try { std::rethrow_exception(errors[i]); }
        catch (const std::exception& error) {
            throw std::runtime_error(narrow(monitor_label(displays[i])) + ": " + error.what());
        }
    }
    if (std::any_of(sdr.begin(), sdr.end(), [](const Image& image) { return image.bgra.empty(); })) {
        for (std::size_t i = 0; i < frames.size(); ++i)
            if (!sdr[i].bgra.empty()) frames[i].pixels = decode_sdr(sdr[i]);
        return compose_desktop(displays, frames);
    }
    CapturedInput result;
    result.bounds = bounds;
    Frame& frame = result.frame;
    frame.width = unsigned(bounds.right - bounds.left); frame.height = unsigned(bounds.bottom - bounds.top);
    frame.backend = L"All monitors";
    Image& image = result.sdr;
    image.width = frame.width; image.height = frame.height; image.description = L"Per-monitor HDR/SDR conversion";
    image.bgra.assign(checked_pixels(image.width, image.height) * 4, 0);
    for (std::size_t i = 3; i < image.bgra.size(); i += 4) image.bgra[i] = 255;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto& source = sdr[i]; const auto& rect = displays[i].bounds;
        if (source.width != unsigned(rect.right - rect.left) || source.height != unsigned(rect.bottom - rect.top))
            throw std::runtime_error("Monitor layout changed during capture. Try again.");
        const unsigned x = unsigned(rect.left - bounds.left), y = unsigned(rect.top - bounds.top);
        for (unsigned row = 0; row < source.height; ++row)
            std::memcpy(image.bgra.data() + (std::size_t(y + row) * image.width + x) * 4,
                        source.bgra.data() + std::size_t(row) * source.width * 4, std::size_t(source.width) * 4);
        frame.regions.push_back({x,y,source.width,source.height,false,frames[i].sdr_white});
    }
    return result;
}

} // namespace hdrshot
