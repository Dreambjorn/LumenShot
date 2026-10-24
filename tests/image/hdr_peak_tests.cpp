#include "hdr_fixtures.hpp"
#include "image/image_io.hpp"
#include <iostream>

using namespace hdrshot;
static unsigned checks=0;
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
template<class F> void rejects(F function,const char* message) {
    bool failed=false;try {function();} catch(const std::exception&) {failed=true;}require(failed,message);
}
int difference(const Image& a,const Image& b) {
    require(a.width==b.width && a.height==b.height,"Comparison dimensions differ");int result=0;
    for(std::size_t i=0;i<a.bgra.size();++i) result=std::max(result,std::abs(int(a.bgra[i])-int(b.bgra[i])));
    return result;
}
std::pair<Image,DiagnosticSnapshot> measured(const Frame& frame,ColorSettings settings,PeakMethod method,bool lookup=true) {
    settings.peak_method=method;auto record=std::make_shared<DiagnosticRecord>(DiagnosticSource::Synthetic);
    Image image;{DiagnosticContext context(record);image=render_sdr(frame,settings,lookup);}
    record->finish(true);return {std::move(image),record->snapshot()};
}
Image comparison(const Image& before,const Image& after) {
    Image result;result.width=before.width+after.width;result.height=before.height;
    result.bgra.resize(checked_pixels(result.width,result.height)*4);
    for(unsigned y=0;y<result.height;++y) {
        auto* row=result.bgra.data()+std::size_t(y)*result.width*4;
        std::copy_n(before.bgra.data()+std::size_t(y)*before.width*4,before.width*4,row);
        std::copy_n(after.bgra.data()+std::size_t(y)*after.width*4,after.width*4,row+before.width*4);
    }
    return result;
}

int wmain(int argc,wchar_t** argv) try {
    if(argc!=2) throw std::runtime_error("Specify an owned HDR peak evidence directory.");
    Runtime runtime;const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
    HdrPeakHistogram distribution;
    for(unsigned i=0;i<10000;++i) distribution.add(i<9900?300:2000,203);
    const auto p99=distribution.measure(203,990),p995=distribution.measure(203,995),p999=distribution.measure(203,999);
    require(p99.samples==10000 && p99.hdr_samples==10000,"Histogram sample counts changed");
    require(p99.observed_nits==2000 && p99.effective_nits>=315 && p99.effective_nits<325,"P99 does not select the supported lower peak");
    require(p995.effective_nits==2000 && p999.effective_nits==2000,"Higher percentiles lost supported highlights");
    HdrPeakHistogram left,right;
    for(unsigned i=0;i<10000;++i) (i%2?left:right).add(i<9900?300:2000,203);
    left.merge(right);require(left.measure(203,990).effective_nits==p99.effective_nits,"Histogram merge depends on partitioning");
    rejects([&]{distribution.measure(203,0);},"Zero percentile was accepted");
    rejects([&]{distribution.measure(203,1001);},"Invalid percentile was accepted");
    rejects([&]{distribution.measure(0);},"Invalid reference white was accepted");
    HdrPeakHistogram invalid;invalid.add(NAN,203);
    rejects([&]{invalid.measure(203);},"Nonfinite histogram input was accepted");
    HdrPeakHistogram empty;require(empty.measure(203).reason==PeakReason::Empty,"Empty histogram invented headroom");
    HdrPeakHistogram negative;for(unsigned i=0;i<2048;++i) negative.add(-1,203);
    require(negative.measure(203).effective_nits==0,"Negative colors raised black");
    std::ofstream report(directory/L"comparison.json");report.exceptions(std::ios::badbit|std::ios::failbit);
    report<<"{\"provenance\":\"synthetic\",\"comparisons\":[";
    for(unsigned scene=0;scene<10;++scene) {
        const auto fixture=fixtures::make(scene);const auto original=fixture.source.pixels;
        const auto before=measured(fixture.source,fixture.settings,PeakMethod::Observed);
        const auto after=measured(fixture.source,fixture.settings,PeakMethod::Robust);
        require(after.first.bgra==measured(fixture.source,fixture.settings,PeakMethod::Robust).first.bgra,"Robust rendering is not repeatable");
        require(difference(after.first,measured(fixture.source,fixture.settings,PeakMethod::Robust,false).first)<=1,"Robust lookup differs from scalar reference");
        if(fixture.source.regions.empty()) {
            auto serial=fixture.source;serial.width=2048;serial.height=32;
            require(after.first.bgra==measured(serial,fixture.settings,PeakMethod::Robust).first.bgra,"Worker count changed robust rendering");
        }
        require(after.first.source_peak==before.first.source_peak,"Peak selection changed reported source luminance");
        auto current=fixture.settings;current.peak_method=ColorSettings{}.peak_method;
        require(render_sdr(fixture.source,current).bgra==after.first.bgra,"Default renderer does not use the compared method");
        require(after.first.tone_mapped==before.first.tone_mapped,"Peak selection changed source-headroom reporting");
        if(scene!=1) require(difference(before.first,after.first)<=1,"Robust peak changed a broad reference scene");
        if(scene==0 || scene==2 || scene==7) {
            unsigned levels=0;
            for(unsigned x=0;x<256;++x) {
                const auto offset=x*4;
                require(after.first.bgra[offset]==after.first.bgra[offset+1] && after.first.bgra[offset+1]==after.first.bgra[offset+2],"Robust ramp acquired a tint");
                if(x) {require(after.first.bgra[offset]>=after.first.bgra[offset-4],"Robust ramp is not monotonic");levels+=after.first.bgra[offset]!=after.first.bgra[offset-4];}
            }
            require(levels>=16,"Robust ramp lost distinguishable levels");
        }
        if(scene==1) {
            auto control=fixture.source;control.pixels.back()=fixtures::fp16({1.25,1.25,1.25});
            const auto reference=measured(control,fixture.settings,PeakMethod::Observed).first;
            int error=0;
            for(std::size_t i=0;i<after.first.bgra.size()-4;++i) error=std::max(error,std::abs(int(reference.bgra[i])-int(after.first.bgra[i])));
            require(error<=1,"Hot pixel still darkens the rest of the scene");
            const auto& peak=after.second.peaks.at(0).measurement;
            require(peak.observed_nits>=9999 && peak.effective_nits==203 && peak.hdr_samples==1,"Sparse hot pixel analysis changed");
            const auto center=(std::size_t(128)*256+128)*4;
            std::cout<<"HOT_PIXEL before loss "<<int(reference.bgra[center])-int(before.first.bgra[center])
                     <<" codes; robust loss "<<int(reference.bgra[center])-int(after.first.bgra[center])<<" codes.\n";
        }
        auto automatic=fixture.settings;automatic.mode=ContentMode::Auto;
        const auto auto_image=measured(fixture.source,automatic,PeakMethod::Robust);
        const auto auto_before=measured(fixture.source,automatic,PeakMethod::Observed);
        if(scene==8) require(auto_before.first.bgra==fixture.sdr_reference.bgra,"Automatic baseline changed native SDR");
        if(scene==8) require(auto_image.first.bgra==fixture.sdr_reference.bgra,"Automatic changed native SDR pixels");
        if(scene==9) {
            for(unsigned y=0;y<256;++y) for(unsigned x=128;x<256;++x) {
                const auto offset=(std::size_t(y)*256+x)*4;
                require(std::equal(auto_image.first.bgra.begin()+offset,auto_image.first.bgra.begin()+offset+4,
                                   fixture.sdr_reference.bgra.begin()+offset),"Automatic lost an SDR reference on mixed monitors");
            }
        }
        if(scene==7) require(auto_image.first.reference_white==480 && after.first.reference_white==203,"Automatic and HDR white assumptions were not compared");
        const auto folder=directory/fixture.id;std::filesystem::create_directory(folder);
        save_image(folder/L"observed.png",before.first);save_image(folder/L"robust.png",after.first);
        save_image(folder/L"comparison.png",comparison(before.first,after.first));save_image(folder/L"automatic.png",auto_image.first);
        save_image(folder/L"automatic-observed.png",auto_before.first);
        require(std::memcmp(original.data(),fixture.source.pixels.data(),original.size()*sizeof(Pixel))==0,"Peak analysis mutated source pixels");
        if(scene) report<<',';
        report<<"{\"id\":\""<<fixture.id<<"\",\"before\":"<<diagnostic_json(before.second)
              <<",\"after\":"<<diagnostic_json(after.second)<<",\"automatic_observed\":"<<diagnostic_json(auto_before.second)
              <<",\"automatic_robust\":"<<diagnostic_json(auto_image.second)<<'}';
    }
    Frame points;points.width=points.height=256;points.hdr=true;points.sdr_white=203;
    points.pixels.assign(256*256,Pixel{0,0,0,1});points.pixels[0]={12.5,12.5,12.5,1};points.pixels[1]={125,125,125,1};
    ColorSettings settings{ContentMode::Hdr,203,0,false};
    const auto dark=measured(points,settings,PeakMethod::Robust);
    require(dark.second.peaks[0].measurement.reason==PeakReason::DarkSparse,"Dark point highlights did not retain the maximum");
    require(dark.first.bgra[0]<dark.first.bgra[4],"Distinct dark-scene highlights collapsed together");
    for(double white:{40.0,80.0,480.0,2000.0}) {
        auto reference=settings;reference.paper_white=white;
        const auto varied=measured(points,reference,PeakMethod::Robust);
        require(varied.second.peaks[0].measurement.reason==PeakReason::DarkSparse,"Dark fallback depends on paper white");
        require(difference(varied.first,measured(points,reference,PeakMethod::Observed).first)<=1,"Dark fallback changed across paper whites");
    }
    points.pixels.assign(points.pixels.size(),Pixel{.5,.5,.5,1});points.pixels[0]={12.5,12.5,12.5,1};points.pixels[1]={125,125,125,1};
    const auto detail=measured(points,settings,PeakMethod::Robust);
    require(detail.second.peaks[0].measurement.reason==PeakReason::HighlightDetail,"Distinct sparse highlights lost conservative fallback");
    require(detail.first.bgra[0]<detail.first.bgra[4],"Distinct midtone-scene highlights collapsed together");
    require(difference(detail.first,measured(points,settings,PeakMethod::Observed).first)<=1,"Highlight fallback changed the established curve");
    points.pixels.assign(points.pixels.size(),Pixel{.5,.5,.5,1});points.pixels[0]={0,125,0,1};
    const auto colored_before=measured(points,settings,PeakMethod::Observed),colored_after=measured(points,settings,PeakMethod::Robust);
    save_image(directory/L"colored-highlight.png",comparison(colored_before.first,colored_after.first));
    require(colored_after.second.peaks[0].measurement.reason==PeakReason::ColorHighlight,"Colored HDR did not retain its curve");
    require(difference(colored_before.first,colored_after.first)<=1,"Robust selection erased colored highlight tint");
    std::cout<<"COLORED_HIGHLIGHT observed RGB "<<int(colored_before.first.bgra[2])<<','<<int(colored_before.first.bgra[1])<<','<<int(colored_before.first.bgra[0])
             <<"; robust RGB "<<int(colored_after.first.bgra[2])<<','<<int(colored_after.first.bgra[1])<<','<<int(colored_after.first.bgra[0])<<".\n";
    report<<"],\"colored_highlight\":{\"observed_rgb\":["<<int(colored_before.first.bgra[2])<<','<<int(colored_before.first.bgra[1])<<','<<int(colored_before.first.bgra[0])
          <<"],\"robust_rgb\":["<<int(colored_after.first.bgra[2])<<','<<int(colored_after.first.bgra[1])<<','<<int(colored_after.first.bgra[0])<<"]}";
    for(const Pixel light:{Pixel{125,124.9f,125,1},Pixel{12.5f,12.499f,12.5f,1},Pixel{125,0,0,1},Pixel{0,0,125,1}}) {
        points.pixels[0]=light;
        const auto guarded=measured(points,settings,PeakMethod::Robust);
        require(guarded.second.peaks[0].measurement.reason==PeakReason::ColorHighlight,"Near-neutral or primary highlight lost color protection");
        require(difference(guarded.first,measured(points,settings,PeakMethod::Observed).first)<=1,"Colored point changed under robust selection");
    }
    points.width=points.height=8;points.pixels.resize(64);
    const auto small=measured(points,settings,PeakMethod::Robust);
    require(small.second.peaks[0].measurement.reason==PeakReason::SmallSample,"Small image trusted an unstable percentile");
    require(difference(small.first,measured(points,settings,PeakMethod::Observed).first)<=1,"Small image fallback changed output");
    Frame large;large.width=4096;large.height=256;large.hdr=true;large.sdr_white=203;
    large.pixels.assign(checked_pixels(large.width,large.height),Pixel{.5,.5,.5,1});large.pixels.back()={125,125,125,1};
    const auto sampled=measured(large,settings,PeakMethod::Robust);
    require(sampled.second.peaks[0].measurement.sample_step==2,"Large histogram was not bounded");
    require(sampled.second.peaks[0].measurement.observed_nits>=9999,"Sampling lost the full-frame maximum");
    auto serial=large;serial.width=32768;serial.height=32;
    require(sampled.first.bgra==measured(serial,settings,PeakMethod::Robust).first.bgra,"Sampling depends on worker count or raster shape");
    large.pixels[large.pixels.size()-3]={12.5,12.5,12.5,1};
    const auto sampled_detail=measured(large,settings,PeakMethod::Robust);
    require(sampled_detail.second.peaks[0].measurement.reason==PeakReason::HighlightDetail,"Sampling lost distinct unsampled highlights");
    require(difference(sampled_detail.first,measured(large,settings,PeakMethod::Observed).first)<=1,"Sampled highlight fallback changed output");
    auto bad=settings;bad.peak_method=PeakMethod(100);
    rejects([&]{render_sdr(points,bad);},"Unknown peak method was accepted");
    report<<"}\n";report.close();
    std::cout<<checks<<" HDR peak checks passed: percentile rank, conservative fallbacks, scalar/parallel agreement, source preservation, and Automatic/HDR comparison.\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"HDR peak fixture failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
