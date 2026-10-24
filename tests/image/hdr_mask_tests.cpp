#include "hdr_fixtures.hpp"
#include "image/image_io.hpp"
#include "image/hdr_output.hpp"
#include <iostream>

using namespace hdrshot;
static unsigned checks=0;
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
std::pair<Image,DiagnosticSnapshot> render(const Frame& frame,ColorSettings settings,bool mask,bool lookup=true) {
    settings.content_aware=mask;auto record=std::make_shared<DiagnosticRecord>(DiagnosticSource::Synthetic);
    Image image;{DiagnosticContext context(record);image=render_sdr(frame,settings,lookup);}
    record->finish(true);return {std::move(image),record->snapshot()};
}
int difference(const Image& a,const Image& b) {
    if(a.bgra.size()!=b.bgra.size()) throw std::runtime_error("Compared image sizes differ.");
    int maximum=0;
    for(std::size_t i=0;i<a.bgra.size();++i) maximum=std::max(maximum,std::abs(int(a.bgra[i])-int(b.bgra[i])));
    return maximum;
}
bool same_area(const Image& a,const Image& b,const FrameRegion& area) {
    for(unsigned y=area.y;y<area.y+area.height;++y) {
        const auto offset=(std::size_t(y)*a.width+area.x)*4;
        if(!std::equal(a.bgra.begin()+offset,a.bgra.begin()+offset+area.width*4,b.bgra.begin()+offset)) return false;
    }
    return true;
}
int reference_error(const fixtures::Fixture& fixture,const Image& image) {
    int maximum=0;const auto& area=fixture.reference_area;
    for(unsigned y=area.y;y<area.y+area.height;++y) for(unsigned x=area.x;x<area.x+area.width;++x) {
        const auto offset=(std::size_t(y)*image.width+x)*4;
        for(unsigned c=0;c<3;++c) maximum=std::max(maximum,std::abs(int(image.bgra[offset+c])-int(fixture.sdr_reference.bgra[offset+c])));
    }
    return maximum;
}
Image comparison(const Image& before,const Image& after) {
    Image image;image.width=before.width*2;image.height=before.height;image.bgra.resize(checked_pixels(image.width,image.height)*4);
    for(unsigned y=0;y<image.height;++y) {
        auto* row=image.bgra.data()+std::size_t(y)*image.width*4;
        std::copy_n(before.bgra.data()+std::size_t(y)*before.width*4,before.width*4,row);
        std::copy_n(after.bgra.data()+std::size_t(y)*after.width*4,after.width*4,row+before.width*4);
    }
    return image;
}
void verify_hdr_export(const std::filesystem::path& path,const Frame& frame,const Image& base) {
    save_hdr_jpeg(path,frame,base);
    std::ifstream input(path,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});
    auto* decoder=uhdr_create_decoder();require(decoder!=nullptr,"Cannot inspect content-aware HDR JPEG");
    struct Release {uhdr_codec_private_t* value;~Release(){uhdr_release_decoder(value);}} release{decoder};
    uhdr_compressed_image_t compressed{};compressed.data=bytes.data();compressed.data_sz=compressed.capacity=bytes.size();
    check_hdr_codec(uhdr_dec_set_image(decoder,&compressed),"Cannot read content-aware HDR JPEG");
    check_hdr_codec(uhdr_dec_set_out_img_format(decoder,UHDR_IMG_FMT_64bppRGBAHalfFloat),"Cannot select HDR reconstruction");
    check_hdr_codec(uhdr_dec_set_out_color_transfer(decoder,UHDR_CT_LINEAR),"Cannot select linear HDR reconstruction");
    check_hdr_codec(uhdr_dec_set_out_max_display_boost(decoder,10000.0f/203),"Cannot select HDR headroom");
    check_hdr_codec(uhdr_decode(decoder),"Cannot reconstruct content-aware HDR JPEG");
    const auto* decoded=uhdr_get_decoded_image(decoder);
    require(decoded && decoded->cg==UHDR_CG_BT_2100 && decoded->w==frame.width && decoded->h==frame.height,
            "Content-aware HDR JPEG lost source dimensions or gamut");
    const auto* pixels=static_cast<const std::uint16_t*>(decoded->planes[0]);
    // Fixed Rec.2020 nits for HDR-006, independent of the renderer's matrix helper.
    const struct {POINT position;double nits[3];} patches[]={
        {{80,100},{34.69518,54.51423,156.30472}},{{160,100},{184.50624,71.01323,160.21866}},
        {{240,32},{1882.5,1882.5,1882.5}}};
    for(const auto& patch:patches) {
        const auto offset=(std::size_t(patch.position.y)*decoded->stride[0]+patch.position.x)*4;
        for(unsigned channel=0;channel<3;++channel) {
            const double actual=half_to_float(pixels[offset+channel])*203;
            require(std::abs(actual-patch.nits[channel])<std::max(1.0,patch.nits[channel]*.08),
                    "SDR mask changed reconstructed HDR brightness or color");
        }
    }
}

int wmain(int argc,wchar_t** argv) try {
    if(argc!=2) throw std::runtime_error("Specify an owned mixed-content evidence directory.");
    Runtime runtime;const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
    std::ofstream report(directory/L"comparison.json");report.exceptions(std::ios::badbit|std::ios::failbit);
    report<<"{\"provenance\":\"synthetic\",\"scenes\":[";
    for(unsigned scene=0;scene<10;++scene) {
        const auto fixture=fixtures::make(scene);auto settings=fixture.settings;settings.mode=ContentMode::Auto;
        const auto before=render(fixture.source,settings,false),after=render(fixture.source,settings,true);
        if(scene==4 || scene==5) {
            require(reference_error(fixture,after.first)<=1,"SDR window no longer matches its controlled reference");
            require(same_area(after.first,before.first,{0,0,256,64}) && same_area(after.first,before.first,{0,192,256,64})
                    && same_area(after.first,before.first,{0,64,32,128}) && same_area(after.first,before.first,{224,64,32,128}),
                    "Mask altered the HDR background outside the SDR window");
            std::cout<<fixture.id<<" SDR reference maximum error: "<<reference_error(fixture,before.first)
                     <<" -> "<<reference_error(fixture,after.first)<<" codes.\n";
        } else require(after.first.bgra==before.first.bgra,"Mask changed an ambiguous overlay or a broad reference scene");
        if(scene==5) {
            const auto source=fixture.source.pixels;
            require(render_sdr(fixture.source,settings).bgra==after.first.bgra,"Default automatic rendering differs from the compared candidate");
            require(after.first.bgra==render(fixture.source,settings,true).first.bgra,"Content detection is not repeatable");
            for(const auto mode:{ContentMode::Hdr,ContentMode::Sdr}) {
                settings.mode=mode;
                require(render(fixture.source,settings,true).first.bgra==render(fixture.source,settings,false).first.bgra,
                        "Manual content mode did not bypass spatial detection");
            }
            verify_hdr_export(directory/L"HDR-006-gainmap.jpg",fixture.source,after.first);
            require(!std::memcmp(source.data(),fixture.source.pixels.data(),source.size()*sizeof(Pixel)),"Mask or HDR export mutated captured pixels");
        }
        if(scene>=4 && scene<=6) save_image(directory/(std::string(fixture.id)+"-comparison.png"),comparison(before.first,after.first));
        if(scene) report<<',';
        report<<"{\"id\":\""<<fixture.id<<"\",\"before_reference_error\":"<<reference_error(fixture,before.first)
              <<",\"after_reference_error\":"<<reference_error(fixture,after.first)<<",\"candidate\":"<<diagnostic_json(after.second)<<'}';
    }
    report<<"]}\n";report.close();
    const FrameRegion interior{77,51,248,152};
    for(double white:{80.0,203.0,480.0,2000.0}) {
        Frame frame;frame.width=512;frame.height=256;frame.hdr=true;frame.sdr_white=white;
        frame.pixels.resize(checked_pixels(frame.width,frame.height));
        for(unsigned y=0;y<frame.height;++y) for(unsigned x=0;x<frame.width;++x) {
            const double nits=2500+5000*double(x)/511;
            frame.pixels[std::size_t(y)*512+x]=x>=61 && x<341 && y>=35 && y<219
                ?fixtures::ui_pixel(fixtures::ui_codes(x,y),white,true):fixtures::fp16({nits/80,nits/80,nits/80});
        }
        ColorSettings settings{ContentMode::Auto,203,0,true};const auto after=render(frame,settings,true).first;
        int error=0;
        for(unsigned y=interior.y;y<interior.y+interior.height;++y) for(unsigned x=interior.x;x<interior.x+interior.width;++x) {
            const auto codes=fixtures::ui_codes(x,y);const auto offset=(std::size_t(y)*512+x)*4;
            for(unsigned c=0;c<3;++c) error=std::max(error,std::abs(int(after.bgra[offset+c])-int(codes[2-c])));
        }
        require(error<=1,"Unaligned SDR interior lost its source codes");
        settings.exposure=4;const auto exposed=render(frame,settings,true).first;
        require(same_area(exposed,after,interior),"HDR exposure changed a confidently preserved SDR interior");
        require(!same_area(exposed,after,{0,0,512,35}),"HDR exposure was disabled outside the SDR mask");
    }
    const auto left=fixtures::make(4),right=fixtures::make(5);
    Frame mixed;mixed.width=512;mixed.height=256;mixed.hdr=true;
    mixed.regions={{0,0,256,256,true,203},{256,0,256,256,true,480}};mixed.pixels.resize(512*256);
    ColorSettings mode{ContentMode::Auto,203,0,false};
    const auto a=render(left.source,mode,true).first,b=render(right.source,mode,true).first;
    Image expected;expected.width=512;expected.height=256;expected.bgra.resize(512*256*4);
    for(unsigned y=0;y<256;++y) {
        std::copy_n(left.source.pixels.begin()+y*256,256,mixed.pixels.begin()+y*512);
        std::copy_n(right.source.pixels.begin()+y*256,256,mixed.pixels.begin()+y*512+256);
        std::copy_n(a.bgra.begin()+y*256*4,256*4,expected.bgra.begin()+y*512*4);
        std::copy_n(b.bgra.begin()+y*256*4,256*4,expected.bgra.begin()+y*512*4+256*4);
    }
    const auto desktop=render(mixed,mode,true);
    require(desktop.first.bgra==expected.bgra,"Neighboring display white changed a content-aware region");
    require(desktop.second.masks.size()==2 && desktop.second.masks[0].sdr_pixels==24576
            && desktop.second.masks[1].sdr_pixels==24576,"Mixed displays lost independent content diagnostics");
    Frame stacked=left.source;stacked.height=1024;stacked.pixels.clear();stacked.pixels.reserve(256*1024);
    std::vector<std::uint8_t> repeated;
    for(unsigned n=0;n<4;++n) {
        stacked.pixels.insert(stacked.pixels.end(),left.source.pixels.begin(),left.source.pixels.end());
        repeated.insert(repeated.end(),a.bgra.begin(),a.bgra.end());
    }
    require(render(stacked,mode,true).first.bgra==repeated,"Parallel tile analysis changed repeated SDR/HDR content");
    Frame feather;feather.width=feather.height=256;feather.hdr=true;feather.sdr_white=203;feather.pixels.resize(256*256);
    for(unsigned y=0;y<256;++y) for(unsigned x=0;x<256;++x) {
        const float level=y<2?25.0f:float((x>=48 && x<120 && y>=48 && y<208?.9:.85+.3*x/255)*203/80);
        feather.pixels[std::size_t(y)*256+x]={level,level,level,1};
    }
    const auto uniform=render(feather,mode,false).first,blended=render(feather,mode,true).first;
    const int target=int(std::lround(srgb_encode(.9)*255)),start=uniform.bgra[(128*256+48)*4];
    require(target-start>10,"Boundary fixture has insufficient tone-map contrast");
    require(std::abs(int(blended.bgra[(128*256+80)*4])-target)<=1,"Blended SDR interior lost its reference white");
    require(blended.bgra[(128*256+47)*4]==uniform.bgra[(128*256+47)*4],"Boundary blending changed the HDR exterior");
    std::array<bool,256> levels{};int maximum_step=0;
    for(unsigned x=48;x<121;++x) {
        const auto offset=(128*256+x)*4;const int value=blended.bgra[offset];
        if(value>start+1 && value<target-1) levels[value]=true;
        maximum_step=std::max(maximum_step,std::abs(value-int(blended.bgra[offset-4])));
    }
    require(std::count(levels.begin(),levels.end(),true)>=3 && maximum_step*2<target-start,
            "Weak boundary became a hard correction seam");
    require(difference(blended,render(feather,mode,true,false).first)<=1,"Blended lookup differs from scalar reference");
    feather.sdr_white=NAN;bool rejected=false;
    try {render(feather,mode,true);} catch(const std::exception&) {rejected=true;}
    require(rejected,"Content-aware analysis accepted invalid monitor white");
    std::cout<<checks<<" HDR mask checks passed: SDR references, manual fallback, transparent overlays, HDR boundaries, exposure, and immutable sources.\n";
    std::cout<<"Synthetic evidence only. Flat dim HDR and transparent SDR remain inherently ambiguous; manual HDR uses uniform mapping.\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"HDR mask fixture failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
