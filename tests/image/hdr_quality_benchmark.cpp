#include "hdr_fixtures.hpp"
#include "image/hdr_output.hpp"
#include <iostream>
#include <iterator>

using namespace hdrshot;

Frame detail_frame(bool noise,bool stress=false) {
    Frame frame;frame.width=513;frame.height=257;frame.hdr=true;frame.sdr_white=203;
    frame.pixels.resize(checked_pixels(frame.width,frame.height));
    constexpr unsigned digits[][7]={{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},
        {31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}};
    std::uint32_t state=0x51f15e;
    for(unsigned y=0;y<frame.height;++y) for(unsigned x=0;x<frame.width;++x) {
        Rgb color{};
        if(noise) {
            double channels[3];
            for(double& channel:channels) {
                state=1664525u*state+1013904223u;const double random=double(state>>8)/16777215;
                channel=stress?.02+12.48*random:4+4.0*x/(frame.width-1)+.2*(random-.5);
            }
            color={channels[0],channels[1],channels[2]};
        } else {
            const unsigned scale=1+y/86,column=x/scale%7,row=y/scale%9;
            const bool ink=column<5 && row<7 && (digits[x/(7*scale)%10][row] & (1u<<(4-column)));
            const double value=ink?2.5375:.025;
            color={value,value,value};
            if(x>450) color={12.5,12.5,12.5};
            if(y>220 && x%8<2) color=x%16<8?Rgb{10,.05,.05}:Rgb{.05,.05,10};
        }
        frame.pixels[std::size_t(y)*frame.width+x]=fixtures::fp16(color);
    }
    return frame;
}

struct Decoded {std::vector<Rgb> hdr,sdr;};
Decoded decode(const std::filesystem::path& path,unsigned width,unsigned height) {
    std::ifstream input(path,std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)),{});
    auto* decoder=uhdr_create_decoder();if(!decoder) throw std::runtime_error("Cannot create quality decoder.");
    struct Release {uhdr_codec_private_t* value;~Release(){uhdr_release_decoder(value);}} release{decoder};
    uhdr_compressed_image_t compressed{};compressed.data=bytes.data();compressed.data_sz=compressed.capacity=bytes.size();
    check_hdr_codec(uhdr_dec_set_image(decoder,&compressed),"Cannot read quality sample");
    check_hdr_codec(uhdr_dec_set_out_img_format(decoder,UHDR_IMG_FMT_64bppRGBAHalfFloat),"Cannot select quality HDR output");
    check_hdr_codec(uhdr_dec_set_out_color_transfer(decoder,UHDR_CT_LINEAR),"Cannot select quality linear output");
    check_hdr_codec(uhdr_dec_set_out_max_display_boost(decoder,10000.0f/203),"Cannot select quality headroom");
    check_hdr_codec(uhdr_decode(decoder),"Cannot decode quality HDR");
    const auto* image=uhdr_get_decoded_image(decoder);
    if(!image || image->w!=width || image->h!=height || image->cg!=UHDR_CG_BT_2100)
        throw std::runtime_error("Quality HDR dimensions or gamut changed.");
    Decoded decoded;decoded.hdr.resize(checked_pixels(width,height));decoded.sdr.resize(decoded.hdr.size());
    const auto* pixels=static_cast<const std::uint16_t*>(image->planes[0]);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        const auto offset=(std::size_t(y)*image->stride[0]+x)*4;
        decoded.hdr[std::size_t(y)*width+x]={half_to_float(pixels[offset])*203,
            half_to_float(pixels[offset+1])*203,half_to_float(pixels[offset+2])*203};
    }
    auto factory=imaging_factory();ComPtr<IWICBitmapDecoder> legacy;
    check(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&legacy),"Cannot open quality fallback");
    ComPtr<IWICBitmapFrameDecode> frame;check(legacy->GetFrame(0,&frame),"Cannot read quality fallback");
    ComPtr<IWICColorContext> from,to;check(factory->CreateColorContext(&from),"Cannot create quality source profile");
    UINT contexts=0;IWICColorContext* profile=from.Get();
    check(frame->GetColorContexts(1,&profile,&contexts),"Cannot read quality ICC");
    if(!contexts) throw std::runtime_error("Quality fallback lost ICC.");
    check(factory->CreateColorContext(&to),"Cannot create quality destination profile");
    check(to->InitializeFromExifColorSpace(1),"Cannot select quality sRGB");
    ComPtr<IWICColorTransform> transform;check(factory->CreateColorTransformer(&transform),"Cannot create quality color transform");
    check(transform->Initialize(frame.Get(),from.Get(),to.Get(),GUID_WICPixelFormat32bppBGRA),"Cannot color-manage quality fallback");
    std::vector<BYTE> fallback(decoded.hdr.size()*4);
    check(transform->CopyPixels(nullptr,width*4,UINT(fallback.size()),fallback.data()),"Cannot decode quality fallback");
    for(std::size_t i=0;i<decoded.sdr.size();++i)
        decoded.sdr[i]={srgb_decode(fallback[i*4+2]/255.0),srgb_decode(fallback[i*4+1]/255.0),srgb_decode(fallback[i*4]/255.0)};
    return decoded;
}

struct Metrics {
    double pq_rmse=0,pq_p95=0,pq_max=0,sdr_oklab_mean=0,sdr_oklab_p95=0;
    double highlight_ratio=1,highlight_p95_relative_error=0;
};
Metrics compare(const Decoded& decoded,const Frame& source,const Image& base) {
    Metrics result;std::vector<double> pq_errors,sdr_errors,highlight_errors;
    pq_errors.reserve(source.pixels.size()*3);sdr_errors.reserve(source.pixels.size());
    double highlight_source=0,highlight_decoded=0,pq_squared=0;
    for(std::size_t i=0;i<source.pixels.size();++i) {
        const auto& pixel=source.pixels[i];const auto expected=to_bt2020({pixel.r,pixel.g,pixel.b});
        const double channels[]={std::clamp(expected.r*80,0.0,10000.0),std::clamp(expected.g*80,0.0,10000.0),std::clamp(expected.b*80,0.0,10000.0)};
        const double actual[]={decoded.hdr[i].r,decoded.hdr[i].g,decoded.hdr[i].b};
        for(unsigned channel=0;channel<3;++channel) {
            const double error=std::abs(pq_encode(std::clamp(actual[channel],0.0,10000.0))-pq_encode(channels[channel]));
            pq_errors.push_back(error);pq_squared+=error*error;result.pq_max=std::max(result.pq_max,error);
        }
        const double luminance=.2627*channels[0]+.6780*channels[1]+.0593*channels[2];
        if(luminance>=400) {
            const double reconstructed=.2627*actual[0]+.6780*actual[1]+.0593*actual[2];
            highlight_source+=luminance;highlight_decoded+=reconstructed;
            highlight_errors.push_back(std::abs(reconstructed-luminance)/luminance);
        }
        const auto target=to_oklab({srgb_decode(base.bgra[i*4+2]/255.0),srgb_decode(base.bgra[i*4+1]/255.0),srgb_decode(base.bgra[i*4]/255.0)});
        const auto fallback=to_oklab(decoded.sdr[i]);
        const double distance=100*std::sqrt(std::pow(target.l-fallback.l,2)+std::pow(target.a-fallback.a,2)+std::pow(target.b-fallback.b,2));
        sdr_errors.push_back(distance);result.sdr_oklab_mean+=distance;
    }
    result.pq_rmse=std::sqrt(pq_squared/pq_errors.size());
    result.pq_p95=diagnostic_quantile(std::move(pq_errors),.95);
    result.sdr_oklab_mean/=sdr_errors.size();result.sdr_oklab_p95=diagnostic_quantile(std::move(sdr_errors),.95);
    if(highlight_source) result.highlight_ratio=highlight_decoded/highlight_source;
    if(!highlight_errors.empty()) result.highlight_p95_relative_error=diagnostic_quantile(std::move(highlight_errors),.95);
    return result;
}

int wmain(int argc,wchar_t** argv) try {
    if(argc==2 && wcscmp(argv[1],L"--help")==0) {
        std::cout<<"Usage: hdr_quality_benchmark.exe <new-evidence-directory> [<source.jxr> <recorded-sdr-white> ...]\n"
                    "Compare full-resolution, multichannel Ultra HDR JPEG qualities 100, 95, 90 and 85.\n"
                    "Sources are synthetic unless explicit local JPEG XR masters are supplied.\n"
                    "Optional display white is recorded capture metadata, not inferred game calibration.\n"
                    "HDR metrics compare clamped BT.2020 nits using PQ; SDR uses 100-times Oklab distance.\n"
                    "Each encode is measured once; timings do not establish a performance claim.\n";return 0;
    }
    if(argc<2 || (argc-2)%2) throw std::runtime_error("Specify new evidence directory and optional source/white pairs.");
    Runtime runtime;const std::filesystem::path directory(argv[1]);
    if(!std::filesystem::create_directory(directory)) throw std::runtime_error("Refusing to reuse a quality evidence directory.");
    std::ofstream report(directory/L"comparison.json");report<<std::fixed<<std::setprecision(7);
    report<<"{\"release\":\"" LUMENSHOT_VERSION "\",\"gainmap_scale\":1,\"gainmap_channels\":3,\"quality_levels\":[100,95,90,85],\"cases\":[";
    struct Case {std::string name;Frame frame;};std::vector<Case> cases;
    for(unsigned scene:{0u,1u,3u,4u,7u}) {auto fixture=fixtures::make(scene);cases.push_back({fixture.name,std::move(fixture.source)});}
    cases.push_back({"fine-text",detail_frame(false)});cases.push_back({"noise",detail_frame(true)});
    cases.push_back({"independent-channel-noise-stress",detail_frame(true,true)});
    for(int argument=2;argument<argc;argument+=2) {
        std::size_t used=0;const std::wstring white(argv[argument+1]);const double value=std::stod(white,&used);
        if(used!=white.size() || !std::isfinite(value) || value<40 || value>2000) throw std::runtime_error("Invalid recorded display white.");
        auto frame=load_master(argv[argument]);frame.sdr_white=value;
        cases.push_back({"local-master-"+std::to_string((argument-2)/2+1),std::move(frame)});
    }
    const char* names[]={"full","high","balanced","compact"};bool case_separator=false;unsigned failures=0;
    for(const auto& sample:cases) {
        ColorSettings settings;settings.mode=ContentMode::Auto;settings.exposure=.1;
        const auto base=render_sdr(sample.frame,settings);
        save_image(directory/(sample.name+"-reference.png"),base);
        if(case_separator) report<<',';
        case_separator=true;report<<"{\"name\":\""<<sample.name<<"\",\"width\":"<<sample.frame.width<<",\"height\":"<<sample.frame.height
            <<",\"recorded_sdr_white\":"<<sample.frame.sdr_white<<",\"outputs\":[";
        std::uintmax_t full_bytes=0;
        for(unsigned index=0;index<4;++index) {
            const auto path=directory/(sample.name+"-"+names[index]+".jpg");
            if(index) report<<',';
            const auto start=std::chrono::steady_clock::now();
            try {save_hdr_jpeg(path,sample.frame,base,HdrJpegQuality(index));}
            catch(const std::exception& error) {
                ++failures;report<<"{\"preset\":\""<<names[index]<<"\",\"error\":"<<std::quoted(error.what())<<'}';report.flush();
                std::cout<<sample.name<<' '<<names[index]<<": FAILED: "<<error.what()<<std::endl;continue;
            }
            const double milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            const auto bytes=std::filesystem::file_size(path);if(index==0) full_bytes=bytes;
            const auto metrics=compare(decode(path,sample.frame.width,sample.frame.height),sample.frame,base);
            report<<"{\"preset\":\""<<names[index]<<"\",\"bytes\":"<<bytes<<",\"size_fraction\":";
            if(full_bytes) report<<double(bytes)/full_bytes;else report<<"null";
            report<<",\"single_encode_ms\":"<<milliseconds<<",\"hdr_pq_rmse\":"<<metrics.pq_rmse<<",\"hdr_pq_p95\":"<<metrics.pq_p95
                <<",\"hdr_pq_max\":"<<metrics.pq_max<<",\"sdr_oklab_mean_x100\":"<<metrics.sdr_oklab_mean
                <<",\"sdr_oklab_p95_x100\":"<<metrics.sdr_oklab_p95<<",\"highlight_mean_ratio\":"<<metrics.highlight_ratio
                <<",\"highlight_p95_relative_error\":"<<metrics.highlight_p95_relative_error<<'}';report.flush();
            std::cout<<sample.name<<' '<<names[index]<<": "<<bytes<<" bytes, PQ RMSE "<<metrics.pq_rmse
                     <<", SDR Oklab p95 "<<metrics.sdr_oklab_p95<<", highlight ratio "<<metrics.highlight_ratio<<std::endl;
        }
        report<<"]}";
    }
    report<<"],\"failed_outputs\":"<<failures<<"}\n";report.close();
    if(!report) throw std::runtime_error("Cannot write quality report.");
    return 0;
} catch(const std::exception& error) {std::cerr<<"HDR quality benchmark failed: "<<error.what()<<'\n';return 1;}
