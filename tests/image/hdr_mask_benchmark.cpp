#include "hdr_fixtures.hpp"
#include "image/image_io.hpp"
#include <iostream>

using namespace hdrshot;

int wmain(int argc,wchar_t** argv) try {
    if(argc<2 || argc>3) throw std::runtime_error("Usage: hdr_mask_benchmark.exe <new-report.json> [samples]");
    unsigned samples=30;
    if(argc==3) {
        std::size_t used=0;const auto value=std::stoul(argv[2],&used);
        if(used!=wcslen(argv[2]) || value<1 || value>1000) throw std::runtime_error("Samples must be between 1 and 1000.");
        samples=unsigned(value);
    }
    Runtime runtime;const auto path=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(path.parent_path());
    OutputTransaction transaction(path);std::ofstream output(transaction.path());output.exceptions(std::ios::badbit|std::ios::failbit);
    output.imbue(std::locale::classic());output<<std::fixed<<std::setprecision(6);
    output<<"{\"provenance\":\"synthetic\",\"render_only\":true,\"diagnostics_enabled\":false,"
              "\"warmup_pairs\":3,\"measured_pairs\":"<<samples<<",\"alternating_pair_order\":true,\"cases\":[";
    const struct {const char* name;unsigned scene,width,height;} cases[]={
        {"sdr_1080p",8,1920,1080},{"hdr_1440p",0,2560,1440},{"mixed_1440p",4,2560,1440},
        {"neon_hdr_4k",3,3840,2160},{"elevated_white_4k",5,3840,2160}};
    bool separator=false;unsigned checksum=0;
    for(const auto& definition:cases) {
        const auto fixture=fixtures::make(definition.scene);Frame frame=fixture.source;
        frame.width=definition.width;frame.height=definition.height;frame.pixels.resize(checked_pixels(frame.width,frame.height));
        for(unsigned y=0;y<frame.height;++y) for(unsigned x=0;x<frame.width;++x) {
            Pixel pixel=fixture.source.pixels[std::size_t(y*256/frame.height)*256+x*256/frame.width];
            if(definition.scene==0 || definition.scene==4 || definition.scene==5) {
                const double nits=2000*double(x)/(frame.width-1);pixel=fixtures::fp16({nits/80,nits/80,nits/80});
                if(definition.scene!=0 && x>=frame.width/8 && x<frame.width*7/8 && y>=frame.height/4 && y<frame.height*3/4)
                    pixel=fixtures::ui_pixel(fixtures::ui_codes(x,y),frame.sdr_white,true);
            }
            frame.pixels[std::size_t(y)*frame.width+x]=pixel;
        }
        std::array<std::vector<double>,2> timings;
        for(unsigned iteration=0;iteration<samples+3;++iteration) for(unsigned order=0;order<2;++order) {
            const unsigned method=(iteration+order)%2;ColorSettings settings{ContentMode::Auto,203,0,false};settings.content_aware=method!=0;
            const auto start=std::chrono::steady_clock::now();const auto image=render_sdr(frame,settings);
            const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            checksum^=image.bgra.front()^image.bgra[image.bgra.size()/2]^image.bgra.back();
            if(iteration>=3) timings[method].push_back(elapsed);
        }
        double delta=0;for(unsigned n=0;n<samples;++n) delta+=timings[1][n]-timings[0][n];delta/=samples;
        if(separator) output<<',';
        separator=true;output<<"{\"name\":\""<<definition.name<<"\",\"width\":"<<frame.width<<",\"height\":"<<frame.height
            <<",\"mean_paired_delta_ms\":"<<delta;
        for(unsigned method=0;method<2;++method) {
            output<<(method?",\"content_aware\":{":",\"region_uniform\":{")<<"\"p50_ms\":"<<diagnostic_quantile(timings[method],.5)
                  <<",\"p95_ms\":"<<diagnostic_quantile(timings[method],.95)<<",\"runs_ms\":[";
            for(unsigned n=0;n<samples;++n) {if(n) output<<',';output<<timings[method][n];}
            output<<"]}";
        }
        output<<'}';std::cout<<definition.name<<": region-uniform p50 "<<diagnostic_quantile(timings[0],.5)
            <<" ms, content-aware p50 "<<diagnostic_quantile(timings[1],.5)<<" ms, paired mean "<<delta<<" ms."<<std::endl;
    }
    output<<"],\"checksum\":"<<checksum<<"}\n";output.close();transaction.commit();return 0;
} catch(const std::exception& error) {std::cerr<<"HDR mask benchmark failed: "<<error.what()<<'\n';return 1;}
