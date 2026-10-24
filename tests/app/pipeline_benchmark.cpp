#include "app/workflow.hpp"
#include "ui/selector.hpp"
#include "../image/hdr_fixtures.hpp"
#include <iostream>

using namespace hdrshot;

struct BenchmarkInput { const char* name;Frame frame;Image native; };
BenchmarkInput benchmark_input(const char* name,unsigned scene,unsigned width,unsigned height) {
    const auto fixture=fixtures::make(scene);
    BenchmarkInput input{name,{}, {}};
    input.frame=fixture.source;input.frame.width=width;input.frame.height=height;
    input.frame.pixels.resize(checked_pixels(width,height));
    if(!input.frame.hdr) {input.native.width=width;input.native.height=height;input.native.bgra.resize(std::size_t(width)*height*4);input.native.description=L"SDR preserved";}
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        const unsigned sx=x*256/width,sy=y*256/height;
        input.frame.pixels[std::size_t(y)*width+x]=fixture.source.pixels[sy*256+sx];
        if(!input.native.bgra.empty()) std::copy_n(fixture.sdr_reference.bgra.data()+(sy*256+sx)*4,4,input.native.bgra.data()+(std::size_t(y)*width+x)*4);
    }
    if(!input.native.bgra.empty()) input.frame.pixels.clear();
    if(scene==9) input.frame.regions={{0,0,width/2,height,true,203},{width/2,0,width/2,height/2,true,320},
                                     {width/2,height/2,width/2,height/2,false,80}};
    return input;
}

void summary(std::ostream& out,const char* name,const std::vector<DiagnosticSnapshot>& samples,
             const DiagnosticSnapshot& first,std::uintmax_t bytes) {
    std::vector<double> totals;
    unsigned failures=0;
    for(const auto& sample:samples) {if(sample.success) totals.push_back(sample.total_ms);else ++failures;}
    out<<"{\"name\":\""<<name<<"\",\"samples\":"<<samples.size()<<",\"successful_samples\":"<<totals.size()
       <<",\"failed_samples\":"<<failures<<",\"first_sample\":"<<diagnostic_json(first)<<",\"output_bytes\":"<<bytes
       <<",\"p50_ms\":";
    if(totals.empty()) out<<"null,\"p95_ms\":null";
    else out<<diagnostic_quantile(totals,.5)<<",\"p95_ms\":"<<diagnostic_quantile(totals,.95);
    out<<",\"stages\":{";
    bool separator=false;
    for(std::size_t stage=0;stage<diagnostic_stage_count;++stage) {
        std::vector<double> values;
        for(const auto& sample:samples) if(sample.success && sample.stages[stage].calls) values.push_back(sample.stages[stage].sum_ms);
        if(values.empty()) continue;
        if(separator) out<<',';
        separator=true;
        out<<'"'<<diagnostic_stage_names[stage]<<"\":{\"p50_sum_ms\":"<<diagnostic_quantile(values,.5)
           <<",\"p95_sum_ms\":"<<diagnostic_quantile(values,.95)<<'}';
    }
    out<<"},\"runs\":[";
    for(std::size_t i=0;i<samples.size();++i) {if(i) out<<',';out<<diagnostic_json(samples[i]);}
    out<<"]}";
}

int wmain(int argc,wchar_t** argv) try {
    if(argc==2 && wcscmp(argv[1],L"--help")==0) {
        std::cout<<"Usage: pipeline_benchmark.exe <new-report.json> [samples [--live]]\n"
                 <<"Default: 30 measured samples per case after three warmups; samples must be 1 through 1000.\n"
                 <<"Synthetic cases: 1080p SDR, 1440p HDR, 4K HDR, and mixed monitors.\n"
                 <<"Synthetic totals include preview, one arrow, PNG, and both HDR encoders for HDR cases.\n"
                 <<"--live adds current desktop capture and preview in memory; no desktop image is written.\n"
                 <<"Reports contain separate provenance and p50/p95 with linear interpolation.\n"
                 <<"Input allocation is excluded. Stage times are inclusive and parallel work may overlap.\n"
                 <<"Existing reports are preserved; temporary synthetic images are removed.\n";
        return 0;
    }
    if(argc<2 || argc>4) throw std::runtime_error("Usage: pipeline_benchmark.exe <report.json> [samples [--live]]");
    unsigned count=30;
    if(argc>=3) {std::size_t used=0;const auto parsed=std::stoul(argv[2],&used);if(used!=wcslen(argv[2]) || parsed<1 || parsed>1000) throw std::runtime_error("Samples must be between 1 and 1000.");count=unsigned(parsed);}
    const bool live=argc==4 && wcscmp(argv[3],L"--live")==0;
    if(argc==4 && !live) throw std::runtime_error("Unknown benchmark option.");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Runtime runtime;
    const auto destination=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(destination.parent_path());
    OutputTransaction transaction(destination);
    std::ofstream output(transaction.path(),std::ios::binary|std::ios::trunc);
    output.exceptions(std::ios::badbit|std::ios::failbit);output.imbue(std::locale::classic());output<<std::fixed<<std::setprecision(6);
    const auto temporary=destination.parent_path()/(L"benchmark-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    if(!std::filesystem::create_directory(temporary)) throw std::runtime_error("Refusing to reuse benchmark data.");
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() {std::error_code error;for(const auto name:{L"sample.png",L"sample.jpg",L"sample.jxr"}) std::filesystem::remove(directory/name,error);std::filesystem::remove(directory,error);}
    } cleanup{temporary};
    output<<"{\"release\":\"" LUMENSHOT_VERSION "\",\"warmup_samples\":3,\"measured_samples\":"<<count
          <<",\"quantiles\":\"linear_interpolation\",\"input_allocation_excluded\":true,\"cases\":[";
    const struct {const char* name;unsigned scene,width,height;} cases[]={
        {"sdr_1080p",8,1920,1080},{"hdr_1440p",0,2560,1440},{"hdr_4k",3,3840,2160},{"mixed_monitors",9,3840,1440}};
    bool separator=false;
    for(const auto& definition:cases) {
        const auto input=benchmark_input(definition.name,definition.scene,definition.width,definition.height);
        std::vector<DiagnosticSnapshot> samples;DiagnosticSnapshot first;std::uintmax_t bytes=0;
        for(unsigned iteration=0;iteration<count+3;++iteration) {
            WorkResult work;work.frame=input.frame;Image native=input.native;
            auto record=std::make_shared<DiagnosticRecord>(DiagnosticSource::Synthetic);
            {
                DiagnosticContext context(record);
                Settings settings;prepare_preview(work,settings,std::move(native));
                const Image base=work.preview;
                Annotation arrow;arrow.tool=MarkTool::Arrow;arrow.points={{20,20},{LONG(work.preview.width/2),LONG(work.preview.height/2)}};
                annotate(work.preview,{arrow},{});
                save_image(temporary/L"sample.png",work.preview);
                bytes=std::filesystem::file_size(temporary/L"sample.png");
                if(work.frame.hdr) {
                    save_master(temporary/L"sample.jxr",work.frame);
                    save_hdr_jpeg(temporary/L"sample.jpg",work.frame,base);
                    bytes+=std::filesystem::file_size(temporary/L"sample.jxr")+std::filesystem::file_size(temporary/L"sample.jpg");
                }
            }
            record->finish(true);const auto sample=record->snapshot();if(iteration==0) first=sample;
            if(iteration>=3) samples.push_back(sample);
            for(const auto name:{L"sample.png",L"sample.jpg",L"sample.jxr"}) std::filesystem::remove(temporary/name);
        }
        if(separator) output<<',';
        separator=true;summary(output,input.name,samples,first,bytes);
        std::cout<<input.name<<": "<<count<<" synthetic samples; p50 "<<diagnostic_quantile([&]{std::vector<double> values;for(const auto& s:samples) values.push_back(s.total_ms);return values;}(),.5)<<" ms.\n";
    }
    if(live) {
        std::vector<DiagnosticSnapshot> samples;DiagnosticSnapshot first;
        for(unsigned iteration=0;iteration<count+3;++iteration) {
            auto record=std::make_shared<DiagnosticRecord>(DiagnosticSource::Live);bool success=false;
            {
                DiagnosticContext context(record);
                try {
                    const auto displays=monitors();auto input=capture_desktop(displays,CaptureBackend::Auto);
                    WorkResult work;work.frame=std::move(input.frame);Settings settings;prepare_preview(work,settings,std::move(input.sdr));success=true;
                } catch(const std::exception&) {}
            }
            record->finish(success);const auto sample=record->snapshot();if(iteration==0) first=sample;
            if(iteration>=3) samples.push_back(sample);
        }
        output<<',';summary(output,"live_capture_memory_only",samples,first,0);
        std::cout<<"Live capture measured in memory; no desktop images were written.\n";
    }
    output<<"]}\n";output.close();transaction.commit();
    return 0;
} catch(const std::exception& error) {std::cerr<<"Benchmark failed: "<<error.what()<<'\n';return 1;}
