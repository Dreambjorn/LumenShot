#include "app/app.hpp"
#include "../image/avif_test_helpers.hpp"
#include <iostream>

using namespace hdrshot;

static unsigned checks=0;
void require(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}

Frame clipboard_frame() {
    Frame frame;frame.width=64;frame.height=48;frame.hdr=true;frame.sdr_white=203;
    frame.pixels.assign(checked_pixels(frame.width,frame.height),Pixel{12.5f,6.25f,3.125f,1});
    Annotation redaction;redaction.tool=MarkTool::Redact;redaction.points={{0,0},{31,31}};
    apply_privacy_marks(frame,{redaction});return frame;
}
Image clipboard_image() {return render_sdr(clipboard_frame(),ColorSettings{ContentMode::Hdr,203,0,false});}

class ClipboardStation {
    HWINSTA original_station_=GetProcessWindowStation(),station_=nullptr;
    HDESK original_desktop_=GetThreadDesktop(GetCurrentThreadId()),desktop_=nullptr;
    HANDLE reservation_=nullptr;
    bool clipboard_reserved_=false;
public:
    std::wstring desktop;
    ClipboardStation() {
        station_=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);
        if(!station_) throw std::runtime_error("Cannot create isolated clipboard window station: Win32 "+std::to_string(GetLastError()));
        ++checks;
        wchar_t name[256]{};DWORD needed=0;
        require(GetUserObjectInformationW(station_,UOI_NAME,name,sizeof(name),&needed)!=FALSE,"Cannot identify owned clipboard station");
        require(std::wstring(name)!=L"WinSta0","Refusing to reserve the interactive clipboard");
        const auto mutex=L"Local\\LumenShotClipboardFixture-"+std::wstring(name);
        reservation_=CreateMutexW(nullptr,FALSE,mutex.c_str());
        require(reservation_ && WaitForSingleObject(reservation_,0)==WAIT_OBJECT_0,"Another isolated clipboard fixture is active");
        const auto owned=L"LumenShotClipboard-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        desktop=std::wstring(name)+L"\\"+owned;
        require(SetProcessWindowStation(station_)!=FALSE,"Cannot enter isolated clipboard window station");
        desktop_=CreateDesktopW(owned.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
        require(desktop_ && SetThreadDesktop(desktop_),"Cannot enter isolated clipboard desktop");
        require(OpenClipboard(nullptr)!=FALSE,"Cannot inspect noninteractive clipboard before reservation");
        const bool empty=CountClipboardFormats()==0;CloseClipboard();
        require(empty,"Refusing to replace existing noninteractive clipboard contents");
        clipboard_reserved_=true;
    }
    ~ClipboardStation() {
        if(clipboard_reserved_ && OpenClipboard(nullptr)) {EmptyClipboard();CloseClipboard();}
        SetThreadDesktop(original_desktop_);SetProcessWindowStation(original_station_);
        if(desktop_) CloseDesktop(desktop_);
        if(station_) CloseWindowStation(station_);
        if(reservation_) {ReleaseMutex(reservation_);CloseHandle(reservation_);}
    }
};

void require_isolated(const std::wstring& desktop) {
    wchar_t name[256]{};DWORD needed=0;
    require(GetUserObjectInformationW(GetProcessWindowStation(),UOI_NAME,name,sizeof(name),&needed)!=FALSE,
            "Cannot identify clipboard window station");
    require(std::wstring(name)!=L"WinSta0" && desktop.find(std::wstring(name)+L"\\LumenShotClipboard-")==0,
            "Refusing to touch the interactive clipboard");
}

void producer(const std::wstring& desktop,const std::filesystem::path& path,const std::wstring& expectation) {
    require_isolated(desktop);
    HWND owner=CreateWindowExW(0,L"STATIC",L"Owned clipboard producer",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(owner!=nullptr,"Cannot create isolated clipboard owner");
    struct Destroy {HWND value;~Destroy(){DestroyWindow(value);}} destroy{owner};
    auto image=clipboard_image();
    if(expectation==L"invalid") image.bgra.pop_back();
    try {
        const bool hdr=copy_image(owner,image,path,expectation==L"hdr-only" || expectation==L"hdr-only-missing");
        require(expectation!=L"busy" && expectation!=L"invalid","Clipboard operation unexpectedly succeeded");
        require(hdr==(expectation==L"hdr" || expectation==L"hdr-only"),"Optional HDR clipboard result is incorrect");
    } catch(const std::runtime_error& error) {
        const std::string message=error.what();
        if(expectation==L"busy") require(message=="Clipboard is busy. Try copying again.","Busy clipboard error changed");
        else if(expectation==L"invalid") require(message=="Invalid clipboard pixels.","Invalid SDR payload error changed");
        else throw;
    }
}

void run_producer(const std::wstring& desktop,const std::filesystem::path& path,const wchar_t* expectation) {
    const auto program=executable_directory()/L"clipboard_tests.exe";
    std::wstring command=L"\""+program.wstring()+L"\" --producer \""+desktop+L"\" \""+path.wstring()+L"\" "+expectation;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.lpDesktop=const_cast<wchar_t*>(desktop.c_str());
    PROCESS_INFORMATION process{};
    require(CreateProcessW(program.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,
            "Cannot start isolated clipboard producer");
    struct Close {PROCESS_INFORMATION value;~Close(){CloseHandle(value.hThread);CloseHandle(value.hProcess);}} close{process};
    if(WaitForSingleObject(process.hProcess,5000)!=WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,1000);
        throw std::runtime_error("Isolated clipboard producer timed out.");
    }
    DWORD code=1;require(GetExitCodeProcess(process.hProcess,&code)!=FALSE,"Cannot read clipboard producer result");
    if(code) throw std::runtime_error("Isolated clipboard producer failed: "+std::to_string(code));
    ++checks;
}

void inspect_clipboard(const Image& image,const std::filesystem::path& path={},bool hdr_only=false) {
    require(OpenClipboard(nullptr)!=FALSE,"Cannot read isolated clipboard");
    struct Close {~Close(){CloseClipboard();}} close;
    UINT preference[]={CF_DIBV5,CF_HDROP};
    require(GetPriorityClipboardFormat(preference,2)==(hdr_only?CF_HDROP:CF_DIBV5),"Clipboard preferred representation differs from the selected mode");
    if(hdr_only) {
        require(!IsClipboardFormatAvailable(CF_DIBV5) && !IsClipboardFormatAvailable(CF_DIB) && !IsClipboardFormatAvailable(CF_BITMAP),
                "HDR-only clipboard still offers an SDR bitmap");
    } else {
        HGLOBAL dib=GetClipboardData(CF_DIBV5);
        const auto expected=clipboard_dib(image);
        require(dib && GlobalSize(dib)>=expected.size(),"SDR clipboard did not survive producer exit");
        const void* pixels=GlobalLock(dib);require(pixels!=nullptr,"Cannot lock SDR clipboard payload");
        const bool equal=std::memcmp(pixels,expected.data(),expected.size())==0;GlobalUnlock(dib);
        require(equal,"Native clipboard changed the exact SDR payload");
    }
    require((IsClipboardFormatAvailable(CF_HDROP)!=FALSE)==!path.empty(),"Native clipboard HDR availability is incorrect");
    if(path.empty()) return;
    const UINT effect_format=RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    HGLOBAL effect=GetClipboardData(effect_format);
    require(effect && GlobalSize(effect)>=sizeof(DWORD),"HDR file clipboard omitted its copy preference");
    const auto* action=static_cast<const DWORD*>(GlobalLock(effect));require(action!=nullptr,"Cannot inspect clipboard copy preference");
    const bool copying=*action==DROPEFFECT_COPY;GlobalUnlock(effect);
    require(copying,"HDR clipboard requested moving its saved artifact");
    HDROP drop=static_cast<HDROP>(GetClipboardData(CF_HDROP));
    require(drop && DragQueryFileW(drop,0xffffffff,nullptr,0)==1,"File consumer did not receive exactly one HDR artifact");
    const UINT length=DragQueryFileW(drop,0,nullptr,0);std::wstring copied(length+1,L'\0');
    require(DragQueryFileW(drop,0,copied.data(),UINT(copied.size()))==length,"Cannot retrieve copied HDR file path");
    copied.resize(length);require(copied==path.wstring(),"Unicode HDR clipboard path changed");
    const auto* header=static_cast<const DROPFILES*>(GlobalLock(drop));require(header!=nullptr,"Cannot inspect file clipboard payload");
    const bool wide=header->fWide && header->pFiles==sizeof(DROPFILES);
    const auto* names=reinterpret_cast<const wchar_t*>(reinterpret_cast<const BYTE*>(header)+header->pFiles);
    const bool terminated=names[length]==L'\0' && names[length+1]==L'\0';GlobalUnlock(drop);
    require(wide && terminated,"HDR file clipboard lost Unicode or double termination");
    require(std::filesystem::is_regular_file(path),"HDR clipboard artifact did not survive producer exit");
}

void inspect_hdr(const std::filesystem::path& path) {
    if(path.extension()==L".avif") {
        validate_hdr_avif(path,64,48);const auto frame=fixtures::decode_avif_bt2020(path);
        const auto secret=frame.pixels[8*frame.width+8],light=frame.pixels.back();
        require(std::abs(secret.r)<.001 && std::abs(secret.g)<.001 && std::abs(secret.b)<.001,"Copied AVIF exposed redacted pixels");
        require(light.r*203>650 && light.g*203>400 && light.b*203>200,"Copied AVIF lost HDR highlights");
        return;
    }
    if(path.extension()==L".jxr") {
        const auto frame=load_master(path);
        require(frame.width==64 && frame.height==48,"Copied JPEG XR changed dimensions");
        const auto secret=frame.pixels[8*frame.width+8],light=frame.pixels.back();
        require(secret.r==0 && secret.g==0 && secret.b==0,"Copied JPEG XR exposed redacted pixels");
        require(light.r==12.5f && light.g==6.25f && light.b==3.125f,"Copied JPEG XR lost HDR highlights");
        return;
    }
    validate_hdr_jpeg(path,64,48);
    std::ifstream input(path,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});
    uhdr_codec_private_t* decoder=uhdr_create_decoder();require(decoder!=nullptr,"Cannot create clipboard HDR consumer");
    struct Release {uhdr_codec_private_t* value;~Release(){uhdr_release_decoder(value);}} release{decoder};
    uhdr_compressed_image_t compressed{};compressed.data=bytes.data();compressed.data_sz=compressed.capacity=bytes.size();
    check_hdr_codec(uhdr_dec_set_image(decoder,&compressed),"Cannot read copied HDR JPEG");
    check_hdr_codec(uhdr_dec_set_out_img_format(decoder,UHDR_IMG_FMT_64bppRGBAHalfFloat),"Cannot select copied HDR pixels");
    check_hdr_codec(uhdr_dec_set_out_color_transfer(decoder,UHDR_CT_LINEAR),"Cannot select copied HDR transfer");
    check_hdr_codec(uhdr_dec_set_out_max_display_boost(decoder,10000.f/203),"Cannot select copied HDR headroom");
    check_hdr_codec(uhdr_decode(decoder),"Cannot reconstruct copied HDR JPEG");
    const auto* decoded=uhdr_get_decoded_image(decoder);
    require(decoded && decoded->w==64 && decoded->h==48,"Copied HDR JPEG changed dimensions");
    const auto* pixels=static_cast<const std::uint16_t*>(decoded->planes[0]);
    const auto dark=(8*decoded->stride[0]+8)*4,bright=(47*decoded->stride[0]+63)*4;
    require(half_to_float(pixels[dark])<.001f && half_to_float(pixels[dark+1])<.001f && half_to_float(pixels[dark+2])<.001f,
            "Copied HDR JPEG exposed redacted pixels");
    require(half_to_float(pixels[bright])>3,"Copied HDR JPEG lost HDR highlights");
}

void finish_copy(const std::filesystem::path& path,const wchar_t* expectation) {
    HWND owner=CreateWindowExW(0,L"STATIC",L"Owned clipboard completion",0,0,0,0,0,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(owner!=nullptr,"Cannot create clipboard completion owner");
    struct Destroy {HWND value;~Destroy(){DestroyWindow(value);}} destroy{owner};
    App app;app.window=owner;
    app.status=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_MULTILINE,0,0,0,0,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(app.status!=nullptr,"Cannot create clipboard completion status");
    WorkResult output;output.success=true;output.status=L"Copied screenshot to clipboard";
    output.clipboard=clipboard_image();output.clipboard_hdr=path;
    const std::wstring expected(expectation);
    output.clipboard_mode=(expected==L"hdr-only" || expected==L"hdr-only-missing")?ClipboardMode::HdrFile:path.empty()?ClipboardMode::SdrImage:ClipboardMode::Both;
    if(expected==L"invalid") output.clipboard.bgra.pop_back();
    app.busy=true;app.worker.start([&]{app.result=output;});app.finish_work();
    require(!app.busy,"Clipboard completion left its worker busy");
    if(expected==L"invalid") {
        require(!app.result.success && app.result.status==L"Copy failed","Mandatory clipboard failure reported success");
        require(app.result.details.find(L"Invalid clipboard pixels.")!=std::wstring::npos,"Mandatory clipboard failure lost its error");
    } else {
        require(app.result.success,"Optional HDR clipboard failure prevented SDR success");
        if(expected==L"hdr-only") require(app.result.details.find(L"saved HDR file is available")!=std::wstring::npos,"HDR-only completion omitted its result");
        else if(expected==L"hdr") require(app.result.details.find(L"saved HDR file and SDR image")!=std::wstring::npos,"HDR clipboard completion omitted its result");
        else if(expected==L"missing" || expected==L"hdr-only-missing") require(app.result.details.find(L"The SDR image was copied.")!=std::wstring::npos,"Optional HDR clipboard failure was hidden");
        else require(app.result.details.empty(),"Default SDR completion acquired HDR messaging");
    }
    if(expected==L"hdr-only") require(app.result.status==L"Copied HDR file to clipboard","HDR-only status claimed SDR copying");
    else if(expected==L"hdr") require(app.result.status==L"Copied SDR image and HDR file to clipboard","Both-mode status lost a representation");
    else if(expected!=L"invalid") require(app.result.status==L"Copied SDR image to clipboard","SDR completion claimed HDR copying");
    require(text(app.status).find(app.result.status)!=std::wstring::npos,"Clipboard completion did not update application status");
}

int wmain(int argc,wchar_t** argv) try {
    if(argc==5 && std::wstring(argv[1])==L"--producer") {producer(argv[2],argv[3],argv[4]);return 0;}
    if(argc!=2) throw std::runtime_error("Specify an owned clipboard evidence directory.");
    ClipboardStation station;require_isolated(station.desktop);
    Runtime runtime;
    const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
    const auto image=clipboard_image();const auto frame=clipboard_frame();const auto original=frame.pixels;
    Settings settings;settings.folder=directory/L"default";
    auto result=process_output(settings,frame,image,{},true);
    require(result.success && result.clipboard_hdr.empty() && result.clipboard.bgra==image.bgra,"Default clipboard behavior changed");
    run_producer(station.desktop,{},L"sdr");inspect_clipboard(image);
    settings.clipboard_mode=ClipboardMode::Both;
    for(unsigned variant=0;variant<9;++variant) {
        settings.folder=directory/(L"HDR \u8bd5\u9a8c "+std::to_wstring(variant));
        settings.hdr_format=variant>=5?HdrFormat::Avif:variant==4?HdrFormat::JpegXr:HdrFormat::UltraJpeg;
        settings.hdr_jpeg_quality=HdrJpegQuality(variant<4?variant:0);
        settings.hdr_avif_quality=HdrAvifQuality(variant>=5?variant-5:0);
        result=process_output(settings,frame,image,{},true);
        require(result.success && result.clipboard.bgra==image.bgra && !result.clipboard_hdr.empty(),"HDR copy workflow lost an output");
        require(result.clipboard_hdr.extension()==(variant>=5?L".avif":variant==4?L".jxr":L".jpg"),"HDR clipboard did not use the saved format");
        require(!std::filesystem::exists(settings.folder/(result.clipboard_hdr.stem().wstring()+L".png")),"Copy unexpectedly saved a companion PNG");
        run_producer(station.desktop,result.clipboard_hdr,L"hdr");inspect_clipboard(image,result.clipboard_hdr);inspect_hdr(result.clipboard_hdr);
        run_producer(station.desktop,result.clipboard_hdr,L"hdr-only");inspect_clipboard(image,result.clipboard_hdr,true);
    }
    const auto saved=result.clipboard_hdr;
    settings.clipboard_mode=ClipboardMode::HdrFile;settings.folder=directory/L"hdr-only-workflow";
    result=process_output(settings,frame,image,{},true);
    require(result.clipboard_mode==ClipboardMode::HdrFile && !result.clipboard_hdr.empty(),"HDR-only mode was not retained by the output workflow");
    finish_copy(saved,L"hdr-only");inspect_clipboard(image,saved,true);
    finish_copy(directory/L"missing-hdr-only.jpg",L"hdr-only-missing");inspect_clipboard(image);
    run_producer(station.desktop,directory/L"missing-hdr-only.jpg",L"hdr-only-missing");inspect_clipboard(image);
    finish_copy(saved,L"hdr");inspect_clipboard(image,saved);
    finish_copy(directory/L"unavailable.jpg",L"missing");inspect_clipboard(image);
    finish_copy({},L"sdr");inspect_clipboard(image);
    finish_copy(saved,L"invalid");inspect_clipboard(image);
    run_producer(station.desktop,saved,L"hdr");
    require(std::memcmp(frame.pixels.data(),original.data(),original.size()*sizeof(Pixel))==0,"Clipboard encoding mutated the HDR source");
    require(OpenClipboard(nullptr)!=FALSE,"Cannot reserve owned clipboard for contention");
    {struct Close {~Close(){CloseClipboard();}} close;run_producer(station.desktop,saved,L"busy");}
    inspect_clipboard(image,saved);
    run_producer(station.desktop,saved,L"invalid");inspect_clipboard(image,saved);
    for(const auto& missing:{directory/L"missing.jpg",std::filesystem::path(L"relative.jpg"),directory}) {
        run_producer(station.desktop,missing,L"sdr");inspect_clipboard(image);
    }
    settings.folder=directory/L"no-retention";settings.keep_original=false;
    result=process_output(settings,frame,image,{},true);
    require(result.success && result.clipboard_hdr.empty() && result.clipboard.bgra==image.bgra && result.clipboard_mode==ClipboardMode::SdrImage,"Disabling HDR saving lost SDR copy");
    require(!std::filesystem::exists(settings.folder/L"HDR images"),"HDR clipboard created an unrequested artifact");
    settings.folder=directory/L"sdr-source";settings.keep_original=true;auto sdr=frame;sdr.hdr=false;
    result=process_output(settings,sdr,image,{},true);
    require(result.success && result.clipboard_hdr.empty() && result.clipboard.bgra==image.bgra,"SDR source acquired an HDR clipboard file");
    settings.folder=directory/L"encoder-failure";settings.hdr_format=HdrFormat::UltraJpeg;
    auto invalid=frame;invalid.pixels[0].r=std::numeric_limits<float>::quiet_NaN();
    result=process_output(settings,invalid,image,{},true);
    require(result.success && result.clipboard_hdr.empty() && result.clipboard.bgra==image.bgra,"HDR encoder failure lost SDR copy");
    require(result.details.find(L"HDR image could not be saved: ")!=std::wstring::npos,"HDR encoder failure was hidden");
    settings.folder=directory/L"avif-encoder-failure";settings.hdr_format=HdrFormat::Avif;
    result=process_output(settings,invalid,image,{},true);
    require(result.success && result.clipboard_hdr.empty() && result.clipboard.bgra==image.bgra,"AVIF encoder failure lost SDR copy");
    require(result.details.find(L"HDR image could not be saved: ")!=std::wstring::npos,"AVIF encoder failure was hidden");
    settings.hdr_format=HdrFormat::UltraJpeg;
    settings.folder=directory/L"tiny";Frame tiny=frame;tiny.width=4;tiny.height=3;tiny.pixels.resize(12);
    auto preview=render_sdr(tiny,settings.color);result=process_output(settings,tiny,preview,{},true);
    require(result.success && result.clipboard_hdr.extension()==L".jxr","Tiny HDR selection lost the JPEG XR fallback");
    const auto reopened=load_master(result.clipboard_hdr);require(reopened.width==4 && reopened.height==3,"Tiny HDR clipboard fallback changed dimensions");
    settings.folder=directory/L"save-only";result=process_output(settings,frame,image);
    require(result.success && result.clipboard.bgra.empty() && result.clipboard_hdr.empty(),"Save-only workflow acquired clipboard outputs");
    std::cout<<checks<<" isolated clipboard checks passed: exact SDR payload, Unicode HDR files, all JPEG and AVIF qualities, JPEG XR, producer exit, busy clipboard, invalid buffers, missing files, and encoder fallback.\n";
    std::cout<<"Each producer exited before consumer reads. Only an empty, reserved noninteractive station clipboard was changed and then cleared; the interactive clipboard was not opened.\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"Clipboard fixture failed after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
