#pragma once
#include "image/color.hpp"

namespace hdrshot::fixtures {

struct Fixture {
    const char* id;
    const char* name;
    Frame source;
    ColorSettings settings{ContentMode::Hdr,203,0,false,PeakMethod::Observed};
    Image sdr_reference;
    FrameRegion reference_area;
};

inline Pixel fp16(Rgb color) {
    return {half_to_float(float_to_half(float(color.r))),half_to_float(float_to_half(float(color.g))),
            half_to_float(float_to_half(float(color.b))),1};
}

inline std::array<unsigned,3> ui_codes(unsigned x,unsigned y) {
    if(y%24<3 && x%16<12) return {235,235,235};
    return {x<128?48u:192u,96,160};
}

inline Pixel ui_pixel(const std::array<unsigned,3>& codes,double white,bool half) {
    const Rgb color{srgb_decode(codes[0]/255.0)*white/80,srgb_decode(codes[1]/255.0)*white/80,
                    srgb_decode(codes[2]/255.0)*white/80};
    return half?fp16(color):Pixel{float(color.r),float(color.g),float(color.b),1};
}

inline void reference_pixel(Fixture& fixture,unsigned x,unsigned y,const std::array<unsigned,3>& codes) {
    const auto offset=(std::size_t(y)*fixture.source.width+x)*4;
    fixture.sdr_reference.bgra[offset]=std::uint8_t(codes[2]);
    fixture.sdr_reference.bgra[offset+1]=std::uint8_t(codes[1]);
    fixture.sdr_reference.bgra[offset+2]=std::uint8_t(codes[0]);
    fixture.sdr_reference.bgra[offset+3]=255;
}

inline Fixture make(unsigned index) {
    static constexpr const char* ids[]={"HDR-001","HDR-002","HDR-003","HDR-004","HDR-005",
                                       "HDR-006","HDR-007","HDR-008","HDR-009","HDR-010"};
    static constexpr const char* names[]={"grayscale","sparse-highlight","bright-scene","wide-gamut-neon",
        "sdr-window","elevated-sdr-white","transparent-overlay","near-black","bright-sdr","mixed-monitors"};
    if(index>=std::size(ids)) throw std::runtime_error("Unknown HDR fixture.");
    Fixture fixture{ids[index],names[index],{}, {}, {}, {}};
    fixture.settings={ContentMode::Hdr,203,0,false,PeakMethod::Observed};
    auto& frame=fixture.source;
    frame.width=frame.height=256;frame.hdr=index!=8;frame.sdr_white=index==5 || index==7?480:203;
    if(!frame.hdr) frame.sdr_white=80;
    frame.backend=L"Deterministic synthetic fixture";
    frame.pixels.resize(checked_pixels(frame.width,frame.height));
    fixture.sdr_reference.width=frame.width;fixture.sdr_reference.height=frame.height;
    fixture.sdr_reference.bgra.assign(frame.pixels.size()*4,0);
    if(index==4 || index==5) fixture.reference_area={32,64,192,128,false,frame.sdr_white};
    if(index==8) fixture.reference_area={0,0,256,256,false,80};
    if(index==9) {
        frame.regions={{0,0,128,256,true,203},{128,0,128,128,true,320},{128,128,128,128,false,80}};
        fixture.reference_area={128,0,128,256,false,0};
    }
    // Fixed linear-sRGB coordinates of the BT.2020 primaries, independent of the production matrix helper.
    static constexpr Rgb primaries[]={{1.66049100210843,-.124550474521591,-.0181507633549053},
        {-.58764113878855,1.13289989712596,-.100578898008007},
        {-.0728498633198849,-.00834942260436948,1.11872966136291}};
    static constexpr Rgb neon[]={{1,.15,.01},{.01,1,.1},{.15,.02,1},{1,.55,.02}};
    for(unsigned y=0;y<frame.height;++y) for(unsigned x=0;x<frame.width;++x) {
        const auto position=std::size_t(y)*frame.width+x;
        const double ramp=double(x)/255;
        double nits=2000*ramp;
        if(index==1) nits=20+80*ramp;
        if(index==2) nits=200+3800*ramp;
        if(index==7) nits=2*ramp;
        Pixel pixel=fp16({nits/80,nits/80,nits/80});
        if(index==3) {
            const auto& color=neon[y/64];
            const Rgb linear{primaries[0].r*color.r+primaries[1].r*color.g+primaries[2].r*color.b,
                             primaries[0].g*color.r+primaries[1].g*color.g+primaries[2].g*color.b,
                             primaries[0].b*color.r+primaries[1].b*color.g+primaries[2].b*color.b};
            pixel=fp16(linear*(.25+24.75*ramp));
        }
        if(index>=4 && index<=6 && x>=32 && x<224 && y>=64 && y<192) {
            const auto codes=ui_codes(x,y);
            const auto ui=ui_pixel(codes,frame.sdr_white,true);
            if(index==6) {
                const double alpha=double(x-32)/191;
                pixel=fp16({ui.r*alpha+pixel.r*(1-alpha),ui.g*alpha+pixel.g*(1-alpha),
                            ui.b*alpha+pixel.b*(1-alpha)});
            } else {
                pixel=ui;
                reference_pixel(fixture,x,y,codes);
            }
        }
        if(index==8) {
            const std::array<unsigned,3> codes={255,x,y};
            pixel=ui_pixel(codes,80,false);
            reference_pixel(fixture,x,y,codes);
        }
        if(index==9) {
            if(x<128) {
                const double value=2000*double(x)/127/80;
                pixel=fp16({value,value,value});
            } else {
                const auto codes=ui_codes(x,y);
                pixel=ui_pixel(codes,y<128?320:80,y<128);
                reference_pixel(fixture,x,y,codes);
            }
        }
        frame.pixels[position]=pixel;
    }
    if(index==1) frame.pixels.back()={125,125,125,1};
    return fixture;
}

} // namespace hdrshot::fixtures
