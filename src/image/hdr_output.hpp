#pragma once
#include "image/image_io.hpp"
#include <ultrahdr_api.h>

// This product includes Gain Map technology under license by Adobe.

namespace hdrshot {

enum class HdrFormat { UltraJpeg, JpegXr };

inline Rgb to_bt2020(Rgb color) {
    return {.627404*color.r+.329282*color.g+.043314*color.b,
            .069097*color.r+.919541*color.g+.011362*color.b,
            .016392*color.r+.088013*color.g+.895595*color.b};
}
inline void check_hdr_codec(uhdr_error_info_t result,const char* context) {
    if(result.error_code!=UHDR_CODEC_OK)
        throw std::runtime_error(std::string(context)+(result.has_detail?": "+std::string(result.detail):"."));
}
inline void save_hdr_jpeg(const std::filesystem::path& path,const Frame& source,const Image& base) {
    DiagnosticTimer timing(DiagnosticStage::EncodeUltraHdr);
    const auto count=checked_pixels(source.width,source.height);
    if(!source.hdr || source.pixels.size()!=count) throw std::runtime_error("Invalid HDR image buffer.");
    if(source.width<8 || source.height<8)
        throw std::runtime_error("Ultra HDR JPEG requires at least 8 x 8 pixels.");
    if(base.width!=source.width || base.height!=source.height || base.bgra.size()!=count*4)
        throw std::runtime_error("HDR JPEG fallback does not match the captured image.");
    std::vector<std::uint16_t> hdr(count*4);
    std::vector<std::uint8_t> sdr(count*4);
    // Gain-map capacity must exceed its 203-nit SDR reference.
    double peak=204;
    for(std::size_t i=0;i<count;++i) {
        const auto& p=source.pixels[i];
        if(!std::isfinite(p.r) || !std::isfinite(p.g) || !std::isfinite(p.b))
            throw std::runtime_error("Capture contains invalid floating-point colors.");
        const Rgb color=to_bt2020({p.r,p.g,p.b});
        const double channels[]={color.r,color.g,color.b};
        for(unsigned channel=0;channel<3;++channel) {
            const double nits=std::clamp(channels[channel]*80,0.0,10000.0);
            peak=std::max(peak,nits);
            // scRGB uses 80 nits per unit; this codec uses 203.
            hdr[i*4+channel]=float_to_half(float(nits/203));
        }
        hdr[i*4+3]=float_to_half(1);
        // A shared gamut preserves wide HDR colors in both metadata variants.
        const Rgb fallback=to_bt2020({srgb_decode(base.bgra[i*4+2]/255.0),
                                    srgb_decode(base.bgra[i*4+1]/255.0),srgb_decode(base.bgra[i*4]/255.0)});
        const double components[]={fallback.r,fallback.g,fallback.b};
        for(unsigned channel=0;channel<3;++channel)
            sdr[i*4+channel]=std::uint8_t(std::clamp(std::lround(srgb_encode(components[channel])*255),0l,255l));
        sdr[i*4+3]=255;
    }
    uhdr_codec_private_t* encoder=uhdr_create_encoder();
    if(!encoder) throw std::runtime_error("Cannot create HDR JPEG encoder.");
    struct Release {uhdr_codec_private_t* value;~Release(){uhdr_release_encoder(value);}} release{encoder};
    uhdr_raw_image_t hdr_image{},sdr_image{};
    hdr_image.fmt=UHDR_IMG_FMT_64bppRGBAHalfFloat;hdr_image.cg=UHDR_CG_BT_2100;
    hdr_image.ct=UHDR_CT_LINEAR;hdr_image.range=UHDR_CR_FULL_RANGE;
    hdr_image.w=source.width;hdr_image.h=source.height;hdr_image.planes[0]=hdr.data();hdr_image.stride[0]=source.width;
    sdr_image=hdr_image;sdr_image.fmt=UHDR_IMG_FMT_32bppRGBA8888;sdr_image.ct=UHDR_CT_SRGB;
    sdr_image.planes[0]=sdr.data();
    check_hdr_codec(uhdr_enc_set_raw_image(encoder,&hdr_image,UHDR_HDR_IMG),"Cannot supply HDR pixels");
    check_hdr_codec(uhdr_enc_set_raw_image(encoder,&sdr_image,UHDR_SDR_IMG),"Cannot supply SDR fallback");
    check_hdr_codec(uhdr_enc_set_quality(encoder,100,UHDR_BASE_IMG),"Cannot set JPEG quality");
    check_hdr_codec(uhdr_enc_set_quality(encoder,100,UHDR_GAIN_MAP_IMG),"Cannot set HDR detail quality");
    check_hdr_codec(uhdr_enc_set_gainmap_scale_factor(encoder,1),"Cannot retain full-size HDR detail");
    check_hdr_codec(uhdr_enc_set_using_multi_channel_gainmap(encoder,1),"Cannot retain HDR color detail");
    check_hdr_codec(uhdr_enc_set_preset(encoder,UHDR_USAGE_BEST_QUALITY),"Cannot set HDR encoding quality");
    check_hdr_codec(uhdr_enc_set_target_display_peak_brightness(encoder,float(peak)),"Cannot set HDR peak metadata");
    check_hdr_codec(uhdr_encode(encoder),"Cannot encode Ultra HDR JPEG");
    const auto* encoded=uhdr_get_encoded_stream(encoder);
    if(!encoded || !encoded->data || !encoded->data_sz) throw std::runtime_error("HDR JPEG encoder returned no image.");
    OutputTransaction transaction(path);
    std::ofstream output(transaction.path(),std::ios::binary|std::ios::trunc);
    output.write(static_cast<const char*>(encoded->data),std::streamsize(encoded->data_sz));
    output.close();
    if(!output) throw std::runtime_error("Cannot write HDR JPEG in the selected folder.");
    transaction.commit();
}

} // namespace hdrshot
