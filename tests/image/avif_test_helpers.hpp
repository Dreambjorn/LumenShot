#pragma once
#include "image/avif_output.hpp"

namespace hdrshot::fixtures {

// Test pixels use linear BT.2020 relative to 203 nits, matching the HDR JPEG decoder.
inline Frame decode_avif_bt2020(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input),{}};
    auto* decoder=avifDecoderCreate();
    if(!decoder) throw std::runtime_error("Cannot create AVIF test decoder.");
    struct Release {avifDecoder* value;~Release(){avifDecoderDestroy(value);}} release{decoder};
    decoder->maxThreads=4;
    check_avif_codec(avifDecoderSetIOMemory(decoder,bytes.data(),bytes.size()),"Cannot read AVIF test image");
    check_avif_codec(avifDecoderParse(decoder),"Cannot parse AVIF test image");
    check_avif_codec(avifDecoderNextImage(decoder),"Cannot decode AVIF test pixels");
    const auto* image=decoder->image;
    if(image->depth!=10 || image->yuvFormat!=AVIF_PIXEL_FORMAT_YUV444 || image->yuvRange!=AVIF_RANGE_FULL
       || image->colorPrimaries!=9 || image->transferCharacteristics!=16 || image->matrixCoefficients!=9)
        throw std::runtime_error("AVIF test image lost its HDR color contract.");
    Frame frame;frame.width=image->width;frame.height=image->height;frame.hdr=true;
    frame.pixels.resize(checked_pixels(frame.width,frame.height));
    for(unsigned y=0;y<frame.height;++y) {
        const auto* luma=reinterpret_cast<const std::uint16_t*>(image->yuvPlanes[0]+y*image->yuvRowBytes[0]);
        const auto* blue=reinterpret_cast<const std::uint16_t*>(image->yuvPlanes[1]+y*image->yuvRowBytes[1]);
        const auto* red=reinterpret_cast<const std::uint16_t*>(image->yuvPlanes[2]+y*image->yuvRowBytes[2]);
        for(unsigned x=0;x<frame.width;++x) {
            const double yy=luma[x]/1023.0,cb=(int(blue[x])-512)/1023.0,cr=(int(red[x])-512)/1023.0;
            const double r=yy+1.4746*cr,b=yy+1.8814*cb,g=(yy-.2627*r-.0593*b)/.6780;
            frame.pixels[std::size_t(y)*frame.width+x]={float(pq_decode(std::clamp(r,0.0,1.0))/203),
                float(pq_decode(std::clamp(g,0.0,1.0))/203),float(pq_decode(std::clamp(b,0.0,1.0))/203),1};
        }
    }
    return frame;
}

} // namespace hdrshot::fixtures
