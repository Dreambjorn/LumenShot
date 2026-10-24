#include "image/sharpen.hpp"
#include "image/image_io.hpp"
#include <iostream>

using namespace hdrshot;
static unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
Image solid(unsigned width,unsigned height,std::uint8_t code) {
    Image image;image.width=width;image.height=height;image.bgra.resize(checked_pixels(width,height)*4);
    for(std::size_t i=0;i<image.bgra.size();i+=4) {
        image.bgra[i]=image.bgra[i+1]=image.bgra[i+2]=code;image.bgra[i+3]=255;
    }
    return image;
}
int wmain(int argc,wchar_t** argv) try {
    if(argc!=2)throw std::runtime_error("Specify an owned sharpening evidence directory.");
    Runtime runtime;const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
    for(unsigned code=0;code<256;++code) for(unsigned strength:{0u,1u,15u,50u,100u}) {
        auto image=solid(9,7,std::uint8_t(code));const auto original=image.bgra;
        sharpen_image(image,strength);
        require(image.bgra==original,"Sharpening changed a flat field or black/white level");
    }
    Image random=solid(73,65,0);random.description=L"Owned color pattern";random.reference_white=203;
    std::uint32_t state=0x87654123;
    for(auto& value:random.bgra){state=state*1664525u+1013904223u;value=std::uint8_t(state>>24);}
    const auto original=random;
    sharpen_image(random,0);
    require(random.bgra==original.bgra,"Zero sharpness changed source pixels");
    for(unsigned strength:{15u,50u,100u}) {
        Image a=original,b=original;sharpen_image(a,strength);sharpen_image(b,strength);
        require(a.bgra==b.bgra,"Parallel sharpening is nondeterministic");
        require(a.width==original.width && a.height==original.height,"Sharpening resized the image");
        require(a.description==original.description && a.reference_white==203,"Sharpening changed color metadata");
        for(std::size_t i=3;i<a.bgra.size();i+=4) require(a.bgra[i]==original.bgra[i],"Sharpening changed alpha");
    }
    const std::uint8_t patch[9][3]={{45,70,25},{75,95,55},{95,120,85},
        {95,130,75},{115,130,100},{150,155,125},{125,145,130},{145,170,135},{180,185,165}};
    auto reference=solid(3,3,0);
    for(unsigned i=0;i<9;++i) for(unsigned c=0;c<3;++c) reference.bgra[i*4+2-c]=patch[i][c];
    sharpen_image(reference,100);
    const int expected[]={112,123,99};
    for(unsigned c=0;c<3;++c)
        require(std::abs(int(reference.bgra[4*4+2-c])-expected[c])<=1,"Linear-light CAS reference vector differs");
    for(const auto size:{std::pair{1u,1u},std::pair{1u,31u},std::pair{17u,1u},std::pair{17u,31u}}) {
        auto image=solid(size.first,size.second,0);
        for(std::size_t i=0;i<image.bgra.size();i+=4) image.bgra[i]=image.bgra[i+1]=image.bgra[i+2]=std::uint8_t(i%256);
        sharpen_image(image,100);
        require(image.width==size.first && image.height==size.second,"A thin or odd image was resized");
    }
    auto wave=solid(257,129,0);
    for(unsigned y=0;y<wave.height;++y) for(unsigned x=0;x<wave.width;++x) {
        const auto value=std::uint8_t(std::lround(128+40*std::sin(x*2*3.141592653589793/16)));
        const auto offset=(std::size_t(y)*wave.width+x)*4;
        wave.bgra[offset]=wave.bgra[offset+1]=wave.bgra[offset+2]=value;
    }
    double previous_contrast=0;
    for(unsigned strength:{0u,15u,50u,100u}) {
        auto image=wave;sharpen_image(image,strength);
        double contrast=0;
        for(unsigned x=2;x+2<image.width;++x)
            contrast+=std::abs(int(image.bgra[(std::size_t(64)*image.width+x)*4])-int(image.bgra[(std::size_t(64)*image.width+x-1)*4]));
        require(contrast>=previous_contrast,"Sharpness strength reduced edge contrast");
        if(strength==100) require(contrast>previous_contrast,"Sharpening did not increase softened detail contrast");
        previous_contrast=contrast;
        for(unsigned y=1;y<image.height;++y)
            require(std::memcmp(image.bgra.data(),image.bgra.data()+std::size_t(y)*image.width*4,image.width*4)==0,
                    "Parallel rows introduced seams or directional smearing");
        save_image(directory/(L"sharpness-"+std::to_wstring(strength)+L".png"),image);
    }
    bool rejected=false;try{auto image=wave;sharpen_image(image,101);}catch(const std::exception&){rejected=true;}
    require(rejected,"Invalid sharpness was accepted");
    rejected=false;try{auto image=wave;image.bgra.pop_back();sharpen_image(image,15);}catch(const std::exception&){rejected=true;}
    require(rejected,"Incomplete sharpening pixels were accepted");
    rejected=false;try{Image empty;sharpen_image(empty,0);}catch(const std::exception&){rejected=true;}
    require(rejected,"Invalid sharpening dimensions were accepted");
    std::cout<<checks<<" sharpening checks passed: exact bypass, flat fields, linear CAS reference, alpha, native dimensions, deterministic rows, and strength.\n";
    return 0;
} catch(const std::exception& error){std::cerr<<"Sharpening failed after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
