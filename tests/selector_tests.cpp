#include "../src/image_io.hpp"
#include "../src/selector.hpp"
#include <iostream>
#include <exception>

using namespace hdrshot;
static unsigned checks=0;
void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}

Image render_selector(RegionSelector& selector,unsigned width,unsigned height) {
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=LONG(width);
    info.bmiHeader.biHeight=-LONG(height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels=nullptr;
    HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!dc || !bitmap) throw std::runtime_error("Cannot allocate owned selector canvas.");
    HGDIOBJ old=SelectObject(dc,bitmap);
    selector.paint(dc);GdiFlush();
    Image image;image.width=width;image.height=height;image.bgra.resize(std::size_t(width)*height*4);
    std::memcpy(image.bgra.data(),pixels,image.bgra.size());
    for(std::size_t i=3;i<image.bgra.size();i+=4) image.bgra[i]=255;
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    return image;
}
void drive_selector(bool save) {
    HWND target=nullptr;
    for(int i=0;i<500;++i) {
        target=FindWindowW(L"LumenShotRegionSelector",nullptr);
        if(target) {
            DWORD pid=0;GetWindowThreadProcessId(target,&pid);
            if(pid!=GetCurrentProcessId()) target=nullptr;
        }
        if(target) break;
        Sleep(10);
    }
    if(!target) return;
    PostMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,100));
    PostMessageW(target,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(500,350));
    PostMessageW(target,WM_LBUTTONUP,0,MAKELPARAM(500,350));
    if(save) PostMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(433,376));
    else PostMessageW(target,WM_KEYDOWN,VK_ESCAPE,0);
}
void drive_editor() {
    HWND target=nullptr;
    for(int i=0;i<500 && !target;++i) {
        target=FindWindowW(L"LumenShotRegionSelector",nullptr);
        if(target) {DWORD pid=0;GetWindowThreadProcessId(target,&pid);if(pid!=GetCurrentProcessId()) target=nullptr;}
        if(!target) Sleep(10);
    }
    if(!target) return;
    const auto click=[&](int x,int y) {
        PostMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
        PostMessageW(target,WM_LBUTTONUP,0,MAKELPARAM(x,y));
    };
    const auto draw=[&](int x1,int y1,int x2,int y2) {
        PostMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x1,y1));
        PostMessageW(target,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x2,y2));
        PostMessageW(target,WM_LBUTTONUP,0,MAKELPARAM(x2,y2));
    };
    draw(100,100,500,350);
    click(525,184);draw(150,150,300,250);
    click(525,344);
    click(525,216);draw(200,170,350,280);
    click(525,120);draw(130,130,200,190);
    click(525,248);draw(140,300,300,300);
    click(525,152);draw(150,200,250,250);
    click(525,280);click(180,210);
    HWND edit=nullptr;
    for(int i=0;i<500 && !edit;++i) {edit=FindWindowExW(target,nullptr,L"EDIT",nullptr);if(!edit) Sleep(10);}
    if(edit) {
        SendMessageW(edit,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"Synthetic text"));
        PostMessageW(edit,WM_KEYDOWN,VK_RETURN,0);
    }
    click(433,376);
}
void drive_escape(int stage,bool& reached) {
    HWND target=nullptr;
    for(int i=0;i<500 && !target;++i) {
        target=FindWindowW(L"LumenShotRegionSelector",nullptr);
        if(target) {DWORD pid=0;GetWindowThreadProcessId(target,&pid);if(pid!=GetCurrentProcessId()) target=nullptr;}
        if(!target) Sleep(10);
    }
    if(!target) return;
    if(stage==1) {
        PostMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,100));
        PostMessageW(target,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(500,350));
    }
    if(stage>=2) {
        PostMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,100));
        PostMessageW(target,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(500,350));
        PostMessageW(target,WM_LBUTTONUP,0,MAKELPARAM(500,350));
        const int button_y=stage==2?280:312;
        PostMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(525,button_y));
        if(stage==2) {
            PostMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(180,210));
            HWND editor=nullptr;
            for(int i=0;i<500 && !editor;++i) {editor=FindWindowExW(target,nullptr,L"EDIT",nullptr);if(!editor) Sleep(10);}
            if(editor) {
                SendMessageW(editor,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"Discard this pending text"));
                reached=true;PostMessageW(editor,WM_KEYDOWN,VK_ESCAPE,0);return;
            }
        } else {
            for(int i=0;i<500;++i) {
                HWND picker=FindWindowW(L"#32770",nullptr);DWORD pid=0;
                if(picker) GetWindowThreadProcessId(picker,&pid);
                if(picker && pid==GetCurrentProcessId()) {reached=true;break;}
                Sleep(10);
            }
        }
    } else reached=true;
    PostMessageW(target,WM_HOTKEY,selection_escape_hotkey_id,MAKELPARAM(MOD_NOREPEAT,VK_ESCAPE));
}
void drive_adjustment(int stage,RECT expected,bool& annotation_tracks) {
    HWND target=nullptr;
    for(int i=0;i<500 && !target;++i) {
        target=FindWindowW(L"LumenShotRegionSelector",nullptr);
        if(target) {DWORD pid=0;GetWindowThreadProcessId(target,&pid);if(pid!=GetCurrentProcessId()) target=nullptr;}
        if(!target) Sleep(10);
    }
    if(!target) return;
    const POINT start=stage==0?POINT{250,200}:stage==1?POINT{496,346}:stage==4?POINT{200,200}:POINT{104,104};
    const POINT end=stage==0?POINT{370,260}:stage==1?POINT{546,376}:stage==2?POINT{84,74}
                    :stage==3?POINT{-50,-50}:POINT{1200,900};
    SendMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));
    SendMessageW(target,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(end.x,end.y));
    if(stage==0) {
        UpdateWindow(target);HDC dc=GetDC(target);
        annotation_tracks=dc && GetPixel(dc,345,210)==RGB(237,13,47);
        if(dc) ReleaseDC(target,dc);
    }
    SendMessageW(target,WM_LBUTTONUP,0,MAKELPARAM(end.x,end.y));
    const LONG x=std::clamp(expected.right-134,0l,766l)+67;
    const LONG y=(expected.bottom+45<=600?expected.bottom+7:std::max(0l,expected.top-45))+19;
    PostMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
}
Image selector_window_image(HWND window,const Image& previous={}) {
    RECT rect{};GetClientRect(window,&rect);
    Image image;image.width=rect.right;image.height=rect.bottom;image.bgra.resize(checked_pixels(image.width,image.height)*4);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=rect.right;info.bmiHeader.biHeight=-rect.bottom;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(nullptr);void* pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!dc || !bitmap)throw std::runtime_error("Cannot allocate tooltip preview");
    HGDIOBJ old=SelectObject(dc,bitmap);HRGN dirty=CreateRectRgn(0,0,0,0);
    if(!previous.bgra.empty()) {
        require(previous.bgra.size()==image.bgra.size(),"Tooltip preview dimensions changed");
        std::memcpy(pixels,previous.bgra.data(),previous.bgra.size());
        require(GetUpdateRgn(window,dirty,FALSE)!=ERROR,"Cannot read tooltip repaint damage");
        SelectClipRgn(dc,dirty);
    }
    SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    GdiFlush();std::memcpy(image.bgra.data(),pixels,image.bgra.size());ValidateRect(window,nullptr);
    SelectObject(dc,old);DeleteObject(dirty);DeleteObject(bitmap);DeleteDC(dc);
    for(std::size_t i=3;i<image.bgra.size();i+=4)image.bgra[i]=255;
    return image;
}
void check_tooltips(HWND window,POINT vertical,POINT horizontal,bool right_edge,const std::filesystem::path& directory) {
    SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(30,500));
    const auto baseline=selector_window_image(window);auto previous=baseline;
    for(int index=0;index<11;++index) {
        const POINT point=index<8?POINT{vertical.x,vertical.y+index*32}:POINT{horizontal.x+(index-8)*42,horizontal.y};
        const RECT button=index<8?RECT{point.x-15,point.y-16,point.x+15,point.y+16}
                                  :RECT{point.x-21,point.y-16,point.x+21,point.y+16};
        SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(point.x,point.y));
        auto image=selector_window_image(window,previous);
        RECT label{LONG(image.width),LONG(image.height),0,0};
        for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x) {
            const auto i=(std::size_t(y)*image.width+x)*4;
            if(image.bgra[i]==47 && image.bgra[i+1]==39 && image.bgra[i+2]==35) {
                label.left=std::min(label.left,LONG(x));label.top=std::min(label.top,LONG(y));
                label.right=std::max(label.right,LONG(x)+1);label.bottom=std::max(label.bottom,LONG(y)+1);
            }
        }
        if(!valid_rect(label))save_image(directory/L"tooltip-failed.png",image);
        require(valid_rect(label),"A toolbar button lost its tooltip");
        require(label.left>=0 && label.top>=0 && label.right<=LONG(image.width) && label.bottom<=LONG(image.height),"Tooltip left its visible capture area");
        const LONG dx=std::max({0l,button.left-label.right,label.left-button.right});
        const LONG dy=std::max({0l,button.top-label.bottom,label.top-button.bottom});
        require(dx+dy>0 && dx+dy<=12,"Tooltip is detached from its button or covers the button");
        if(index<8 && right_edge)require(label.right<button.left,"Right-edge tooltip did not flip beside its button");
        unsigned ink=0;
        for(LONG y=label.top;y<label.bottom;++y)for(LONG x=label.left;x<label.right;++x) {
            const auto i=(std::size_t(y)*image.width+x)*4;
            if(image.bgra[i]==255 && image.bgra[i+1]==255 && image.bgra[i+2]==255)++ink;
        }
        require(ink>5,"Tooltip label is unreadable");
        bool corner_unchanged=true;
        for(unsigned y=10;y<55;++y)for(unsigned x=10;x<250;++x) {
            const auto i=(std::size_t(y)*image.width+x)*4;
            if(std::memcmp(image.bgra.data()+i,baseline.bgra.data()+i,4))corner_unchanged=false;
        }
        require(corner_unchanged,"Tooltip still uses the distant screen corner");
        if(index==0 || index==9)save_image(directory/((right_edge?L"edge-tooltip-":L"tooltip-")+std::to_wstring(index)+L".png"),image);
        previous=std::move(image);
    }
    SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(30,500));
    auto cleared=selector_window_image(window,previous);
    require(cleared.bgra==baseline.bgra,"Leaving a button left tooltip pixels or a stale highlight");
    SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(vertical.x,vertical.y));
    auto hovered=selector_window_image(window,cleared);SendMessageW(window,WM_MOUSELEAVE,0,0);
    require(selector_window_image(window,hovered).bgra==baseline.bgra,"Leaving capture left tooltip pixels or a stale highlight");
}
constexpr UINT tooltip_probe_message=WM_APP+17;
struct TooltipProbe {POINT vertical,horizontal;bool edge;std::filesystem::path directory;std::exception_ptr failure;};
LRESULT CALLBACK tooltip_probe_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==tooltip_probe_message) {
        auto& probe=*reinterpret_cast<TooltipProbe*>(lp);
        try{check_tooltips(window,probe.vertical,probe.horizontal,probe.edge,probe.directory);}
        catch(...){probe.failure=std::current_exception();}
        return 0;
    }
    return CallWindowProcW(reinterpret_cast<WNDPROC>(GetClassLongPtrW(window,GCLP_WNDPROC)),window,message,wp,lp);
}
void drive_tooltips(POINT vertical,POINT horizontal,bool edge,const std::filesystem::path& directory) {
    HWND window=nullptr;
    for(unsigned i=0;i<500 && !window;++i) {
        window=FindWindowW(L"LumenShotRegionSelector",nullptr);DWORD pid=0;
        if(window)GetWindowThreadProcessId(window,&pid);
        if(window && (pid!=GetCurrentProcessId() || !IsWindowVisible(window)))window=nullptr;
        if(!window)Sleep(10);
    }
    require(window!=nullptr,"Tooltip selector did not open");
    TooltipProbe probe{vertical,horizontal,edge,directory,{}};
    const auto previous=SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(tooltip_probe_proc));
    require(previous!=0,"Cannot attach owned tooltip probe");
    SendMessageW(window,tooltip_probe_message,0,reinterpret_cast<LPARAM>(&probe));
    SetWindowLongPtrW(window,GWLP_WNDPROC,previous);
    if(probe.failure)std::rethrow_exception(probe.failure);
}
int wmain(int argc,wchar_t** argv) try {
    if(argc!=2) throw std::runtime_error("Specify a synthetic evidence directory.");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Runtime runtime;
    const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
    Frame source;source.width=9;source.height=5;source.hdr=true;source.sdr_white=240;
    source.backend=L"Synthetic HDR";
    for(unsigned y=0;y<5;++y) for(unsigned x=0;x<9;++x)
        source.pixels.push_back({float(x)-.125f,float(y)*2,float(x+y),1});
    const auto crop=crop_frame(source,{2,1,8,4});
    require(crop.width==6 && crop.height==3 && crop.hdr && crop.sdr_white==240,"HDR crop metadata changed");
    for(unsigned y=0;y<3;++y) for(unsigned x=0;x<6;++x) {
        const auto a=crop.pixels[y*6+x],b=source.pixels[(y+1)*9+x+2];
        require(a.r==b.r && a.g==b.g && a.b==b.b,"HDR crop changed linear pixels");
    }
    bool rejected=false;
    try{crop_frame(source,{-1,0,2,2});}catch(const std::exception&){rejected=true;}
    require(rejected,"Out-of-bounds selection accepted");
    ColorSettings hdr;hdr.mode=ContentMode::Hdr;hdr.dither=false;
    const auto full=render_sdr(source,hdr);
    auto image=crop_image(full,{2,1,8,4});
    for(unsigned y=0;y<3;++y) for(unsigned x=0;x<6;++x)
        require(std::memcmp(image.bgra.data()+(y*6+x)*4,full.bgra.data()+((y+1)*9+x+2)*4,4)==0,
                "Selection changed tone-mapped pixels");
    const auto dib=clipboard_dib(image);
    BITMAPV5HEADER header{};std::memcpy(&header,dib.data(),sizeof(header));
    require(header.bV5Width==6 && header.bV5Height==-3,"Clipboard orientation changed");
    require(header.bV5CSType==0x73524742 && header.bV5Intent==LCS_GM_IMAGES,"Clipboard color tag missing");
    require(header.bV5RedMask==0x00ff0000 && header.bV5BlueMask==0x000000ff,"Clipboard RGB masks swapped");
    require(std::memcmp(dib.data()+sizeof(header),image.bgra.data(),image.bgra.size())==0,"Clipboard altered encoded pixels");

    Frame sdr;sdr.width=256;sdr.height=128;sdr.pixels.resize(256*128);
    for(unsigned y=0;y<128;++y) for(unsigned x=0;x<256;++x) {
        const float v=float(srgb_decode((x+y)%256/255.0));sdr.pixels[y*256+x]={v,v,v,1};
    }
    const auto parallel=render_sdr(sdr,{});
    for(unsigned y=0;y<128;++y) {
        const unsigned x=(y*17)%256;
        require(parallel.bgra[(y*256+x)*4]==(x+y)%256,"Native parallel worker changed row pixels");
    }
    Image black;black.width=100;black.height=80;black.bgra.resize(100*80*4);
    annotate(black,{{MarkTool::Line,RGB(255,0,0),{{15,15},{75,15}},L""}},{5,5});
    const auto red=(10*100+30)*4;
    require(black.bgra[red]==0 && black.bgra[red+1]==0 && black.bgra[red+2]==255,"Annotation channel order changed");
    require(black.bgra[3]==255,"Annotation output lost opacity");
    require(black.bgra[0]==0 && black.bgra[1]==0 && black.bgra[2]==0,"Annotation changed unrelated pixel");

    Frame scene;scene.width=900;scene.height=600;scene.hdr=true;scene.sdr_white=203;
    scene.pixels.resize(900*600);
    for(unsigned y=0;y<600;++y) for(unsigned x=0;x<900;++x) {
        const double u=x/899.0,v=y/599.0;
        const double sun=std::exp(-((u-.71)*(u-.71)+(v-.25)*(v-.25))*180)*40;
        const double ridge=.55+.10*std::sin(u*14)+.04*std::sin(u*35);
        Rgb c=v>ridge?Rgb{.015+.08*u,.045+.1*u,.07+.16*u}
                        :Rgb{.08+u*.5+sun,.28+(1-v)*.5+sun,.5+(1-v)*.8+sun};
        if(y>510 && y<555) c=Rgb{1,1,1}*(.005+std::pow(u,3)*12.5);
        scene.pixels[y*900+x]={float(c.r),float(c.g),float(c.b),1};
    }
    const auto preview=render_sdr(scene,hdr);
    {
        RegionSelector selector(preview);
        selector.preview_selection({120,105,700,455},{
            {MarkTool::Arrow,RGB(255,65,83),{{390,330},{625,180}},L""},
            {MarkTool::Rectangle,RGB(255,210,53),{{260,255},{360,355}},L""},
            {MarkTool::Text,RGB(255,255,255),{{150,125}},L"Retain HDR. Share SDR."}});
        save_image(directory/L"selector-preview.png",render_selector(selector,900,600));
    }
    const std::pair<RECT,RECT> tooltip_areas[]={
        {{0,0,900,600},{507,104,540,136}},{{-1920,-120,-640,960},{-680,200,-650,232}},
        {{2560,360,4480,1440},{2562,1400,2604,1432}},{{0,0,900,600},{868,4,898,36}},
        {{0,0,900,600},{854,560,896,592}}};
    for(const auto& [area,button]:tooltip_areas)for(bool vertical:{false,true}) {
        const auto label=tooltip_rect(area,button,{160,32},vertical);RECT overlap{};
        require(valid_rect(label) && label.left>=area.left && label.top>=area.top && label.right<=area.right && label.bottom<=area.bottom,
                "Tooltip escaped a monitor or entered a virtual desktop gap");
        require(label.right-label.left==160 && label.bottom-label.top==32,"Tooltip clipped its measured text size");
        require(!IntersectRect(&overlap,&label,&button),"Tooltip covered its button despite available space");
    }
    require(!valid_rect(tooltip_rect({0,0,0,0},{1,1,2,2},{160,32},true)),"Empty tooltip area was accepted");
    Image tooltip_image;tooltip_image.width=900;tooltip_image.height=600;tooltip_image.bgra.resize(900*600*4);
    for(std::size_t i=0;i<tooltip_image.bgra.size();i+=4) {
        tooltip_image.bgra[i]=150;tooltip_image.bgra[i+1]=120;tooltip_image.bgra[i+2]=90;tooltip_image.bgra[i+3]=255;
    }
    for(bool edge:{false,true}) {
        RegionSelector selector(tooltip_image);selector.preview_selection(edge?RECT{450,100,830,350}:RECT{100,100,500,350});
        std::exception_ptr failure;
        NativeWork driver;driver.start([&] {
            try{drive_tooltips(edge?POINT{855,120}:POINT{525,120},edge?POINT{721,376}:POINT{391,376},edge,directory);}
            catch(...){failure=std::current_exception();}
            HWND target=FindWindowW(L"LumenShotRegionSelector",nullptr);DWORD pid=0;if(target)GetWindowThreadProcessId(target,&pid);
            if(target && pid==GetCurrentProcessId())PostMessageW(target,WM_CLOSE,0,0);
        });
        selector.run(nullptr,{80,80,980,680});driver.join();
        if(failure)std::rethrow_exception(failure);
        require(selector.image().bgra==tooltip_image.bgra,"Hover labels modified the screenshot source");
    }
    BYTE saved_keys[256]{};GetKeyboardState(saved_keys);BYTE neutral[256]{};SetKeyboardState(neutral);
    struct RestoreKeys{BYTE* keys;~RestoreKeys(){SetKeyboardState(keys);}} restore{saved_keys};
    for(bool save:{false,true}) {
        RegionSelector selector(preview);
        NativeWork driver;driver.start([save]{drive_selector(save);});
        const auto selected=selector.run(nullptr,{80,80,980,680});driver.join();
        require(selected.action==(save?SelectionAction::Save:SelectionAction::Cancel),"Selector action did not match owned input");
        require(selected.rect.left==100 && selected.rect.top==100 && selected.rect.right==500
             && selected.rect.bottom==350,"Owned drag selected incorrect rectangle");
    }
    {
        RegionSelector selector(preview);
        NativeWork driver;driver.start(drive_editor);
        const auto selected=selector.run(nullptr,{80,80,980,680});driver.join();
        require(selected.action==SelectionAction::Save,"Editor toolbar did not save");
        require(selected.marks.size()==5,"Tool selection, text commit, or undo lost annotations");
        const MarkTool expected[]={MarkTool::Rectangle,MarkTool::Pen,MarkTool::Marker,MarkTool::Line,MarkTool::Text};
        for(unsigned i=0;i<5;++i) require(selected.marks.at(i).tool==expected[i],"Toolbar chose an incorrect annotation tool");
        require(selected.marks.back().text==L"Synthetic text","Text tool lost entered content");
        Image annotated=crop_image(preview,selected.rect);
        annotate(annotated,selected.marks,{selected.rect.left,selected.rect.top});
        save_image(directory/L"annotated-selection.png",annotated);
    }
    const DWORD drawing_objects=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    const RECT adjusted[]={{220,160,620,410},{100,100,550,380},{80,70,500,350},{0,0,500,350},{500,350,900,600}};
    for(int stage=0;stage<5;++stage) {
        RegionSelector selector(preview);
        selector.preview_selection({100,100,500,350},{{MarkTool::Line,RGB(237,13,47),{{150,150},{300,150}},L""}});
        bool annotation_tracks=false;
        NativeWork driver;driver.start([&]{drive_adjustment(stage,adjusted[stage],annotation_tracks);});
        const auto selected=selector.run(nullptr,{80,80,980,680});driver.join();
        require(selected.action==SelectionAction::Save,"Adjusted selection did not save");
        require(EqualRect(&selected.rect,&adjusted[stage]),"Move or resize lost its grab point or desktop boundary");
        require(selected.marks.size()==1,"Moving or resizing discarded an annotation");
        if(stage==0) {
            require(annotation_tracks,"Annotation did not follow the selection during dragging");
            require(selected.marks[0].points[0].x==270 && selected.marks[0].points[0].y==210,
                    "Moving the selection applied annotation offset incorrectly");
        }
    }
    GdiFlush();
    require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==drawing_objects,"Repeated capture overlays leaked drawing resources");
    for(int stage=0;stage<4;++stage) {
        RegionSelector selector(preview);bool reached=false;
        NativeWork driver;driver.start([&]{drive_escape(stage,reached);});
        const auto selected=selector.run(nullptr,{80,80,980,680});driver.join();
        require(reached,"Owned Escape fixture did not reach its focus stage");
        require(selected.action==SelectionAction::Cancel,"One Escape did not close selection");
        require(selected.marks.empty(),"Canceled pending text became an annotation");
        require(GetCapture()==nullptr,"Canceled drag retained mouse capture");
        require(RegisterHotKey(nullptr,778,MOD_NOREPEAT,VK_ESCAPE)!=FALSE,"Selector retained Escape after closing");
        UnregisterHotKey(nullptr,778);
    }
    std::cout<<checks<<" selector checks passed: HDR/SDR crop, parallel rows, annotations, sRGB DIBV5, nearby tooltips, edge flips, hover cleanup, owned drag/save/cancel.\n";
    std::cout<<"System clipboard was not changed. Physical Print Screen was not simulated.\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"FAILED after "<<checks<<" selector checks: "<<error.what()<<"\n";return 1;}
