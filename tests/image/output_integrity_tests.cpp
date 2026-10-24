#include "image/hdr_output.hpp"
#include <iostream>

using namespace hdrshot;
static unsigned checks=0;
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
template<class F> void rejects(F function,const char* message) {
    bool failed=false;try {function();} catch(const std::exception&) {failed=true;}
    require(failed,message);
}
std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input) throw std::runtime_error("Cannot read owned integrity fixture.");
    return {std::istreambuf_iterator<char>(input),{}};
}
void write_file(const std::filesystem::path& path,const std::string& bytes) {
    std::ofstream output(path,std::ios::binary);output.write(bytes.data(),std::streamsize(bytes.size()));
    output.close();if(!output) throw std::runtime_error("Cannot write owned integrity fixture.");
}
void floating_master(const std::filesystem::path& path,const Frame& source,const GUID& format,unsigned bytes,const uhdr_mem_block_t* profile=nullptr) {
    auto factory=imaging_factory();ComPtr<IWICStream> stream;check(factory->CreateStream(&stream),"Cannot create float fixture stream");
    check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"Cannot open float fixture");
    ComPtr<IWICBitmapEncoder> encoder;check(factory->CreateEncoder(GUID_ContainerFormatWmp,nullptr,&encoder),"Cannot create float fixture encoder");
    check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Cannot initialize float fixture encoder");
    ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> properties;
    check(encoder->CreateNewFrame(&frame,&properties),"Cannot create float fixture frame");
    PROPBAG2 option{};option.pstrName=const_cast<wchar_t*>(L"Lossless");
    VARIANT lossless{};lossless.vt=VT_BOOL;lossless.boolVal=VARIANT_TRUE;
    check(properties->Write(1,&option,&lossless),"Cannot select float fixture losslessness");
    check(frame->Initialize(properties.Get()),"Cannot initialize float fixture frame");
    check(frame->SetSize(source.width,source.height),"Cannot set float fixture dimensions");
    auto selected=format;check(frame->SetPixelFormat(&selected),"Cannot select float fixture format");
    require(IsEqualGUID(selected,format),"Float fixture encoder changed format");
    if(profile) {
        ComPtr<IWICColorContext> context;check(factory->CreateColorContext(&context),"Cannot create owned color context");
        check(context->InitializeFromMemory(static_cast<BYTE*>(profile->data),UINT(profile->data_sz)),"Cannot initialize owned color context");
        IWICColorContext* value=context.Get();check(frame->SetColorContexts(1,&value),"Cannot profile owned HDR fixture");
    }
    const unsigned channels=bytes==6 || bytes==12?3:4,component=bytes/channels;
    std::vector<BYTE> pixels(source.pixels.size()*bytes);
    for(std::size_t i=0;i<source.pixels.size();++i) {
        const auto& p=source.pixels[i];const float values[]={p.r+.000013f,p.g,p.b,1};
        for(unsigned c=0;c<channels;++c) {
            auto* target=pixels.data()+i*bytes+c*component;
            if(component==2) {const auto half=float_to_half(values[c]);std::memcpy(target,&half,2);}
            else std::memcpy(target,&values[c],4);
        }
    }
    check(frame->WritePixels(source.height,source.width*bytes,UINT(pixels.size()),pixels.data()),"Cannot write float fixture");
    check(frame->Commit(),"Cannot finish float fixture frame");check(encoder->Commit(),"Cannot finish float fixture");
}

int wmain(int argc,wchar_t** argv) try {
    if(argc!=2) throw std::runtime_error("Specify an owned output integrity directory.");
    Runtime runtime;const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
    auto factory=imaging_factory();
    Frame source;source.width=129;source.height=97;source.hdr=true;source.sdr_white=203;
    source.pixels.resize(source.width*source.height);
    for(unsigned y=0;y<source.height;++y) for(unsigned x=0;x<source.width;++x)
        source.pixels[y*source.width+x]={float(x)/16,float(y)/16,.125f,1};
    source.pixels[0]={-0.0f,half_to_float(1),half_to_float(0x8001),1};
    const auto image=render_sdr(source,{ContentMode::Hdr,203,0,false});
    const auto png=directory/L"valid.png",jxr=directory/L"valid.jxr",jpeg=directory/L"valid.jpg";
    auto record=std::make_shared<DiagnosticRecord>(DiagnosticSource::Synthetic);
    {
        DiagnosticContext context(record);
        save_image(png,image);save_master(jxr,source);save_hdr_jpeg(jpeg,source,image);
    }
    require(record->snapshot().stages[std::size_t(DiagnosticStage::Validate)].calls==3,"An encoder skipped validation");
    require(record->snapshot().stages[std::size_t(DiagnosticStage::Commit)].calls==3,"Valid output was not committed once per format");
    const auto png_bytes=read_file(png),jxr_bytes=read_file(jxr),jpeg_bytes=read_file(jpeg);
    const auto decoded=load_master(jxr).pixels[0];
    require(decoded.r==0 && decoded.g==source.pixels[0].g && decoded.b==source.pixels[0].b,
            "JPEG XR changed signed zero or nonzero subnormal samples");
    rejects([&]{save_image(png,image);},"PNG collision was accepted");
    rejects([&]{save_master(jxr,source);},"JPEG XR collision was accepted");
    rejects([&]{save_hdr_jpeg(jpeg,source,image);},"HDR JPEG collision was accepted");
    require(read_file(png)==png_bytes && read_file(jxr)==jxr_bytes && read_file(jpeg)==jpeg_bytes,"A collision changed existing output");
    for(const auto& item:{std::make_pair(png,png_bytes),std::make_pair(jxr,jxr_bytes),std::make_pair(jpeg,jpeg_bytes)}) {
        for(const auto size:{std::size_t(8),item.second.size()/2}) {
            const auto malformed=directory/(item.first.stem().wstring()+L"-truncated-"+std::to_wstring(size)+item.first.extension().wstring());
            write_file(malformed,item.second.substr(0,size));
            rejects([&]{
                if(item.first==jpeg) validate_hdr_jpeg(malformed,source.width,source.height);
                else validate_wic_output(factory.Get(),malformed,item.first==png?WicOutput::SrgbPng:WicOutput::ScRgbJpegXr,source.width,source.height);
            },"Truncated output was accepted");
        }
    }
    auto corrupt=png_bytes;const auto idat=corrupt.find("IDAT");require(idat!=std::string::npos,"PNG fixture has no pixels");
    corrupt[idat+4]=corrupt[idat+5]=0;const auto bad_png=directory/L"bad-payload.png";write_file(bad_png,corrupt);
    rejects([&]{validate_wic_output(factory.Get(),bad_png,WicOutput::SrgbPng,image.width,image.height);},"Broken PNG payload was accepted");
    for(int intent:{-1,1}) {
        const auto path=directory/(L"intent-"+std::to_wstring(intent)+L".png");
        auto metadata_bytes=png_bytes;const auto srgb=metadata_bytes.find("sRGB");
        require(srgb!=std::string::npos && srgb>=4 && metadata_bytes[srgb-1]==1,"PNG fixture has no sRGB intent");
        if(intent<0) metadata_bytes.erase(srgb-4,13);
        else {
            metadata_bytes[srgb+4]=char(intent);
            std::uint32_t crc=0xffffffffu;
            for(unsigned i=0;i<5;++i) {
                crc^=std::uint8_t(metadata_bytes[srgb+i]);
                for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
            }
            crc^=0xffffffffu;
            for(unsigned i=0;i<4;++i) metadata_bytes[srgb+5+i]=char(crc>>(24-i*8));
        }
        write_file(path,metadata_bytes);
        rejects([&]{validate_wic_output(factory.Get(),path,WicOutput::SrgbPng,image.width,image.height);},"Invalid sRGB metadata was accepted");
    }
    rejects([&]{validate_wic_output(factory.Get(),png,WicOutput::SrgbPng,image.width+1,image.height);},"PNG size mismatch was accepted");
    rejects([&]{validate_wic_output(factory.Get(),jxr,WicOutput::ScRgbJpegXr,source.width,source.height+1);},"JPEG XR size mismatch was accepted");
    rejects([&]{validate_hdr_jpeg(jpeg,source.width+1,source.height);},"HDR JPEG size mismatch was accepted");
    rejects([&]{validate_hdr_jpeg(jpeg,source.width,source.height,10000);},"HDR peak mismatch was accepted");
    rejects([&]{validate_wic_output(factory.Get(),png,WicOutput::ScRgbJpegXr,image.width,image.height);},"HDR container mismatch was accepted");
    auto changed=image.bgra;changed[0]^=1;
    rejects([&]{validate_wic_output(factory.Get(),png,WicOutput::SrgbPng,image.width,image.height,changed.data());},"Lossless pixel mismatch was accepted");
    const auto copied=directory/L"copied.jxr";copy_master(jxr,copied,source.width,source.height);
    require(read_file(copied)==jxr_bytes,"Imported original was changed");
    rejects([&]{copy_master(jxr,copied,source.width,source.height);},"Import copy overwrote existing output");
    const auto failed_copy=directory/L"failed-copy.jxr";
    rejects([&]{copy_master(bad_png,failed_copy,source.width,source.height);},"Malformed import copy was committed");
    require(!std::filesystem::exists(failed_copy),"Failed import left a partial file");
    const struct {const GUID* format;unsigned bytes;} formats[]={
        {&GUID_WICPixelFormat64bppRGBAHalf,8},{&GUID_WICPixelFormat128bppRGBAFloat,16}};
    for(const auto& format:formats) {
        const auto input=directory/(L"float-"+std::to_wstring(format.bytes)+L".jxr");
        const auto output=directory/(L"float-copy-"+std::to_wstring(format.bytes)+L".jxr");
        floating_master(input,source,*format.format,format.bytes);copy_master(input,output,source.width,source.height);
        require(read_file(input)==read_file(output),"Imported float encoding or precision changed");
        const auto original=load_master(input),loaded=load_master(output);
        require(loaded.pixels[130].r==original.pixels[130].r,"Imported float value changed");
        if(format.bytes==16)
            require(loaded.pixels[130].r!=half_to_float(float_to_half(loaded.pixels[130].r)),"Float fixture has no precision beyond FP16");
    }
    uhdr_codec_private_t* hdr=uhdr_create_decoder();require(hdr!=nullptr,"Cannot create ordinary JPEG fixture");
    struct Release {uhdr_codec_private_t* value;~Release(){uhdr_release_decoder(value);}} release{hdr};
    auto jpeg_buffer=jpeg_bytes;uhdr_compressed_image_t compressed{};
    compressed.data=jpeg_buffer.data();compressed.data_sz=compressed.capacity=jpeg_buffer.size();
    check_hdr_codec(uhdr_dec_set_image(hdr,&compressed),"Cannot read HDR base fixture");
    check_hdr_codec(uhdr_dec_probe(hdr),"Cannot inspect HDR base fixture");
    const auto* profile=uhdr_dec_get_icc(hdr);require(profile && profile->data,"Owned ICC fixture is missing");
    const auto profiled=directory/L"profiled.jxr";floating_master(profiled,source,GUID_WICPixelFormat64bppRGBAHalf,8,profile);
    rejects([&]{copy_master(profiled,directory/L"profiled-copy.jxr",source.width,source.height);},"Profiled HDR import was accepted");
    require(!std::filesystem::exists(directory/L"profiled-copy.jxr"),"Rejected color profile left output");
    const auto* base=uhdr_dec_get_base_image(hdr);require(base && base->data,"HDR base fixture is missing");
    const auto ordinary=directory/L"ordinary.jpg";
    write_file(ordinary,std::string(static_cast<const char*>(base->data),base->data_sz));
    rejects([&]{validate_hdr_jpeg(ordinary,source.width,source.height);},"Ordinary JPEG was accepted as HDR");
    const auto codec_failure=directory/L"codec-failure.png";
    rejects([&]{
        OutputTransaction transaction(codec_failure);ComPtr<IWICBitmapEncoder> encoder;const GUID unavailable{};
        check(factory->CreateEncoder(unavailable,nullptr,&encoder),"Injected unavailable encoder");
    },"Unavailable codec was accepted");
    require(!std::filesystem::exists(codec_failure),"Codec failure left output");
    const auto write_failure=directory/L"write-failure.png";bool write_attempt=false;
    rejects([&]{
        OutputTransaction transaction(write_failure);
        HANDLE locked=CreateFileW(transaction.path().c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(locked==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot lock owned write fixture.");
        struct Unlock {HANDLE file;~Unlock(){CloseHandle(file);}} unlock{locked};
        ComPtr<IWICStream> stream;check(factory->CreateStream(&stream),"Cannot create failing stream");
        write_attempt=true;check(stream->InitializeFromFilename(transaction.path().c_str(),GENERIC_WRITE),"Injected output write failure");
    },"Blocked output write was accepted");
    require(write_attempt,"Write fixture failed before the injected error");
    require(!std::filesystem::exists(write_failure),"Write failure left output");
    const auto collision=directory/L"commit-race.png";
    rejects([&]{OutputTransaction transaction(collision);write_file(transaction.path(),png_bytes);write_file(collision,"preserve me");transaction.commit();},
            "Commit replaced a destination created during encoding");
    require(read_file(collision)=="preserve me","Commit collision changed the other file");
    const auto uncommitted=directory/L"malformed-transaction.png";
    rejects([&]{
        OutputTransaction transaction(uncommitted);write_file(transaction.path(),"bad codec output");
        validate_wic_output(factory.Get(),transaction.path(),WicOutput::SrgbPng,image.width,image.height);transaction.commit();
    },"Malformed codec output was committed");
    require(!std::filesystem::exists(uncommitted),"Malformed codec output left a partial file");
    for(const auto& entry:std::filesystem::directory_iterator(directory))
        require(entry.path().filename().wstring().find(L".writing-")==std::wstring::npos,"Unfinished transaction remains");
    std::cout<<checks<<" output integrity checks passed: all formats, pixel decoding, metadata, truncated data, collisions, codec/write failures, and atomic import copies.\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"Output integrity failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
