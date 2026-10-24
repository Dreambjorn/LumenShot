#pragma once
#include "image/sharpen.hpp"

// Copyright (c) 2017-2019 Advanced Micro Devices, Inc.; see FidelityFX-CAS-MIT.txt.
namespace hdrshot {

inline constexpr std::array<unsigned,4> image_scales{100,125,150,200};
inline bool valid_scale(unsigned percent) {
    return std::find(image_scales.begin(),image_scales.end(),percent)!=image_scales.end();
}
inline std::pair<unsigned,unsigned> scaled_size(unsigned width,unsigned height,unsigned percent) {
    checked_pixels(width,height);
    if(!valid_scale(percent)) throw std::runtime_error("Choose an image size between 100% and 200%.");
    const unsigned w=unsigned((std::uint64_t(width)*percent+50)/100);
    const unsigned h=unsigned((std::uint64_t(height)*percent+50)/100);
    checked_pixels(w,h);return {w,h};
}

template<class Load,class Store>
void cas_resample(unsigned width,unsigned height,unsigned out_width,unsigned out_height,unsigned strength,
                  bool hdr,Load load,Store store) {
    const double blend=strength/100.0,peak=-1/(8-3*blend);
    const auto rows=[&](unsigned first,unsigned last) {
        for(unsigned y=first;y<last;++y) for(unsigned x=0;x<out_width;++x) {
            const double px=(x+.5)*width/out_width-.5,py=(y+.5)*height/out_height-.5;
            const int ix=int(std::floor(px)),iy=int(std::floor(py));
            const double fx=px-ix,fy=py-iy;
            const std::array<double,4> bilinear{(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
            std::array<std::array<double,4>,16> pixels;
            for(int row=0;row<4;++row) for(int column=0;column<4;++column)
                pixels[row*4+column]=load(unsigned(std::clamp(ix+column-1,0,int(width)-1)),
                                         unsigned(std::clamp(iy+row-1,0,int(height)-1)));
            std::array<std::array<double,3>,16> signal;
            for(unsigned i=0;i<16;++i) {
                const auto& p=pixels[i];
                if(hdr) signal[i].fill(std::max(0.0,luminance({p[0],p[1],p[2]})));
                else signal[i]={p[0],p[1],p[2]};
            }
            constexpr unsigned centers[]{5,6,9,10};
            std::array<std::array<double,3>,4> weights{},sums{},low{},high{};
            for(unsigned n=0;n<4;++n) for(unsigned channel=0;channel<3;++channel) {
                const unsigned i=centers[n];
                const double small=std::min({signal[i-4][channel],signal[i-1][channel],signal[i][channel],
                                              signal[i+1][channel],signal[i+4][channel]});
                const double large=std::max({signal[i-4][channel],signal[i-1][channel],signal[i][channel],
                                              signal[i+1][channel],signal[i+4][channel]});
                low[n][channel]=small+std::min({small,signal[i-5][channel],signal[i-3][channel],signal[i+3][channel],signal[i+5][channel]});
                high[n][channel]=large+std::max({large,signal[i-5][channel],signal[i-3][channel],signal[i+3][channel],signal[i+5][channel]});
                const double amplitude=high[n][channel]>0
                    ?std::sqrt(std::clamp(std::min(low[n][channel],2-high[n][channel])/high[n][channel],0.0,1.0)):0;
                weights[n][channel]=amplitude*peak;
                sums[n][channel]=signal[i-4][channel]+signal[i-1][channel]+signal[i+1][channel]+signal[i+4][channel];
            }
            std::array<double,4> interpolation{};
            double total=0;
            for(unsigned n=0;n<4;++n) {
                interpolation[n]=bilinear[n]/(1.0/32+high[n][1]-low[n][1]);total+=interpolation[n];
            }
            std::array<double,4> output{};
            if(hdr) {
                double plain_y=0,numerator=0,denominator=0;
                for(unsigned n=0;n<4;++n) {
                    const auto& p=pixels[centers[n]];const double w=interpolation[n];
                    for(unsigned channel=0;channel<3;++channel) output[channel]+=p[channel]*w/total;
                    plain_y+=signal[centers[n]][0]*w/total;
                    numerator+=(signal[centers[n]][0]+sums[n][0]*weights[n][0])*w;
                    denominator+=(1+4*weights[n][0])*w;
                }
                // Sharpen HDR luminance without clipping negative scRGB color components.
                const double target=plain_y+(std::clamp(numerator/denominator,0.0,1.0)-plain_y)*blend;
                if(plain_y>1e-12) for(unsigned channel=0;channel<3;++channel) output[channel]*=target/ plain_y;
            } else for(unsigned channel=0;channel<3;++channel) {
                double plain=0,numerator=0,denominator=0;
                for(unsigned n=0;n<4;++n) {
                    const double w=interpolation[n];
                    plain+=signal[centers[n]][channel]*w/total;
                    numerator+=(signal[centers[n]][channel]+sums[n][channel]*weights[n][channel])*w;
                    denominator+=(1+4*weights[n][channel])*w;
                }
                output[channel]=plain+(std::clamp(numerator/denominator,0.0,1.0)-plain)*blend;
            }
            for(unsigned n=0;n<4;++n) output[3]+=pixels[centers[n]][3]*bilinear[n];
            store(x,y,output);
        }
    };
    const unsigned workers=out_height<64?1
        :std::min(4u,std::max(1u,unsigned(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS))));
    std::array<NativeWork,3> jobs;
    for(unsigned i=1;i<workers;++i)
        jobs[i-1].start([&,i]{rows(out_height*i/workers,out_height*(i+1)/workers);});
    rows(0,out_height/workers);
    for(auto& job:jobs) job.join();
}

inline void scale_image(Image& image,unsigned percent,unsigned strength) {
    const auto [width,height]=scaled_size(image.width,image.height,percent);
    if(image.bgra.size()!=checked_pixels(image.width,image.height)*4 || strength>100)
        throw std::runtime_error("Invalid image scaling input.");
    if(percent==100){sharpen_image(image,strength);return;}
    static const auto linear=[] {
        std::array<double,256> values{};for(unsigned i=0;i<256;++i)values[i]=srgb_decode(i/255.0);return values;
    }();
    const ToneCurve curve(1);const RenderLookup lookup(curve,1,true,false);
    std::vector<std::uint8_t> result(checked_pixels(width,height)*4);
    cas_resample(image.width,image.height,width,height,strength,false,
        [&](unsigned x,unsigned y) {
            const auto i=(std::size_t(y)*image.width+x)*4;
            return std::array<double,4>{linear[image.bgra[i+2]],linear[image.bgra[i+1]],linear[image.bgra[i]],image.bgra[i+3]/255.0};
        },[&](unsigned x,unsigned y,const std::array<double,4>& color) {
            const auto i=(std::size_t(y)*width+x)*4;
            for(unsigned c=0;c<3;++c)result[i+2-c]=std::uint8_t(std::clamp(std::lround(lookup.encode(color[c])*255),0l,255l));
            result[i+3]=std::uint8_t(std::clamp(std::lround(color[3]*255),0l,255l));
        });
    image.width=width;image.height=height;image.bgra=std::move(result);
}

inline void enhance_hdr(Frame& frame,unsigned percent,unsigned strength) {
    const auto [width,height]=scaled_size(frame.width,frame.height,percent);
    if(!frame.hdr || frame.pixels.size()!=checked_pixels(frame.width,frame.height) || strength>100)
        throw std::runtime_error("Invalid HDR enhancement input.");
    if(percent==100 && !strength) return;
    double normalization=std::max(1.0,frame.sdr_white/80);
    for(const auto& p:frame.pixels) {
        if(!std::isfinite(p.r) || !std::isfinite(p.g) || !std::isfinite(p.b) || !std::isfinite(p.a))
            throw std::runtime_error("HDR enhancement contains invalid floating-point colors.");
        normalization=std::max(normalization,luminance({p.r,p.g,p.b}));
    }
    std::vector<Pixel> result(checked_pixels(width,height));
    cas_resample(frame.width,frame.height,width,height,strength,true,
        [&](unsigned x,unsigned y) {
            const auto& p=frame.pixels[std::size_t(y)*frame.width+x];
            return std::array<double,4>{p.r/normalization,p.g/normalization,p.b/normalization,p.a};
        },[&](unsigned x,unsigned y,const std::array<double,4>& color) {
            result[std::size_t(y)*width+x]={float(color[0]*normalization),float(color[1]*normalization),
                                           float(color[2]*normalization),float(color[3])};
        });
    const auto edge=[](unsigned value,unsigned input,unsigned output){return unsigned((std::uint64_t(value)*output+input/2)/input);};
    for(auto& region:frame.regions) {
        const unsigned right=edge(region.x+region.width,frame.width,width),bottom=edge(region.y+region.height,frame.height,height);
        region.x=edge(region.x,frame.width,width);region.y=edge(region.y,frame.height,height);
        region.width=right-region.x;region.height=bottom-region.y;
    }
    frame.width=width;frame.height=height;frame.pixels=std::move(result);
}

} // namespace hdrshot
