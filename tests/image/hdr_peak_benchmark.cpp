#include "hdr_fixtures.hpp"
#include "image/image_io.hpp"
#include <iostream>

using namespace hdrshot;

Frame input_frame(unsigned scene,unsigned width,unsigned height) {
    const auto fixture=fixtures::make(scene);Frame frame=fixture.source;
    frame.width=width;frame.height=height;frame.pixels.resize(checked_pixels(width,height));
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x)
        frame.pixels[std::size_t(y)*width+x]=fixture.source.pixels[std::size_t(y*256/height)*256+x*256/width];
    if(scene==9) frame.regions={{0,0,width/2,height,true,203},{width/2,0,width/2,height/2,true,320},
                                {width/2,height/2,width/2,height/2,false,80}};
    return frame;
}
double metric(const std::vector<DiagnosticSnapshot>& samples,bool analysis,double percentile) {
    std::vector<double> values;
    for(const auto& sample:samples)
        values.push_back(analysis?sample.stages[std::size_t(DiagnosticStage::ColorAnalysis)].sum_ms:sample.total_ms);
    return diagnostic_quantile(std::move(values),percentile);
}
void mode_report(std::ostream& output,const std::vector<DiagnosticSnapshot>& samples) {
    output<<"{\"p50_ms\":"<<metric(samples,false,.5)<<",\"p95_ms\":"<<metric(samples,false,.95)
          <<",\"analysis_p50_ms\":";
    if(samples.front().stages[std::size_t(DiagnosticStage::ColorAnalysis)].calls)
        output<<metric(samples,true,.5)<<",\"analysis_p95_ms\":"<<metric(samples,true,.95);
    else output<<"null,\"analysis_p95_ms\":null";
    output<<",\"runs\":[";
    for(std::size_t i=0;i<samples.size();++i) {if(i) output<<',';output<<diagnostic_json(samples[i]);}
    output<<"]}";
}

int wmain(int argc,wchar_t** argv) try {
    if(argc==2 && wcscmp(argv[1],L"--help")==0) {
        std::cout<<"Usage: hdr_peak_benchmark.exe <new-report.json> [samples [--no-diagnostics]]\n"
                 <<"Compare observed and robust peaks on the same owned synthetic frames.\n"
                 <<"Default: 30 measured pairs after three warmup pairs per case; allowed range 1 to 1000.\n"
                 <<"--no-diagnostics measures the normal path without active stage collection.\n"
                 <<"Pair order alternates. Timings cover Automatic SDR rendering and analysis only.\n"
                 <<"Source allocation, image encoding, capture, and physical display latency are excluded.\n"
                 <<"Reports preserve per-run provenance, p50/p95, and paired mean differences.\n"
                 <<"Existing reports are never overwritten; no desktop content is read.\n";
        return 0;
    }
    if(argc<2 || argc>4) throw std::runtime_error("Specify a new benchmark JSON path and optional sample count.");
    unsigned count=30;
    if(argc>=3) {
        std::size_t used=0;const auto parsed=std::stoul(argv[2],&used);
        if(used!=wcslen(argv[2]) || parsed<1 || parsed>1000) throw std::runtime_error("Samples must be between 1 and 1000.");
        count=unsigned(parsed);
    }
    const bool diagnostics=argc<4;
    if(!diagnostics && wcscmp(argv[3],L"--no-diagnostics")) throw std::runtime_error("Unknown benchmark option.");
    Runtime runtime;const auto path=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(path.parent_path());
    OutputTransaction transaction(path);std::ofstream output(transaction.path(),std::ios::binary|std::ios::trunc);
    output.exceptions(std::ios::badbit|std::ios::failbit);output.imbue(std::locale::classic());output<<std::fixed<<std::setprecision(6);
    output<<"{\"release\":\"" LUMENSHOT_VERSION "\",\"provenance\":\"synthetic\",\"render_sdr_only\":true,\"content_mode\":\"automatic\","
            "\"warmup_pairs\":3,\"diagnostics_enabled\":"<<(diagnostics?"true":"false")<<",\"measured_pairs\":"<<count<<",\"alternating_pair_order\":true,\"cases\":[";
    const struct {const char* name;unsigned scene,width,height;} cases[]={
        {"sdr_1080p",8,1920,1080},{"hdr_1440p",0,2560,1440},{"sparse_1440p",1,2560,1440},
        {"hdr_4k",3,3840,2160},{"mixed_monitors",9,3840,1440}};
    bool separator=false;unsigned checksum=0;
    for(const auto& definition:cases) {
        const auto frame=input_frame(definition.scene,definition.width,definition.height);
        std::array<std::vector<DiagnosticSnapshot>,2> samples;
        for(unsigned iteration=0;iteration<count+3;++iteration) for(unsigned order=0;order<2;++order) {
            const unsigned method=(iteration+order)%2;
            ColorSettings settings{ContentMode::Auto,203,0,false};settings.peak_method=method?PeakMethod::Robust:PeakMethod::Observed;
            auto record=std::make_shared<DiagnosticRecord>(DiagnosticSource::Synthetic);Image image;
            {DiagnosticContext context(diagnostics?record:nullptr);image=render_sdr(frame,settings);}
            record->finish(true);record->dimensions(frame.width,frame.height,frame.hdr,unsigned(frame.regions.size()));checksum^=image.bgra.front()^image.bgra[image.bgra.size()/2]^image.bgra.back();
            if(iteration>=3) samples[method].push_back(record->snapshot());
        }
        double paired_difference=0;
        for(unsigned i=0;i<count;++i) paired_difference+=samples[1][i].total_ms-samples[0][i].total_ms;
        if(separator) output<<',';
        separator=true;output<<"{\"name\":\""<<definition.name<<"\",\"width\":"<<frame.width<<",\"height\":"<<frame.height
            <<",\"mean_paired_delta_ms\":"<<paired_difference/count<<",\"observed\":";
        mode_report(output,samples[0]);output<<",\"robust\":";mode_report(output,samples[1]);output<<'}';
        std::cout<<definition.name<<": observed p50 "<<metric(samples[0],false,.5)<<" ms, robust p50 "<<metric(samples[1],false,.5)
                 <<" ms, mean paired delta "<<paired_difference/count<<" ms."<<std::endl;
    }
    output<<"],\"checksum\":"<<checksum<<"}\n";output.close();transaction.commit();return 0;
} catch(const std::exception& error) {std::cerr<<"HDR peak benchmark failed: "<<error.what()<<'\n';return 1;}
