#pragma once
#include "platform/platform.hpp"
#include "image/color.hpp"
#include <wincodec.h>
#include <fstream>

namespace hdrshot {

enum class WicOutput { SrgbPng, ScRgbJpegXr, ImportedScRgbJpegXr };

inline void validate_png_container(const std::filesystem::path& path) {
    static constexpr auto table=[] {
        std::array<std::uint32_t,256> result{};
        for(unsigned i=0;i<result.size();++i) {
            auto crc=std::uint32_t(i);
            for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
            result[i]=crc;
        }
        return result;
    }();
    const auto integer=[](const BYTE* value) {
        return (std::uint32_t(value[0])<<24)|(std::uint32_t(value[1])<<16)|(std::uint32_t(value[2])<<8)|value[3];
    };
    const auto size=std::filesystem::file_size(path);
    std::ifstream input(path,std::ios::binary);
    const BYTE signature[]={137,80,78,71,13,10,26,10};BYTE actual[8]{};
    input.read(reinterpret_cast<char*>(actual),sizeof(actual));
    if(!input || std::memcmp(actual,signature,sizeof(signature))) throw std::runtime_error("PNG output has an invalid signature.");
    std::uintmax_t offset=8;bool header=false,pixels=false;
    while(offset<=size && size-offset>=12) {
        BYTE chunk[8]{};input.read(reinterpret_cast<char*>(chunk),sizeof(chunk));
        const auto length=integer(chunk);
        if(!input || length>size-offset-12) throw std::runtime_error("PNG output contains a truncated chunk.");
        if(!header && (length!=13 || std::memcmp(chunk+4,"IHDR",4))) throw std::runtime_error("PNG output is missing its header.");
        header=true;
        std::uint32_t crc=0xffffffffu;
        for(unsigned i=4;i<8;++i) crc=table[(crc^chunk[i])&255]^(crc>>8);
        BYTE buffer[8192];std::uint32_t remaining=length;
        while(remaining) {
            const auto count=std::min(remaining,std::uint32_t(sizeof(buffer)));
            input.read(reinterpret_cast<char*>(buffer),count);
            if(!input) throw std::runtime_error("PNG output contains incomplete pixel data.");
            for(unsigned i=0;i<count;++i) crc=table[(crc^buffer[i])&255]^(crc>>8);
            remaining-=count;
        }
        BYTE checksum[4]{};input.read(reinterpret_cast<char*>(checksum),sizeof(checksum));
        if(!input || (crc^0xffffffffu)!=integer(checksum)) throw std::runtime_error("PNG output failed a chunk checksum.");
        offset+=std::uintmax_t(length)+12;
        if(!std::memcmp(chunk+4,"IDAT",4)) pixels=true;
        if(!std::memcmp(chunk+4,"IEND",4)) {
            if(length || !pixels || offset!=size) throw std::runtime_error("PNG output has an invalid end marker.");
            return;
        }
    }
    throw std::runtime_error("PNG output is incomplete.");
}

inline void validate_wic_output(IWICImagingFactory* factory,const std::filesystem::path& path,
                                WicOutput kind,unsigned width,unsigned height,const BYTE* expected=nullptr) {
    DiagnosticTimer timing(DiagnosticStage::Validate);
    checked_pixels(width,height);
    const bool png=kind==WicOutput::SrgbPng;
    if(png) validate_png_container(path);
    ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),
          "Cannot reopen encoded image");
    GUID container{};check(decoder->GetContainerFormat(&container),"Cannot inspect encoded container");
    if(!IsEqualGUID(container,png?GUID_ContainerFormatPng:GUID_ContainerFormatWmp))
        throw std::runtime_error("Encoded image has the wrong container.");
    UINT count=0;check(decoder->GetFrameCount(&count),"Cannot inspect encoded frame count");
    if(count!=1) throw std::runtime_error("Encoded image must contain one frame.");
    ComPtr<IWICBitmapFrameDecode> frame;check(decoder->GetFrame(0,&frame),"Cannot reopen encoded frame");
    UINT actual_width=0,actual_height=0;check(frame->GetSize(&actual_width,&actual_height),"Cannot inspect encoded dimensions");
    if(actual_width!=width || actual_height!=height) throw std::runtime_error("Encoded dimensions changed.");
    WICPixelFormatGUID format{};check(frame->GetPixelFormat(&format),"Cannot inspect encoded pixels");
    const bool floating=IsEqualGUID(format,GUID_WICPixelFormat64bppRGBAHalf) || IsEqualGUID(format,GUID_WICPixelFormat48bppRGBHalf)
        || IsEqualGUID(format,GUID_WICPixelFormat96bppRGBFloat) || IsEqualGUID(format,GUID_WICPixelFormat128bppRGBAFloat);
    if(kind==WicOutput::ImportedScRgbJpegXr?!floating:
       !IsEqualGUID(format,png?GUID_WICPixelFormat32bppBGRA:GUID_WICPixelFormat64bppRGBAHalf))
        throw std::runtime_error("Encoded image changed the requested pixel format.");
    if(png) {
        ComPtr<IWICMetadataQueryReader> metadata;check(frame->GetMetadataQueryReader(&metadata),"Cannot inspect PNG color metadata");
        PROPVARIANT intent{};
        struct Clear {PROPVARIANT& value;~Clear(){PropVariantClear(&value);}} clear{intent};
        check(metadata->GetMetadataByName(L"/sRGB/RenderingIntent",&intent),"PNG output is missing sRGB metadata");
        if(intent.vt!=VT_UI1 || intent.bVal!=0) throw std::runtime_error("PNG output changed its rendering intent.");
    } else {
        UINT contexts=0;check(frame->GetColorContexts(0,nullptr,&contexts),"Cannot inspect encoded HDR color profile");
        if(contexts) throw std::runtime_error("HDR output must retain unprofiled scRGB.");
    }
    const unsigned channels=png?4:IsEqualGUID(format,GUID_WICPixelFormat64bppRGBAHalf)?8:
        IsEqualGUID(format,GUID_WICPixelFormat48bppRGBHalf)?6:IsEqualGUID(format,GUID_WICPixelFormat96bppRGBFloat)?12:16;
    const unsigned stride=width*channels;
    std::vector<BYTE> pixels(std::size_t(stride)*std::min(height,64u));
    for(unsigned y=0;y<height;y+=64) {
        WICRect rect{0,INT(y),INT(width),INT(std::min(height-y,64u))};
        const auto bytes=stride*unsigned(rect.Height);
        check(frame->CopyPixels(&rect,stride,bytes,pixels.data()),"Cannot decode completed image");
        if(expected && std::memcmp(pixels.data(),expected+std::size_t(y)*stride,bytes))
            throw std::runtime_error("Lossless output changed the encoded pixels.");
    }
}

} // namespace hdrshot
