#pragma once
#include "platform/platform.hpp"
#include "image/color.hpp"
#include <wincodec.h>
#include <fstream>

namespace hdrshot {

inline ComPtr<IWICImagingFactory> imaging_factory() {
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
          IID_PPV_ARGS(&factory)), "Windows image codecs are unavailable");
    return factory;
}

class OutputTransaction {
    std::filesystem::path destination_, temporary_;
    bool committed_ = false;
public:
    explicit OutputTransaction(const std::filesystem::path& destination) : destination_(destination) {
        if (std::filesystem::exists(destination_)) throw std::runtime_error("Output file already exists.");
        temporary_ = destination_;
        temporary_ += L".writing-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        HANDLE file = CreateFileW(temporary_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create screenshot in the selected folder.");
        CloseHandle(file);
    }
    ~OutputTransaction() { if (!committed_) DeleteFileW(temporary_.c_str()); }
    const std::filesystem::path& path() const { return temporary_; }
    void commit() {
        if (!MoveFileExW(temporary_.c_str(), destination_.c_str(), MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot finish screenshot file without overwriting an existing image.");
        committed_ = true;
    }
};

inline void save_image(const std::filesystem::path& path, const Image& image) {
    const auto count = checked_pixels(image.width, image.height);
    if (image.bgra.size() != count * 4) throw std::runtime_error("Invalid SDR image buffer.");
    if (std::filesystem::exists(path)) throw std::runtime_error("Screenshot file already exists.");
    OutputTransaction transaction(path);
    auto factory = imaging_factory();
    ComPtr<IWICStream> stream;
    check(factory->CreateStream(&stream), "Cannot create output stream");
    check(stream->InitializeFromFilename(transaction.path().c_str(), GENERIC_WRITE), "Cannot open screenshot file");
    ComPtr<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "Cannot create PNG encoder");
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Cannot initialize PNG encoder");
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    check(encoder->CreateNewFrame(&frame, &properties), "Cannot create PNG frame");
    check(frame->Initialize(properties.Get()), "Cannot initialize PNG frame");
    check(frame->SetSize(image.width, image.height), "Cannot set PNG dimensions");
    check(frame->SetResolution(96, 96), "Cannot set PNG resolution");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&format), "Cannot set PNG pixel format");
    if (!IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA))
        throw std::runtime_error("PNG encoder changed the requested pixel format.");
    ComPtr<IWICMetadataQueryWriter> metadata;
    check(frame->GetMetadataQueryWriter(&metadata), "Cannot access PNG metadata");
    PROPVARIANT intent{};
    intent.vt = VT_UI1; intent.bVal = 0;
    check(metadata->SetMetadataByName(L"/sRGB/RenderingIntent", &intent), "Cannot write sRGB rendering intent");
    check(frame->WritePixels(image.height, image.width*4, UINT(image.bgra.size()),
                             const_cast<BYTE*>(image.bgra.data())), "Cannot write PNG pixels");
    check(frame->Commit(), "Cannot finish PNG frame");
    check(encoder->Commit(), "Cannot finish PNG file");
    metadata.Reset(); frame.Reset(); properties.Reset(); encoder.Reset(); stream.Reset();
    transaction.commit();
}

inline void save_master(const std::filesystem::path& path, const Frame& source) {
    const auto count = checked_pixels(source.width, source.height);
    if (source.pixels.size() != count) throw std::runtime_error("Invalid HDR image buffer.");
    if (std::filesystem::exists(path)) throw std::runtime_error("HDR original already exists.");
    OutputTransaction transaction(path);
    auto factory = imaging_factory();
    ComPtr<IWICStream> stream;
    check(factory->CreateStream(&stream), "Cannot create HDR output stream");
    check(stream->InitializeFromFilename(transaction.path().c_str(), GENERIC_WRITE), "Cannot open HDR original file");
    ComPtr<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(GUID_ContainerFormatWmp, nullptr, &encoder), "Cannot create JPEG XR encoder");
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Cannot initialize JPEG XR encoder");
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    check(encoder->CreateNewFrame(&frame, &properties), "Cannot create HDR frame");
    PROPBAG2 option{}; option.pstrName = const_cast<wchar_t*>(L"Lossless");
    VARIANT lossless{}; lossless.vt = VT_BOOL; lossless.boolVal = VARIANT_TRUE;
    check(properties->Write(1, &option, &lossless), "Cannot select lossless HDR encoding");
    check(frame->Initialize(properties.Get()), "Cannot initialize HDR frame");
    check(frame->SetSize(source.width, source.height), "Cannot set HDR dimensions");
    WICPixelFormatGUID format = GUID_WICPixelFormat64bppRGBAHalf;
    check(frame->SetPixelFormat(&format), "Cannot set HDR pixel format");
    if (!IsEqualGUID(format, GUID_WICPixelFormat64bppRGBAHalf))
        throw std::runtime_error("JPEG XR encoder did not preserve floating-point HDR format.");
    std::vector<std::uint16_t> pixels(count*4);
    for (std::size_t i = 0; i < count; ++i) {
        const Pixel& p = source.pixels[i];
        pixels[i*4] = float_to_half(p.r); pixels[i*4+1] = float_to_half(p.g);
        pixels[i*4+2] = float_to_half(p.b); pixels[i*4+3] = float_to_half(1);
    }
    check(frame->WritePixels(source.height, source.width*8, UINT(pixels.size()*sizeof(std::uint16_t)),
          reinterpret_cast<BYTE*>(pixels.data())), "Cannot write HDR original");
    check(frame->Commit(), "Cannot finish HDR frame");
    check(encoder->Commit(), "Cannot finish HDR original");
    frame.Reset(); properties.Reset(); encoder.Reset(); stream.Reset();
    transaction.commit();
}

inline Frame load_master(const std::filesystem::path& path) {
    auto factory = imaging_factory();
    ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
          WICDecodeMetadataCacheOnLoad, &decoder), "Cannot open HDR screenshot");
    GUID container{};
    check(decoder->GetContainerFormat(&container), "Cannot identify HDR file type");
    if (!IsEqualGUID(container, GUID_ContainerFormatWmp))
        throw std::runtime_error("Import requires a floating-point JPEG XR (.jxr) HDR screenshot.");
    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, &frame), "Cannot read HDR screenshot");
    UINT context_count = 0;
    check(frame->GetColorContexts(0, nullptr, &context_count), "Cannot inspect HDR color profile");
    if (context_count)
        throw std::runtime_error("This HDR original has an explicit color profile. Import currently requires unprofiled scRGB.");
    WICPixelFormatGUID source_format{};
    check(frame->GetPixelFormat(&source_format), "Cannot identify HDR pixel encoding");
    const bool floating = IsEqualGUID(source_format, GUID_WICPixelFormat64bppRGBAHalf)
        || IsEqualGUID(source_format, GUID_WICPixelFormat48bppRGBHalf)
        || IsEqualGUID(source_format, GUID_WICPixelFormat96bppRGBFloat)
        || IsEqualGUID(source_format, GUID_WICPixelFormat128bppRGBAFloat);
    if (!floating) throw std::runtime_error("This JPEG XR is not a supported scRGB floating-point HDR original.");
    Frame result;
    check(frame->GetSize(&result.width, &result.height), "Cannot read HDR dimensions");
    result.pixels.resize(checked_pixels(result.width, result.height));
    result.hdr = true; result.sdr_white = 203; result.backend = L"Imported scRGB HDR original";
    ComPtr<IWICFormatConverter> converter;
    check(factory->CreateFormatConverter(&converter), "Cannot create HDR format converter");
    check(converter->Initialize(frame.Get(), GUID_WICPixelFormat128bppRGBAFloat,
          WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "Cannot decode floating-point HDR");
    check(converter->CopyPixels(nullptr, result.width * sizeof(Pixel), UINT(result.pixels.size()*sizeof(Pixel)),
          reinterpret_cast<BYTE*>(result.pixels.data())), "Cannot read HDR image pixels");
    return result;
}

inline std::wstring screenshot_name() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t name[96];
    swprintf_s(name, L"Screenshot_%04u-%02u-%02u_%02u-%02u-%02u-%03u",
               time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
    return name;
}

} // namespace hdrshot
