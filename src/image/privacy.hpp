#pragma once
#include "image/color.hpp"

namespace hdrshot {

enum class PrivacyEffect { Redact, Blur, Pixelate };

template<class Load,class Store>
void privacy_filter(unsigned width,unsigned height,PrivacyEffect effect,unsigned amount,Load load,Store store) {
    if(!width || !height) return;
    if(effect==PrivacyEffect::Redact) {
        for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) store(x,y,{0,0,0});
    } else if(effect==PrivacyEffect::Pixelate) {
        const unsigned block=std::max(2u,amount);
        for(unsigned y=0;y<height;y+=block) for(unsigned x=0;x<width;x+=block) {
            const unsigned right=std::min(width,x+block),bottom=std::min(height,y+block);
            std::array<double,3> sum{};
            for(unsigned v=y;v<bottom;++v) for(unsigned u=x;u<right;++u) {
                const auto pixel=load(u,v);for(unsigned c=0;c<3;++c) sum[c]+=pixel[c];
            }
            for(auto& c:sum) c/=double(right-x)*(bottom-y);
            for(unsigned v=y;v<bottom;++v) for(unsigned u=x;u<right;++u) store(u,v,sum);
        }
    } else {
        const int radius=int(std::clamp(amount,1u,64u)),diameter=2*radius+1;
        std::vector<std::array<float,3>> rows(checked_pixels(width,height));
        for(unsigned y=0;y<height;++y) {
            std::array<double,3> sum{};
            for(int k=-radius;k<=radius;++k) {
                const auto pixel=load(unsigned(std::clamp(k,0,int(width)-1)),y);
                for(unsigned c=0;c<3;++c) sum[c]+=pixel[c];
            }
            for(unsigned x=0;x<width;++x) {
                for(unsigned c=0;c<3;++c) rows[std::size_t(y)*width+x][c]=float(sum[c]/diameter);
                const auto leaving=load(unsigned(std::clamp(int(x)-radius,0,int(width)-1)),y);
                const auto entering=load(unsigned(std::clamp(int(x)+radius+1,0,int(width)-1)),y);
                for(unsigned c=0;c<3;++c) sum[c]+=entering[c]-leaving[c];
            }
        }
        for(unsigned x=0;x<width;++x) {
            std::array<double,3> sum{};
            for(int k=-radius;k<=radius;++k) {
                const auto& pixel=rows[std::size_t(std::clamp(k,0,int(height)-1))*width+x];
                for(unsigned c=0;c<3;++c) sum[c]+=pixel[c];
            }
            for(unsigned y=0;y<height;++y) {
                std::array<double,3> pixel{};for(unsigned c=0;c<3;++c) pixel[c]=sum[c]/diameter;
                store(x,y,pixel);
                const auto& leaving=rows[std::size_t(std::clamp(int(y)-radius,0,int(height)-1))*width+x];
                const auto& entering=rows[std::size_t(std::clamp(int(y)+radius+1,0,int(height)-1))*width+x];
                for(unsigned c=0;c<3;++c) sum[c]+=double(entering[c])-leaving[c];
            }
        }
    }
}
inline RECT privacy_bounds(RECT rect,unsigned width,unsigned height) {
    return {std::clamp(rect.left,0l,LONG(width)),std::clamp(rect.top,0l,LONG(height)),
            std::clamp(rect.right,0l,LONG(width)),std::clamp(rect.bottom,0l,LONG(height))};
}
inline void privacy_bgra(std::uint8_t* pixels,unsigned width,unsigned height,RECT rect,PrivacyEffect effect,unsigned amount) {
    rect=privacy_bounds(rect,width,height);
    if(rect.right<=rect.left || rect.bottom<=rect.top) return;
    const auto index=[&](unsigned x,unsigned y){return (std::size_t(y+rect.top)*width+x+rect.left)*4;};
    privacy_filter(unsigned(rect.right-rect.left),unsigned(rect.bottom-rect.top),effect,amount,
        [&](unsigned x,unsigned y) {const auto i=index(x,y);return std::array<double,3>{double(pixels[i]),double(pixels[i+1]),double(pixels[i+2])};},
        [&](unsigned x,unsigned y,const std::array<double,3>& pixel) {
            const auto i=index(x,y);for(unsigned c=0;c<3;++c) pixels[i+c]=std::uint8_t(std::clamp(std::lround(pixel[c]),0l,255l));
            pixels[i+3]=255;
        });
}
inline void apply_privacy(Image& image,RECT rect,PrivacyEffect effect,unsigned amount) {
    if(image.bgra.size()!=checked_pixels(image.width,image.height)*4) throw std::runtime_error("Invalid privacy image pixels.");
    privacy_bgra(image.bgra.data(),image.width,image.height,rect,effect,amount);
}
inline void apply_privacy(Frame& frame,RECT rect,PrivacyEffect effect,unsigned amount) {
    if(frame.pixels.size()!=checked_pixels(frame.width,frame.height)) throw std::runtime_error("Invalid privacy HDR pixels.");
    rect=privacy_bounds(rect,frame.width,frame.height);
    if(rect.right<=rect.left || rect.bottom<=rect.top) return;
    const auto index=[&](unsigned x,unsigned y){return std::size_t(y+rect.top)*frame.width+x+rect.left;};
    privacy_filter(unsigned(rect.right-rect.left),unsigned(rect.bottom-rect.top),effect,amount,
        [&](unsigned x,unsigned y) {const auto& p=frame.pixels[index(x,y)];return std::array<double,3>{p.r,p.g,p.b};},
        [&](unsigned x,unsigned y,const std::array<double,3>& p) {frame.pixels[index(x,y)]={float(p[0]),float(p[1]),float(p[2]),1};});
}

} // namespace hdrshot
