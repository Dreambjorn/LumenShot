#include "app/workflow.hpp"
#include "ui/selector.hpp"
#include <iostream>

using namespace hdrshot;
static unsigned checks=0;
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
void check_near(double value,double expected,double tolerance,const char* message) {require(std::abs(value-expected)<=tolerance,message);}
Image decode_sdr(const std::filesystem::path& path,bool managed) {
    auto factory=imaging_factory();ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;
    check(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),"Cannot reopen annotated SDR output");
    check(decoder->GetFrame(0,&frame),"Cannot read annotated SDR frame");Image image;
    check(frame->GetSize(&image.width,&image.height),"Cannot read annotated SDR dimensions");image.bgra.resize(checked_pixels(image.width,image.height)*4);
    if(managed) {
        ComPtr<IWICColorContext> from,to;check(factory->CreateColorContext(&from),"Cannot create source annotation profile");
        UINT contexts=0;IWICColorContext* profile=from.Get();check(frame->GetColorContexts(1,&profile,&contexts),"Cannot read annotation ICC profile");
        require(contexts==1,"Annotated JPEG lost its fallback profile");
        check(factory->CreateColorContext(&to),"Cannot create sRGB annotation profile");check(to->InitializeFromExifColorSpace(1),"Cannot select sRGB");
        ComPtr<IWICColorTransform> transform;check(factory->CreateColorTransformer(&transform),"Cannot color-manage annotation fallback");
        check(transform->Initialize(frame.Get(),from.Get(),to.Get(),GUID_WICPixelFormat32bppBGRA),"Cannot select annotation fallback color space");
        check(transform->CopyPixels(nullptr,image.width*4,UINT(image.bgra.size()),image.bgra.data()),"Cannot decode annotation fallback");
    } else check(frame->CopyPixels(nullptr,image.width*4,UINT(image.bgra.size()),image.bgra.data()),"Cannot decode annotation PNG");
    return image;
}
int wmain(int argc,wchar_t** argv) try {
    if(argc!=2) throw std::runtime_error("Specify an owned HDR annotation evidence directory.");
    Runtime runtime;const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
    Frame source;source.width=4;source.height=1;source.hdr=true;source.sdr_white=203;source.pixels.assign(4,Pixel{8,4,-.5f,1});
    Image mask;mask.width=4;mask.height=1;mask.bgra.resize(16);
    const unsigned alpha[]={0,64,128,255};for(unsigned i=0;i<4;++i) mask.bgra[i*4]=alpha[i];
    auto blended=source;composite_annotation(blended,mask,{1,.21586050011389926,0});
    require(std::memcmp(&blended.pixels[0],&source.pixels[0],sizeof(Pixel))==0,"Transparent overlay altered HDR highlight or negative color");
    for(unsigned i=1;i<4;++i) {
        const double a=alpha[i]/255.0;const auto p=blended.pixels[i];
        check_near(p.r,203.0/80*a+8*(1-a),.000001,"Red annotation was blended in nonlinear light");
        check_near(p.g,.21586050011389926*203/80*a+4*(1-a),.000001,"Midtone annotation skipped sRGB decoding");
        check_near(p.b,-.5*(1-a),.000001,"Overlay clipped an underlying negative HDR channel");check_near(p.a,1,0,"Overlay changed screenshot opacity");
    }
    source.regions={{0,0,1,1,true,80},{1,0,1,1,true,320},{2,0,2,1,false,480}};mask.bgra.assign(16,255);
    blended=source;composite_annotation(blended,mask,{1,1,1});
    for(unsigned i=0;i<4;++i) check_near(blended.pixels[i].r,i==1?4:1,0,"Annotation ignored a monitor's HDR/SDR reference white");
    blended=source;composite_annotation(blended,mask,{1,1,1},250);
    for(const auto& p:blended.pixels) check_near(p.r,3.125,0,"Explicit annotation white was ignored");
    for(const double white:{-1.0,1.0,2001.0,std::numeric_limits<double>::infinity()}) {
        bool rejected=false;try {composite_annotation(blended,mask,{1,1,1},white);}catch(const std::exception&){rejected=true;}
        require(rejected,"Invalid annotation reference white was accepted");
    }
    Frame scene;scene.width=320;scene.height=160;scene.hdr=true;scene.sdr_white=203;
    scene.pixels.assign(checked_pixels(scene.width,scene.height),Pixel{.25f,.5f,4,1});
    const auto frozen=scene.pixels;
    const std::vector<Annotation> marks={
        {MarkTool::Pen,RGB(255,0,0),{{30,35},{110,35}},L""},
        {MarkTool::Line,RGB(0,255,0),{{145,35},{230,35}},L""},
        {MarkTool::Arrow,RGB(255,255,0),{{40,75},{120,60}},L""},
        {MarkTool::Rectangle,RGB(0,255,255),{{155,65},{235,110}},L""},
        {MarkTool::Marker,RGB(255,0,255),{{40,120},{115,120}},L""},
        {MarkTool::Text,RGB(128,128,128),{{245,50}},L"HDR"}};
    auto edited=scene;annotate_hdr(edited,marks);
    require(std::memcmp(scene.pixels.data(),frozen.data(),frozen.size()*sizeof(Pixel))==0,"Annotation modified the captured HDR source");
    check_near(edited.pixels[35*320+60].r,2.5375,.000001,"Opaque pen has incorrect HDR white");
    check_near(edited.pixels[35*320+60].g,0,0,"Opaque pen leaked underlying green");
    check_near(edited.pixels[35*320+180].g,2.5375,.000001,"Line lost its green channel");
    check_near(edited.pixels[120*320+70].b,2.5375,.000001,"Marker lost its blue channel");
    require(std::memcmp(&edited.pixels[0],&scene.pixels[0],sizeof(Pixel))==0,"Drawing affected an unrelated HDR pixel");
    unsigned antialias=0;
    for(unsigned y=50;y<80;++y) for(unsigned x=245;x<320;++x) {
        const auto p=edited.pixels[y*320+x];
        const double gray=.21586050011389926*203/80,coverage=(p.r-.25)/(gray-.25);
        if(coverage>0 && coverage<.999) ++antialias;
        check_near(p.g,.5*(1-coverage)+gray*coverage,.000002,"Text edges blended green outside linear light");
        check_near(p.b,4*(1-coverage)+gray*coverage,.000002,"Text edges darkened their HDR background");
        require(std::isfinite(p.r) && std::isfinite(p.g) && std::isfinite(p.b),"Antialiased text produced invalid HDR colors");
    }
    require(antialias>10,"Text edges lost antialias coverage");
    auto black_ink=scene;annotate_hdr(black_ink,{{MarkTool::Line,RGB(0,0,0),{{30,35},{110,35}},L""}});
    check_near(black_ink.pixels[35*320+60].r,0,0,"Black ink disappeared into an empty overlay mask");
    const DWORD resources=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(unsigned i=0;i<20;++i) {auto repeated=scene;annotate_hdr(repeated,marks);}
    GdiFlush();require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==resources,"Repeated HDR annotation leaked drawing resources");
    auto clipped=scene;
    annotate_hdr(clipped,{{MarkTool::Line,RGB(255,255,255),{{-10,20},{500,20}},L""}},{10,10},1.25,1.25);
    check_near(clipped.pixels[13*320+100].r,2.5375,.000001,"Fractional crop scaling displaced an HDR annotation");
    auto redacted=scene;const std::vector<Annotation> ordered={marks.front(),{MarkTool::Redact,0,{{20,20},{120,45}},L""}};
    annotate_hdr(redacted,ordered);
    check_near(redacted.pixels[35*320+60].r,0,0,"A later redaction failed to cover an earlier HDR stroke");
    check_near(redacted.pixels[35*320+60].b,0,0,"HDR redaction retained captured blue");
    ColorSettings color{ContentMode::Hdr,203,0,false};auto sdr=render_sdr(scene,color);annotate(sdr,marks,{});
    for(const auto format:{HdrFormat::UltraJpeg,HdrFormat::JpegXr}) {
        Settings settings;settings.folder=directory/(format==HdrFormat::UltraJpeg?L"jpeg":L"jxr");settings.hdr_format=format;settings.annotate_hdr=true;
        const auto result=process_output(settings,edited,sdr);
        require(result.success && result.details.find(L"could not be saved")==std::wstring::npos,"Annotated HDR artifact could not be saved");
        require(result.details.find(L"Annotated HDR")!=std::wstring::npos,"Export result omitted its HDR annotation mode");
        std::filesystem::path hdr,png;
        for(const auto& item:std::filesystem::directory_iterator(settings.folder))if(item.path().extension()==L".png")png=item.path();
        unsigned outputs=0;for(const auto& item:std::filesystem::directory_iterator(settings.folder/L"HDR images")){hdr=item.path();++outputs;}
        require(outputs==1 && hdr.stem().wstring().find(L"-annotated")!=std::wstring::npos,"Annotated export duplicated or mislabeled the original");
        require(decode_sdr(png,false).bgra==sdr.bgra,"Saved PNG differed from the annotated SDR rendering");
        if(format==HdrFormat::JpegXr) {
            const auto decoded=load_master(hdr);
            for(std::size_t i=0;i<edited.pixels.size();++i) {
                const auto a=decoded.pixels[i],b=edited.pixels[i];
                require(a.r==half_to_float(float_to_half(b.r)) && a.g==half_to_float(float_to_half(b.g))
                        && a.b==half_to_float(float_to_half(b.b)),"Reopened JPEG XR omitted or changed an HDR annotation");
            }
        } else {
            const auto fallback=decode_sdr(hdr,true);
            for(const POINT p:{POINT{60,35},POINT{180,35},POINT{70,120}}) {
                const auto offset=(std::size_t(p.y)*320+p.x)*4;
                const auto lab=[&](const Image& image) {return to_oklab({srgb_decode(image.bgra[offset+2]/255.0),
                    srgb_decode(image.bgra[offset+1]/255.0),srgb_decode(image.bgra[offset]/255.0)});};
                const auto a=lab(fallback),b=lab(sdr);
                const double distance=std::sqrt((a.l-b.l)*(a.l-b.l)+(a.a-b.a)*(a.a-b.a)+(a.b-b.b)*(a.b-b.b));
                std::cout<<"ANNOTATION_FALLBACK distance "<<distance<<"\n";
                require(distance<.02,"JPEG fallback did not depict the SDR annotations consistently");
            }
        }
    }
    std::cout<<checks<<" HDR annotation checks passed: linear coverage, monitor whites, immutable source, all drawing tools, text edges, scaled clipping, privacy ordering, and reopened exports.\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"HDR annotation fixture failed after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
