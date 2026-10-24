#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include "platform/work.hpp"
#include "image/hdr_analysis.hpp"
#include "image/hdr_mask.hpp"
#include <vector>

namespace hdrshot {

struct Rgb {
    double r = 0, g = 0, b = 0;
    Rgb operator*(double scale) const { return {r * scale, g * scale, b * scale}; }
};

struct Pixel { float r, g, b, a; };
static_assert(sizeof(Pixel) == 16);

struct FrameRegion {
    unsigned x = 0, y = 0, width = 0, height = 0;
    bool hdr = false;
    double sdr_white = 80;
};

struct Frame {
    unsigned width = 0, height = 0;
    std::vector<Pixel> pixels;
    bool hdr = false;
    double sdr_white = 80;
    std::wstring backend;
    std::vector<FrameRegion> regions;
};

enum class ContentMode { Auto, Hdr, Sdr };
struct ColorSettings {
    ContentMode mode = ContentMode::Auto;
    double paper_white = 203;
    double exposure = 0;
    bool dither = true;
    PeakMethod peak_method = PeakMethod::Robust;
    bool content_aware = true;
    double peak_nits = 0;
};

struct Image {
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> bgra;
    bool tone_mapped = false;
    double source_peak = 0, reference_white = 80;
    std::wstring description;
};

inline double srgb_decode(double value) {
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

inline double srgb_encode(double value) {
    return value <= 0.0031308 ? 12.92 * value : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

inline const std::array<float, 256>& srgb_lookup() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> values{};
        for (unsigned i = 0; i < 256; ++i) values[i] = float(srgb_decode(i / 255.0));
        return values;
    }();
    return table;
}

// Returns the 8-bit code whose decode is exactly this value, or -1. A float's top 17 bits hold at most one code.
inline int srgb_code(float value) {
    struct Buckets { std::uint32_t first = 0; std::vector<std::int16_t> codes; };
    static const Buckets buckets = [] {
        const auto key = [](float v) { std::uint32_t bits; std::memcpy(&bits, &v, 4); return bits >> 15; };
        Buckets result;
        result.first = key(srgb_lookup()[1]);
        result.codes.assign(key(1.0f) - result.first + 1, -1);
        for (int i = 1; i < 256; ++i) result.codes[key(srgb_lookup()[i]) - result.first] = std::int16_t(i);
        return result;
    }();
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    if (!bits) return 0;
    const std::uint32_t index = (bits >> 15) - buckets.first;
    if (index >= buckets.codes.size()) return -1;
    const int code = buckets.codes[index];
    return code >= 0 && srgb_lookup()[code] == value ? code : -1;
}

inline double luminance(Rgb value) {
    return 0.2126390059 * value.r + 0.7151686788 * value.g + 0.0721923154 * value.b;
}

inline double pq_encode(double nits) {
    constexpr double m1 = 2610.0 / 16384, m2 = 2523.0 / 32;
    constexpr double c1 = 3424.0 / 4096, c2 = 2413.0 / 128, c3 = 2392.0 / 128;
    const double power = std::pow(std::max(0.0, nits) / 10000, m1);
    return std::pow((c1 + c2 * power) / (1 + c3 * power), m2);
}

inline double pq_decode(double signal) {
    constexpr double m1 = 2610.0 / 16384, m2 = 2523.0 / 32;
    constexpr double c1 = 3424.0 / 4096, c2 = 2413.0 / 128, c3 = 2392.0 / 128;
    const double power = std::pow(std::clamp(signal, 0.0, 1.0), 1 / m2);
    return 10000 * std::pow(std::max(power - c1, 0.0) / (c2 - c3 * power), 1 / m1);
}

inline Rgb to_rec2020(Rgb v) {
    return {.627403895934699*v.r+.329283038377884*v.g+.0433130656874172*v.b,
            .0690972893582321*v.r+.919540395075459*v.g+.0113623155663092*v.b,
            .0163914388751503*v.r+.0880133078772257*v.g+.895595253247624*v.b};
}

inline Rgb from_rec2020(Rgb v) {
    return {1.66049100210843*v.r-.58764113878855*v.g-.0728498633198849*v.b,
            -.124550474521591*v.r+1.13289989712596*v.g-.00834942260436948*v.b,
            -.0181507633549053*v.r-.100578898008007*v.g+1.11872966136291*v.b};
}

struct Lab { double l, a, b; };

inline Lab to_oklab(Rgb v) {
    const double l = std::cbrt(0.4122214708*v.r + 0.5363325363*v.g + 0.0514459929*v.b);
    const double m = std::cbrt(0.2119034982*v.r + 0.6806995451*v.g + 0.1073969566*v.b);
    const double s = std::cbrt(0.0883024619*v.r + 0.2817188376*v.g + 0.6299787005*v.b);
    return {0.2104542553*l + 0.7936177850*m - 0.0040720468*s,
            1.9779984951*l - 2.4285922050*m + 0.4505937099*s,
            0.0259040371*l + 0.7827717662*m - 0.8086757660*s};
}

inline Rgb from_oklab(Lab v) {
    double l = v.l + 0.3963377774*v.a + 0.2158037573*v.b;
    double m = v.l - 0.1055613458*v.a - 0.0638541728*v.b;
    double s = v.l - 0.0894841775*v.a - 1.2914855480*v.b;
    l *= l*l; m *= m*m; s *= s*s;
    return {4.0767416621*l - 3.3077115913*m + 0.2309699292*s,
            -1.2684380046*l + 2.6097574011*m - 0.3413193965*s,
            -0.0041960863*l - 0.7034186147*m + 1.7076147010*s};
}

inline bool in_gamut(Rgb v, double tolerance = 0) {
    return std::min({v.r, v.g, v.b}) >= -tolerance
        && std::max({v.r, v.g, v.b}) <= 1 + tolerance;
}

inline Rgb compress_gamut(Rgb v) {
    if (in_gamut(v)) return v;
    Lab lab = to_oklab(v);
    lab.l = std::clamp(lab.l, 0.0, 1.0);
    double low = 0, high = 1;
    for (int iteration = 0; iteration < 20; ++iteration) {
        const double scale = (low + high) * 0.5;
        if (in_gamut(from_oklab({lab.l, lab.a * scale, lab.b * scale}), 1e-10))
            low = scale;
        else
            high = scale;
    }
    const Rgb result = from_oklab({lab.l, lab.a * low, lab.b * low});
    return {std::clamp(result.r, 0.0, 1.0), std::clamp(result.g, 0.0, 1.0),
            std::clamp(result.b, 0.0, 1.0)};
}

class ToneCurve {
    double peak_ = 1, pq_peak_ = 0, pq_target_ = 0, knee_ = 1;
public:
    explicit ToneCurve(double peak) : peak_(std::max(1.0, peak)) {
        pq_peak_ = pq_encode(std::min(10000.0, peak_ * 100));
        pq_target_ = pq_encode(100) / pq_peak_;
        knee_ = std::clamp(1.5 * pq_target_ - 0.5, 0.0, 1.0);
    }

    double map(double value) const {
        if (value <= 0) return 0;
        if (peak_ <= 1.000001) return std::min(value, 1.0);
        double signal = pq_encode(std::min(value * 100, 10000.0)) / pq_peak_;
        if (signal > knee_) {
            const double t = std::clamp((signal - knee_) / (1 - knee_), 0.0, 1.0);
            const double t2 = t*t, t3 = t2*t;
            signal = (2*t3 - 3*t2 + 1)*knee_ + (t3 - 2*t2 + t)*(1-knee_)
                   + (-2*t3 + 3*t2)*pq_target_;
        }
        return std::clamp(pq_decode(signal * pq_peak_) / 100, 0.0, 1.0);
    }
};

class RenderLookup {
    static constexpr unsigned tone_size=65536, transfer_size=16384;
    const ToneCurve& curve_;
    double peak_;
    std::vector<double> tone_;
    const std::array<double,transfer_size+1>* transfer_=nullptr;
public:
    RenderLookup(const ToneCurve& curve,double peak,bool enabled,bool hdr) : curve_(curve),peak_(std::min(peak,100.0)) {
        if (!enabled) return;
        static const auto transfer=[] {
            std::array<double,transfer_size+1> values{};
            for (unsigned i=0;i<=transfer_size;++i) values[i]=srgb_encode(double(i)/transfer_size);
            return values;
        }();
        transfer_=&transfer;
        if (!hdr) return;
        tone_.resize(tone_size+1);
        for (unsigned i=0;i<=tone_size;++i) {
            const double position=double(i)/tone_size;
            tone_[i]=curve_.map(peak_*position*position);
        }
    }
    double map(double value) const {
        if (tone_.empty()) return curve_.map(value);
        if (value<=0) return 0;
        const double position=std::sqrt(std::min(value/peak_,1.0))*tone_size;
        const unsigned index=std::min(unsigned(position),tone_size-1);
        return tone_[index]+(tone_[index+1]-tone_[index])*(position-index);
    }
    double encode(double value) const {
        if (!transfer_) return srgb_encode(value);
        const double position=value*transfer_size;
        const unsigned index=std::min(unsigned(position),transfer_size-1);
        return (*transfer_)[index]+((*transfer_)[index+1]-(*transfer_)[index])*(position-index);
    }
};

inline std::size_t checked_pixels(unsigned width, unsigned height) {
    if (!width || !height || width > 32768 || height > 32768
        || std::uint64_t(width) * height > 100000000)
        throw std::runtime_error("Image dimensions are unsupported.");
    return std::size_t(width) * height;
}

inline float half_to_float(std::uint16_t half) {
    const int exponent = (half >> 10) & 31;
    const int fraction = half & 1023;
    float result;
    if (exponent == 31)
        result = fraction ? std::numeric_limits<float>::quiet_NaN()
                          : std::numeric_limits<float>::infinity();
    else if (exponent == 0)
        result = std::ldexp(float(fraction), -24);
    else
        result = std::ldexp(float(fraction + 1024), exponent - 25);
    return (half & 0x8000) ? -result : result;
}

inline std::uint16_t float_to_half(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint16_t sign = std::uint16_t((bits >> 16) & 0x8000);
    int exponent = int((bits >> 23) & 255) - 127 + 15;
    std::uint32_t fraction = bits & 0x7fffff;
    if (exponent >= 31) return sign | 0x7bff;
    if (exponent <= 0) {
        if (exponent < -10) return sign;
        fraction |= 0x800000;
        const unsigned shift = unsigned(14 - exponent);
        const std::uint32_t rounded = fraction + ((1u << (shift - 1)) - 1)
                                   + ((fraction >> shift) & 1);
        return sign | std::uint16_t(rounded >> shift);
    }
    fraction += 0xfff + ((fraction >> 13) & 1);
    if (fraction & 0x800000) { fraction = 0; ++exponent; }
    if (exponent >= 31) return sign | 0x7bff;
    return sign | std::uint16_t(exponent << 10) | std::uint16_t(fraction >> 13);
}

// Splits rows across up to four workers; the body runs on pool threads and must not throw.
template<class Body> void parallel_rows(unsigned height, const Body& body) {
    const unsigned workers = height < 64 ? 1
        : std::min(4u, std::max(1u, unsigned(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS))));
    std::array<NativeWork, 3> jobs;
    for (unsigned i = 1; i < workers; ++i)
        jobs[i-1].start([&,i] { body(i, height*i/workers, height*(i+1)/workers); });
    body(0u, 0u, height/workers);
    for (auto& job : jobs) job.join();
}

struct RenderedRegion {
    bool tone_mapped = false;
    double source_peak = 0, reference_white = 80;
    std::wstring description;
};

// Converts one display's area of the frame in place, so monitors need no tile copies.
inline RenderedRegion render_region(const Frame& frame, const FrameRegion& region, const ColorSettings& settings,
                                    bool use_lookup, Image& image) {
    DiagnosticTimer timing(DiagnosticStage::ColorAnalysis);
    const std::size_t count = std::size_t(region.width) * region.height;
    const auto offset = [&](unsigned x, unsigned y) {
        return std::size_t(region.y + y) * frame.width + region.x + x;
    };
    const double hdr_white=settings.paper_white>0?settings.paper_white:region.sdr_white;
    struct Stats {
        double component_peak = 0, luminance_peak = 0, tone_peak = 0, minimum_hdr = std::numeric_limits<double>::infinity();
        std::uint64_t hdr_samples = 0;
        bool wide_gamut = false, invalid = false, color_highlights = false;
    };
    std::array<Stats, 4> partial{};
    const bool details=bool(active_diagnostics);
    const bool robust=region.hdr && settings.mode!=ContentMode::Sdr && settings.peak_method==PeakMethod::Robust && settings.peak_nits==0;
    parallel_rows(region.height, [&](unsigned worker, unsigned first, unsigned last) {
        Stats& stats = partial[worker];
        for (unsigned y = first; y < last; ++y) for (unsigned x = 0; x < region.width; ++x) {
            const Pixel& p = frame.pixels[offset(x, y)];
            if (!std::isfinite(p.r) || !std::isfinite(p.g) || !std::isfinite(p.b)) { stats.invalid = true; return; }
            stats.component_peak = std::max(stats.component_peak, double(std::max({p.r, p.g, p.b})));
            stats.luminance_peak = std::max(stats.luminance_peak, luminance({p.r, p.g, p.b}));
            if (region.hdr) {
                const Rgb wide = to_rec2020({p.r,p.g,p.b});
                const double maximum=std::max({0.0,wide.r,wide.g,wide.b});
                stats.tone_peak = std::max(stats.tone_peak,maximum);
                const double nits=maximum*80;
                if((details || robust) && nits>hdr_white*1.01) {
                    ++stats.hdr_samples;
                    if(robust && !stats.color_highlights) {
                        stats.minimum_hdr=std::min(stats.minimum_hdr,nits);
                        stats.color_highlights=maximum-std::min({wide.r,wide.g,wide.b})>maximum*1e-5;
                    }
                }
            }
            stats.wide_gamut = stats.wide_gamut || std::min({p.r,p.g,p.b}) < -0.0001f;
        }
    });
    double component_peak = 0, luminance_peak = 0, tone_peak = 0;
    std::uint64_t hdr_samples=0;
    bool wide_gamut = false,color_highlights=false;double minimum_hdr=std::numeric_limits<double>::infinity();
    for (const auto& stats : partial) {
        if (stats.invalid) throw std::runtime_error("Capture contains invalid floating-point colors.");
        component_peak = std::max(component_peak, stats.component_peak);
        luminance_peak = std::max(luminance_peak, stats.luminance_peak);
        tone_peak = std::max(tone_peak, stats.tone_peak);hdr_samples+=stats.hdr_samples;
        minimum_hdr=std::min(minimum_hdr,stats.minimum_hdr);color_highlights=color_highlights || stats.color_highlights;
        wide_gamut = wide_gamut || stats.wide_gamut;
    }
    HdrPeakMeasurement measurement;
    measurement.observed_nits=measurement.effective_nits=tone_peak*80;
    measurement.reference_white=hdr_white;measurement.samples=count;measurement.hdr_samples=hdr_samples;measurement.reason=PeakReason::Observed;
    if(robust && count<1024) measurement.reason=tone_peak*80<=hdr_white?PeakReason::NoHeadroom:PeakReason::SmallSample;
    // Channel clipping can erase tint in isolated color highlights.
    else if(robust && color_highlights) measurement.reason=PeakReason::ColorHighlight;
    else if(robust) {
        const unsigned step=unsigned((count+999999)/1000000);
        std::array<HdrPeakHistogram,4> histograms;
        parallel_rows(region.height,[&](unsigned worker,unsigned first,unsigned last) {
            for(unsigned y=first;y<last;++y) {
                const unsigned start=unsigned((step-(std::size_t(y)*region.width)%step)%step);
                for(unsigned x=start;x<region.width;x+=step) {
                    const auto& pixel=frame.pixels[offset(x,y)];const auto wide=to_rec2020({pixel.r,pixel.g,pixel.b});
                    histograms[worker].add(std::max({0.0,wide.r,wide.g,wide.b})*80,hdr_white);
                }
            }
        });
        HdrPeakHistogram combined;for(const auto& histogram:histograms) combined.merge(histogram);
        combined.include_maximum(tone_peak*80);measurement=combined.measure(hdr_white);measurement.sample_step=step;
        if((measurement.reason==PeakReason::Percentile || measurement.reason==PeakReason::SparseBackground)
           && measurement.effective_nits<tone_peak*80 && hdr_samples && hdr_samples*1000<=count && tone_peak*80>minimum_hdr*1.1) {
            measurement.effective_nits=tone_peak*80;measurement.reason=PeakReason::HighlightDetail;
        }
    }
    HdrContentMask mask;
    if(region.hdr && settings.mode==ContentMode::Auto && settings.content_aware) {
        if(!std::isfinite(region.sdr_white) || region.sdr_white<40 || region.sdr_white>2000)
            throw std::runtime_error("Invalid display reference white.");
        if(wide_gamut || component_peak*80>region.sdr_white*1.01)
            mask=analyze_content_mask(region.width,region.height,region.sdr_white,
                [&](unsigned x,unsigned y)->const Pixel& {return frame.pixels[offset(x,y)];},
                [](unsigned rows,const auto& body) {
                    parallel_rows(rows,[&](unsigned,unsigned first,unsigned last) {body(first,last);});
                });
    }
    if(active_diagnostics) active_diagnostics->mask({region.x,region.y,region.width,region.height,
        mask.regions,mask.sdr_pixels,mask.blended_pixels});
    timing.stop();
    RenderedRegion result;
    result.source_peak = luminance_peak * 80;
    const bool embedded_sdr = region.hdr && settings.mode == ContentMode::Auto
        && !wide_gamut && component_peak * 80 <= region.sdr_white * 1.01;
    const bool sdr = !region.hdr || settings.mode == ContentMode::Sdr || embedded_sdr;
    const double white = !region.hdr ? 80 : (sdr ? region.sdr_white : hdr_white);
    if (!std::isfinite(white) || white <= 0) throw std::runtime_error("Invalid reference white.");
    result.reference_white = white;
    const double gain = 80 / white * (sdr ? 1 : std::exp2(settings.exposure));
    if(!sdr && settings.peak_nits>0) {
        measurement.effective_nits=std::min(tone_peak*80,settings.peak_nits);measurement.reason=PeakReason::Configured;
    }
    if(sdr) {measurement.reason=PeakReason::Sdr;measurement.reference_white=white;measurement.effective_nits=tone_peak*80;}
    if(active_diagnostics) active_diagnostics->peak({region.x,region.y,region.width,region.height,measurement});
    const double peak = std::max(1.0, !sdr && measurement.effective_nits!=tone_peak*80?measurement.effective_nits / 80 * gain:tone_peak * gain);
    result.tone_mapped = !sdr && tone_peak * gain > 1.000001;
    result.description = !region.hdr ? L"SDR preserved"
        : sdr ? L"SDR normalization" : L"HDR to SDR";
    if(mask.regions) result.description=L"HDR to SDR with preserved SDR regions";
    if(region.hdr && settings.mode==ContentMode::Auto) result.description+=L" (automatic estimate)";
    const ToneCurve curve(peak);
    const RenderLookup lookup(curve,peak,use_lookup && count>=65536,!sdr);
    static constexpr int bayer[16] = {0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    // SDR captures hold exact 8-bit decodes, so their codes come from a table built with the same math.
    std::array<std::uint8_t, 256> codes{};
    if (!region.hdr)
        for (unsigned i = 0; i < 256; ++i)
            codes[i] = std::uint8_t(std::clamp(std::lround(lookup.encode(std::clamp(double(srgb_lookup()[i]), 0.0, 1.0)) * 255), 0l, 255l));
    parallel_rows(region.height, [&](unsigned, unsigned first, unsigned last) {
        for (unsigned y = first; y < last; ++y) for (unsigned x = 0; x < region.width; ++x) {
            const std::size_t index = offset(x, y);
            const Pixel& p = frame.pixels[index];
            if (!region.hdr) {
                const int r = srgb_code(p.r), g = srgb_code(p.g), b = srgb_code(p.b);
                if (r >= 0 && g >= 0 && b >= 0) {
                    image.bgra[index*4] = codes[b]; image.bgra[index*4+1] = codes[g];
                    image.bgra[index*4+2] = codes[r]; image.bgra[index*4+3] = 255;
                    continue;
                }
            }
            const double amount=mask.amounts.empty()?1:mask.amounts[std::size_t(y)*region.width+x]/255.0;
            Rgb color = Rgb{p.r,p.g,p.b} * gain;
            if (!sdr && amount>0) {
                // Compress in the source gamut before limiting colors to sRGB.
                const Rgb wide = to_rec2020(color);
                color=from_rec2020({lookup.map(wide.r),lookup.map(wide.g),lookup.map(wide.b)});
            }
            if(!sdr && amount<1) {
                const Rgb normalized=Rgb{p.r,p.g,p.b}*(80/region.sdr_white);
                color={color.r*amount+normalized.r*(1-amount),color.g*amount+normalized.g*(1-amount),
                       color.b*amount+normalized.b*(1-amount)};
            }
            const double noise = settings.dither && !sdr
                ? (bayer[(y % 4) * 4 + x % 4] / 16.0 - 0.46875) * 0.75 * amount : 0;
            const auto quantize = [&](double v) {
                return std::uint8_t(std::clamp(std::lround(lookup.encode(std::clamp(v,0.0,1.0))
                                                        * 255 + (v > 0 && v < 1 ? noise : 0)), 0l, 255l));
            };
            image.bgra[index*4] = quantize(color.b);
            image.bgra[index*4+1] = quantize(color.g);
            image.bgra[index*4+2] = quantize(color.r);
            image.bgra[index*4+3] = 255;
        }
    });
    return result;
}

inline Image render_sdr(const Frame& frame, const ColorSettings& settings, bool use_lookup=true) {
    DiagnosticTimer timing(DiagnosticStage::RenderSdr);
    if(active_diagnostics) active_diagnostics->dimensions(frame.width,frame.height,frame.hdr,unsigned(frame.regions.size()));
    const auto count = checked_pixels(frame.width, frame.height);
    if (frame.pixels.size() != count) throw std::runtime_error("Incomplete capture frame.");
    if (!std::isfinite(settings.paper_white) || (settings.paper_white!=0 && settings.paper_white < 40)
        || settings.paper_white > 2000 || !std::isfinite(settings.exposure)
        || settings.exposure < -4 || settings.exposure > 4)
        throw std::runtime_error("Color settings are out of range.");
    if(!std::isfinite(settings.peak_nits) || settings.peak_nits<0 || settings.peak_nits>10000
       || (settings.peak_nits>0 && settings.peak_nits<40)) throw std::runtime_error("HDR peak must be zero or between 40 and 10000 nits.");
    if(settings.peak_method!=PeakMethod::Observed && settings.peak_method!=PeakMethod::Robust)
        throw std::runtime_error("Invalid HDR peak method.");
    Image image;
    image.width = frame.width; image.height = frame.height;
    if (frame.regions.empty()) {
        image.bgra.resize(count * 4);
        const auto converted = render_region(frame, {0,0,frame.width,frame.height,frame.hdr,frame.sdr_white},
                                             settings, use_lookup, image);
        image.tone_mapped = converted.tone_mapped; image.source_peak = converted.source_peak;
        image.reference_white = converted.reference_white; image.description = converted.description;
        return image;
    }
    image.bgra.assign(count * 4, 0);
    for (std::size_t i = 3; i < image.bgra.size(); i += 4) image.bgra[i] = 255;
    image.description = L"Per-monitor HDR/SDR conversion";
    bool first = true;
    for (const auto& region : frame.regions) {
        if (!region.width || !region.height || region.x > frame.width || region.y > frame.height
            || region.width > frame.width - region.x || region.height > frame.height - region.y)
            throw std::runtime_error("Monitor region is outside the captured image.");
        // Each display has its own SDR white and HDR classification.
        const auto converted = render_region(frame, region, settings, use_lookup, image);
        image.tone_mapped = image.tone_mapped || converted.tone_mapped;
        image.source_peak = std::max(image.source_peak, converted.source_peak);
        image.reference_white = first ? converted.reference_white
            : image.reference_white == converted.reference_white ? image.reference_white : 0;
        first = false;
    }
    return image;
}

} // namespace hdrshot
