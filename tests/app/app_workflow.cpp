#include "app/app.hpp"
#include <iostream>
#include "image/scaling.hpp"

using namespace hdrshot;

Image privacy_output_pixels(const std::filesystem::path& path) {
    auto factory=imaging_factory();ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;
    check(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),"Cannot reopen privacy output");
    check(decoder->GetFrame(0,&frame),"Cannot read privacy output");ComPtr<IWICFormatConverter> converter;
    check(factory->CreateFormatConverter(&converter),"Cannot create privacy output converter");
    check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Cannot read privacy output channels");
    Image image;check(frame->GetSize(&image.width,&image.height),"Cannot read privacy output dimensions");image.bgra.resize(checked_pixels(image.width,image.height)*4);
    check(converter->CopyPixels(nullptr,image.width*4,UINT(image.bgra.size()),image.bgra.data()),"Cannot decode privacy output");return image;
}
Frame privacy_output_hdr(const std::filesystem::path& path) {
    if(path.extension()==L".jxr") return load_master(path);
    std::ifstream input(path,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});
    uhdr_codec_private_t* decoder=uhdr_create_decoder();if(!decoder) throw std::runtime_error("Cannot create privacy HDR decoder.");
    struct Release {uhdr_codec_private_t* value;~Release(){uhdr_release_decoder(value);}} release{decoder};
    uhdr_compressed_image_t compressed{};compressed.data=bytes.data();compressed.data_sz=compressed.capacity=bytes.size();
    check_hdr_codec(uhdr_dec_set_image(decoder,&compressed),"Cannot reopen privacy HDR JPEG");
    check_hdr_codec(uhdr_dec_set_out_img_format(decoder,UHDR_IMG_FMT_64bppRGBAHalfFloat),"Cannot select privacy HDR pixels");
    check_hdr_codec(uhdr_dec_set_out_color_transfer(decoder,UHDR_CT_LINEAR),"Cannot select privacy HDR transfer");
    check_hdr_codec(uhdr_dec_set_out_max_display_boost(decoder,10000.0f/203),"Cannot select privacy HDR headroom");
    check_hdr_codec(uhdr_decode(decoder),"Cannot reconstruct privacy HDR JPEG");const auto* decoded=uhdr_get_decoded_image(decoder);
    if(!decoded) throw std::runtime_error("Privacy HDR JPEG lost its pixels.");
    Frame frame;frame.width=decoded->w;frame.height=decoded->h;frame.hdr=true;frame.pixels.resize(checked_pixels(frame.width,frame.height));
    const auto* pixels=static_cast<const std::uint16_t*>(decoded->planes[0]);
    for(unsigned y=0;y<frame.height;++y) for(unsigned x=0;x<frame.width;++x) {
        const auto i=(std::size_t(y)*decoded->stride[0]+x)*4;
        frame.pixels[std::size_t(y)*frame.width+x]={half_to_float(pixels[i]),half_to_float(pixels[i+1]),half_to_float(pixels[i+2]),1};
    }
    return frame;
}
int wmain(int argc,wchar_t** argv) try {
    if(argc!=2) throw std::runtime_error("Specify an owned application evidence directory.");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Cannot initialize own app fixture");
    struct FinishCom {~FinishCom(){CoUninitialize();}} finish_com;
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    App app;
    app.settings.folder=directory/L"selected-output";
    app.settings.modifiers=MOD_CONTROL|MOD_SHIFT|MOD_ALT;app.settings.key=VK_F9;
    app.settings.color.mode=ContentMode::Hdr;
    app.settings.hdr_format=HdrFormat::JpegXr;app.settings.sharpness=25;app.settings.scale=150;
    app.capture_settings=app.settings;
    const auto output_folder=app.settings.folder;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=window_class;RegisterClassW(&wc);
    constexpr DWORD settings_style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    RECT settings_bounds{0,0,settings_width,settings_height};
    AdjustWindowRectEx(&settings_bounds,settings_style,FALSE,0);
    HWND window=CreateWindowExW(0,window_class,L"LumenShot",settings_style,
        0,0,settings_bounds.right-settings_bounds.left,settings_bounds.bottom-settings_bounds.top,nullptr,nullptr,wc.hInstance,&app);
    if(!window) throw std::runtime_error("Cannot create own app workflow window.");
    struct CloseWindow {HWND window;~CloseWindow(){DestroyWindow(window);}} close{window};
    Frame frame;frame.width=900;frame.height=600;frame.hdr=true;frame.sdr_white=203;
    frame.backend=L"Owned synthetic HDR frame";frame.pixels.resize(900*600);
    for(unsigned y=0;y<600;++y) for(unsigned x=0;x<900;++x)
        frame.pixels[y*900+x]={float(x%16)*.5f,float(y%16)*.25f,.125f,1};
    auto expected=crop_image(render_sdr(frame,app.settings.color),{100,100,500,350});
    scale_image(expected,150,25);
    app.busy=true;
    app.capture_diagnostics=std::make_shared<DiagnosticRecord>(DiagnosticSource::Synthetic);
    {
    DiagnosticContext diagnostics(app.capture_diagnostics);
    app.worker.start([&] {
        app.result={};app.result.frame=frame;
        prepare_preview(app.result,app.capture_settings);
        app.result.bounds={80,80,980,680};app.result.needs_selection=true;
        PostMessageW(window,done_message,0,0);
    });
    }
    app.settings.folder=directory/L"future-output";
    app.settings.hdr_format=HdrFormat::UltraJpeg;app.settings.sharpness=80;app.settings.scale=200;
    NativeWork driver;
    driver.start([] {
        HWND target=nullptr;
        for(int i=0;i<800 && !target;++i) {
            target=FindWindowW(L"LumenShotRegionSelector",nullptr);
            if(target) {DWORD pid=0;GetWindowThreadProcessId(target,&pid);if(pid!=GetCurrentProcessId()) target=nullptr;}
            if(!target) Sleep(10);
        }
        if(!target) return;
        PostMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,100));
        PostMessageW(target,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(500,350));
        PostMessageW(target,WM_LBUTTONUP,0,MAKELPARAM(500,350));
        PostMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(433,376));
    });
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(std::chrono::steady_clock::now()<deadline) {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
        if(!app.busy) break;
        Sleep(10);
    }
    driver.join();
    if(app.busy || !app.result.success || app.result.status.find(L"Saved ")!=0)
        throw std::runtime_error("Asynchronous selected-area export failed: "+narrow(app.result.status+L" "+app.result.details));
    std::filesystem::path png,jxr;
    for(const auto& entry:std::filesystem::directory_iterator(output_folder))
        if(entry.path().extension()==L".png") png=entry.path();
    for(const auto& entry:std::filesystem::directory_iterator(output_folder/L"HDR images"))
        if(entry.path().extension()==L".jxr") jxr=entry.path();
    if(png.empty() || jxr.empty()) throw std::runtime_error("Selected export lost the SDR or HDR output.");
    if(std::filesystem::exists(app.settings.folder))
        throw std::runtime_error("An in-flight screenshot used settings from a later edit.");
    auto factory=imaging_factory();ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromFilename(png.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),"Cannot reopen selected PNG");
    ComPtr<IWICBitmapFrameDecode> decoded;check(decoder->GetFrame(0,&decoded),"Cannot inspect selected PNG");
    UINT width=0,height=0;check(decoded->GetSize(&width,&height),"Cannot read selected dimensions");
    if(width!=expected.width || height!=expected.height) throw std::runtime_error("Selected-area dimensions are incorrect.");
    std::vector<BYTE> pixels(expected.bgra.size());
    check(decoded->CopyPixels(nullptr,width*4,UINT(pixels.size()),pixels.data()),"Cannot decode selected output");
    if(pixels!=expected.bgra) throw std::runtime_error("Selected PNG differs from the exact SDR preview crop.");
    const Frame original=load_master(jxr);
    auto expected_hdr=crop_frame(frame,{100,100,500,350});enhance_hdr(expected_hdr,150,25);
    if(original.width!=expected.width || original.height!=expected.height || original.pixels.size()!=expected_hdr.pixels.size())
        throw std::runtime_error("Selected HDR image has incorrect dimensions.");
    for(std::size_t i=0;i<original.pixels.size();++i) {
        const auto a=original.pixels[i],b=expected_hdr.pixels[i];
        if(a.r!=half_to_float(float_to_half(b.r)) || a.g!=half_to_float(float_to_half(b.g)) || a.b!=half_to_float(float_to_half(b.b)))
            throw std::runtime_error("Selected JPEG XR lost the processed HDR values.");
    }
    const auto diagnostic=app.capture_diagnostics->snapshot();
    if(!diagnostic.success || diagnostic.selector_first_paint_ms<0 ||
       !diagnostic.stages[std::size_t(DiagnosticStage::Preview)].calls ||
       !diagnostic.stages[std::size_t(DiagnosticStage::Selection)].calls ||
       !diagnostic.stages[std::size_t(DiagnosticStage::EncodePng)].calls ||
       !diagnostic.stages[std::size_t(DiagnosticStage::EncodeJxr)].calls)
        throw std::runtime_error("Capture diagnostics lost a workflow stage or first paint endpoint.");
    unsigned reports=0;
    for(const auto& entry:std::filesystem::directory_iterator(executable_directory()/L"Diagnostics")) {
        if(entry.path().extension()!=L".json") throw std::runtime_error("Incomplete diagnostic report remains.");
        ++reports;
    }
    if(reports!=1) throw std::runtime_error("Capture diagnostics were not saved exactly once.");
    app.capture_diagnostics.reset();
    const auto future_folder=app.settings.folder;
    app.settings.color={ContentMode::Auto,203,.1,true};
    app.settings.hdr_format=HdrFormat::UltraJpeg;
    frame.pixels.assign(frame.pixels.size(),Pixel{.5f,.4f,.3f,1});
    frame.pixels[0]={50,50,50,1};
    for(unsigned y=180;y<300;++y) for(unsigned x=340;x<460;++x) {
        const float value=float(.75+.25*std::sin(x*2*3.141592653589793/16));
        frame.pixels[y*frame.width+x]={.5f*value,.4f*value,.3f*value,1};
    }
    const auto full_preview=render_sdr(frame,app.settings.color);
    const RECT selection{100,100,500,350};
    const auto preview_base=crop_image(full_preview,selection);
    const auto hdr_selection=crop_frame(frame,selection);
    if(preview_base.bgra==render_sdr(hdr_selection,app.settings.color).bgra)
        throw std::runtime_error("HDR crop fixture failed to distinguish selection from whole-scene tone mapping.");
    const auto file_bytes=[](const std::filesystem::path& path) {
        std::ifstream input(path,std::ios::binary);
        if(!input) throw std::runtime_error("Cannot read owned output fixture.");
        return std::string(std::istreambuf_iterator<char>(input),{});
    };
    const std::pair<unsigned,unsigned> options[]={{100,0},{100,35},{125,35},{150,0},{200,35}};
    for(const auto& [scale,sharpness]:options) for(bool marked:{false,true}) for(bool annotated:{false,true}) {
        const auto suffix=std::to_wstring(scale)+L"-"+std::to_wstring(sharpness)+(annotated?L"-annotated":L"-original");
        app.settings.folder=directory/((marked?L"jpeg-marked-":L"jpeg-unmarked-")+suffix);
        app.settings.hdr_jpeg_quality=HdrJpegQuality(scale==100?0:scale==125?1:scale==150?2:3);
        app.settings.sharpness=sharpness;app.settings.scale=scale;app.settings.annotate_hdr=annotated;app.capture_settings=app.settings;
        Image expected_png;
        if(scale==100) {
            auto enhanced_preview=full_preview;sharpen_image(enhanced_preview,sharpness);
            expected_png=crop_image(enhanced_preview,selection);
        } else {expected_png=preview_base;scale_image(expected_png,scale,sharpness);}
        if(sharpness && scale==100 && expected_png.bgra==preview_base.bgra)
            throw std::runtime_error("Sharpening fixture did not enhance any detail.");
        auto processed_hdr=hdr_selection;enhance_hdr(processed_hdr,scale,sharpness);
        const auto reference_jpeg=directory/((marked?L"marked-reference-":L"reference-")+suffix+L".jpg");
        if(marked && annotated) {
            const std::vector<Annotation> marks{{MarkTool::Pen,RGB(255,0,0),{{150,150},{300,150}},L""}};
            const double sx=double(expected_png.width)/400,sy=double(expected_png.height)/250;
            annotate_hdr(processed_hdr,marks,{100,100},sx,sy,app.settings.color.paper_white);
            annotate(expected_png,marks,{100,100},sx,sy);
        }
        save_hdr_jpeg(reference_jpeg,processed_hdr,expected_png,app.settings.hdr_jpeg_quality);
        const auto reference_bytes=file_bytes(reference_jpeg);
        app.busy=true;
        app.worker.start([&] {
            app.result={};app.result.frame=frame;prepare_preview(app.result,app.capture_settings);
            app.result.bounds={80,80,980,680};app.result.needs_selection=true;
            PostMessageW(window,done_message,0,0);
        });
        NativeWork jpeg_driver;jpeg_driver.start([marked] {
            HWND target=nullptr;
            for(int i=0;i<800 && !target;++i) {
                target=FindWindowW(L"LumenShotRegionSelector",nullptr);
                if(target) {DWORD pid=0;GetWindowThreadProcessId(target,&pid);if(pid!=GetCurrentProcessId()) target=nullptr;}
                if(!target) Sleep(10);
            }
            if(!target) return;
            const auto click=[&](int x,int y) {
                PostMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
                PostMessageW(target,WM_LBUTTONUP,0,MAKELPARAM(x,y));
            };
            const auto drag=[&](int x1,int y1,int x2,int y2) {
                PostMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x1,y1));
                PostMessageW(target,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x2,y2));
                PostMessageW(target,WM_LBUTTONUP,0,MAKELPARAM(x2,y2));
            };
            drag(100,100,500,350);
            if(marked) {click(525,20);drag(150,150,300,150);}
            click(433,376);
        });
        const auto jpeg_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(app.busy && std::chrono::steady_clock::now()<jpeg_deadline) {
            MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
            Sleep(10);
        }
        jpeg_driver.join();
        if(app.busy || !app.result.success || app.result.details.find(L"could not be saved")!=std::wstring::npos)
            throw std::runtime_error("HDR JPEG selection failed: "+narrow(app.result.details));
        std::filesystem::path jpeg_png,jpeg;
        for(const auto& entry:std::filesystem::directory_iterator(app.settings.folder))
            if(entry.path().extension()==L".png") jpeg_png=entry.path();
        for(const auto& entry:std::filesystem::directory_iterator(app.settings.folder/L"HDR images"))
            if(entry.path().extension()==L".jpg") jpeg=entry.path();
        if(jpeg_png.empty() || jpeg.empty() || file_bytes(jpeg)!=reference_bytes)
            throw std::runtime_error("HDR JPEG changed the preview tone mapping or included SDR annotations.");
        auto hdr_bytes=file_bytes(jpeg);
        uhdr_codec_private_t* hdr_decoder=uhdr_create_decoder();
        if(!hdr_decoder) throw std::runtime_error("Cannot create selection HDR decoder.");
        struct ReleaseHdr {uhdr_codec_private_t* value;~ReleaseHdr(){uhdr_release_decoder(value);}} release_hdr{hdr_decoder};
        uhdr_compressed_image_t compressed{};
        compressed.data=hdr_bytes.data();compressed.data_sz=compressed.capacity=hdr_bytes.size();
        check_hdr_codec(uhdr_dec_set_image(hdr_decoder,&compressed),"Cannot read selection HDR JPEG");
        check_hdr_codec(uhdr_dec_set_out_img_format(hdr_decoder,UHDR_IMG_FMT_64bppRGBAHalfFloat),"Cannot select HDR reconstruction");
        check_hdr_codec(uhdr_dec_set_out_color_transfer(hdr_decoder,UHDR_CT_LINEAR),"Cannot select linear HDR reconstruction");
        check_hdr_codec(uhdr_dec_set_out_max_display_boost(hdr_decoder,10000.0f/203),"Cannot select HDR reconstruction headroom");
        check_hdr_codec(uhdr_decode(hdr_decoder),"Cannot reconstruct selection HDR pixels");
        const auto* hdr_decoded=uhdr_get_decoded_image(hdr_decoder);
        if(!hdr_decoded || hdr_decoded->cg!=UHDR_CG_BT_2100 || hdr_decoded->w!=expected_png.width || hdr_decoded->h!=expected_png.height)
            throw std::runtime_error("Selection HDR dimensions or gamut changed.");
        const auto* hdr_pixels=static_cast<const std::uint16_t*>(hdr_decoded->planes[0]);
        const unsigned probe_x=unsigned(std::lround(100.0*expected_png.width/preview_base.width));
        const unsigned probe_y=unsigned(std::lround(50.0*expected_png.height/preview_base.height));
        const auto hdr_offset=(std::size_t(probe_y)*hdr_decoded->stride[0]+probe_x)*4;
        const Rgb hdr_expected=to_bt2020(marked && annotated?Rgb{203.0/80,0,0}:Rgb{.5,.4,.3})*(80.0/203);
        const double hdr_channels[]={hdr_expected.r,hdr_expected.g,hdr_expected.b};
        for(unsigned channel=0;channel<3;++channel) {
            const double actual=half_to_float(hdr_pixels[hdr_offset+channel]);
            std::cout<<"CROPPED_HDR channel "<<channel<<" nits "<<actual*203<<" expected "<<hdr_channels[channel]*203<<"\n";
            const double tolerance=marked && annotated?std::max(.01,hdr_channels[channel]*.06):.01;
            if(std::abs(actual-hdr_channels[channel])>tolerance)
                throw std::runtime_error("Preview matching changed the original HDR brightness or color.");
        }
        ComPtr<IWICBitmapDecoder> png_decoder;
        check(factory->CreateDecoderFromFilename(jpeg_png.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&png_decoder),"Cannot reopen JPEG companion PNG");
        ComPtr<IWICBitmapFrameDecode> png_frame;check(png_decoder->GetFrame(0,&png_frame),"Cannot read JPEG companion PNG");
        UINT png_width=0,png_height=0;check(png_frame->GetSize(&png_width,&png_height),"Cannot inspect JPEG companion PNG size");
        if(png_width!=expected_png.width || png_height!=expected_png.height)
            throw std::runtime_error("Companion PNG lost its selected image size.");
        std::vector<BYTE> png_pixels(expected_png.bgra.size());
        check(png_frame->CopyPixels(nullptr,expected_png.width*4,UINT(png_pixels.size()),png_pixels.data()),"Cannot decode JPEG companion PNG");
        if(!marked && png_pixels!=expected_png.bgra)
            throw std::runtime_error("Companion PNG changed the exact selection preview.");
        const auto mark_offset=(std::size_t(probe_y)*expected_png.width+probe_x)*4;
        if(marked && (png_pixels[mark_offset]!=0 || png_pixels[mark_offset+1]!=0 || png_pixels[mark_offset+2]!=255))
            throw std::runtime_error("Annotated SDR output lost its pen stroke.");
    }
    app.settings.annotate_hdr=false;
    Settings copy_settings=app.settings;copy_settings.keep_original=false;
    copy_settings.folder=directory/L"clipboard-result";
    copy_settings.scale=150;copy_settings.sharpness=35;
    Image copy_image=preview_base;scale_image(copy_image,copy_settings.scale,copy_settings.sharpness);
    const auto copied=process_output(copy_settings,hdr_selection,copy_image,{},true);
    if(copied.clipboard.bgra!=copy_image.bgra || copied.clipboard.width!=600 || copied.clipboard.height!=375 ||
       copied.details.find(L"Sharpness: 35%")==std::wstring::npos || copied.details.find(L"Image size: 150%")==std::wstring::npos)
        throw std::runtime_error("Clipboard output lost enhancements or its saved settings.");
    unsigned privacy_exports=0;
    for(const auto format:{HdrFormat::UltraJpeg,HdrFormat::JpegXr}) for(const unsigned scale:{100u,125u})
        for(bool annotated:{false,true}) for(const auto tool:{MarkTool::Redact,MarkTool::Blur,MarkTool::Pixelate}) {
            std::string first_png,first_hdr;
            for(unsigned variant=0;variant<(tool==MarkTool::Redact?2u:1u);++variant) {
                const auto suffix=std::to_wstring(int(format))+L"-"+std::to_wstring(scale)+L"-"+std::to_wstring(int(tool))+L"-"+std::to_wstring(variant)+(annotated?L"-annotated":L"-original");
                app.settings.folder=directory/(L"privacy-"+suffix);app.settings.hdr_format=format;
                app.settings.scale=scale;app.settings.sharpness=35;app.settings.keep_original=true;app.settings.annotate_hdr=annotated;
                app.settings.hdr_jpeg_quality=HdrJpegQuality((scale==125?2:0)+(annotated?1:0));
                app.settings.color.peak_nits=4000;app.capture_settings=app.settings;
                Frame private_frame=frame;
                for(unsigned y=150;y<=220;++y) for(unsigned x=150;x<=300;++x) {
                    const float value=(((x/3+y/3)%2)^variant)?3.0f:.25f;
                    private_frame.pixels[y*900+x]={value,value*.8f,value*.6f,1};
                }
                const auto frozen=private_frame.pixels;
                auto unedited=crop_frame(private_frame,selection);enhance_hdr(unedited,scale,35);
                app.busy=true;app.worker.start([&] {
                    app.result={};app.result.frame=private_frame;prepare_preview(app.result,app.capture_settings);
                    app.result.bounds={80,80,980,680};app.result.needs_selection=true;PostMessageW(window,done_message,0,0);
                });
                NativeWork privacy_driver;privacy_driver.start([tool] {
                    HWND target=nullptr;
                    for(int i=0;i<800 && !target;++i) {
                        target=FindWindowW(L"LumenShotRegionSelector",nullptr);DWORD pid=0;if(target) GetWindowThreadProcessId(target,&pid);
                        if(target && pid!=GetCurrentProcessId()) target=nullptr;
                        if(!target) Sleep(10);
                    }
                    if(!target) return;
                    const auto click=[&](int x,int y) {
                        PostMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));PostMessageW(target,WM_LBUTTONUP,0,MAKELPARAM(x,y));
                    };
                    const auto drag=[&](int x,int y,int right,int bottom) {
                        PostMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));PostMessageW(target,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(right,bottom));
                        PostMessageW(target,WM_LBUTTONUP,0,MAKELPARAM(right,bottom));
                    };
                    drag(100,100,500,350);click(525,20+(6+int(tool)-int(MarkTool::Redact))*32);drag(150,150,300,220);click(433,376);
                });
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
                while(app.busy && std::chrono::steady_clock::now()<deadline) {
                    MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
                    Sleep(10);
                }
                privacy_driver.join();
                if(app.busy || !app.result.success || app.result.details.find(L"could not be saved")!=std::wstring::npos)
                    throw std::runtime_error("Privacy selection export failed: "+narrow(app.result.details));
                if(std::memcmp(private_frame.pixels.data(),frozen.data(),frozen.size()*sizeof(Pixel)))
                    throw std::runtime_error("Privacy export modified the captured HDR source.");
                std::filesystem::path png,hdr;
                for(const auto& entry:std::filesystem::directory_iterator(app.settings.folder)) if(entry.path().extension()==L".png") png=entry.path();
                unsigned hdr_files=0;
                for(const auto& entry:std::filesystem::directory_iterator(app.settings.folder/L"HDR images")) {hdr=entry.path();++hdr_files;}
                if(png.empty() || hdr_files!=1) throw std::runtime_error("Privacy export retained an extra unredacted HDR artifact.");
                const auto sdr=privacy_output_pixels(png);const auto linear=privacy_output_hdr(hdr);
                if(sdr.width!=unedited.width || sdr.height!=unedited.height || linear.width!=sdr.width || linear.height!=sdr.height)
                    throw std::runtime_error("Privacy export changed crop or scaled dimensions.");
                const auto fallback=format==HdrFormat::UltraJpeg?privacy_output_pixels(hdr):sdr;
                const unsigned left=unsigned(std::ceil(58.0*sdr.width/400)),right=unsigned(std::floor(193.0*sdr.width/400));
                const unsigned top=unsigned(std::ceil(58.0*sdr.height/250)),bottom=unsigned(std::floor(113.0*sdr.height/250));
                double changed=0;
                for(unsigned y=top;y<bottom;++y) for(unsigned x=left;x<right;++x) {
                    const auto i=std::size_t(y)*sdr.width+x;const auto& p=linear.pixels[i];
                    if(tool==MarkTool::Redact) {
                        if(sdr.bgra[i*4] || sdr.bgra[i*4+1] || sdr.bgra[i*4+2]
                           || fallback.bgra[i*4]>3 || fallback.bgra[i*4+1]>3 || fallback.bgra[i*4+2]>3
                           || std::abs(p.r)>.001 || std::abs(p.g)>.001 || std::abs(p.b)>.001)
                            throw std::runtime_error("Reopened PNG, HDR fallback, or HDR master exposed redacted pixels.");
                    } else {
                        const auto q=unedited.pixels[i];
                        const auto expected=format==HdrFormat::UltraJpeg?to_bt2020({q.r,q.g,q.b})*(80.0/203):Rgb{q.r,q.g,q.b};
                        changed=std::max(changed,std::abs(p.r-expected.r)+std::abs(p.g-expected.g)+std::abs(p.b-expected.b));
                    }
                }
                if(tool!=MarkTool::Redact && changed<.1) throw std::runtime_error("HDR export omitted its blur or pixelation.");
                if(tool==MarkTool::Redact) {
                    if(!variant) {first_png=file_bytes(png);first_hdr=file_bytes(hdr);}
                    else if(file_bytes(png)!=first_png || file_bytes(hdr)!=first_hdr)
                        throw std::runtime_error("Encoded output depends on covered secrets after sharpening or scaling.");
                }
                ++privacy_exports;
            }
        }
    std::cout<<privacy_exports<<" privacy workflows passed: native tools, immutable captures, scaled/sharpened PNG, all JPEG qualities, HDR fallback/reconstruction, and JPEG XR; covered-secret variants encode identically.\n";
    app.settings.folder=future_folder;
    app.busy=true;app.preparing=true;app.listen_for_escape();
    app.worker.start([&] {
        app.result={};app.result.frame=frame;app.result.preview=render_sdr(frame,app.settings.color);
        app.result.bounds={80,80,980,680};app.result.needs_selection=true;
        PostMessageW(window,done_message,0,0);
    });
    NativeWork cancel_driver;cancel_driver.start([&] {
        for(int i=0;i<800;++i) {
            HWND selector=FindWindowW(L"LumenShotRegionSelector",nullptr);DWORD pid=0;
            if(selector) GetWindowThreadProcessId(selector,&pid);
            if(selector && pid==GetCurrentProcessId()) {PostMessageW(window,WM_HOTKEY,cancel_hotkey_id,0);return;}
            Sleep(10);
        }
    });
    const auto cancel_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(app.busy && std::chrono::steady_clock::now()<cancel_deadline) {
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
        Sleep(10);
    }
    cancel_driver.join();
    if(app.busy || !app.result.canceled || app.escape_registered || app.active_selector)
        throw std::runtime_error("Escape did not finish the complete capture workflow.");
    if(std::filesystem::exists(app.settings.folder))
        throw std::runtime_error("Canceled screenshot created output files.");
    std::cout<<"HDR JPEG workflow passed: preview tone mapping retained with a bright peak outside the crop; scaling and sharpening retained in HDR images; original and annotated HDR choices verified.\n";
    std::cout<<"App Escape workflow passed: active overlay closed without output; shortcut released.\n";
    std::cout<<"App workflow passed: owned HDR frame -> selector -> async save -> scaled SDR PNG + lossless processed HDR + settings snapshot.\n";
    std::cout<<"No desktop capture, game, launcher, or system clipboard was used by this workflow fixture.\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"App workflow failed: "<<error.what()<<"\n";return 1;}
