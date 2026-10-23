#pragma once
#include "color.hpp"

// Copyright (c) 2017-2019 Advanced Micro Devices, Inc.; see FidelityFX-CAS-MIT.txt.
namespace hdrshot {

inline void sharpen_image(Image& image,unsigned strength) {
    const auto count=checked_pixels(image.width,image.height);
    if(image.bgra.size()!=count*4) throw std::runtime_error("Incomplete sharpening image.");
    if(strength>100) throw std::runtime_error("Sharpness must be between 0 and 100.");
    if(!strength) return;
    const auto source=image.bgra;
    static const auto linear=[] {
        std::array<double,256> values{};
        for(unsigned i=0;i<values.size();++i) values[i]=srgb_decode(i/255.0);
        return values;
    }();
    const ToneCurve curve(1);
    const RenderLookup lookup(curve,1,true,false);
    // CAS's native zero still sharpens, so blend from an exact bypass.
    const double blend=strength/100.0,peak=-1/(8-3*blend);
    const auto rows=[&](unsigned first,unsigned last) {
        for(unsigned y=first;y<last;++y) for(unsigned x=0;x<image.width;++x) {
            const unsigned left=x?x-1:0,right=std::min(x+1,image.width-1);
            const unsigned top=y?y-1:0,bottom=std::min(y+1,image.height-1);
            const std::size_t index=(std::size_t(y)*image.width+x)*4;
            const auto pixel=[&](unsigned column,unsigned row,unsigned channel) {
                return linear[source[(std::size_t(row)*image.width+column)*4+channel]];
            };
            for(unsigned channel=0;channel<3;++channel) {
                const double a=pixel(left,top,channel),b=pixel(x,top,channel),c=pixel(right,top,channel);
                const double d=pixel(left,y,channel),e=pixel(x,y,channel),f=pixel(right,y,channel);
                const double g=pixel(left,bottom,channel),h=pixel(x,bottom,channel),i=pixel(right,bottom,channel);
                const double cross_min=std::min({b,d,e,f,h}),cross_max=std::max({b,d,e,f,h});
                const double low=cross_min+std::min({cross_min,a,c,g,i});
                const double high=cross_max+std::max({cross_max,a,c,g,i});
                const double amplitude=high>0?std::sqrt(std::clamp(std::min(low,2-high)/high,0.0,1.0)):0;
                const double weight=amplitude*peak;
                const double filtered=std::clamp((e+(b+d+f+h)*weight)/(1+4*weight),0.0,1.0);
                const double mixed=e+(filtered-e)*blend;
                image.bgra[index+channel]=std::uint8_t(std::clamp(std::lround(lookup.encode(mixed)*255),0l,255l));
            }
        }
    };
    const unsigned workers=image.height<64?1
        :std::min(4u,std::max(1u,unsigned(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS))));
    std::array<NativeWork,3> jobs;
    for(unsigned i=1;i<workers;++i)
        jobs[i-1].start([&,i]{rows(image.height*i/workers,image.height*(i+1)/workers);});
    rows(0,image.height/workers);
    for(auto& job:jobs) job.join();
}

} // namespace hdrshot
