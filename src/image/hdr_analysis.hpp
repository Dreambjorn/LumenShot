#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace hdrshot {

enum class PeakMethod { Observed, Robust };
enum class PeakReason { Observed, Percentile, NoHeadroom, SmallSample, SparseBackground, DarkSparse, HighlightDetail, ColorHighlight, Sdr, Empty, Configured };
inline const char* peak_reason_name(PeakReason reason) {
    static constexpr const char* names[]={"observed_max","percentile","no_hdr_headroom","small_sample",
        "sparse_background","dark_sparse","highlight_detail","chromatic_hdr","sdr_bypass","empty","configured_limit"};
    const auto index=std::size_t(reason);return index<std::size(names)?names[index]:"unknown";
}
struct HdrPeakMeasurement {
    double observed_nits=0,effective_nits=0,percentile_nits=0,reference_white=203;
    std::uint64_t samples=0,hdr_samples=0;
    unsigned percentile_per_mille=999;
    unsigned sample_step=1;
    PeakReason reason=PeakReason::Empty;
};

// Bins use the renderer's Rec.2020 maximum channel in nits.
class HdrPeakHistogram {
    std::array<std::uint64_t,512> bins_{};
    double observed_=0;
    std::uint64_t samples_=0,hdr_samples_=0;
    bool invalid_=false;
public:
    void include_maximum(double nits) noexcept {
        if(!std::isfinite(nits)) invalid_=true;else observed_=std::max(observed_,nits);
    }
    void add(double nits,double reference_white) noexcept {
        if(!std::isfinite(nits)) {invalid_=true;return;}
        nits=std::max(0.0,nits);observed_=std::max(observed_,nits);
        ++samples_;hdr_samples_+=nits>reference_white*1.01;
        const float bounded=float(std::clamp(nits,1.0,10000.0));
        std::uint32_t bits;std::memcpy(&bits,&bounded,sizeof(bits));
        const unsigned exponent=((bits>>23)&255)-127;
        ++bins_[exponent*32+((bits>>18)&31)];
    }
    void merge(const HdrPeakHistogram& other) noexcept {
        for(std::size_t i=0;i<bins_.size();++i) bins_[i]+=other.bins_[i];
        samples_+=other.samples_;hdr_samples_+=other.hdr_samples_;
        observed_=std::max(observed_,other.observed_);invalid_=invalid_ || other.invalid_;
    }
    HdrPeakMeasurement measure(double reference_white,unsigned percentile=999) const {
        if(invalid_ || !std::isfinite(reference_white) || reference_white<40 || reference_white>2000
           || percentile<1 || percentile>1000) throw std::runtime_error("Invalid HDR peak analysis input.");
        HdrPeakMeasurement result;result.observed_nits=observed_;result.effective_nits=std::min(observed_,10000.0);
        result.reference_white=reference_white;result.samples=samples_;result.hdr_samples=hdr_samples_;
        result.percentile_per_mille=percentile;
        if(!samples_) return result;
        if(observed_<=reference_white) {result.reason=PeakReason::NoHeadroom;return result;}
        if(samples_<1024) {result.reason=PeakReason::SmallSample;return result;}
        const auto rank=(samples_*percentile+999)/1000;
        std::uint64_t accumulated=0;unsigned selected=0;
        for(;selected<bins_.size()-1;++selected) {accumulated+=bins_[selected];if(accumulated>=rank) break;}
        const double upper=std::ldexp(1+double(selected%32+1)/32,int(selected/32));
        result.percentile_nits=std::min({upper,observed_,10000.0});
        // Sparse bright points on black may be real highlights.
        if(result.percentile_nits<=std::max(1.1,reference_white*.01)) {result.reason=PeakReason::DarkSparse;return result;}
        result.effective_nits=std::min({observed_,10000.0,std::max(reference_white,result.percentile_nits*1.05)});
        unsigned highlight_bins=0;
        for(unsigned bin=0;bin<bins_.size();++bin) {
            const double lower=std::ldexp(1+double(bin%32)/32,int(bin/32));
            if(bins_[bin] && lower>result.effective_nits && ++highlight_bins>1) {
                result.effective_nits=std::min(observed_,10000.0);result.reason=PeakReason::HighlightDetail;return result;
            }
        }
        result.reason=hdr_samples_<8?PeakReason::SparseBackground:PeakReason::Percentile;
        return result;
    }
};

} // namespace hdrshot
