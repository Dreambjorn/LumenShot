#include "avif_test_helpers.hpp"
#include "hdr_fixtures.hpp"
#include "app/workflow.hpp"
#include <iostream>

using namespace hdrshot;
static unsigned checks=0;
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
template<class F> void rejects(F function,const char* message) {
    bool failed=false;try {function();}catch(const std::exception&){failed=true;}
    require(failed,message);
}
std::string file_bytes(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};
}
void write_bytes(const std::filesystem::path& path,const std::string& data) {
    std::ofstream output(path,std::ios::binary);output.write(data.data(),std::streamsize(data.size()));
    output.close();if(!output) throw std::runtime_error("Cannot write owned AVIF mutation.");
}

int wmain(int argc,wchar_t** argv) try {
    if(argc!=2) throw std::runtime_error("Specify an owned AVIF evidence directory.");
    Runtime runtime;const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
    require(hdr_avif_quality(HdrAvifQuality::Full)==100 && hdr_avif_quality(HdrAvifQuality::High)==95
            && hdr_avif_quality(HdrAvifQuality::Balanced)==90 && hdr_avif_quality(HdrAvifQuality::Compact)==85,
            "AVIF presets changed their measured quality levels");
    require(hdr_avif_quality(HdrAvifQuality(-1))==100,"Invalid AVIF quality reduced fidelity");
    std::filesystem::path sample;unsigned outputs=0;
    for(unsigned scene:{0u,1u,3u,4u,7u}) for(unsigned quality=0;quality<4;++quality) {
        const auto fixture=fixtures::make(scene);const auto frozen=fixture.source.pixels;
        const auto path=directory/(std::string(fixture.name)+"-"+std::to_string(quality)+".avif");
        auto diagnostics=std::make_shared<DiagnosticRecord>(DiagnosticSource::Synthetic);
        {DiagnosticContext context(diagnostics);save_hdr_avif(path,fixture.source,HdrAvifQuality(quality));}
        const auto record=diagnostics->snapshot();
        require(record.stages[std::size_t(DiagnosticStage::EncodeAvif)].calls==1
                && record.stages[std::size_t(DiagnosticStage::Validate)].calls==1
                && record.stages[std::size_t(DiagnosticStage::Commit)].calls==1,"AVIF skipped encode, decode validation or commit diagnostics");
        require(std::memcmp(frozen.data(),fixture.source.pixels.data(),frozen.size()*sizeof(Pixel))==0,"AVIF modified the captured master");
        validate_hdr_avif(path,fixture.source.width,fixture.source.height);
        const auto decoded=fixtures::decode_avif_bt2020(path);
        require(decoded.width==fixture.source.width && decoded.height==fixture.source.height,"AVIF changed dimensions");
        double maximum_error=0;
        for(std::size_t i=0;i<frozen.size();++i) {
            const auto expected=to_bt2020({frozen[i].r,frozen[i].g,frozen[i].b});const auto actual=decoded.pixels[i];
            const double before[]={expected.r,expected.g,expected.b},after[]={actual.r,actual.g,actual.b};
            for(unsigned c=0;c<3;++c)
                maximum_error=std::max(maximum_error,std::abs(pq_encode(std::clamp(before[c]*80,0.0,10000.0))
                    -pq_encode(std::clamp(after[c]*203,0.0,10000.0))));
        }
        require(maximum_error<(quality==0?.0016:.06),"AVIF lost HDR highlights, color or near-black precision");
        if(scene==1) require(decoded.pixels.back().r*203>9000,"AVIF clipped the isolated 10000-nit highlight");
        if(scene==0 && quality==0) sample=path;
        ++outputs;
    }
    Frame tiny;tiny.width=tiny.height=1;tiny.hdr=true;tiny.pixels={{12.5f,12.5f,12.5f,1}};
    const auto tiny_path=directory/L"tiny-\u9a8c.avif";save_hdr_avif(tiny_path,tiny);
    require(fixtures::decode_avif_bt2020(tiny_path).pixels[0].r*203>990,"Tiny AVIF lost its HDR pixel");
    auto black=tiny;black.pixels[0]={0,0,0,1};save_hdr_avif(directory/L"black.avif",black);
    require(fixtures::decode_avif_bt2020(directory/L"black.avif").pixels[0].r==0,"Black AVIF gained light or invalid metadata");
    const auto original=file_bytes(sample);
    rejects([&]{save_hdr_avif(sample,fixtures::make(0).source);},"AVIF overwrote an existing image");
    require(file_bytes(sample)==original,"AVIF collision changed the original file");
    rejects([&]{validate_hdr_avif(sample,257,256);},"AVIF accepted different dimensions");
    const avifContentLightLevelInformationBox wrong_light{1,1};
    rejects([&]{validate_hdr_avif(sample,256,256,&wrong_light);},"AVIF accepted different light metadata");
    for(const auto length:{std::size_t(8),original.size()/2,original.size()-1}) {
        const auto path=directory/(L"truncated-"+std::to_wstring(length)+L".avif");write_bytes(path,original.substr(0,length));
        rejects([&]{validate_hdr_avif(path,256,256);},"AVIF accepted a truncated payload");
    }
    auto corrupt=original;const auto mdat=corrupt.find("mdat");require(mdat!=std::string::npos,"AVIF fixture has no AV1 payload");
    std::fill(corrupt.begin()+std::ptrdiff_t(mdat+4),corrupt.end(),0);const auto bad_payload=directory/L"bad-payload.avif";
    write_bytes(bad_payload,corrupt);rejects([&]{validate_hdr_avif(bad_payload,256,256);},"AVIF accepted an undecodable payload");
    corrupt=original;const auto nclx=corrupt.find("nclx");require(nclx!=std::string::npos,"AVIF fixture has no explicit color encoding");
    corrupt[nclx+7]=1;const auto bad_transfer=directory/L"bad-transfer.avif";write_bytes(bad_transfer,corrupt);
    rejects([&]{validate_hdr_avif(bad_transfer,256,256);},"AVIF accepted non-PQ pixels");
    for(unsigned failure=0;failure<5;++failure) {
        auto invalid=tiny;
        if(failure==0) invalid.hdr=false;
        if(failure==1) invalid.pixels.clear();
        if(failure==2) invalid.width=0;
        if(failure==3) invalid.pixels[0].r=std::numeric_limits<float>::quiet_NaN();
        if(failure==4) invalid.pixels[0].b=std::numeric_limits<float>::infinity();
        const auto path=directory/(L"invalid-"+std::to_wstring(failure)+L".avif");
        rejects([&]{save_hdr_avif(path,invalid);},"AVIF accepted invalid source pixels");
        require(!std::filesystem::exists(path),"Invalid AVIF input left output");
    }
    const auto failed_commit=directory/L"commit-race.avif";
    {
        OutputTransaction transaction(failed_commit);std::filesystem::copy_file(sample,transaction.path(),std::filesystem::copy_options::overwrite_existing);
        validate_hdr_avif(transaction.path(),256,256);write_bytes(failed_commit,"existing");
        rejects([&]{transaction.commit();},"AVIF commit overwrote a concurrent destination");
    }
    require(file_bytes(failed_commit)=="existing","AVIF commit changed another writer's output");
    for(const auto& entry:std::filesystem::directory_iterator(directory))
        require(entry.path().filename().wstring().find(L".writing-")==std::wstring::npos,"AVIF left an orphan transaction");
    std::cout<<checks<<" AVIF checks passed: "<<outputs<<" HDR round trips, immutable masters, 10-bit PQ precision, all qualities, tiny/black images, metadata, corruption and atomic output failures.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"AVIF fixture failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
