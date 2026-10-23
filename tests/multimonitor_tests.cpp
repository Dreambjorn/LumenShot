#include "../src/main.cpp"
#include <iostream>
#include <atomic>

static unsigned checks=0;
void require(bool condition,const char* message) {++checks;if(!condition) throw std::runtime_error(message);}

Frame solid(unsigned width,unsigned height,float value,bool hdr,double white) {
    Frame frame;frame.width=width;frame.height=height;frame.hdr=hdr;frame.sdr_white=white;
    frame.pixels.assign(checked_pixels(width,height),Pixel{value,value,value,1});
    return frame;
}

void verify_tile(const Image& desktop,const Image& tile,unsigned x,unsigned y) {
    for(unsigned row=0;row<tile.height;++row)
        require(std::memcmp(desktop.bgra.data()+(std::size_t(y+row)*desktop.width+x)*4,
                            tile.bgra.data()+std::size_t(row)*tile.width*4,std::size_t(tile.width)*4)==0,
                "A monitor's color conversion changed in the combined image");
}

COLORREF monitor_color(std::size_t index) {
    return RGB(64+(index*37)%128,72+(index*53)%128,88+(index*29)%128);
}
LRESULT CALLBACK monitor_fixture(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_PAINT) {
        PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT client{};GetClientRect(window,&client);
        HBRUSH brush=CreateSolidBrush(COLORREF(GetWindowLongPtrW(window,GWLP_USERDATA)));
        FillRect(dc,&client,brush);DeleteObject(brush);EndPaint(window,&paint);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}

int wmain(int argc,wchar_t** argv) try {
    if(argc!=2) throw std::runtime_error("Specify an owned multi-monitor evidence directory.");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Runtime runtime;
    const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
    std::vector<Monitor> layout(3);
    layout[0].bounds={-64,-16,0,48};layout[1].bounds={0,0,48,48};layout[2].bounds={0,-48,32,-16};
    std::vector<Frame> frames={solid(64,64,2.5375f,true,203),solid(48,48,.18f,false,80),
                              solid(32,32,float(srgb_decode(128.0/255)*4),true,320)};
    for(unsigned y=0;y<64;++y) for(unsigned x=32;x<64;++x) frames[0].pixels[y*64+x]={12.5f,12.5f,12.5f,1};
    const auto desktop=compose_desktop(layout,frames);
    require(desktop.bounds.left==-64 && desktop.bounds.top==-48 && desktop.bounds.right==48 && desktop.bounds.bottom==48,
            "Negative desktop coordinates or stacked monitor layout changed");
    require(desktop.frame.width==112 && desktop.frame.height==96 && desktop.frame.hdr,"Combined capture lost dimensions or HDR");
    require(desktop.frame.regions.size()==3 && desktop.frame.regions[2].sdr_white==320,"Monitor color metadata was lost");
    require(desktop.frame.pixels[0].r==0 && desktop.frame.pixels[0].a==1,"Desktop gaps are not opaque black");
    require(desktop.frame.pixels[48*112+16].r==2.5375f && desktop.frame.pixels[64*112+80].r==.18f,
            "Raw HDR or SDR pixels moved or changed brightness");
    for(const auto mode:{ContentMode::Auto,ContentMode::Hdr,ContentMode::Sdr}) {
        ColorSettings settings;settings.mode=mode;settings.exposure=.1;
        const auto converted=render_sdr(desktop.frame,settings);
        verify_tile(converted,render_sdr(frames[0],settings),0,32);
        verify_tile(converted,render_sdr(frames[1],settings),64,48);
        verify_tile(converted,render_sdr(frames[2],settings),64,0);
        require(converted.bgra[0]==0 && converted.bgra[3]==255,"Color conversion raised black desktop gaps");
    }
    const auto crossing=crop_frame(desktop.frame,{48,32,96,80});
    require(crossing.width==48 && crossing.height==48 && crossing.hdr && crossing.regions.size()==2,
            "Cross-monitor selection lost dimensions or monitor regions");
    require(crossing.regions[0].x==0 && crossing.regions[0].width==16 && crossing.regions[1].x==16
            && crossing.regions[1].y==16 && !crossing.regions[1].hdr,"Cropped monitor offsets are incorrect");
    const auto sdr_only=crop_frame(desktop.frame,{64,48,112,96});
    require(!sdr_only.hdr && sdr_only.regions.size()==1,"Selecting only an SDR display retained another display's HDR flag");
    require(render_sdr(sdr_only,{}).bgra==render_sdr(frames[1],{}).bgra,"SDR-only selection was darkened by an HDR display");
    const auto hdr_only=crop_frame(desktop.frame,{0,32,64,96});
    require(hdr_only.hdr && hdr_only.regions.size()==1,"HDR-only crop lost its original monitor");
    require(render_sdr(hdr_only,{}).bgra==render_sdr(frames[0],{}).bgra,"HDR-only crop lost its reference white");
    const auto gap=crop_frame(desktop.frame,{0,0,16,16});
    require(!gap.hdr && gap.regions.empty() && render_sdr(gap,{}).bgra[0]==0,"Gap-only selection is not ordinary black SDR");
    const auto embedded=crop_frame(desktop.frame,{64,0,96,32});
    require(render_sdr(embedded,{}).bgra==render_sdr(frames[2],{}).bgra,"Cropped embedded SDR lost the display's white level");
    bool rejected=false;try {desktop_bounds({});}catch(const std::exception&){rejected=true;}
    require(rejected,"Empty desktop layout was accepted");
    auto invalid=layout;invalid[0].bounds.right=invalid[0].bounds.left;
    rejected=false;try {compose_desktop(invalid,frames);}catch(const std::exception&){rejected=true;}
    require(rejected,"Invalid monitor bounds were accepted");
    rejected=false;try {compose_desktop(layout,{frames[0]});}catch(const std::exception&){rejected=true;}
    require(rejected,"Missing monitor capture was accepted");
    auto wrong=frames;wrong[0].width=32;
    rejected=false;try {compose_desktop(layout,wrong);}catch(const std::exception&){rejected=true;}
    require(rejected,"Changed monitor dimensions were silently rescaled");
    auto bad_region=desktop.frame;bad_region.regions[0].width=113;
    rejected=false;try {render_sdr(bad_region,{});}catch(const std::exception&){rejected=true;}
    require(rejected,"Out-of-bounds monitor color region was accepted");
    ColorSettings color;color.mode=ContentMode::Hdr;color.exposure=.1;color.dither=false;
    save_image(directory/L"mixed-monitors.png",render_sdr(desktop.frame,color));
    save_hdr_jpeg(directory/L"mixed-monitors.jpg",desktop.frame,render_sdr(desktop.frame,color));
    save_master(directory/L"mixed-monitors.jxr",desktop.frame);
    const auto restored=load_master(directory/L"mixed-monitors.jxr");
    require(restored.width==112 && restored.height==96,"Lossless combined HDR dimensions changed");
    for(std::size_t i=0;i<restored.pixels.size();++i)
        require(restored.pixels[i].r==half_to_float(float_to_half(desktop.frame.pixels[i].r)),"Combined HDR original changed captured brightness");
    std::ifstream input(directory/L"mixed-monitors.jpg",std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)),{});
    uhdr_codec_private_t* decoder=uhdr_create_decoder();require(decoder!=nullptr,"Cannot inspect combined HDR JPEG");
    struct ReleaseDecoder {uhdr_codec_private_t* value;~ReleaseDecoder(){uhdr_release_decoder(value);}} release_decoder{decoder};
    uhdr_compressed_image_t compressed{};compressed.data=bytes.data();compressed.data_sz=compressed.capacity=bytes.size();
    check_hdr_codec(uhdr_dec_set_image(decoder,&compressed),"Cannot read combined HDR JPEG");
    check_hdr_codec(uhdr_dec_set_out_img_format(decoder,UHDR_IMG_FMT_64bppRGBAHalfFloat),"Cannot select linear HDR output");
    check_hdr_codec(uhdr_dec_set_out_color_transfer(decoder,UHDR_CT_LINEAR),"Cannot select HDR transfer");
    check_hdr_codec(uhdr_dec_set_out_max_display_boost(decoder,10000.0f/203),"Cannot select HDR headroom");
    check_hdr_codec(uhdr_decode(decoder),"Cannot decode combined HDR JPEG");
    const auto* decoded=uhdr_get_decoded_image(decoder);require(decoded && decoded->w==112 && decoded->h==96,"Combined HDR JPEG dimensions changed");
    const auto* pixels=static_cast<const std::uint16_t*>(decoded->planes[0]);
    for(const POINT point:{POINT{16,48},POINT{48,48},POINT{80,64},POINT{80,16}}) {
        const double expected=desktop.frame.pixels[std::size_t(point.y)*112+point.x].r*80;
        const double actual=half_to_float(pixels[(std::size_t(point.y)*decoded->stride[0]+point.x)*4])*203;
        require(std::abs(actual-expected)<std::max(.75,expected*.04),"Combined HDR JPEG changed a display's captured brightness");
    }
    Settings output;output.folder=directory/L"sdr-only";output.color=color;
    const auto saved=process_output(output,sdr_only,render_sdr(sdr_only,color));
    require(saved.success && !std::filesystem::exists(output.folder/L"HDR images"),"SDR-only selection saved a misleading HDR copy");
    require(saved.details.find(L"exposure")==std::wstring::npos,"SDR-only status claims an HDR exposure adjustment");

    const auto displays=monitors();
    WNDCLASSW wc{};wc.lpfnWndProc=monitor_fixture;wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=L"LumenShotAllMonitorsFixture";RegisterClassW(&wc);
    struct CloseWindows {std::vector<HWND> values;~CloseWindows(){for(HWND value:values)DestroyWindow(value);}} windows;
    for(std::size_t i=0;i<displays.size();++i) {
        const auto rect=displays[i].bounds;
        HWND window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,wc.lpszClassName,L"Owned monitor color fixture",WS_POPUP,
            rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,wc.hInstance,nullptr);
        require(window!=nullptr,"Cannot create an owned display fixture");windows.values.push_back(window);
        SetWindowLongPtrW(window,GWLP_USERDATA,LONG_PTR(monitor_color(i)));ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);
    }
    DwmFlush();Sleep(100);
    Settings settings;settings.monitor=L"*";settings.backend=CaptureBackend::Windows;
    settings.color.mode=ContentMode::Sdr;settings.color.dither=false;
    auto captured=capture_input(settings,displays.front().handle);
    const auto bounds=desktop_bounds(displays);
    require(EqualRect(&captured.bounds,&bounds),"All-monitor option did not capture the complete virtual desktop");
    require(captured.frame.regions.size()==displays.size(),"A connected monitor was omitted");
    auto preview=captured.sdr.bgra.empty()?render_sdr(captured.frame,settings.color):captured.sdr;
    for(std::size_t i=0;i<displays.size();++i) {
        const auto rect=displays[i].bounds;
        const unsigned x=unsigned((rect.left+rect.right)/2-bounds.left),y=unsigned((rect.top+rect.bottom)/2-bounds.top);
        const auto offset=(std::size_t(y)*preview.width+x)*4;
        const COLORREF expected=monitor_color(i);
        require(std::abs(int(preview.bgra[offset])-GetBValue(expected))<=3
            && std::abs(int(preview.bgra[offset+1])-GetGValue(expected))<=3
            && std::abs(int(preview.bgra[offset+2])-GetRValue(expected))<=3,"Physical monitor capture lost its fixture color");
    }
    const auto first=displays.front().bounds,last=displays.back().bounds;
    const POINT a{(first.left+first.right)/2-bounds.left,(first.top+first.bottom)/2-bounds.top};
    const POINT b{(last.left+last.right)/2-bounds.left,(last.top+last.bottom)/2-bounds.top};
    const RECT selection{std::max(0l,std::min(a.x,b.x)-20),std::max(0l,std::min(a.y,b.y)-20),
                         std::min(LONG(preview.width),std::max(a.x,b.x)+20),std::min(LONG(preview.height),std::max(a.y,b.y)+20)};
    std::atomic<bool> overlay_seen=false,overlay_bounds_match=false;
    const unsigned width=preview.width,height=preview.height;
    NativeWork driver;
    driver.start([&] {
        HWND overlay=nullptr;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(std::chrono::steady_clock::now()<deadline) {
            overlay=FindWindowW(L"LumenShotRegionSelector",nullptr);DWORD pid=0;
            if(overlay) GetWindowThreadProcessId(overlay,&pid);
            if(overlay && pid==GetCurrentProcessId() && IsWindowVisible(overlay)) break;
            overlay=nullptr;Sleep(5);
        }
        if(!overlay) return;
        overlay_seen=true;RECT actual{};GetWindowRect(overlay,&actual);overlay_bounds_match=EqualRect(&actual,&bounds)!=FALSE;
        PostMessageW(overlay,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(selection.left,selection.top));
        PostMessageW(overlay,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(selection.right,selection.bottom));
        PostMessageW(overlay,WM_LBUTTONUP,0,MAKELPARAM(selection.right,selection.bottom));
        const LONG x=std::clamp(selection.right-134,0l,std::max(0l,LONG(width)-134))+67;
        const LONG y=(selection.bottom+45<=LONG(height)?selection.bottom+7:std::max(0l,selection.top-45))+19;
        PostMessageW(overlay,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
    });
    RegionSelector selector(std::move(preview));
    const auto selected=selector.run(nullptr,captured.bounds);driver.join();
    require(overlay_seen && overlay_bounds_match,"Selection overlay did not span all physical monitors at native coordinates");
    require(selected.action==SelectionAction::Save && EqualRect(&selected.rect,&selection),"Cross-monitor drag did not preserve selected pixel coordinates");
    const auto selected_frame=crop_frame(captured.frame,selected.rect);
    const auto selected_image=crop_image(selector.image(),selected.rect);
    require(selected_frame.width==selected_image.width && selected_frame.height==selected_image.height,"Cross-monitor HDR and SDR crops disagree");
    std::cout<<checks<<" multi-monitor checks passed: negative and stacked layouts, gaps, mixed HDR/SDR, reference whites, cropped HDR exports, "
             <<displays.size()<<" physical monitor captures and spanning selector.\n";
    if(displays.size()<2) std::cout<<"Cross-screen physical validation unavailable: only one connected monitor.\n";
    std::cout<<"Only owned color windows were sampled. No display settings, game, launcher or clipboard was changed.\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"Multi-monitor fixture failed after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
