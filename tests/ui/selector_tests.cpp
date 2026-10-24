#include "image/image_io.hpp"
#include "ui/selector.hpp"
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
    SendMessageW(window,WM_MOUSELEAVE,0,0);const auto outside=selector_window_image(window);
    SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(30,500));
    const auto baseline=selector_window_image(window);auto previous=baseline;
    for(int index=0;index<14;++index) {
        const bool side=index<8 || index>=11;
        const POINT point=side?POINT{vertical.x,vertical.y+(index>=11?index-3:index)*32}:POINT{horizontal.x+(index-8)*42,horizontal.y};
        const RECT button=side?RECT{point.x-15,point.y-16,point.x+15,point.y+16}
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
        if(side && right_edge)require(label.right<button.left,"Right-edge tooltip did not flip beside its button");
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
    require(selector_window_image(window,hovered).bgra==outside.bgra,"Leaving capture left tooltip or magnifier pixels");
}
RECT magnifier_bounds(const Image& image,bool expected=true) {
    RECT bounds{LONG(image.width),LONG(image.height),0,0};
    for(unsigned y=0;y<image.height;++y) for(unsigned x=0;x<image.width;++x) {
        const auto i=(std::size_t(y)*image.width+x)*4;
        if(image.bgra[i]==119 && image.bgra[i+1]==104 && image.bgra[i+2]==94) {
            bounds.left=std::min(bounds.left,LONG(x));bounds.top=std::min(bounds.top,LONG(y));
            bounds.right=std::max(bounds.right,LONG(x)+1);bounds.bottom=std::max(bounds.bottom,LONG(y)+1);
        }
    }
    require(valid_rect(bounds)==expected,"Magnifier visibility disagrees with the capture setting");return bounds;
}
void check_magnifier(HWND window,const Image& source,bool selected,const std::filesystem::path& directory,bool enabled) {
    if(!enabled) {
        for(const POINT point:{POINT{160,180},POINT{899,599}}) {
            SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(point.x,point.y));magnifier_bounds(selector_window_image(window),false);
        }
        SendMessageW(window,WM_CLOSE,0,0);return;
    }
    SendMessageW(window,WM_MOUSELEAVE,0,0);const auto baseline=selector_window_image(window);auto previous=baseline;
    const int cell=MulDiv(8,GetDpiForWindow(window),96);
    const DWORD drawing_objects=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    const POINT points[]={{160,180},{161,180},{161,181},{161,180},{0,0},{899,0},{899,599},{0,599}};
    RECT previous_bounds{};POINT previous_pointer{};
    for(const auto point:points) {
        SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(point.x,point.y));
        const auto image=selector_window_image(window,previous);
        require(image.bgra==selector_window_image(window).bgra,"Moving magnifier left stale pixels outside repaint damage");
        const RECT bounds=magnifier_bounds(image);
        if(valid_rect(previous_bounds) && std::abs(point.x-previous_pointer.x)+std::abs(point.y-previous_pointer.y)==1)
            require(std::abs(bounds.left-previous_bounds.left)<=8 && std::abs(bounds.top-previous_bounds.top)<=8,
                    "One-pixel pointer movement made the magnifier jump to another anchor");
        previous_bounds=bounds;previous_pointer=point;
        require(bounds.left>=0 && bounds.top>=0 && bounds.right<=900 && bounds.bottom<=600,"Magnifier left its visible monitor area");
        const LONG dx=std::max({0l,bounds.left-point.x,point.x-bounds.right});
        const LONG dy=std::max({0l,bounds.top-point.y,point.y-bounds.bottom});
        require(dx<=18 && dy<=18,"Magnifier moved away from the current pointer");
        const LONG left=bounds.left+(bounds.right-bounds.left-11*cell)/2,top=bounds.top+6;
        for(int y=0;y<11;++y) for(int x=0;x<11;++x) {
            if((x==5 && (y==0 || y==10)) || (y==5 && (x==0 || x==10))) continue;
            const LONG sx=std::clamp(point.x+x-5,0l,899l),sy=std::clamp(point.y+y-5,0l,599l);
            const auto actual=(std::size_t(top+y*cell+cell/2)*900+left+x*cell+cell/2)*4;
            const auto expected=(std::size_t(sy)*900+sx)*4;
            require(std::memcmp(image.bgra.data()+actual,source.bgra.data()+expected,4)==0,
                    "Magnifier interpolated, shifted or dimmed a frozen source pixel");
        }
        if(point.x==160 && point.y==180) save_image(directory/(selected?L"magnifier-selected.png":L"magnifier-hover.png"),image);
        previous=image;
    }
    GdiFlush();require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==drawing_objects,"Magnifier pointer updates leaked drawing resources");
    SendMessageW(window,WM_MOUSELEAVE,0,0);
    require(selector_window_image(window,previous).bgra==baseline.bgra,"Leaving capture retained magnifier pixels");
    if(selected) {
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(500,350));
        SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(501,351));
        SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(501,351));
        SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(700,450));const auto before=selector_window_image(window);
        const RECT old_bounds=magnifier_bounds(before);
        BYTE saved[256]{},keys[256]{};GetKeyboardState(saved);keys[VK_SHIFT]=0x80;SetKeyboardState(keys);
        SendMessageW(window,WM_KEYDOWN,VK_RIGHT,0);const auto after=selector_window_image(window,before);
        const RECT new_bounds=magnifier_bounds(after);
        require(EqualRect(&old_bounds,&new_bounds),"Keyboard adjustment moved a safely placed magnifier");
        bool label_changed=false;
        for(LONG y=old_bounds.top+6+11*cell;y<old_bounds.bottom;++y) for(LONG x=old_bounds.left;x<old_bounds.right;++x) {
            const auto i=(std::size_t(y)*900+x)*4;
            label_changed=label_changed || std::memcmp(before.bgra.data()+i,after.bgra.data()+i,4)!=0;
        }
        require(label_changed,"Keyboard resize did not update live magnifier dimensions");
        SendMessageW(window,WM_KEYDOWN,VK_DOWN,0);keys[VK_SHIFT]=0;keys[VK_CONTROL]=0x80;SetKeyboardState(keys);
        SendMessageW(window,WM_KEYDOWN,VK_RIGHT,0);SendMessageW(window,WM_KEYDOWN,VK_DOWN,0);
        keys[VK_CONTROL]=0;SetKeyboardState(keys);SendMessageW(window,WM_KEYDOWN,VK_RIGHT,0);SetKeyboardState(saved);
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(436,378));
    } else SendMessageW(window,WM_CLOSE,0,0);
}
void check_drag_magnifier(HWND window,int direction,const std::filesystem::path& directory) {
    const LONG sx=direction==1 || direction==2?-1:1,sy=direction>=2?-1:1;
    const POINT start{sx>0?100l:800l,sy>0?100l:500l};
    POINT pointer=start;RECT previous{};
    const auto check_panel=[&](RECT selection) {
        const auto image=selector_window_image(window);RECT panel{};
        try {panel=magnifier_bounds(image);} catch(...) {
            std::cerr<<"DRAG_CONTEXT direction="<<direction<<" pointer="<<pointer.x<<","<<pointer.y
                     <<" selection="<<selection.left<<","<<selection.top<<","<<selection.right<<","<<selection.bottom<<"\n";
            save_image(directory/L"magnifier-drag-failed.png",image);throw;
        }
        const LONG dx=std::max({0l,panel.left-pointer.x,pointer.x-panel.right});
        const LONG dy=std::max({0l,panel.top-pointer.y,pointer.y-panel.bottom});
        require(dx<=24 && dy<=24,"Dragging left the magnifier behind the current pointer");

        HDC dc=GetDC(window);require(dc!=nullptr,"Cannot inspect owned drag repaint");GdiFlush();
        const POINT samples[]={{panel.left,panel.top},{panel.right-1,panel.bottom-1},{previous.left,previous.top},{previous.right-1,previous.bottom-1}};
        for(const auto point:samples) if(point.x>=0 && point.y>=0 && point.x<900 && point.y<600) {
            const auto i=(std::size_t(point.y)*900+point.x)*4;
            const COLORREF expected=RGB(image.bgra[i+2],image.bgra[i+1],image.bgra[i]);
            require(GetPixel(dc,point.x,point.y)==expected,"Visible drag repaint left an old magnifier or missed its current location");
        }
        ReleaseDC(window,dc);previous=panel;return image;
    };
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));
    for(int step=1;step<=20;++step) {
        pointer={start.x+sx*step*25,start.y+sy*step*16};
        SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(pointer.x,pointer.y));
        check_panel(normalize_rect(start,pointer));
    }
    for(int step=19;step>=5;--step) {
        pointer={start.x+sx*step*25,start.y+sy*step*16};
        SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(pointer.x,pointer.y));check_panel(normalize_rect(start,pointer));
    }
    for(int step=6;step<=20;++step) {
        pointer={start.x+sx*step*25,start.y+sy*step*16};
        SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(pointer.x,pointer.y));check_panel(normalize_rect(start,pointer));
    }
    RECT selection=normalize_rect(start,pointer);const RECT before_release=previous;
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(pointer.x,pointer.y));UpdateWindow(window);
    save_image(directory/(L"magnifier-drag-"+std::to_wstring(direction)+L".png"),check_panel(selection));
    require(EqualRect(&before_release,&previous),"Releasing a capture box changed the magnifier anchor");
    if(direction==0) {
        const std::pair<LONG,LONG> paths[]={{80,120},{416,448},{580,606}};
        for(const auto& path:paths) {
            RECT last_sweep{};
            for(LONG x=path.first;x<=path.second;++x) {
                pointer={x,250};SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(pointer.x,pointer.y));UpdateWindow(window);
                const auto frame=check_panel(selection);
                if(x==416 || x==432 || x==448)
                    save_image(directory/(L"magnifier-free-"+std::to_wstring(x)+L".png"),frame);
                if(valid_rect(last_sweep)) require(std::abs(previous.left-last_sweep.left)<=1 && std::abs(previous.top-last_sweep.top)<=1,
                        "Magnifier teleported while cursor crossed the capture box");
                last_sweep=previous;
            }
        }
        pointer={start.x+sx*500,start.y+sy*320};
    }
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(pointer.x,pointer.y));
    pointer.x+=sx*30;pointer.y+=sy*20;
    SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(pointer.x,pointer.y));
    selection=normalize_rect(start,pointer);check_panel(selection);
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(pointer.x,pointer.y));
    POINT center{(selection.left+selection.right)/2,(selection.top+selection.bottom)/2};
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(center.x,center.y));
    pointer={center.x+15,center.y+10};OffsetRect(&selection,15,10);
    SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(pointer.x,pointer.y));check_panel(selection);
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(pointer.x,pointer.y));
    SendMessageW(window,WM_CLOSE,0,0);
}
constexpr UINT magnifier_probe_message=WM_APP+19;
struct MagnifierProbe {const Image& source;bool selected;std::filesystem::path directory;std::exception_ptr failure;bool enabled;int drag_direction;};
LRESULT CALLBACK magnifier_probe_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==magnifier_probe_message) {
        auto& probe=*reinterpret_cast<MagnifierProbe*>(lp);
        try {
            if(probe.drag_direction>=0) check_drag_magnifier(window,probe.drag_direction,probe.directory);
            else check_magnifier(window,probe.source,probe.selected,probe.directory,probe.enabled);
        } catch(...) {probe.failure=std::current_exception();}
        return 0;
    }
    return CallWindowProcW(reinterpret_cast<WNDPROC>(GetClassLongPtrW(window,GCLP_WNDPROC)),window,message,wp,lp);
}
void drive_magnifier(const Image& source,bool selected,const std::filesystem::path& directory,bool enabled=true,int drag_direction=-1) {
    HWND window=nullptr;
    for(unsigned i=0;i<500 && !window;++i) {
        window=FindWindowW(L"LumenShotRegionSelector",nullptr);DWORD pid=0;
        if(window) GetWindowThreadProcessId(window,&pid);
        if(window && (pid!=GetCurrentProcessId() || !IsWindowVisible(window))) window=nullptr;
        if(!window) Sleep(10);
    }
    require(window!=nullptr,"Magnifier selector did not open");
    MagnifierProbe probe{source,selected,directory,{},enabled,drag_direction};
    const auto previous=SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(magnifier_probe_proc));
    require(previous!=0,"Cannot attach owned magnifier probe");
    SendMessageW(window,magnifier_probe_message,0,reinterpret_cast<LPARAM>(&probe));
    SetWindowLongPtrW(window,GWLP_WNDPROC,previous);
    if(probe.failure) {PostMessageW(window,WM_CLOSE,0,0);std::rethrow_exception(probe.failure);}
}
void check_privacy_input(HWND window,const std::filesystem::path& directory) {
    const auto click=[&](LONG x,LONG y) {
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(x,y));
    };
    const auto draw=[&](LONG x,LONG y,LONG right,LONG bottom) {
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
        SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(right,bottom));
        SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(right,bottom));
    };
    click(525,376);draw(151,151,211,181);
    auto previous=selector_window_image(window);
    require(previous.bgra[(165*900+180)*4]==0 && previous.bgra[(165*900+180)*4+2]==0,"Redaction toolbar did not draw an opaque region");
    SendMessageW(window,WM_KEYDOWN,'B',0);draw(231,151,291,181);
    auto current=selector_window_image(window,previous);
    require(current.bgra==selector_window_image(window).bgra,"Blur preview depended on repaint damage");previous=current;
    click(525,440);draw(311,151,371,181);current=selector_window_image(window,previous);
    require(current.bgra==selector_window_image(window).bgra,"Pixelation preview left stale repaint pixels");
    save_image(directory/L"privacy-tools.png",current);
    click(525,344);current=selector_window_image(window,current);
    for(unsigned x=320;x<360;++x) {
        const auto i=(165*900+x)*4;
        require(current.bgra[i]==std::uint8_t((x+165)%100+120),"Undo did not restore pixelated source detail");
    }
    SendMessageW(window,WM_KEYDOWN,'P',0);draw(311,151,371,181);
    SendMessageW(window,WM_KEYDOWN,'V',0);draw(450,300,465,310);
    SendMessageW(window,WM_KEYDOWN,VK_RIGHT,0);current=selector_window_image(window);
    require(current.bgra[(170*900+180)*4]==0 && current.bgra[(170*900+180)*4+2]==0,"Moving selection left its redaction behind");
    SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(180,170));current=selector_window_image(window);
    const RECT panel=magnifier_bounds(current);const int cell=MulDiv(8,GetDpiForWindow(window),96);
    const LONG cx=panel.left+(panel.right-panel.left-11*cell)/2+5*cell+cell/2,cy=panel.top+6+5*cell+cell/2;
    const auto pixel=(std::size_t(cy)*900+cx)*4;
    require(current.bgra[pixel]==0 && current.bgra[pixel+1]==0 && current.bgra[pixel+2]==0,"Magnifier exposed original pixels beneath opaque redaction");
    click(449,386);
}
constexpr UINT privacy_probe_message=WM_APP+20;
struct PrivacyProbe {std::filesystem::path directory;std::exception_ptr failure;};
LRESULT CALLBACK privacy_probe_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==privacy_probe_message) {
        auto& probe=*reinterpret_cast<PrivacyProbe*>(lp);
        try {check_privacy_input(window,probe.directory);} catch(...) {probe.failure=std::current_exception();}
        return 0;
    }
    return CallWindowProcW(reinterpret_cast<WNDPROC>(GetClassLongPtrW(window,GCLP_WNDPROC)),window,message,wp,lp);
}
void drive_privacy(const std::filesystem::path& directory) {
    HWND window=nullptr;
    for(int i=0;i<500 && !window;++i) {
        window=FindWindowW(L"LumenShotRegionSelector",nullptr);DWORD pid=0;if(window) GetWindowThreadProcessId(window,&pid);
        if(window && (pid!=GetCurrentProcessId() || !IsWindowVisible(window))) window=nullptr;
        if(!window) Sleep(10);
    }
    require(window!=nullptr,"Privacy selector did not open");PrivacyProbe probe{directory,{}};
    const auto previous=SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(privacy_probe_proc));
    require(previous!=0,"Cannot attach owned privacy probe");
    SendMessageW(window,privacy_probe_message,0,reinterpret_cast<LPARAM>(&probe));SetWindowLongPtrW(window,GWLP_WNDPROC,previous);
    if(probe.failure) {PostMessageW(window,WM_CLOSE,0,0);std::rethrow_exception(probe.failure);}
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
    for(const RECT area:{RECT{0,0,3840,2160},RECT{-1920,-120,0,960},RECT{2560,400,4480,1480}}) {
        for(const POINT pointer:{POINT{area.left,area.top},POINT{area.right-1,area.bottom-1},POINT{area.left+300,area.top+300}}) {
            const auto panel=magnifier_rect(area,pointer,{144,148});
            require(valid_rect(panel) && panel.left>=area.left && panel.top>=area.top && panel.right<=area.right && panel.bottom<=area.bottom,
                    "Magnifier entered a virtual desktop gap or lost negative coordinates");
        }
    }
    require(!valid_rect(magnifier_rect({0,0,40,40},{20,20},{144,148})),"Magnifier obscured an undersized capture");
    const RECT panel_area{0,0,900,600};const SIZE panel_size{144,148};
    RECT last=magnifier_rect(panel_area,{734,300},panel_size);
    for(LONG x=735;x<=760;++x) {
        const RECT next=magnifier_rect(panel_area,{x,300},panel_size);
        require(next.left>=last.left && next.left<=last.left+1 && next.top==last.top,"Magnifier flipped sides at a monitor edge");
        last=next;
    }
    last=magnifier_rect(panel_area,{450,430},panel_size);
    for(LONG y=431;y<=460;++y) {
        const RECT next=magnifier_rect(panel_area,{450,y},panel_size);
        require(next.top>=last.top && next.top<=last.top+1 && next.left==last.left,"Magnifier flipped vertically at a monitor edge");
        last=next;
    }
    Image grid;grid.width=900;grid.height=600;grid.bgra.resize(900*600*4);
    for(unsigned y=0;y<600;++y) for(unsigned x=0;x<900;++x) {
        const auto i=(std::size_t(y)*900+x)*4;
        grid.bgra[i]=(x+y)%100+120;grid.bgra[i+1]=y%180+40;grid.bgra[i+2]=x%200+20;grid.bgra[i+3]=255;
    }
    {
        RegionSelector selector(grid);selector.preview_selection({100,100,500,350});std::exception_ptr failure;NativeWork driver;
        driver.start([&] {try {drive_privacy(directory);} catch(...) {failure=std::current_exception();
            HWND target=FindWindowW(L"LumenShotRegionSelector",nullptr);DWORD pid=0;if(target) GetWindowThreadProcessId(target,&pid);
            if(target && pid==GetCurrentProcessId()) PostMessageW(target,WM_CLOSE,0,0);
        }});
        const auto selected=selector.run(nullptr,{80,80,980,680});driver.join();if(failure) std::rethrow_exception(failure);
        const RECT expected{116,110,516,360};
        require(selected.action==SelectionAction::Save && EqualRect(&selected.rect,&expected),"Privacy tools disrupted crop movement or saving");
        require(selected.marks.size()==3 && selected.marks[0].tool==MarkTool::Redact && selected.marks[1].tool==MarkTool::Blur
                && selected.marks[2].tool==MarkTool::Pixelate,"Privacy buttons, shortcuts, or undo selected the wrong tools");
        require(selected.marks[0].points.front().x==167 && selected.marks[0].points.front().y==161,"Privacy annotations lost physical crop coordinates");
        require(selector.image().bgra==grid.bgra,"Privacy editor changed the frozen capture");
    }
    for(int direction=0;direction<4;++direction) {
        RegionSelector selector(grid);std::exception_ptr failure;NativeWork driver;
        driver.start([&] {try {drive_magnifier(grid,false,directory,true,direction);} catch(...) {failure=std::current_exception();}});
        const auto result=selector.run(nullptr,{80,80,980,680});driver.join();if(failure) std::rethrow_exception(failure);
        require(result.action==SelectionAction::Cancel && selector.image().bgra==grid.bgra,"Magnifier dragging changed capture pixels or action");
    }
    {
        RegionSelector selector(grid,false);std::exception_ptr failure;NativeWork driver;
        driver.start([&] {try {drive_magnifier(grid,false,directory,false);} catch(...) {failure=std::current_exception();}});
        const auto result=selector.run(nullptr,{80,80,980,680});driver.join();if(failure) std::rethrow_exception(failure);
        require(result.action==SelectionAction::Cancel && selector.image().bgra==grid.bgra,"Disabled magnifier changed capture pixels or action");
    }
    for(bool selected:{false,true}) {
        RegionSelector selector(grid);if(selected) selector.preview_selection({100,100,500,350});
        std::exception_ptr failure;NativeWork driver;driver.start([&] {
            try {drive_magnifier(grid,selected,directory);} catch(...) {failure=std::current_exception();}
        });
        const auto result=selector.run(nullptr,{80,80,980,680});driver.join();if(failure) std::rethrow_exception(failure);
        require(selector.image().bgra==grid.bgra,"Magnifier modified exported source pixels");
        if(selected) {
            const RECT expected{102,101,503,352};
            require(result.action==SelectionAction::Save && EqualRect(&result.rect,&expected),"One-pixel resize or arrow-key adjustment changed crop coordinates");
            const auto cropped=crop_image(selector.image(),result.rect);
            require(cropped.width==401 && cropped.height==251 && std::memcmp(cropped.bgra.data(),grid.bgra.data()+(101*900+102)*4,4)==0,
                    "Magnifier or keyboard adjustment leaked overlay pixels into the crop");
        } else require(result.action==SelectionAction::Cancel,"Magnifier hover changed the capture action");
    }
    std::cout<<checks<<" selector checks passed: HDR/SDR crop, annotations, sRGB DIBV5, tooltips, magnifier pixels/edges/repaint, keyboard dimensions, owned drag/save/cancel.\n";
    std::cout<<"System clipboard was not changed. Physical Print Screen was not simulated.\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"FAILED after "<<checks<<" selector checks: "<<error.what()<<"\n";return 1;}
