#include "app/workflow.hpp"
#include "ui/selector.hpp"
#include "image/scaling.hpp"
#include <iostream>

using namespace hdrshot;
static unsigned checks=0;
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
Image read_pixels(const std::filesystem::path& path) {
    auto factory=imaging_factory();ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;
    check(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),"Cannot reopen privacy output");
    check(decoder->GetFrame(0,&frame),"Cannot read privacy output");
    ComPtr<IWICFormatConverter> converter;check(factory->CreateFormatConverter(&converter),"Cannot convert privacy output");
    check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Cannot read privacy channels");
    Image image;check(frame->GetSize(&image.width,&image.height),"Cannot read privacy dimensions");
    image.bgra.resize(checked_pixels(image.width,image.height)*4);
    check(converter->CopyPixels(nullptr,image.width*4,UINT(image.bgra.size()),image.bgra.data()),"Cannot decode privacy pixels");return image;
}
int wmain(int argc,wchar_t** argv) try {
    if(argc!=2) throw std::runtime_error("Specify an owned privacy evidence directory.");
    Runtime runtime;const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
    Image source;source.width=96;source.height=64;source.bgra.resize(96*64*4);
    for(unsigned y=0;y<64;++y) for(unsigned x=0;x<96;++x) {
        const auto i=(y*96+x)*4;source.bgra[i]=std::uint8_t((x*31+y*7)%256);
        source.bgra[i+1]=std::uint8_t((y*29+x*11)%256);source.bgra[i+2]=std::uint8_t((x+y)%2?240:15);source.bgra[i+3]=255;
    }
    const auto frozen=source.bgra;const RECT rect{23,17,59,41};
    for(const auto tool:{MarkTool::Redact,MarkTool::Blur,MarkTool::Pixelate}) {
        const Annotation mark{tool,RGB(0,255,0),{{23,17},{58,40}},L""};
        auto output=source;annotate(output,{mark},{});
        auto direct=source;apply_privacy_marks(direct,{mark});
        require(output.bgra==direct.bgra,"Preview/export effect paths disagree");
        require(output.bgra!=source.bgra,"Privacy effect left the fixture unchanged");
        for(unsigned y=0;y<64;++y) for(unsigned x=0;x<96;++x) {
            const auto i=(y*96+x)*4;const bool inside=x>=23 && x<59 && y>=17 && y<41;
            if(!inside) require(std::memcmp(output.bgra.data()+i,source.bgra.data()+i,4)==0,"Privacy effect modified an unrelated pixel");
            else {
                require(output.bgra[i+3]==255,"Privacy effect lost opacity");
                if(tool==MarkTool::Redact) require(output.bgra[i]==0 && output.bgra[i+1]==0 && output.bgra[i+2]==0,"Solid redaction depends on the selected drawing color");
                if(tool==MarkTool::Blur) for(unsigned c=0;c<3;++c) {
                    unsigned sum=0;
                    for(int v=-8;v<=8;++v) for(int u=-8;u<=8;++u) {
                        const auto sx=std::clamp(int(x)+u,23,58),sy=std::clamp(int(y)+v,17,40);
                        sum+=source.bgra[(sy*96+sx)*4+c];
                    }
                    require(std::abs(int(output.bgra[i+c])-int(std::lround(sum/289.0)))<=1,"Blur differs from the independent clamped box reference");
                }
            }
        }
        auto reversed=source;annotate(reversed,{{tool,0,{{58,40},{23,17}},L""}},{});
        require(reversed.bgra==output.bgra,"Reverse-direction privacy drag changed its region");
        if(tool==MarkTool::Pixelate) for(unsigned y=17;y<41;y+=12) for(unsigned x=23;x<59;x+=12) {
            for(unsigned c=0;c<3;++c) {
                unsigned sum=0;for(unsigned v=y;v<y+12;++v) for(unsigned u=x;u<x+12;++u) sum+=source.bgra[(v*96+u)*4+c];
                const auto expected=std::lround(sum/144.0);
                for(unsigned v=y;v<y+12;++v) for(unsigned u=x;u<x+12;++u)
                    require(output.bgra[(v*96+u)*4+c]==expected,"Pixelation retained detail within an averaged block");
            }
        }
        const auto path=directory/(std::to_wstring(int(tool))+L".png");save_image(path,output);
        require(read_pixels(path).bgra==output.bgra,"Saved PNG changed privacy pixels");
        const auto dib=clipboard_dib(output);
        require(std::memcmp(dib.data()+sizeof(BITMAPV5HEADER),output.bgra.data(),output.bgra.size())==0,"Clipboard payload exposed unedited pixels");
        auto frame=Frame{};frame.width=96;frame.height=64;frame.hdr=true;
        for(std::size_t i=0;i<96*64;++i) frame.pixels.push_back({float(i%37)*.5f,float(i%29)*.25f,-.125f,1});
        const auto original=frame.pixels;apply_privacy_marks(frame,{mark});
        for(unsigned y=0;y<64;++y) for(unsigned x=0;x<96;++x) {
            const auto i=y*96+x;const auto& pixel=frame.pixels[i];
            if(x<23 || x>=59 || y<17 || y>=41) require(std::memcmp(&pixel,&original[i],sizeof(Pixel))==0,"HDR effect changed pixels outside its rectangle");
            else if(tool==MarkTool::Redact) require(pixel.r==0 && pixel.g==0 && pixel.b==0 && pixel.a==1,"HDR redaction retained linear source values");
            else require(std::isfinite(pixel.r) && std::isfinite(pixel.g) && pixel.b==-.125f,"HDR effect clipped signed or bright linear values");
        }
        const auto master=directory/(std::to_wstring(int(tool))+L".jxr");save_master(master,frame);const auto decoded=load_master(master);
        for(std::size_t i=0;i<frame.pixels.size();++i) {
            const auto a=decoded.pixels[i],b=frame.pixels[i];
            require(a.r==half_to_float(float_to_half(b.r)) && a.g==half_to_float(float_to_half(b.g)) && a.b==b.b,"Reopened HDR master lost its privacy edits");
        }
    }
    for(const auto tool:{MarkTool::Redact,MarkTool::Blur,MarkTool::Pixelate}) {
        Image tiny;tiny.width=tiny.height=1;tiny.bgra={25,100,200,255};
        apply_privacy_marks(tiny,{{tool,0,{{-10,-10},{10,10}},L""}});
        require(tiny.bgra==(tool==MarkTool::Redact?std::vector<std::uint8_t>{0,0,0,255}:std::vector<std::uint8_t>{25,100,200,255}),"Tiny or clipped privacy region failed");
        auto outside=source;apply_privacy_marks(outside,{{tool,0,{{-30,-30},{-10,-10}},L""}});
        require(outside.bgra==source.bgra,"Out-of-crop privacy mark changed unrelated pixels");
    }
    Image text;text.width=384;text.height=96;text.bgra.assign(384*96*4,255);
    annotate(text,{{MarkTool::Text,RGB(0,0,0),{{12,8}},L"Account: sample@example.test"},
                   {MarkTool::Text,RGB(0,0,0),{{12,45}},L"Code: 6A3F-90D2-C18B"}},{});
    Image comparison;comparison.width=1536;comparison.height=256;comparison.bgra.assign(1536*256*4,255);
    std::vector<Annotation> captions;
    const unsigned strengths[2][4]={{0,4,8,12},{0,8,12,16}};
    for(unsigned row=0;row<2;++row) for(unsigned column=0;column<4;++column) {
        auto panel=text;const unsigned amount=strengths[row][column];
        if(amount) apply_privacy(panel,{0,0,384,96},row?PrivacyEffect::Pixelate:PrivacyEffect::Blur,amount);
        double edges=0;
        for(unsigned y=1;y<96;++y) for(unsigned x=1;x<384;++x) {
            const auto i=(y*384+x)*4;
            edges+=std::abs(int(panel.bgra[i])-int(panel.bgra[i-4]))+std::abs(int(panel.bgra[i])-int(panel.bgra[i-384*4]));
        }
        std::cout<<(row?"PIXELATE ":"BLUR ")<<amount<<" fixture edge energy "<<edges/(383*95)<<"\n";
        for(unsigned y=0;y<96;++y) std::memcpy(comparison.bgra.data()+((std::size_t(row*128+32+y)*1536)+column*384)*4,
                                             panel.bgra.data()+std::size_t(y)*384*4,384*4);
        const auto title=amount?(row?L"Pixelate ":L"Blur ")+std::to_wstring(amount):L"Original";
        captions.push_back({MarkTool::Text,RGB(0,0,0),{{LONG(column*384+12),LONG(row*128)}},title});
    }
    annotate(comparison,captions,{});save_image(directory/L"privacy-strengths.png",comparison);
    const Annotation redact{MarkTool::Redact,RGB(255,0,0),{{23,17},{58,40}},L""};
    const auto mapped=privacy_rect(redact,{11,7},1.25,1.5);
    require(mapped.left==15 && mapped.top==15 && mapped.right==60 && mapped.bottom==51,"Scaled privacy bounds failed to cover fractional source pixels");
    require(source.bgra==frozen,"Privacy derivatives modified the frozen source");
    auto other=source;
    for(LONG y=rect.top;y<rect.bottom;++y) for(LONG x=rect.left;x<rect.right;++x) {
        const auto i=(y*96+x)*4;other.bgra[i]^=255;other.bgra[i+1]^=255;other.bgra[i+2]^=255;
    }
    for(const unsigned scale:{100u,125u,150u,200u}) for(const unsigned sharpness:{0u,35u,100u}) {
        auto first=source,second=other;
        for(auto* image:{&first,&second}) {
            apply_privacy_marks(*image,{redact},{},1,1,true);scale_image(*image,scale,sharpness);
            apply_privacy_marks(*image,{redact},{},double(image->width)/96,double(image->height)/64);
        }
        require(first.bgra==second.bgra,"Resampling or sharpening leaked redacted source pixels outside the mask");
    }
    std::cout<<checks<<" privacy checks passed: independent blur/block references, opaque fill, crop/scaling, immutable sources, PNG/JPEG XR reopening, and clipboard payload.\n";
    std::cout<<"Blur and pixelation obscure detail; only opaque redaction removes the covered source pixels. System clipboard was not changed.\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"FAILED after "<<checks<<" privacy checks: "<<error.what()<<"\n";return 1;}
