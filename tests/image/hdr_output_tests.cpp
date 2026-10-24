#include "image/hdr_output.hpp"
#include <iostream>
#include <iterator>

using namespace hdrshot;
static unsigned checks=0;
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
void verify_low_peak(const std::filesystem::path& directory,float value,unsigned index) {
    Frame source;source.width=source.height=16;source.hdr=true;source.sdr_white=203;
    source.pixels.assign(256,Pixel{value,value,value,1});
    ColorSettings settings;settings.mode=ContentMode::Hdr;settings.dither=false;
    const auto path=directory/(L"low-peak-"+std::to_wstring(index)+L".jpg");
    save_hdr_jpeg(path,source,render_sdr(source,settings));
    std::ifstream input(path,std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)),{});
    uhdr_codec_private_t* decoder=uhdr_create_decoder();require(decoder!=nullptr,"Cannot inspect low-peak HDR JPEG");
    struct Release {uhdr_codec_private_t* value;~Release(){uhdr_release_decoder(value);}} release{decoder};
    uhdr_compressed_image_t compressed{};compressed.data=bytes.data();compressed.data_sz=compressed.capacity=bytes.size();
    check_hdr_codec(uhdr_dec_set_image(decoder,&compressed),"Cannot read low-peak HDR JPEG");
    check_hdr_codec(uhdr_dec_set_out_img_format(decoder,UHDR_IMG_FMT_64bppRGBAHalfFloat),"Cannot select low-peak HDR output");
    check_hdr_codec(uhdr_dec_set_out_color_transfer(decoder,UHDR_CT_LINEAR),"Cannot select low-peak HDR transfer");
    check_hdr_codec(uhdr_dec_set_out_max_display_boost(decoder,10000.0f/203),"Cannot select low-peak HDR headroom");
    check_hdr_codec(uhdr_decode(decoder),"Cannot reconstruct low-peak HDR JPEG");
    const auto* metadata=uhdr_dec_get_gainmap_metadata(decoder);
    require(metadata && metadata->hdr_capacity_max>metadata->hdr_capacity_min,"Low-peak HDR has invalid display headroom");
    const auto* decoded=uhdr_get_decoded_image(decoder);
    require(decoded && decoded->cg==UHDR_CG_BT_2100,"Low-peak HDR lost its color space");
    const auto* pixels=static_cast<const std::uint16_t*>(decoded->planes[0]);
    for(unsigned channel=0;channel<3;++channel) {
        const double nits=half_to_float(pixels[(8*decoded->stride[0]+8)*4+channel])*203;
        require(std::abs(nits-value*80)<std::max(.05,double(value)*80*.025),"Low-peak HDR brightness changed during reconstruction");
    }
    std::cout<<"LOW_PEAK "<<index<<" expected nits "<<value*80<<" decoded "<<half_to_float(pixels[0])*203<<"\n";
}
int wmain(int argc,wchar_t** argv) try {
    if(argc!=2) throw std::runtime_error("Specify an owned HDR output evidence directory.");
    Runtime runtime;
    const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
    Frame source;source.width=129;source.height=97;source.hdr=true;source.sdr_white=203;
    source.pixels.resize(source.width*source.height);
    const Pixel patches[]={{0,0,0,1},{2.5375f,2.5375f,2.5375f,1},{12.5f,12.5f,12.5f,1},
                           {-.5876411f*2.5375f,1.1328999f*2.5375f,-.1005789f*2.5375f,1},
                           {1.660491f*2.5375f,-.1245505f*2.5375f,-.0181508f*2.5375f,1},
                           {.01f,.01f,.01f,1}};
    for(unsigned y=0;y<source.height;++y) for(unsigned x=0;x<source.width;++x)
        source.pixels[y*source.width+x]=patches[std::min(y/49,1u)*3+std::min(x/43,2u)];
    ColorSettings settings;settings.mode=ContentMode::Hdr;settings.dither=false;
    const auto path=directory/L"HDR-\u8bd5\u9a8c.jpg";
    const auto expected_sdr=render_sdr(source,settings);
    save_hdr_jpeg(path,source,expected_sdr);
    require(std::filesystem::is_regular_file(path),"HDR JPEG was not created");
    std::ifstream input(path,std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)),{});
    require(bytes.size()>2 && std::uint8_t(bytes[0])==0xff && std::uint8_t(bytes[1])==0xd8,"Output is not JPEG");
    require(bytes.find("hdrgm:Version")!=std::string::npos,"Legacy gain map metadata missing");
    require(bytes.find("urn:iso:std:iso:ts:21496:-1")!=std::string::npos,"ISO gain map metadata missing");
    require(bytes.find("ICC_PROFILE")!=std::string::npos,"SDR fallback color profile missing");
    uhdr_codec_private_t* decoder=uhdr_create_decoder();
    require(decoder!=nullptr,"Cannot create HDR decoder");
    struct Release {uhdr_codec_private_t* value;~Release(){uhdr_release_decoder(value);}} release{decoder};
    uhdr_compressed_image_t compressed{};compressed.data=bytes.data();compressed.data_sz=compressed.capacity=bytes.size();
    compressed.cg=UHDR_CG_UNSPECIFIED;compressed.ct=UHDR_CT_UNSPECIFIED;compressed.range=UHDR_CR_UNSPECIFIED;
    check_hdr_codec(uhdr_dec_set_image(decoder,&compressed),"Cannot read HDR JPEG");
    check_hdr_codec(uhdr_dec_set_out_img_format(decoder,UHDR_IMG_FMT_64bppRGBAHalfFloat),"Cannot select HDR decoding");
    check_hdr_codec(uhdr_dec_set_out_color_transfer(decoder,UHDR_CT_LINEAR),"Cannot select linear decoding");
    check_hdr_codec(uhdr_dec_set_out_max_display_boost(decoder,10000.0f/203),"Cannot select HDR headroom");
    check_hdr_codec(uhdr_dec_probe(decoder),"Cannot inspect HDR JPEG");
    require(uhdr_dec_get_image_width(decoder)==129 && uhdr_dec_get_image_height(decoder)==97,"Odd selection dimensions changed");
    require(uhdr_dec_get_gainmap_width(decoder)==129 && uhdr_dec_get_gainmap_height(decoder)==97,"HDR detail was downscaled");
    const auto* metadata=uhdr_dec_get_gainmap_metadata(decoder);
    require(metadata && std::abs(metadata->hdr_capacity_max-1000.0/203)<.01,"HDR peak metadata has wrong brightness scale");
    const auto* gainmap=uhdr_dec_get_gainmap_image(decoder);
    require(gainmap && gainmap->data_sz,"HDR JPEG is missing gain map pixels");
    check_hdr_codec(uhdr_decode(decoder),"Cannot decode HDR JPEG");
    const auto* decoded=uhdr_get_decoded_image(decoder);
    require(decoded && decoded->cg==UHDR_CG_BT_2100,"Wide HDR gamut was lost");
    const auto* pixels=static_cast<const std::uint16_t*>(decoded->planes[0]);
    const Rgb expected[]={{0,0,0},{1,1,1},{1000.0/203,1000.0/203,1000.0/203},
                          {0,1,0},{1,0,0},{.8/203,.8/203,.8/203}};
    for(unsigned patch=0;patch<6;++patch) {
        const unsigned x=(patch%3)*43+21,y=(patch/3)*49+24;
        const auto offset=(std::size_t(y)*decoded->stride[0]+x)*4;
        const double channels[]={expected[patch].r,expected[patch].g,expected[patch].b};
        for(unsigned channel=0;channel<3;++channel) {
            const double actual=half_to_float(pixels[offset+channel]);
            std::cout<<"HDR_PATCH "<<patch<<" channel "<<channel<<" nits "<<actual*203<<" expected "<<channels[channel]*203<<"\n";
            require(std::abs(actual-channels[channel])<std::max(.005,channels[channel]*.06),"HDR brightness or color round-trip exceeded JPEG tolerance");
        }
    }
    auto factory=imaging_factory();ComPtr<IWICBitmapDecoder> legacy;
    check(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&legacy),"Legacy JPEG viewer could not open file");
    ComPtr<IWICBitmapFrameDecode> frame;check(legacy->GetFrame(0,&frame),"Legacy JPEG base missing");
    UINT width=0,height=0;check(frame->GetSize(&width,&height),"Cannot read JPEG fallback dimensions");
    require(width==129 && height==97,"Legacy JPEG fallback dimensions changed");
    UINT contexts=0;check(frame->GetColorContexts(0,nullptr,&contexts),"Cannot inspect fallback profile");
    require(contexts>0,"Legacy JPEG fallback lacks a readable color profile");
    ComPtr<IWICColorContext> from,to;check(factory->CreateColorContext(&from),"Cannot read source profile");
    IWICColorContext* profile=from.Get();check(frame->GetColorContexts(1,&profile,&contexts),"Cannot read JPEG ICC");
    check(factory->CreateColorContext(&to),"Cannot select fallback destination");
    check(to->InitializeFromExifColorSpace(1),"Cannot select sRGB destination");
    ComPtr<IWICColorTransform> transform;check(factory->CreateColorTransformer(&transform),"Cannot color-manage JPEG fallback");
    check(transform->Initialize(frame.Get(),from.Get(),to.Get(),GUID_WICPixelFormat32bppBGRA),"Cannot convert JPEG fallback to sRGB");
    std::vector<BYTE> fallback(source.width*source.height*4);
    check(transform->CopyPixels(nullptr,source.width*4,UINT(fallback.size()),fallback.data()),"Cannot read ordinary JPEG fallback");
    for(unsigned patch=0;patch<6;++patch) {
        const std::size_t offset=((patch/3*49+24)*source.width+patch%3*43+21)*4;
        const auto lab=[&](const std::vector<std::uint8_t>& pixels) {
            return to_oklab({srgb_decode(pixels[offset+2]/255.0),srgb_decode(pixels[offset+1]/255.0),srgb_decode(pixels[offset]/255.0)});
        };
        const auto actual=lab(fallback),target=lab(expected_sdr.bgra);
        const double distance=std::sqrt(std::pow(actual.l-target.l,2)+std::pow(actual.a-target.a,2)+std::pow(actual.b-target.b,2));
        std::cout<<"SDR_PATCH "<<patch<<" Oklab distance "<<distance<<"\n";
        require(distance<.01,"Color-managed SDR fallback differs from PNG appearance");
    }
    save_master(directory/L"source.jxr",source);
    bool rejected=false;try {save_hdr_jpeg(path,source,expected_sdr);}catch(const std::exception&){rejected=true;}
    require(rejected && std::filesystem::file_size(path)==bytes.size(),"HDR export overwrote an existing file");
    for(int mismatch=0;mismatch<3;++mismatch) {
        Image invalid=expected_sdr;
        if(mismatch==0) ++invalid.width;
        else if(mismatch==1) ++invalid.height;
        else invalid.bgra.pop_back();
        const auto invalid_path=directory/(L"invalid-base-"+std::to_wstring(mismatch)+L".jpg");
        rejected=false;try {save_hdr_jpeg(invalid_path,source,invalid);}catch(const std::exception&){rejected=true;}
        require(rejected && !std::filesystem::exists(invalid_path),"Mismatched preview produced an HDR file");
    }
    unsigned low_peak_index=0;
    for(float value:{0.0f,.01f,1.0f,2.5375f,2.55f}) verify_low_peak(directory,value,low_peak_index++);
    source.pixels[0].r=std::numeric_limits<float>::quiet_NaN();
    rejected=false;try {save_hdr_jpeg(directory/L"invalid.jpg",source,expected_sdr);}catch(const std::exception&){rejected=true;}
    require(rejected && !std::filesystem::exists(directory/L"invalid.jpg"),"Invalid source produced an HDR file");
    for(const auto& entry:std::filesystem::directory_iterator(directory))
        require(entry.path().filename().wstring().find(L".writing-")==std::wstring::npos,"Incomplete HDR file left behind");
    std::cout<<checks<<" HDR output checks passed: dual gain metadata, low HDR peaks, 80-to-203-nit scale, wide gamut, SDR fallback, odd crop, and file safety.\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"HDR output failed after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
