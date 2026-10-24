#pragma once
#include "image/color.hpp"

namespace hdrshot {

inline void composite_annotation(Frame& frame,const Image& coverage,Rgb ink,double paper_white=0) {
    const auto count=checked_pixels(frame.width,frame.height);
    if(!frame.hdr || frame.pixels.size()!=count || coverage.width!=frame.width || coverage.height!=frame.height
       || coverage.bgra.size()!=count*4 || !std::isfinite(paper_white) || (paper_white!=0 && paper_white<40) || paper_white>2000)
        throw std::runtime_error("Invalid HDR annotation buffer or reference white.");
    for(unsigned y=0;y<frame.height;++y) for(unsigned x=0;x<frame.width;++x) {
        const auto i=std::size_t(y)*frame.width+x;
        const double alpha=coverage.bgra[i*4]/255.0;
        if(!alpha) continue;
        double white=frame.sdr_white;
        for(const auto& region:frame.regions)
            if(x>=region.x && y>=region.y && x-region.x<region.width && y-region.y<region.height) {
                white=region.hdr?region.sdr_white:80;break;
            }
        if(paper_white) white=paper_white;
        if(!std::isfinite(white) || white<40 || white>10000) white=80;
        auto& pixel=frame.pixels[i];
        if(!std::isfinite(pixel.r) || !std::isfinite(pixel.g) || !std::isfinite(pixel.b) || !std::isfinite(pixel.a))
            throw std::runtime_error("HDR annotation contains invalid floating-point colors.");
        const double gain=white/80*alpha,remaining=1-alpha;
        pixel={float(ink.r*gain+pixel.r*remaining),float(ink.g*gain+pixel.g*remaining),
               float(ink.b*gain+pixel.b*remaining),float(alpha+pixel.a*remaining)};
    }
}

} // namespace hdrshot
