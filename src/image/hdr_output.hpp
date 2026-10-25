#pragma once
#include "image/image_io.hpp"
#include <ultrahdr_api.h>

// This product includes Gain Map technology under license by Adobe.

namespace hdrshot {

enum class HdrFormat { UltraJpeg, JpegXr };
enum class HdrJpegQuality { Full, High, Balanced, Compact };

inline int hdr_jpeg_quality(HdrJpegQuality quality) {
    switch(quality) {
    case HdrJpegQuality::High: return 95;
    case HdrJpegQuality::Balanced: return 90;
    case HdrJpegQuality::Compact: return 85;
    default: return 100;
    }
}

inline Rgb to_bt2020(Rgb color) {
    return {.627404*color.r+.329282*color.g+.043314*color.b,
            .069097*color.r+.919541*color.g+.011362*color.b,
            .016392*color.r+.088013*color.g+.895595*color.b};
}
inline void check_hdr_codec(uhdr_error_info_t result,const char* context) {
    if(result.error_code!=UHDR_CODEC_OK)
        throw std::runtime_error(std::string(context)+(result.has_detail?": "+std::string(result.detail):"."));
}
inline void validate_hdr_jpeg(const std::filesystem::path& path,unsigned width,unsigned height,double peak=0) {
    DiagnosticTimer timing(DiagnosticStage::Validate);
    checked_pixels(width,height);
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    const auto size=input.tellg();
    if(!input || size<4 || std::uintmax_t(size)>std::uintmax_t(std::numeric_limits<int>::max()))
        throw std::runtime_error("HDR JPEG output is empty or unsupported.");
    std::string bytes(std::size_t(size),'\0');input.seekg(0);input.read(bytes.data(),std::streamsize(bytes.size()));
    if(!input || std::uint8_t(bytes[0])!=0xff || std::uint8_t(bytes[1])!=0xd8
       || std::uint8_t(bytes[bytes.size()-2])!=0xff || std::uint8_t(bytes.back())!=0xd9)
        throw std::runtime_error("HDR JPEG output is incomplete.");
    uhdr_codec_private_t* decoder=uhdr_create_decoder();
    if(!decoder) throw std::runtime_error("Cannot create HDR output verifier.");
    struct Release {uhdr_codec_private_t* value;~Release(){uhdr_release_decoder(value);}} release{decoder};
    uhdr_compressed_image_t image{};image.data=bytes.data();image.data_sz=image.capacity=bytes.size();
    image.cg=UHDR_CG_UNSPECIFIED;image.ct=UHDR_CT_UNSPECIFIED;image.range=UHDR_CR_UNSPECIFIED;
    check_hdr_codec(uhdr_dec_set_image(decoder,&image),"Cannot reopen HDR JPEG output");
    check_hdr_codec(uhdr_dec_probe(decoder),"Cannot inspect HDR JPEG output");
    if(uhdr_dec_get_image_width(decoder)!=int(width) || uhdr_dec_get_image_height(decoder)!=int(height)
       || uhdr_dec_get_gainmap_width(decoder)!=int(width) || uhdr_dec_get_gainmap_height(decoder)!=int(height))
        throw std::runtime_error("HDR JPEG output changed base or gain map dimensions.");
    const auto complete=[](const uhdr_mem_block_t* block) {
        if(!block || !block->data || block->data_sz<4) return false;
        const auto* data=static_cast<const std::uint8_t*>(block->data);
        return data[0]==0xff && data[1]==0xd8 && data[block->data_sz-2]==0xff && data[block->data_sz-1]==0xd9;
    };
    const auto* profile=uhdr_dec_get_icc(decoder);
    if(!complete(uhdr_dec_get_base_image(decoder)) || !complete(uhdr_dec_get_gainmap_image(decoder))
       || !profile || !profile->data || !profile->data_sz)
        throw std::runtime_error("HDR JPEG output lost image data or its color profile.");
    const auto* metadata=uhdr_dec_get_gainmap_metadata(decoder);
    if(!metadata || !std::isfinite(metadata->hdr_capacity_min) || !std::isfinite(metadata->hdr_capacity_max)
       || metadata->hdr_capacity_min<1 || metadata->hdr_capacity_max<=metadata->hdr_capacity_min)
        throw std::runtime_error("HDR JPEG output has invalid display headroom.");
    for(unsigned channel=0;channel<3;++channel) {
        if(!std::isfinite(metadata->min_content_boost[channel]) || metadata->min_content_boost[channel]<=0
           || !std::isfinite(metadata->max_content_boost[channel]) || metadata->max_content_boost[channel]<metadata->min_content_boost[channel]
           || !std::isfinite(metadata->gamma[channel]) || metadata->gamma[channel]<=0
           || !std::isfinite(metadata->offset_sdr[channel]) || metadata->offset_sdr[channel]<0
           || !std::isfinite(metadata->offset_hdr[channel]) || metadata->offset_hdr[channel]<0)
            throw std::runtime_error("HDR JPEG output has invalid gain map metadata.");
    }
    if(peak>0 && std::abs(metadata->hdr_capacity_max-peak/203)>std::max(.001,peak/203*.0001))
        throw std::runtime_error("HDR JPEG output changed its peak brightness metadata.");
    if(bytes.find("hdrgm:Version")==std::string::npos || bytes.find("urn:iso:std:iso:ts:21496:-1")==std::string::npos)
        throw std::runtime_error("HDR JPEG output is missing gain map metadata variants.");
}
inline void save_hdr_jpeg(const std::filesystem::path& path,const Frame& source,const Image& base,
                          HdrJpegQuality quality=HdrJpegQuality::Full) {
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
    const int requested_quality=hdr_jpeg_quality(quality);
    check_hdr_codec(uhdr_enc_set_quality(encoder,requested_quality,UHDR_BASE_IMG),"Cannot set JPEG quality");
    check_hdr_codec(uhdr_enc_set_quality(encoder,requested_quality,UHDR_GAIN_MAP_IMG),"Cannot set HDR detail quality");
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
    validate_hdr_jpeg(transaction.path(),source.width,source.height,peak);
    transaction.commit();
}

} // namespace hdrshot
