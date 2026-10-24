#include "hdr_fixtures.hpp"
#include "image/image_io.hpp"
#include <fstream>
#include <iostream>
#include <locale>

using namespace hdrshot;
static unsigned checks=0;
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}

struct GoldenScene {
    const char* id;
    int colors[8][3];
};

// Renderer baseline: 0fabce30.
static constexpr GoldenScene golden_scenes[]={
    {"HDR-001",{
        {0,0,0},{246,246,246},
        {246,246,246},{254,254,254},
        {255,255,255},{255,255,255},
        {255,255,255},{151,151,151}
    }},
    {"HDR-002",{
        {88,88,88},{120,120,120},
        {120,120,120},{139,139,139},
        {152,152,152},{255,255,255},
        {152,152,152},{94,94,94}
    }},
    {"HDR-003",{
        {206,206,206},{251,251,251},
        {251,251,251},{254,254,254},
        {255,255,255},{255,255,255},
        {255,255,255},{224,224,224}
    }},
    {"HDR-004",{
        {110,14,0},{255,151,0},
        {0,255,121},{239,48,255},
        {255,74,255},{255,255,90},
        {255,218,0},{191,124,0}
    }},
    {"HDR-005",{
        {0,0,0},{246,246,246},
        {209,209,209},{190,96,159},
        {190,96,159},{255,255,255},
        {255,255,255},{151,151,151}
    }},
    {"HDR-006",{
        {0,0,0},{246,246,246},
        {241,241,241},{238,150,210},
        {238,150,210},{255,255,255},
        {255,255,255},{151,151,151}
    }},
    {"HDR-007",{
        {0,0,0},{246,246,246},
        {243,243,243},{248,246,247},
        {236,228,233},{255,255,255},
        {255,255,255},{151,151,151}
    }},
    {"HDR-008",{
        {0,0,0},{8,8,8},
        {8,8,8},{15,15,15},
        {21,21,21},{25,25,25},
        {21,21,21},{1,1,1}
    }},
    {"HDR-009",{
        {255,0,0},{255,64,32},
        {255,64,96},{255,128,128},
        {255,192,160},{255,255,255},
        {255,192,32},{255,8,224}
    }},
    {"HDR-010",{
        {0,0,0},{254,254,254},
        {254,254,254},{192,96,160},
        {192,96,160},{192,96,160},
        {235,119,196},{194,194,194}
    }},
};
static_assert(std::size(golden_scenes)==10);

int difference(const Image& first,const Image& second) {
    require(first.width==second.width && first.height==second.height && first.bgra.size()==second.bgra.size(),
            "Compared images have different dimensions");
    int error=0;
    for(std::size_t i=0;i<first.bgra.size();++i)
        error=std::max(error,std::abs(int(first.bgra[i])-int(second.bgra[i])));
    return error;
}

std::pair<int,double> reference_error(const fixtures::Fixture& fixture,const Image& image) {
    const auto& area=fixture.reference_area;
    if(!area.width) return {-1,-1};
    int maximum=0;
    double total=0;
    for(unsigned y=area.y;y<area.y+area.height;++y) for(unsigned x=area.x;x<area.x+area.width;++x) {
        const auto offset=(std::size_t(y)*image.width+x)*4;
        for(unsigned channel=0;channel<3;++channel) {
            const int error=std::abs(int(image.bgra[offset+channel])-int(fixture.sdr_reference.bgra[offset+channel]));
            maximum=std::max(maximum,error);total+=error;
        }
    }
    return {maximum,total/(std::size_t(area.width)*area.height*3)};
}

void verify_png(const std::filesystem::path& path,const Image& expected) {
    auto factory=imaging_factory();
    ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),
          "Cannot reopen golden PNG");
    ComPtr<IWICBitmapFrameDecode> frame;check(decoder->GetFrame(0,&frame),"Cannot decode golden PNG");
    UINT width=0,height=0;check(frame->GetSize(&width,&height),"Cannot read golden PNG dimensions");
    require(width==expected.width && height==expected.height,"Golden PNG dimensions changed");
    WICPixelFormatGUID format{};check(frame->GetPixelFormat(&format),"Cannot read golden PNG pixel format");
    require(IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA),"Golden PNG channel format changed");
    std::vector<BYTE> pixels(expected.bgra.size());
    check(frame->CopyPixels(nullptr,width*4,UINT(pixels.size()),pixels.data()),"Cannot read golden PNG pixels");
    require(pixels==expected.bgra,"Golden PNG changed rendered pixels");
    ComPtr<IWICMetadataQueryReader> metadata;
    check(frame->GetMetadataQueryReader(&metadata),"Cannot read golden PNG metadata");
    PROPVARIANT intent{};
    check(metadata->GetMetadataByName(L"/sRGB/RenderingIntent",&intent),"Golden PNG lost sRGB metadata");
    const bool srgb=intent.vt==VT_UI1 && intent.bVal==0;
    PropVariantClear(&intent);
    require(srgb,"Golden PNG rendering intent changed");
}

void verify_master(const std::filesystem::path& path,const Frame& source) {
    const auto restored=load_master(path);
    require(restored.width==source.width && restored.height==source.height,"Golden HDR master dimensions changed");
    require(restored.pixels.size()==source.pixels.size(),"Golden HDR master is incomplete");
    for(std::size_t i=0;i<source.pixels.size();++i) {
        const auto& actual=restored.pixels[i];const auto& expected=source.pixels[i];
        require(actual.r==half_to_float(float_to_half(expected.r))
                && actual.g==half_to_float(float_to_half(expected.g))
                && actual.b==half_to_float(float_to_half(expected.b)) && actual.a==1,
                "Golden HDR master changed source pixels");
    }
}

void verify_gray(const Image& image) {
    unsigned steps=0;
    for(unsigned x=0;x<image.width;++x) {
        const auto offset=std::size_t(x)*4;
        require(image.bgra[offset]==image.bgra[offset+1] && image.bgra[offset+1]==image.bgra[offset+2],
                "Neutral HDR ramp acquired a tint");
        if(x) {
            require(image.bgra[offset]>=image.bgra[offset-4],"HDR ramp is not monotonic");
            steps+=image.bgra[offset]!=image.bgra[offset-4];
        }
    }
    require(steps>=16,"HDR ramp lost distinguishable levels");
}

int wmain(int argc,wchar_t** argv) try {
    if(argc!=2) throw std::runtime_error("Specify an owned HDR golden evidence directory.");
    Runtime runtime;
    const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
    std::ofstream report(directory/L"measurements.csv"),observed(directory/L"observed-probes.csv");
    report.exceptions(std::ios::badbit|std::ios::failbit);observed.exceptions(std::ios::badbit|std::ios::failbit);
    report.imbue(std::locale::classic());observed.imbue(std::locale::classic());
    report<<"id,mode,width,height,windows_sdr_white_nits,paper_white_nits,render_reference_white_nits,observed_peak_nits,"
              "max_sdr_reference_error_codes,mean_sdr_reference_error_codes\n";
    observed<<"# id,x,y,r,g,b,tolerance\n";
    const std::array<POINT,8> points={{{0,0},{64,32},{64,96},{128,128},{192,160},{255,255},{192,32},{8,224}}};
    std::vector<Image> images;
    std::vector<fixtures::Fixture> scenes;
    for(unsigned scene=0;scene<10;++scene) {
        auto fixture=fixtures::make(scene);
        const auto& frame=fixture.source;
        const auto regenerated=fixtures::make(scene);
        const auto original=frame.pixels;
        require(std::memcmp(original.data(),regenerated.source.pixels.data(),original.size()*sizeof(Pixel))==0,
                "HDR fixture generation is not deterministic");
        const auto image=render_sdr(frame,fixture.settings);
        require(image.width==256 && image.height==256,"Golden render dimensions changed");
        require(image.bgra==render_sdr(frame,fixture.settings).bgra,"Repeated golden render changed output");
        require(difference(image,render_sdr(frame,fixture.settings,false))<=1,
                "Lookup rendering differs from scalar reference by more than one code");
        if(frame.regions.empty()) {
            auto serial=frame;serial.width=2048;serial.height=32;
            require(render_sdr(serial,fixture.settings).bgra==image.bgra,
                    "Serial and parallel HDR rendering disagree");
        }
        for(std::size_t offset=3;offset<image.bgra.size();offset+=4)
            require(image.bgra[offset]==255,"Golden output contains non-opaque pixels");
        if(scene==0 || scene==2 || scene==7) verify_gray(image);
        if(scene==0 || scene==7) require(image.bgra[0]==0,"HDR rendering raised black");
        if(scene==0) require(image.bgra[255*4]==255,"HDR ramp lost peak white");
        if(scene==7) {
            require(image.bgra[255*4]<=26,"Near-black HDR was amplified");
            auto other_white=frame;other_white.sdr_white=80;
            require(image.bgra==render_sdr(other_white,fixture.settings).bgra,
                    "Forced HDR treatment depends on Windows SDR white");
        }
        if(scene==8) require(image.bgra==fixture.sdr_reference.bgra,"Bright native SDR was tone mapped");
        if(scene==9) {
            require(frame.regions.size()==3 && frame.regions[1].sdr_white==320,"Mixed-monitor references changed");
            for(const auto& region:frame.regions) {
                Frame tile;tile.width=region.width;tile.height=region.height;tile.hdr=region.hdr;tile.sdr_white=region.sdr_white;
                for(unsigned y=0;y<region.height;++y) {
                    const auto start=frame.pixels.begin()+std::size_t(region.y+y)*frame.width+region.x;
                    tile.pixels.insert(tile.pixels.end(),start,start+region.width);
                }
                const auto rendered=render_sdr(tile,fixture.settings);
                for(unsigned y=0;y<region.height;++y)
                    require(std::memcmp(image.bgra.data()+(std::size_t(region.y+y)*frame.width+region.x)*4,
                            rendered.bgra.data()+std::size_t(y)*region.width*4,region.width*4)==0,
                            "Combined rendering changed an independent monitor");
            }
        }
        const auto folder=directory/fixture.id;std::filesystem::create_directories(folder);
        save_image(folder/L"current.png",image);verify_png(folder/L"current.png",image);
        if(frame.hdr) {save_master(folder/L"source.jxr",frame);verify_master(folder/L"source.jxr",frame);}
        std::ofstream metadata(folder/L"source.csv");metadata.exceptions(std::ios::badbit|std::ios::failbit);
        metadata.imbue(std::locale::classic());
        metadata<<"provenance,original_synthetic_fixture\nencoding,linear_scRGB_1_equals_80_nits\nquantization,"
                <<(!frame.regions.empty()?"binary16_HDR_and_decoded_sRGB8_SDR":frame.hdr?"binary16":"decoded_sRGB8")
                <<"\nname,"<<fixture.name<<"\nwidth,"<<frame.width<<"\nheight,"<<frame.height
                <<"\nhdr,"<<frame.hdr<<"\nwindows_sdr_white_nits,"<<frame.sdr_white
                <<"\npaper_white_nits,"<<fixture.settings.paper_white<<"\nexposure_stops,0\ndither,0\n"
                  "region_x,region_y,width,height,hdr,sdr_white_nits\n";
        for(const auto& region:frame.regions)
            metadata<<region.x<<','<<region.y<<','<<region.width<<','<<region.height<<','<<region.hdr<<','<<region.sdr_white<<'\n';
        metadata.close();
        for(const auto mode:{ContentMode::Hdr,ContentMode::Auto,ContentMode::Sdr}) {
            auto settings=fixture.settings;settings.mode=mode;
            const auto rendered=mode==ContentMode::Hdr?image:render_sdr(frame,settings);
            const auto error=reference_error(fixture,rendered);
            const char* name=mode==ContentMode::Hdr?"hdr":mode==ContentMode::Auto?"auto":"sdr";
            report<<fixture.id<<','<<name<<','<<frame.width<<','<<frame.height<<','<<frame.sdr_white<<','
                  <<settings.paper_white<<','<<rendered.reference_white<<','<<rendered.source_peak<<','
                  <<error.first<<','<<error.second<<'\n';
            if(fixture.reference_area.width && (mode==ContentMode::Sdr || scene==8
                                               || (scene==9 && mode==ContentMode::Auto)))
                require(error.first<=1,"Known SDR reference changed by more than one code");
            if(mode==ContentMode::Hdr && error.first>1)
                std::cout<<"QUALITY GAP "<<fixture.id<<": SDR reference max error "<<error.first<<" codes (LS-004).\n";
        }
        if(fixture.reference_area.width) save_image(folder/L"sdr-reference.png",fixture.sdr_reference);
        for(const auto point:points) {
            const auto offset=(std::size_t(point.y)*image.width+point.x)*4;
            observed<<fixture.id<<','<<point.x<<','<<point.y<<','<<int(image.bgra[offset+2])<<','
                    <<int(image.bgra[offset+1])<<','<<int(image.bgra[offset])<<','<<(scene==8?0:1)<<'\n';
        }
        require(std::memcmp(original.data(),frame.pixels.data(),original.size()*sizeof(Pixel))==0,
                "Rendering or exporting changed the original HDR fixture");
        images.push_back(image);scenes.push_back(std::move(fixture));
    }
    auto without_outlier=scenes[1].source;
    without_outlier.pixels.back()=fixtures::fp16({1.25,1.25,1.25});
    const auto control=render_sdr(without_outlier,scenes[1].settings);
    const auto center=(std::size_t(128)*256+128)*4;
    save_image(directory/L"HDR-002"/L"without-outlier.png",control);
    std::cout<<"PEAK BASELINE: hot-pixel midtone loss "<<int(control.bgra[center])-int(images[1].bgra[center])
             <<" codes (LS-003).\n";
    report.close();observed.close();
    for(unsigned scene=0;scene<10;++scene) {
        require(std::strcmp(golden_scenes[scene].id,scenes[scene].id)==0,"Golden scene order changed");
        const auto& pixels=images[scene].bgra;
        const int tolerance=scene==8?0:1;
        for(std::size_t sample=0;sample<points.size();++sample) {
            const auto point=points[sample];
            const auto& expected=golden_scenes[scene].colors[sample];
            const auto offset=(std::size_t(point.y)*256+point.x)*4;
            const bool matches=std::abs(int(pixels[offset+2])-expected[0])<=tolerance
                && std::abs(int(pixels[offset+1])-expected[1])<=tolerance
                && std::abs(int(pixels[offset])-expected[2])<=tolerance;
            if(!matches)
                std::cerr<<golden_scenes[scene].id<<" ("<<point.x<<','<<point.y<<") expected RGB "
                         <<expected[0]<<','<<expected[1]<<','<<expected[2]<<" +/- "<<tolerance
                         <<", actual "<<int(pixels[offset+2])<<','<<int(pixels[offset+1])
                         <<','<<int(pixels[offset])<<'\n';
            require(matches,"Golden HDR color sample changed");
        }
    }
    std::cout<<checks<<" HDR golden checks passed across HDR-001 through HDR-010.\n"
             <<"Synthetic characterization only; known quality gaps remain measured, not fixed.\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"HDR golden fixture failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;
}
