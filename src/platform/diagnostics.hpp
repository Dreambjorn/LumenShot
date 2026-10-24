#pragma once
#include "image/hdr_analysis.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iterator>
#include <locale>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace hdrshot {

enum class DiagnosticStage { Enumerate, Device, CaptureItem, Session, FreshFrame, GpuCopy, DecodePixels,
    CaptureWindows, CaptureDesktop, Preview, ColorAnalysis, RenderSdr, Scale, Sharpen, EnhanceHdr,
    Selection, Annotation, EncodePng, EncodeJxr, EncodeUltraHdr, Validate, Commit, Clipboard, Count };
inline constexpr const char* diagnostic_stage_names[]={"enumerate","device","capture_item","session",
    "fresh_frame","gpu_copy","decode_pixels","capture_windows","capture_desktop","preview","color_analysis",
    "render_sdr","scale","sharpen","enhance_hdr","selection","annotation","encode_png","encode_jxr",
    "encode_ultra_hdr","validate","commit","clipboard"};
inline constexpr std::size_t diagnostic_stage_count=std::size(diagnostic_stage_names);
static_assert(diagnostic_stage_count==std::size_t(DiagnosticStage::Count));
enum class DiagnosticCounter { DeviceReused, ItemReused, WindowsRetry, BackendFallback, Count };
enum class DiagnosticSource { Live, Imported, Synthetic };
enum class DiagnosticBackend { Unknown, Windows, Desktop };
struct DiagnosticDuration { unsigned calls=0,failures=0;double sum_ms=0,max_ms=0; };
struct DiagnosticFrame { unsigned width,height;bool hdr;double sdr_white;unsigned pixel_format;DiagnosticBackend backend; };
struct DiagnosticMask { unsigned x,y,width,height,regions;std::uint64_t sdr_pixels,blended_pixels; };
struct DiagnosticPeak { unsigned x,y,width,height;HdrPeakMeasurement measurement; };
struct DiagnosticIssue { DiagnosticStage stage;std::uint32_t hresult; };
struct DiagnosticSnapshot {
    DiagnosticSource source=DiagnosticSource::Live;
    unsigned width=0,height=0,monitors=0,regions=0,dropped=0;
    bool hdr=false,finished=false,success=false,canceled=false;
    double total_ms=0,selector_first_paint_ms=-1;
    std::array<DiagnosticDuration,diagnostic_stage_count> stages{};
    std::array<unsigned,std::size_t(DiagnosticCounter::Count)> counters{};
    std::vector<DiagnosticFrame> frames;
    std::vector<DiagnosticIssue> issues;
    std::vector<DiagnosticPeak> peaks;
    std::vector<DiagnosticMask> masks;
};

class DiagnosticRecord {
public:
    using Clock=std::chrono::steady_clock;
private:
    Clock::time_point start_;
    mutable std::mutex mutex_;
    DiagnosticSnapshot data_;
public:
    explicit DiagnosticRecord(DiagnosticSource source,Clock::time_point start=Clock::now()) : start_(start) {data_.source=source;}
    double elapsed() const {return std::chrono::duration<double,std::milli>(Clock::now()-start_).count();}
    void duration(DiagnosticStage stage,double value,bool failed) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            auto& sample=data_.stages.at(std::size_t(stage));
            ++sample.calls;sample.failures+=failed;sample.sum_ms+=value;sample.max_ms=std::max(sample.max_ms,value);
        } catch(...) {}
    }
    void count(DiagnosticCounter counter) noexcept {
        try {std::lock_guard<std::mutex> lock(mutex_);++data_.counters.at(std::size_t(counter));} catch(...) {}
    }
    void dimensions(unsigned width,unsigned height,bool hdr,unsigned regions) noexcept {
        try {std::lock_guard<std::mutex> lock(mutex_);data_.width=width;data_.height=height;data_.hdr=hdr;data_.regions=regions;} catch(...) {}
    }
    void monitors(unsigned value) noexcept {
        try {std::lock_guard<std::mutex> lock(mutex_);data_.monitors=value;} catch(...) {}
    }
    void frame(DiagnosticFrame value) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if(data_.frames.size()==64) {++data_.dropped;return;}
            if(!std::isfinite(value.sdr_white) || value.sdr_white<0) value.sdr_white=0;
            data_.frames.push_back(value);
        } catch(...) {}
    }
    void mask(DiagnosticMask value) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if(data_.masks.size()==64) {++data_.dropped;return;}
            data_.masks.push_back(value);
        } catch(...) {}
    }
    void peak(DiagnosticPeak value) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if(data_.peaks.size()==64) {++data_.dropped;return;}
            data_.peaks.push_back(value);
        } catch(...) {}
    }
    void issue(DiagnosticStage stage,std::uint32_t code) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if(data_.issues.size()==64) {++data_.dropped;return;}
            data_.issues.push_back({stage,code});
        } catch(...) {}
    }
    void selector_painted() noexcept {
        try {std::lock_guard<std::mutex> lock(mutex_);if(data_.selector_first_paint_ms<0) data_.selector_first_paint_ms=elapsed();} catch(...) {}
    }
    void finish(bool success,bool canceled=false) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            data_.finished=true;data_.success=success;data_.canceled=canceled;data_.total_ms=elapsed();
        } catch(...) {}
    }
    DiagnosticSnapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto result=data_;if(!result.finished) result.total_ms=elapsed();return result;
    }
};

extern thread_local std::shared_ptr<DiagnosticRecord> active_diagnostics;
extern thread_local DiagnosticStage active_diagnostic_stage;
class DiagnosticContext {
    std::shared_ptr<DiagnosticRecord> previous_;
    DiagnosticStage stage_;
public:
    explicit DiagnosticContext(std::shared_ptr<DiagnosticRecord> record,DiagnosticStage stage=DiagnosticStage::Count)
        : previous_(std::move(active_diagnostics)),stage_(active_diagnostic_stage) {
        active_diagnostics=std::move(record);active_diagnostic_stage=stage;
    }
    ~DiagnosticContext() {active_diagnostics=std::move(previous_);active_diagnostic_stage=stage_;}
    DiagnosticContext(const DiagnosticContext&)=delete;
    DiagnosticContext& operator=(const DiagnosticContext&)=delete;
};

class DiagnosticTimer {
    std::shared_ptr<DiagnosticRecord> record_=active_diagnostics;
    DiagnosticStage stage_,previous_=active_diagnostic_stage;
    DiagnosticRecord::Clock::time_point start_{};
    int exceptions_=0;
public:
    explicit DiagnosticTimer(DiagnosticStage stage) : stage_(stage) {
        if(record_) {start_=DiagnosticRecord::Clock::now();exceptions_=std::uncaught_exceptions();active_diagnostic_stage=stage;}
    }
    void stop() noexcept {
        if(!record_) return;
        const double elapsed=std::chrono::duration<double,std::milli>(DiagnosticRecord::Clock::now()-start_).count();
        record_->duration(stage_,elapsed,std::uncaught_exceptions()>exceptions_);
        active_diagnostic_stage=previous_;record_.reset();
    }
    ~DiagnosticTimer() {stop();}
    DiagnosticTimer(const DiagnosticTimer&)=delete;
    DiagnosticTimer& operator=(const DiagnosticTimer&)=delete;
};

inline void diagnostic_count(DiagnosticCounter counter) noexcept {if(active_diagnostics) active_diagnostics->count(counter);}
inline void diagnostic_error(std::uint32_t code) noexcept {if(active_diagnostics) active_diagnostics->issue(active_diagnostic_stage,code);}
inline void diagnostic_paint() noexcept {if(active_diagnostics) active_diagnostics->selector_painted();}
inline double diagnostic_quantile(std::vector<double> values,double quantile) {
    if(values.empty() || !std::isfinite(quantile) || quantile<0 || quantile>1
       || std::any_of(values.begin(),values.end(),[](double v){return !std::isfinite(v) || v<0;}))
        throw std::invalid_argument("Invalid benchmark samples.");
    std::sort(values.begin(),values.end());
    const double position=quantile*(values.size()-1);
    const auto first=std::size_t(position),last=std::min(first+1,values.size()-1);
    return values[first]+(values[last]-values[first])*(position-first);
}

inline std::string diagnostic_json(const DiagnosticSnapshot& value) {
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(6);
    const char* source=value.source==DiagnosticSource::Synthetic?"synthetic":value.source==DiagnosticSource::Imported?"imported":"live";
    const char* outcome=!value.finished?"running":value.canceled?"canceled":value.success?"success":"failed";
    out<<"{\"source\":\""<<source<<"\",\"outcome\":\""<<outcome<<"\",\"width\":"<<value.width
       <<",\"height\":"<<value.height<<",\"hdr\":"<<(value.hdr?"true":"false")<<",\"monitor_count\":"<<value.monitors
       <<",\"region_count\":"<<value.regions<<",\"total_ms\":"<<value.total_ms<<",\"selector_first_paint_ms\":";
    if(value.selector_first_paint_ms<0) out<<"null";else out<<value.selector_first_paint_ms;
    out<<",\"inclusive_stage_times\":true,\"parallel_stages_may_overlap\":true,\"dropped_events\":"<<value.dropped<<",\"stages\":{";
    for(std::size_t i=0;i<value.stages.size();++i) {
        const auto& stage=value.stages[i];if(i) out<<',';
        out<<'"'<<diagnostic_stage_names[i]<<"\":{\"calls\":"<<stage.calls<<",\"failures\":"<<stage.failures
           <<",\"sum_ms\":"<<stage.sum_ms<<",\"max_ms\":"<<stage.max_ms<<'}';
    }
    out<<"},\"counters\":{\"device_reused\":"<<value.counters[0]<<",\"item_reused\":"<<value.counters[1]
       <<",\"windows_retry\":"<<value.counters[2]<<",\"backend_fallback\":"<<value.counters[3]<<"},\"frames\":[";
    for(std::size_t i=0;i<value.frames.size();++i) {
        const auto& frame=value.frames[i];if(i) out<<',';
        const char* backend=frame.backend==DiagnosticBackend::Windows?"windows":frame.backend==DiagnosticBackend::Desktop?"desktop":"unknown";
        out<<"{\"width\":"<<frame.width<<",\"height\":"<<frame.height<<",\"hdr\":"<<(frame.hdr?"true":"false")
           <<",\"sdr_white_nits\":"<<frame.sdr_white<<",\"dxgi_format\":"<<frame.pixel_format<<",\"backend\":\""<<backend<<"\"}";
    }
    out<<"],\"peaks\":[";
    for(std::size_t i=0;i<value.peaks.size();++i) {
        const auto& region=value.peaks[i];const auto& peak=region.measurement;if(i) out<<',';
        out<<"{\"x\":"<<region.x<<",\"y\":"<<region.y<<",\"width\":"<<region.width<<",\"height\":"<<region.height
           <<",\"metric\":\"rec2020_max_channel_nits\",\"observed_nits\":"<<peak.observed_nits<<",\"effective_nits\":"<<peak.effective_nits
           <<",\"percentile_nits\":"<<peak.percentile_nits<<",\"reference_white_nits\":"<<peak.reference_white
           <<",\"samples\":"<<peak.samples<<",\"hdr_samples\":"<<peak.hdr_samples
           <<",\"sample_step\":"<<peak.sample_step<<",\"percentile_per_mille\":"<<peak.percentile_per_mille<<",\"reason\":\""<<peak_reason_name(peak.reason)<<"\"}";
    }
    out<<"],\"masks\":[";
    for(std::size_t i=0;i<value.masks.size();++i) {
        const auto& mask=value.masks[i];if(i) out<<',';
        out<<"{\"x\":"<<mask.x<<",\"y\":"<<mask.y<<",\"width\":"<<mask.width<<",\"height\":"<<mask.height
           <<",\"sdr_regions\":"<<mask.regions<<",\"sdr_pixels\":"<<mask.sdr_pixels<<",\"blended_pixels\":"<<mask.blended_pixels<<'}';
    }
    out<<"],\"issues\":[";
    for(std::size_t i=0;i<value.issues.size();++i) {
        const auto& issue=value.issues[i];if(i) out<<',';
        const char* stage=std::size_t(issue.stage)<diagnostic_stage_count?diagnostic_stage_names[std::size_t(issue.stage)]:"unscoped";
        out<<"{\"stage\":\""<<stage<<"\",\"hresult\":"<<issue.hresult<<'}';
    }
    out<<"]}";return out.str();
}

} // namespace hdrshot
