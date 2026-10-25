#pragma once
#include "image/hdr_output.hpp"
#include <avif/avif.h>
#include <thread>

namespace hdrshot {

inline int hdr_avif_quality(HdrAvifQuality quality) {
    switch(quality) {
    case HdrAvifQuality::High: return 95;
    case HdrAvifQuality::Balanced: return 90;
    case HdrAvifQuality::Compact: return 85;
    default: return 100;
    }
}
inline void check_avif_codec(avifResult result,const char* context,const avifDiagnostics* diagnostics=nullptr) {
    if(result!=AVIF_RESULT_OK)
        throw std::runtime_error(std::string(context)+": "+avifResultToString(result)
            +(diagnostics && diagnostics->error[0]?"; "+std::string(diagnostics->error):""));
}
inline void validate_hdr_avif(const std::filesystem::path& path,unsigned width,unsigned height,
                              const avifContentLightLevelInformationBox* expected=nullptr) {
    DiagnosticTimer timing(DiagnosticStage::Validate);
    const auto count=checked_pixels(width,height);
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    const auto size=input.tellg();
    if(!input || size<16 || std::uintmax_t(size)>std::uintmax_t(std::numeric_limits<int>::max()))
        throw std::runtime_error("HDR AVIF output is empty or unsupported.");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));
    if(!input) throw std::runtime_error("HDR AVIF output is incomplete.");
    auto* decoder=avifDecoderCreate();
    if(!decoder) throw std::runtime_error("Cannot create HDR AVIF verifier.");
    struct Release {avifDecoder* value;~Release(){avifDecoderDestroy(value);}} release{decoder};
    decoder->maxThreads=4;decoder->imageCountLimit=1;
    decoder->imageSizeLimit=std::uint32_t(count);decoder->imageDimensionLimit=std::max(width,height);
    check_avif_codec(avifDecoderSetIOMemory(decoder,bytes.data(),bytes.size()),"Cannot reopen HDR AVIF",&decoder->diag);
    check_avif_codec(avifDecoderParse(decoder),"Cannot inspect HDR AVIF",&decoder->diag);
    const auto* image=decoder->image;
    if(!image || image->width!=width || image->height!=height || image->depth!=10
       || image->yuvFormat!=AVIF_PIXEL_FORMAT_YUV444 || image->yuvRange!=AVIF_RANGE_FULL
       || image->colorPrimaries!=AVIF_COLOR_PRIMARIES_BT2020
       || image->transferCharacteristics!=AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084
       || image->matrixCoefficients!=AVIF_MATRIX_COEFFICIENTS_BT2020_NCL
       || image->icc.size || image->gainMap || image->transformFlags!=AVIF_TRANSFORM_NONE
       || decoder->imageCount!=1 || decoder->alphaPresent)
        throw std::runtime_error("HDR AVIF output changed its dimensions or color encoding.");
    if(expected && (image->clli.maxCLL!=expected->maxCLL || image->clli.maxPALL!=expected->maxPALL))
        throw std::runtime_error("HDR AVIF output changed its content light metadata.");
    check_avif_codec(avifDecoderNextImage(decoder),"Cannot decode HDR AVIF output",&decoder->diag);
    for(unsigned channel=0;channel<3;++channel)
        if(!image->yuvPlanes[channel] || image->yuvRowBytes[channel]<width*2)
            throw std::runtime_error("HDR AVIF output lost color pixels.");
}
inline void save_hdr_avif(const std::filesystem::path& path,const Frame& source,
                          HdrAvifQuality quality=HdrAvifQuality::Full) {
    DiagnosticTimer timing(DiagnosticStage::EncodeAvif);
    const auto count=checked_pixels(source.width,source.height);
    if(!source.hdr || source.pixels.size()!=count) throw std::runtime_error("Invalid HDR image buffer.");
    for(const auto& pixel:source.pixels)
        if(!std::isfinite(pixel.r) || !std::isfinite(pixel.g) || !std::isfinite(pixel.b))
            throw std::runtime_error("Capture contains invalid floating-point colors.");
    auto* image=avifImageCreate(source.width,source.height,10,AVIF_PIXEL_FORMAT_YUV444);
    if(!image) throw std::runtime_error("Cannot create HDR AVIF image.");
    struct ReleaseImage {avifImage* value;~ReleaseImage(){avifImageDestroy(value);}} release_image{image};
    image->colorPrimaries=AVIF_COLOR_PRIMARIES_BT2020;
    image->transferCharacteristics=AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084;
    image->matrixCoefficients=AVIF_MATRIX_COEFFICIENTS_BT2020_NCL;image->yuvRange=AVIF_RANGE_FULL;
    check_avif_codec(avifImageAllocatePlanes(image,AVIF_PLANES_YUV),"Cannot allocate HDR AVIF colors");
    std::vector<double> peaks(source.height),totals(source.height);
    parallel_rows(source.height,[&](unsigned,unsigned begin,unsigned end) {
        for(unsigned y=begin;y<end;++y) {
            auto* luma=reinterpret_cast<std::uint16_t*>(image->yuvPlanes[0]+y*image->yuvRowBytes[0]);
            auto* blue=reinterpret_cast<std::uint16_t*>(image->yuvPlanes[1]+y*image->yuvRowBytes[1]);
            auto* red=reinterpret_cast<std::uint16_t*>(image->yuvPlanes[2]+y*image->yuvRowBytes[2]);
            for(unsigned x=0;x<source.width;++x) {
                const auto& pixel=source.pixels[std::size_t(y)*source.width+x];
                const auto color=to_bt2020({pixel.r,pixel.g,pixel.b});
                const double r=std::clamp(color.r*80,0.0,10000.0),g=std::clamp(color.g*80,0.0,10000.0),b=std::clamp(color.b*80,0.0,10000.0);
                const double luminance=.2627*r+.6780*g+.0593*b;
                peaks[y]=std::max(peaks[y],luminance);totals[y]+=luminance;
                const double pq_r=pq_encode(r),pq_g=pq_encode(g),pq_b=pq_encode(b);
                const double pq_y=.2627*pq_r+.6780*pq_g+.0593*pq_b;
                const auto code=[](double value){return std::uint16_t(std::clamp(std::lround(value),0l,1023l));};
                luma[x]=code(pq_y*1023);
                // Full-range chroma is centered on 512, not half of 1023.
                blue[x]=code((pq_b-pq_y)/(2*(1-.0593))*1023+512);
                red[x]=code((pq_r-pq_y)/(2*(1-.2627))*1023+512);
            }
        }
    });
    double maximum=0,total=0;
    for(unsigned y=0;y<source.height;++y) {maximum=std::max(maximum,peaks[y]);total+=totals[y];}
    image->clli.maxCLL=std::uint16_t(std::clamp(std::ceil(maximum),0.0,10000.0));
    image->clli.maxPALL=std::uint16_t(std::clamp(std::ceil(total/count),0.0,10000.0));
    auto* encoder=avifEncoderCreate();
    if(!encoder) throw std::runtime_error("Cannot create HDR AVIF encoder.");
    struct ReleaseEncoder {avifEncoder* value;~ReleaseEncoder(){avifEncoderDestroy(value);}} release_encoder{encoder};
    encoder->codecChoice=AVIF_CODEC_CHOICE_AOM;encoder->quality=hdr_avif_quality(quality);
    encoder->speed=6;encoder->maxThreads=int(std::clamp(std::thread::hardware_concurrency(),1u,4u));encoder->autoTiling=AVIF_TRUE;
    avifRWData encoded=AVIF_DATA_EMPTY;
    struct ReleaseData {avifRWData* value;~ReleaseData(){avifRWDataFree(value);}} release_data{&encoded};
    check_avif_codec(avifEncoderWrite(encoder,image,&encoded),"Cannot encode HDR AVIF",&encoder->diag);
    if(!encoded.data || !encoded.size) throw std::runtime_error("HDR AVIF encoder returned no image.");
    OutputTransaction transaction(path);
    std::ofstream output(transaction.path(),std::ios::binary|std::ios::trunc);
    output.write(reinterpret_cast<const char*>(encoded.data),std::streamsize(encoded.size));output.close();
    if(!output) throw std::runtime_error("Cannot write HDR AVIF in the selected folder.");
    validate_hdr_avif(transaction.path(),source.width,source.height,&image->clli);
    transaction.commit();
}

} // namespace hdrshot
