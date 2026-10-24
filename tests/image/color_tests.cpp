#include "image/color.hpp"
#include <iostream>
#include <functional>

using namespace hdrshot;
static unsigned checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void expect_near(double a, double b, double tolerance, const char* message) {
    require(std::abs(a-b) <= tolerance, message);
}
Frame single(Pixel pixel, bool hdr = true, double white = 203) {
    Frame frame; frame.width = frame.height = 1; frame.pixels = {pixel};
    frame.hdr = hdr; frame.sdr_white = white; return frame;
}
int main() try {
    expect_near(srgb_encode(0.18)*255, 118, 0.5, "18% gray must encode to 118");
    expect_near(pq_encode(1000),0.751827096247,1e-10,"PQ 1000-nit reference");
    expect_near(pq_encode(203),0.580688881042,1e-10,"PQ 203-nit reference");
    expect_near(pq_decode(0.751827096247),1000,1e-6,"PQ decoding reference");
    for (double white : {80.0,160.0,203.0,320.0,480.0}) {
        for (int code = 0; code < 256; ++code) {
            const float v = float(srgb_decode(code/255.0)*white/80);
            const auto image = render_sdr(single({v,v,v,1},true,white), {});
            require(image.bgra[0] == code, "SDR-in-HDR round-trip changed a code");
            require(!image.tone_mapped, "SDR-in-HDR received a tone curve");
        }
    }
    ColorSettings hdr; hdr.mode = ContentMode::Hdr; hdr.paper_white = 80; hdr.dither = false;
    const auto dark = render_sdr(single({0.18f,0.18f,0.18f,1}), hdr);
    require(dark.bgra[0] == 118, "Dark FP16 input skipped gamma encoding");
    for (double peak : {1.0,2.0,5.0,10.0,50.0,100.0}) {
        ToneCurve curve(peak);
        expect_near(curve.map(0),0,0,"Tone curve raised black");
        expect_near(curve.map(peak),1,1e-8,"Tone curve did not reach output white");
        double previous = 0;
        for (int i = 0; i <= 10000; ++i) {
            const double mapped = curve.map(peak * i / 10000.0);
            require(std::isfinite(mapped) && mapped >= previous-1e-10 && mapped >= 0 && mapped <= 1,
                    "Tone curve is not monotonic and bounded");
            previous = mapped;
        }
    }
    for (Rgb color : {Rgb{.1,.2,.3},Rgb{.9,.1,.05},Rgb{0,1,0},Rgb{1,1,1}}) {
        const auto compressed = compress_gamut(color);
        expect_near(compressed.r,color.r,0,"In-gamut red changed");
        expect_near(compressed.g,color.g,0,"In-gamut green changed");
        expect_near(compressed.b,color.b,0,"In-gamut blue changed");
        const auto restored = from_oklab(to_oklab(color));
        expect_near(restored.r,color.r,1e-6,"Oklab red round-trip");
        expect_near(restored.g,color.g,1e-6,"Oklab green round-trip");
        expect_near(restored.b,color.b,1e-6,"Oklab blue round-trip");
    }
    for (Rgb color : {Rgb{1.8,-.1,.1},Rgb{-.15,.9,1.5},Rgb{.6,-.1,1.2}}) {
        const auto mapped = compress_gamut(color);
        require(in_gamut(mapped,1e-8),"Gamut compression returned out-of-range channels");
        const auto a = to_oklab(color), b = to_oklab(mapped);
        const auto hue_difference = std::remainder(std::atan2(a.b,a.a)-std::atan2(b.b,b.a),2*3.141592653589793);
        require(std::abs(hue_difference) < 1e-5,"Oklab hue changed during gamut compression");
    }
    Frame overlay;overlay.width=2;overlay.height=1;overlay.hdr=true;
    // Captured orange overlay and neutral white from a native HDR10 game frame.
    overlay.pixels={{206.75f,-14.2421875f,-2.384765625f,1},{124.9375f,125,124.9375f,1}};
    const auto converted_overlay=render_sdr(overlay,{ContentMode::Hdr,240,.1,false});
    require(converted_overlay.bgra[2]==255 && converted_overlay.bgra[1]>=136
            && converted_overlay.bgra[1]<=138 && converted_overlay.bgra[0]==0,
            "Bright HDR orange was whitened during SDR conversion");
    require(converted_overlay.bgra[4]==255 && converted_overlay.bgra[5]==255
            && converted_overlay.bgra[6]==255,"Neutral HDR highlight gained a color cast");
    Frame calibrated;calibrated.width=4;calibrated.height=1;calibrated.hdr=true;calibrated.sdr_white=240;
    calibrated.pixels={{.54f,.54f,.54f,1},overlay.pixels[0],{-1.60546875f,1.2197265625f,13.9765625f,1},
                       {-.6923828125f,1.32421875f,-.044342041f,1}};
    ColorSettings limit{ContentMode::Hdr,0,0,false};limit.peak_nits=600;
    const auto limited=render_sdr(calibrated,limit);
    require(limited.reference_white==240 && limited.bgra[0]==118,"Configured peak was used as diffuse white or darkened midtones");
    limit.mode=ContentMode::Auto;
    require(render_sdr(calibrated,limit).bgra==limited.bgra,"Automatic mode ignored the configured game peak");
    const std::array<std::pair<std::uint16_t,float>,8> halves = {{{0x0000,0},{0x3c00,1},{0x4000,2},
        {0xbc00,-1},{0x0001,0.000000059604644775390625f},{0x7bff,65504},{0x3800,.5f},{0x4900,10}}};
    for (const auto& [bits,value] : halves) {
        expect_near(half_to_float(bits),value,0,"Half-float decoding reference");
        require(float_to_half(value) == bits,"Half-float encoding reference");
    }
    for (unsigned bits = 0; bits < 0x7c00; ++bits)
        require(float_to_half(half_to_float(std::uint16_t(bits))) == bits,"Finite half-float round-trip");
    bool rejected = false;
    try { render_sdr(single({std::numeric_limits<float>::quiet_NaN(),0,0,1}), {}); }
    catch (const std::exception&) { rejected = true; }
    require(rejected,"Invalid floating-point input was accepted");
    rejected = false;
    try { checked_pixels(0,100); } catch (const std::exception&) { rejected = true; }
    require(rejected,"Zero image dimensions accepted");
    const auto black = render_sdr(single({0,0,0,1}),hdr);
    require(black.bgra[0] == 0 && black.bgra[1] == 0 && black.bgra[2] == 0,"Black output is not zero");
    Frame large;large.width=512;large.height=128;large.hdr=true;large.sdr_white=320;
    large.pixels.resize(std::size_t(large.width)*large.height);
    std::uint32_t random=0x2490a6;
    for(double range:{.5,12.5,125.0,65504.0}) {
        for(auto& pixel:large.pixels) {
            const auto channel=[&] {random=random*1664525u+1013904223u;return float((double(random)/4294967295.0-.05)*range);};
            pixel={channel(),channel(),channel(),1};
        }
        for(double exposure:{-4.0,0.0,4.0}) {
            ColorSettings sample=hdr;sample.paper_white=203;sample.exposure=exposure;sample.dither=true;
            const auto reference=render_sdr(large,sample,false),accelerated=render_sdr(large,sample);
            int maximum_error=0;
            for(std::size_t i=0;i<reference.bgra.size();++i)
                maximum_error=std::max(maximum_error,std::abs(int(reference.bgra[i])-int(accelerated.bgra[i])));
            require(maximum_error<=1,"Lookup conversion exceeds one SDR code value");
        }
    }
    for(std::size_t i=0;i<large.pixels.size();++i) {
        const float value=float(srgb_decode(double(i%256)/255));
        large.pixels[i]={value,value,value,1};
    }
    large.pixels.back()={1e30f,1e30f,1e30f,1};
    const auto extreme_reference=render_sdr(large,hdr,false),extreme_fast=render_sdr(large,hdr);
    int extreme_error=0;
    for(std::size_t i=0;i<extreme_reference.bgra.size();++i)
        extreme_error=std::max(extreme_error,std::abs(int(extreme_reference.bgra[i])-int(extreme_fast.bgra[i])));
    require(extreme_error<=1,"Extreme source peak breaks lookup conversion");
    Frame native;native.width=512;native.height=128;
    native.pixels.resize(checked_pixels(native.width,native.height));
    std::vector<std::uint8_t> native_codes(native.pixels.size()*4);
    for(unsigned y=0;y<native.height;++y) for(unsigned x=0;x<native.width;++x) {
        const auto i=std::size_t(y)*native.width+x;
        const unsigned r=x%256,g=(x+y*17)%256,b=(x*37+y*29)%256;
        native.pixels[i]={float(srgb_decode(r/255.0)),float(srgb_decode(g/255.0)),float(srgb_decode(b/255.0)),1};
        native_codes[i*4]=b;native_codes[i*4+1]=g;native_codes[i*4+2]=r;native_codes[i*4+3]=255;
    }
    for(auto mode:{ContentMode::Auto,ContentMode::Hdr,ContentMode::Sdr})
        for(double exposure:{-4.0,.1,4.0}) for(double white:{40.0,2000.0}) {
            ColorSettings sample{mode,white,exposure,true};
            const auto preserved=render_sdr(native,sample);
            require(preserved.bgra==native_codes,"HDR settings altered native SDR pixels");
            require(!preserved.tone_mapped && preserved.description==L"SDR preserved","Native SDR was classified as an HDR conversion");
            require(render_sdr(native,sample,false).bgra==native_codes,"Exact SDR transfer changed source codes");
        }
    require(render_sdr(native,limit).bgra==native_codes,"Configured HDR peak changed a native SDR capture");
    for(double white:{80.0,160.0,203.0,320.0,480.0}) {
        Frame embedded=native;embedded.hdr=true;embedded.sdr_white=white;
        for(auto& pixel:embedded.pixels) {
            pixel.r*=float(white/80);pixel.g*=float(white/80);pixel.b*=float(white/80);
        }
        for(auto mode:{ContentMode::Auto,ContentMode::Sdr}) for(double exposure:{-4.0,.1,4.0}) {
            const auto normalized=render_sdr(embedded,{mode,203,exposure,true});
            require(normalized.bgra==native_codes,"HDR exposure altered normalized SDR content");
            require(!normalized.tone_mapped,"Normalized SDR received an HDR tone curve");
        }
    }
    ColorSettings lifted=hdr;lifted.exposure=.1;
    const auto brighter=render_sdr(single({.18f,.18f,.18f,1}),lifted);
    require(brighter.bgra[0]>dark.bgra[0],"HDR exposure no longer brightens actual HDR content");
    expect_near(brighter.bgra[0],srgb_encode(.18*std::exp2(.1))*255,.5,"HDR exposure uses the wrong linear-light gain");
    require(render_sdr(single({0,0,0,1}),lifted).bgra==black.bgra,"HDR exposure raised black");
    std::cout << checks << " color checks passed.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAILED after " << checks << " checks: " << error.what() << "\n";
    return 1;
}
