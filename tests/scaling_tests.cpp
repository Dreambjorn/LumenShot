#include "../src/hdr_output.hpp"
#include "../src/scaling.hpp"
#include "../src/selector.hpp"
#include <iostream>

using namespace hdrshot;
static unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
Image pattern(unsigned width,unsigned height) {
    Image image;image.width=width;image.height=height;image.bgra.resize(checked_pixels(width,height)*4);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        const auto i=(std::size_t(y)*width+x)*4;
        image.bgra[i]=std::uint8_t(20+x*20+y*30);image.bgra[i+1]=std::uint8_t(50+x*10+y*25);
        image.bgra[i+2]=std::uint8_t(30+x*35+y*10);image.bgra[i+3]=255;
    }
    return image;
}
int wmain(int argc,wchar_t** argv) try {
    if(argc!=2) throw std::runtime_error("Specify an owned scaling evidence directory.");
    Runtime runtime;const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
    auto source=pattern(73,65);source.description=L"Owned native image";
    auto native=source;scale_image(native,100,0);
    require(native.bgra==source.bgra && native.width==73 && native.height==65,"Original size was not an exact bypass");
    const std::pair<unsigned,unsigned> sizes[]={{91,81},{110,98},{146,130}};
    unsigned option=0;
    for(unsigned scale:{125u,150u,200u}) {
        for(unsigned strength:{0u,35u,100u}) {
            auto a=source,b=source;scale_image(a,scale,strength);scale_image(b,scale,strength);
            require(a.width==sizes[option].first && a.height==sizes[option].second,"Fractional scaling rounded the image incorrectly");
            require(a.bgra==b.bgra,"Parallel scaling is nondeterministic");
            require(a.description==source.description,"Scaling changed color metadata");
            for(std::size_t i=3;i<a.bgra.size();i+=4)require(a.bgra[i]==255,"Scaling changed screenshot opacity");
        }
        ++option;
    }
    auto reference=pattern(4,4);scale_image(reference,200,100);
    const int expected[]={89,103,94};
    for(unsigned c=0;c<3;++c) require(std::abs(int(reference.bgra[(4*8+3)*4+2-c])-expected[c])<=1,"CAS scaling reference pixel differs");
    for(unsigned code:{0u,17u,118u,203u,255u}) for(unsigned scale:{125u,150u,200u}) {
        auto flat=pattern(9,7);
        for(std::size_t i=0;i<flat.bgra.size();i+=4) flat.bgra[i]=flat.bgra[i+1]=flat.bgra[i+2]=std::uint8_t(code);
        scale_image(flat,scale,100);
        for(std::size_t i=0;i<flat.bgra.size();i+=4)
            require(flat.bgra[i]==code && flat.bgra[i+1]==code && flat.bgra[i+2]==code,"Scaling raised black or altered a flat field");
    }
    Frame hdr;hdr.width=44;hdr.height=33;hdr.hdr=true;hdr.sdr_white=203;
    hdr.pixels.assign(44*33,Pixel{12.5f,12.5f,12.5f,1});
    hdr.regions={{0,0,22,33,true,203},{22,0,22,33,false,80}};
    const auto original=hdr;enhance_hdr(hdr,100,0);
    require(std::memcmp(hdr.pixels.data(),original.pixels.data(),hdr.pixels.size()*sizeof(Pixel))==0,"Disabled HDR enhancement changed captured values");
    for(unsigned scale:image_scales) {
        auto image=original;enhance_hdr(image,scale,35);
        for(const auto& p:image.pixels)require(p.r==12.5f && p.g==12.5f && p.b==12.5f,"HDR scaling changed the absolute 1000-nit white level");
        require(image.regions[0].width+image.regions[1].width==image.width && image.regions[1].x==image.regions[0].width,
                "Scaling broke per-monitor color regions");
    }
    const Rgb wide{-1.491,2.874,-.255};
    for(unsigned y=0;y<hdr.height;++y) for(unsigned x=0;x<hdr.width;++x) {
        const double gain=.6+.3*std::sin(x*2*3.141592653589793/16);
        hdr.pixels[y*hdr.width+x]={float(wide.r*gain),float(wide.g*gain),float(wide.b*gain),1};
    }
    for(unsigned scale:image_scales) {
        auto image=hdr;enhance_hdr(image,scale,35);
        require(std::any_of(image.pixels.begin(),image.pixels.end(),[](const Pixel& p){return p.g>1;}),"HDR enhancement clipped values above SDR white");
        for(const auto& p:image.pixels) {
            require(std::isfinite(p.r) && std::isfinite(p.g) && std::isfinite(p.b),"HDR enhancement generated invalid colors");
            require(p.r<0 && p.b<0 && p.g>0,"HDR enhancement clipped wide color components");
            require(std::abs(p.r/p.g-wide.r/wide.g)<1e-6 && std::abs(p.b/p.g-wide.b/wide.g)<1e-6,
                    "HDR luminance sharpening changed color ratios");
        }
    }
    Image annotation;annotation.width=200;annotation.height=160;annotation.bgra.assign(200*160*4,0);
    annotate(annotation,{{MarkTool::Line,RGB(255,0,0),{{15,15},{75,15}},L""}},{5,5},2,2);
    require(annotation.bgra[(20*200+60)*4+2]==255,"Scaled annotation position was lost");
    require(annotation.bgra[(10*200+30)*4+2]==0,"Scaled annotation retained its old position");
    save_image(directory/L"scaled-annotation.png",annotation);
    bool rejected=false;try{auto image=source;scale_image(image,201,15);}catch(const std::exception&){rejected=true;}
    require(rejected,"Unsupported scaling was accepted");
    rejected=false;try{scaled_size(32768,1,200);}catch(const std::exception&){rejected=true;}
    require(rejected,"Oversized scaling was not rejected before allocation");
    rejected=false;try{auto image=hdr;image.pixels[0].r=std::numeric_limits<float>::quiet_NaN();enhance_hdr(image,125,15);}catch(const std::exception&){rejected=true;}
    require(rejected,"Invalid HDR enhancement pixels were accepted");
    std::cout<<checks<<" scaling checks passed: original bypass, CAS reference, dimensions, parallel output, HDR nits and gamut, monitor regions, and annotations.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"Scaling failed after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
