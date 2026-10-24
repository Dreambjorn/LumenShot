#include "app/diagnostics.hpp"
#include "image/image_io.hpp"
#include "platform/work.hpp"
#include <iostream>

using namespace hdrshot;
static unsigned checks=0;
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}

int wmain(int argc,wchar_t** argv) try {
    if(argc!=2) throw std::runtime_error("Specify an owned diagnostics evidence directory.");
    std::filesystem::create_directories(argv[1]);
    Runtime runtime;
    require(!active_diagnostics,"Diagnostics must start disabled");
    require(save_capture_diagnostics(nullptr,true) && !std::filesystem::exists(executable_directory()/L"Diagnostics"),
            "Disabled diagnostics touched the filesystem");
    {DiagnosticTimer disabled(DiagnosticStage::Preview);diagnostic_count(DiagnosticCounter::DeviceReused);}
    require(!active_diagnostics,"Disabled timing enabled diagnostics");
    auto record=std::make_shared<DiagnosticRecord>(DiagnosticSource::Synthetic);
    {
        DiagnosticContext context(record);
        DiagnosticTimer outer(DiagnosticStage::Preview);
        NativeWork worker;
        worker.start([&] {
            require(active_diagnostics==record,"Worker lost diagnostic context");
            DiagnosticTimer timer(DiagnosticStage::ColorAnalysis);
            diagnostic_count(DiagnosticCounter::DeviceReused);
        });
        worker.join();
        try {DiagnosticTimer failed(DiagnosticStage::CaptureDesktop);throw PlatformError(E_ACCESSDENIED,"Private title and C:\\private\\file.jxr");}
        catch(const PlatformError&) {}
        {
            DiagnosticContext disabled(nullptr);
            require(!active_diagnostics,"Paused diagnostics remained active");
            DiagnosticTimer ignored(DiagnosticStage::EncodeJxr);
        }
        require(active_diagnostics==record,"Nested context did not restore its owner");
        diagnostic_paint();diagnostic_paint();
    }
    require(!active_diagnostics,"Context escaped to later work");
    record->dimensions(256,128,true,2);record->monitors(2);
    record->frame({256,128,true,203,10,DiagnosticBackend::Windows});
    record->finish(false);
    const auto snapshot=record->snapshot();
    require(snapshot.stages[std::size_t(DiagnosticStage::Preview)].calls==1,"Outer timing was lost");
    require(snapshot.stages[std::size_t(DiagnosticStage::ColorAnalysis)].calls==1,"Worker timing was lost");
    require(snapshot.stages[std::size_t(DiagnosticStage::CaptureDesktop)].failures==1,"Exception was not timed");
    require(snapshot.stages[std::size_t(DiagnosticStage::EncodeJxr)].calls==0,"Paused timing was recorded");
    require(snapshot.issues.size()==1 && snapshot.issues[0].hresult==std::uint32_t(E_ACCESSDENIED),"HRESULT was lost");
    require(snapshot.selector_first_paint_ms>=0 && snapshot.selector_first_paint_ms<=snapshot.total_ms,"First paint endpoint is invalid");
    require(snapshot.counters[std::size_t(DiagnosticCounter::DeviceReused)]==1,"Worker counter was lost");
    const auto json=diagnostic_json(snapshot);
    require(json.find("Private title")==std::string::npos && json.find("private")==std::string::npos,"Diagnostics exposed exception content");
    require(json.find("\"source\":\"synthetic\"")!=std::string::npos && json.find("\"outcome\":\"failed\"")!=std::string::npos,"Diagnostic provenance is missing");
    require(diagnostic_quantile({5,1,3,2,4},.5)==3,"Median is incorrect");
    require(std::abs(diagnostic_quantile({5,1,3,2,4},.95)-4.8)<1e-12,"P95 interpolation is incorrect");
    require(diagnostic_quantile({7},.95)==7,"One-sample quantile is incorrect");
    for(const auto& values:{std::vector<double>{},std::vector<double>{-1},std::vector<double>{NAN}}) {
        bool rejected=false;try {diagnostic_quantile(values,.5);} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected,"Invalid samples were accepted");
    }
    auto bounded=std::make_shared<DiagnosticRecord>(DiagnosticSource::Live);
    for(unsigned i=0;i<70;++i) bounded->issue(DiagnosticStage::CaptureWindows,i);
    require(bounded->snapshot().issues.size()==64 && bounded->snapshot().dropped==6,"Diagnostic event storage is unbounded");
    require(save_capture_diagnostics(record,false),"Cannot save owned diagnostic report");
    const auto reports=executable_directory()/L"Diagnostics";
    unsigned files=0;
    for(const auto& entry:std::filesystem::directory_iterator(reports)) {
        require(entry.path().extension()==L".json","Unfinished diagnostic output remains");++files;
    }
    require(files==1,"Diagnostic report count is incorrect");
    require(save_capture_diagnostics(nullptr,true),"Disabled diagnostics failed");
    std::cout<<checks<<" diagnostics checks passed: disabled mode, workers, failures, privacy, endpoints, quantiles, bounded events, and atomic reports.\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"Diagnostics fixture failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
