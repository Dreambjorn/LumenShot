#pragma once
#include "image/color.hpp"
#include "ui/window_snap.hpp"
#include <commdlg.h>
#include <windowsx.h>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace hdrshot {

enum class MarkTool { Select, Pen, Line, Arrow, Rectangle, Marker, Text };
struct Annotation {
    MarkTool tool = MarkTool::Pen;
    COLORREF color = RGB(255,0,0);
    std::vector<POINT> points;
    std::wstring text;
};
enum class SelectionAction { Cancel, Save, Copy };
struct SelectionResult {
    SelectionAction action = SelectionAction::Cancel;
    RECT rect{};
    std::vector<Annotation> marks;
};
inline RECT normalize_rect(POINT a, POINT b) {
    return {std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x,b.x),std::max(a.y,b.y)};
}
inline bool valid_rect(RECT r) { return r.right>r.left && r.bottom>r.top; }
inline RECT tooltip_rect(RECT area,RECT button,SIZE size,bool vertical) {
    if(!valid_rect(area) || size.cx<=0 || size.cy<=0) return {};
    const LONG margin=std::min({4l,(area.right-area.left-1)/2,(area.bottom-area.top-1)/2});
    const LONG width=std::min(size.cx,area.right-area.left-2*margin),height=std::min(size.cy,area.bottom-area.top-2*margin);
    LONG x=vertical?button.right+8:(button.left+button.right-width)/2;
    LONG y=vertical?(button.top+button.bottom-height)/2:button.bottom+8;
    if(vertical && x+width>area.right-margin) x=button.left-8-width;
    if(!vertical && y+height>area.bottom-margin) y=button.top-8-height;
    x=std::clamp(x,area.left+margin,area.right-margin-width);
    y=std::clamp(y,area.top+margin,area.bottom-margin-height);
    return {x,y,x+width,y+height};
}
inline Frame crop_frame(const Frame& source,RECT rect) {
    if (!valid_rect(rect) || rect.left<0 || rect.top<0
        || rect.right>LONG(source.width) || rect.bottom>LONG(source.height))
        throw std::runtime_error("Selection is outside the captured image.");
    Frame result;
    result.width=unsigned(rect.right-rect.left);result.height=unsigned(rect.bottom-rect.top);
    result.hdr=source.hdr;result.sdr_white=source.sdr_white;result.backend=source.backend;
    if(!source.regions.empty()) {
        result.hdr=false;result.sdr_white=80;
        for(const auto& region:source.regions) {
            const LONG left=std::max(rect.left,LONG(region.x)),top=std::max(rect.top,LONG(region.y));
            const LONG right=std::min(rect.right,LONG(region.x+region.width));
            const LONG bottom=std::min(rect.bottom,LONG(region.y+region.height));
            if(right<=left || bottom<=top) continue;
            result.regions.push_back({unsigned(left-rect.left),unsigned(top-rect.top),unsigned(right-left),
                                      unsigned(bottom-top),region.hdr,region.sdr_white});
            result.hdr=result.hdr || region.hdr;
        }
    }
    // An all-SDR capture has no float pixels; HDR consumers reject the empty crop.
    if(source.pixels.empty()) return result;
    result.pixels.resize(checked_pixels(result.width,result.height));
    for(unsigned y=0;y<result.height;++y)
        std::copy_n(source.pixels.begin()+std::size_t(y+rect.top)*source.width+rect.left,
                    result.width,result.pixels.begin()+std::size_t(y)*result.width);
    return result;
}
inline Image crop_image(const Image& source,RECT rect) {
    if(!valid_rect(rect) || rect.left<0 || rect.top<0
       || rect.right>LONG(source.width) || rect.bottom>LONG(source.height))
        throw std::runtime_error("Selection is outside the converted image.");
    Image result;
    result.width=unsigned(rect.right-rect.left);result.height=unsigned(rect.bottom-rect.top);
    result.tone_mapped=source.tone_mapped;result.source_peak=source.source_peak;
    result.reference_white=source.reference_white;result.description=source.description;
    result.bgra.resize(checked_pixels(result.width,result.height)*4);
    for(unsigned y=0;y<result.height;++y)
        std::memcpy(result.bgra.data()+std::size_t(y)*result.width*4,
            source.bgra.data()+(std::size_t(y+rect.top)*source.width+rect.left)*4,result.width*4);
    return result;
}
inline void draw_annotation(HDC dc,const Annotation& mark,POINT offset={},double scale_x=1,double scale_y=1) {
    if(mark.points.empty()) return;
    const auto point=[&](std::size_t i) {
        return POINT{LONG(std::lround((mark.points[i].x-offset.x)*scale_x)),LONG(std::lround((mark.points[i].y-offset.y)*scale_y))};
    };
    const double scale=(scale_x+scale_y)*.5;
    const int width=std::max(1,int(std::lround((mark.tool==MarkTool::Marker?14:3)*scale)));
    HPEN pen=CreatePen(PS_SOLID,width,mark.color);
    HGDIOBJ old_pen=SelectObject(dc,pen),old_brush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    const POINT a=point(0),b=point(mark.points.size()-1);
    if(mark.tool==MarkTool::Text) {
        HFONT font=CreateFontW(-std::max(1,int(std::lround(23*scale_y))),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        HGDIOBJ old_font=SelectObject(dc,font);
        SetTextColor(dc,mark.color);SetBkMode(dc,TRANSPARENT);
        TextOutW(dc,a.x,a.y,mark.text.c_str(),int(mark.text.size()));
        SelectObject(dc,old_font);DeleteObject(font);
    } else if(mark.tool==MarkTool::Rectangle) {
        Rectangle(dc,std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x,b.x)+1,std::max(a.y,b.y)+1);
    } else {
        MoveToEx(dc,a.x,a.y,nullptr);
        if(mark.points.size()==1) LineTo(dc,a.x+1,a.y+1);
        for(std::size_t i=1;i<mark.points.size();++i) {const POINT p=point(i);LineTo(dc,p.x,p.y);}
        if(mark.tool==MarkTool::Arrow) {
            const double angle=std::atan2(double(b.y-a.y),double(b.x-a.x));
            const POINT head[3]={{b.x-LONG(15*scale*std::cos(angle-.45)),b.y-LONG(15*scale*std::sin(angle-.45))},
                                 b,{b.x-LONG(15*scale*std::cos(angle+.45)),b.y-LONG(15*scale*std::sin(angle+.45))}};
            Polyline(dc,head,3);
        }
    }
    SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(pen);
}
inline void annotate(Image& image,const std::vector<Annotation>& marks,POINT offset,double scale_x=1,double scale_y=1) {
    DiagnosticTimer timing(DiagnosticStage::Annotation);
    if(marks.empty()) return;
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=LONG(image.width);info.bmiHeader.biHeight=-LONG(image.height);
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* data=nullptr;
    HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);
    if(!dc || !bitmap) {if(dc) DeleteDC(dc);throw std::runtime_error("Cannot draw screenshot annotations.");}
    HGDIOBJ old=SelectObject(dc,bitmap);
    std::memcpy(data,image.bgra.data(),image.bgra.size());
    for(const auto& mark:marks) draw_annotation(dc,mark,offset,scale_x,scale_y);
    GdiFlush();
    std::memcpy(image.bgra.data(),data,image.bgra.size());
    for(std::size_t i=3;i<image.bgra.size();i+=4) image.bgra[i]=255;
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
inline std::vector<std::uint8_t> clipboard_dib(const Image& image) {
    const auto bytes=checked_pixels(image.width,image.height)*4;
    if(image.bgra.size()!=bytes) throw std::runtime_error("Invalid clipboard pixels.");
    BITMAPV5HEADER header{};
    header.bV5Size=sizeof(header);header.bV5Width=LONG(image.width);header.bV5Height=-LONG(image.height);
    header.bV5Planes=1;header.bV5BitCount=32;header.bV5Compression=BI_BITFIELDS;
    header.bV5SizeImage=DWORD(bytes);header.bV5RedMask=0x00ff0000;header.bV5GreenMask=0x0000ff00;
    header.bV5BlueMask=0x000000ff;header.bV5AlphaMask=0xff000000;
    header.bV5CSType=0x73524742;header.bV5Intent=LCS_GM_IMAGES;
    std::vector<std::uint8_t> data(sizeof(header)+bytes);
    std::memcpy(data.data(),&header,sizeof(header));
    std::memcpy(data.data()+sizeof(header),image.bgra.data(),bytes);
    return data;
}
inline void copy_image(HWND owner,const Image& image) {
    DiagnosticTimer timing(DiagnosticStage::Clipboard);
    const auto data=clipboard_dib(image);
    HGLOBAL block=GlobalAlloc(GMEM_MOVEABLE,data.size());
    if(!block) throw std::runtime_error("Cannot allocate clipboard image.");
    void* memory=GlobalLock(block);
    if(!memory) {GlobalFree(block);throw std::runtime_error("Cannot access clipboard image.");}
    std::memcpy(memory,data.data(),data.size());GlobalUnlock(block);
    bool opened=false;
    for(int i=0;i<12 && !opened;++i) {opened=OpenClipboard(owner)!=FALSE;if(!opened) Sleep(15);}
    if(!opened) {GlobalFree(block);throw std::runtime_error("Clipboard is busy. Try copying again.");}
    if(!EmptyClipboard() || !SetClipboardData(CF_DIBV5,block)) {
        CloseClipboard();GlobalFree(block);throw std::runtime_error("Cannot copy screenshot to clipboard.");
    }
    CloseClipboard();
}

inline constexpr int selection_escape_hotkey_id=1;

class SelectionSurface {
    HDC dc_=nullptr;
    HBITMAP bitmap_=nullptr;
    HGDIOBJ previous_=nullptr;
    void* pixels_=nullptr;
    void clear() noexcept {
        if(previous_ && previous_!=HGDI_ERROR) SelectObject(dc_,previous_);
        if(bitmap_) DeleteObject(bitmap_);
        if(dc_) DeleteDC(dc_);
        dc_=nullptr;bitmap_=nullptr;previous_=nullptr;pixels_=nullptr;
    }
public:
    SelectionSurface()=default;
    SelectionSurface(const SelectionSurface&)=delete;
    SelectionSurface& operator=(const SelectionSurface&)=delete;
    ~SelectionSurface(){clear();}
    void create(HDC target,unsigned width,unsigned height) {
        if(dc_) return;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=LONG(width);info.bmiHeader.biHeight=-LONG(height);
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        dc_=CreateCompatibleDC(target);
        if(dc_) bitmap_=CreateDIBSection(target,&info,DIB_RGB_COLORS,&pixels_,nullptr,0);
        if(bitmap_) previous_=SelectObject(dc_,bitmap_);
        if(!dc_ || !bitmap_ || !previous_ || previous_==HGDI_ERROR) {
            clear();throw std::runtime_error("Cannot allocate selection drawing surface.");
        }
    }
    HDC dc() const {return dc_;}
    std::uint8_t* pixels() const {return static_cast<std::uint8_t*>(pixels_);}
};

class RegionSelector {
    static constexpr wchar_t class_name[]=L"LumenShotRegionSelector";
    HWND window_=nullptr,edit_=nullptr;
    Image image_;
    SelectionSurface original_,dimmed_,buffer_;
    RECT last_visual_{};
    RECT selection_{},drag_start_{},snap_hover_{},snap_pending_{};
    std::vector<RECT> snap_windows_;
    POINT anchor_{},text_origin_{};
    std::vector<Annotation> marks_;
    Annotation current_;
    MarkTool tool_=MarkTool::Select;
    COLORREF color_=RGB(255,0,0);
    HFONT font_=nullptr;
    SelectionAction action_=SelectionAction::Cancel;
    int drag_=0,resize_=0,hover_=-1;
    bool quick_copy_=false,finished_=false;
    RECT vertical_{},horizontal_{},hint_bounds_{};
    std::wstring hint_;

    POINT clamp_point(POINT p) const {
        return {std::clamp(p.x,0l,LONG(image_.width)),std::clamp(p.y,0l,LONG(image_.height))};
    }
    void layout() {
        RECT area{0,0,LONG(image_.width),LONG(image_.height)};
        if(window_) {
            POINT origin{};ClientToScreen(window_,&origin);
            const POINT corner{origin.x+selection_.right-1,origin.y+selection_.bottom-1};
            MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
            if(GetMonitorInfoW(MonitorFromPoint(corner,MONITOR_DEFAULTTONEAREST),&monitor)) {
                OffsetRect(&monitor.rcMonitor,-origin.x,-origin.y);RECT visible{};
                if(IntersectRect(&visible,&area,&monitor.rcMonitor)) area=visible;
            }
        }
        // A virtual desktop can contain gaps where no toolbar is visible.
        const LONG preferred=selection_.right+43<=area.right?selection_.right+7:selection_.left-43;
        const LONG x=std::clamp(preferred,area.left,std::max(area.left,area.right-36));
        const LONG y=std::clamp(selection_.top,area.top,std::max(area.top,area.bottom-8*32-8));
        vertical_={x,y,x+36,y+8*32+8};
        const LONG bx=std::clamp(selection_.right-3*42-8,area.left,std::max(area.left,area.right-3*42-8));
        const LONG below=selection_.bottom+45<=area.bottom?selection_.bottom+7:selection_.top-45;
        const LONG by=std::clamp(below,area.top,std::max(area.top,area.bottom-38));
        horizontal_={bx,by,bx+3*42+8,by+38};
        hint_bounds_={};
        if(!hint_.empty() && hover_>=0) {
            HDC dc=GetDC(window_);
            if(!dc) throw std::runtime_error("Cannot measure selection tooltip.");
            HGDIOBJ old=SelectObject(dc,font_);SIZE size{};
            const BOOL measured=GetTextExtentPoint32W(dc,hint_.c_str(),int(hint_.size()),&size);
            SelectObject(dc,old);ReleaseDC(window_,dc);
            if(!measured) throw std::runtime_error("Cannot measure selection tooltip.");
            hint_bounds_=tooltip_rect(area,button_bounds(hover_),{size.cx+16,size.cy+10},hover_<8);
        }
    }
    RECT button_bounds(int index) const {
        if(index<8) return {vertical_.left+3,vertical_.top+4+index*32,vertical_.right-3,vertical_.top+4+(index+1)*32};
        const int i=index-8;
        return {horizontal_.left+4+i*42,horizontal_.top+3,horizontal_.left+4+(i+1)*42,horizontal_.bottom-3};
    }
    int hit_button(POINT p) const {
        if(!valid_rect(selection_)) return -1;
        if(PtInRect(&vertical_,p)) return std::clamp(int((p.y-vertical_.top-4)/32),0,7);
        if(PtInRect(&horizontal_,p)) return 8+std::clamp(int((p.x-horizontal_.left-4)/42),0,2);
        return -1;
    }
    int hit_handle(POINT p) const {
        if(!valid_rect(selection_)) return 0;
        int bits=0;
        if(std::abs(p.x-selection_.left)<=7) bits|=1;
        if(std::abs(p.x-selection_.right)<=7) bits|=2;
        if(std::abs(p.y-selection_.top)<=7) bits|=4;
        if(std::abs(p.y-selection_.bottom)<=7) bits|=8;
        if(p.x<selection_.left-7 || p.x>selection_.right+7
           || p.y<selection_.top-7 || p.y>selection_.bottom+7) return 0;
        return bits;
    }
    RECT visual_bounds() const {
        RECT bounds{};
        const auto include=[&](RECT rect) {RECT combined{};UnionRect(&combined,&bounds,&rect);bounds=combined;};
        if(valid_rect(selection_)) {
            RECT selection=selection_;InflateRect(&selection,5,5);
            selection.top=std::min(selection.top,std::max(0l,selection_.top-25));
            selection.right=std::max(selection.right,selection_.left+220);
            include(selection);
            if(!drag_) {include(vertical_);include(horizontal_);}
        }
        if(valid_rect(snap_hover_)) {RECT hover=snap_hover_;InflateRect(&hover,2,2);include(hover);}
        if(!hint_.empty()) include(hint_bounds_);
        return bounds;
    }
    void invalidate() {
        if(!window_ || finished_) return;
        const RECT previous=last_visual_;
        if(!drag_) layout();
        last_visual_=visual_bounds();
        if(valid_rect(previous)) InvalidateRect(window_,&previous,FALSE);
        if(valid_rect(last_visual_)) InvalidateRect(window_,&last_visual_,FALSE);
    }
    void finish(SelectionAction action) {
        if(finished_) return;
        if(action==SelectionAction::Cancel) {
            const HWND popup=GetLastActivePopup(window_);
            if(popup && popup!=window_) SendMessageW(popup,WM_CLOSE,0,0);
        }
        commit_text(action==SelectionAction::Cancel);
        if(action!=SelectionAction::Cancel && !valid_rect(selection_)) return;
        if(GetCapture()==window_) ReleaseCapture();
        drag_=0;action_=action;finished_=true;ShowWindow(window_,SW_HIDE);
    }
    void commit_text(bool cancel=false) {
        if(!edit_) return;
        const int count=GetWindowTextLengthW(edit_);
        if(!cancel && count>0) {
            std::wstring value(std::size_t(count)+1,L'\0');
            GetWindowTextW(edit_,value.data(),count+1);value.resize(count);
            marks_.push_back({MarkTool::Text,color_,{text_origin_},value});
        }
        DestroyWindow(edit_);edit_=nullptr;SetFocus(window_);
    }
    void button(int index) {
        commit_text();
        if(index<=5) tool_=static_cast<MarkTool>(index+1);
        else if(index==6) {
            static COLORREF custom[16]{};
            CHOOSECOLORW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window_;
            dialog.rgbResult=color_;dialog.lpCustColors=custom;dialog.Flags=CC_FULLOPEN|CC_RGBINIT;
            if(ChooseColorW(&dialog)) color_=dialog.rgbResult;
        } else if(index==7 && !marks_.empty()) marks_.pop_back();
        else if(index==8) finish(SelectionAction::Copy);
        else if(index==9) finish(SelectionAction::Save);
        else if(index==10) finish(SelectionAction::Cancel);
        invalidate();
    }
    void mouse_down(POINT p) {
        const int button_index=hit_button(p);
        if(button_index>=0) {button(button_index);return;}
        commit_text();
        hint_.clear();hover_=-1;
        anchor_=clamp_point(p);drag_start_=selection_;
        snap_pending_=valid_rect(selection_)?RECT{}:window_snap_at(snap_windows_,anchor_);
        snap_hover_={};
        resize_=hit_handle(p);
        if(resize_) drag_=3;
        else if(valid_rect(selection_) && PtInRect(&selection_,p) && tool_!=MarkTool::Select) {
            if(tool_==MarkTool::Text) {
                text_origin_=p;
                const int width=std::max(40l,std::min(420l,selection_.right-p.x));
                edit_=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
                    p.x,p.y,width,31,window_,nullptr,GetModuleHandleW(nullptr),nullptr);
                SendMessageW(edit_,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);SetFocus(edit_);
                return;
            }
            drag_=4;current_={tool_,color_,{p},{}};
        } else if(valid_rect(selection_) && PtInRect(&selection_,p)) drag_=2;
        else {
            drag_=valid_rect(snap_pending_)?5:1;snap_hover_=snap_pending_;
            selection_={anchor_.x,anchor_.y,anchor_.x,anchor_.y};marks_.clear();
            tool_=MarkTool::Select;quick_copy_=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        }
        SetCapture(window_);invalidate();
    }
    void mouse_move(POINT p) {
        p=clamp_point(p);
        const RECT before=selection_;
        if(drag_==5) {
            // Wait for release so window clicks still allow free dragging.
            if(std::abs(p.x-anchor_.x)<GetSystemMetrics(SM_CXDRAG)
               && std::abs(p.y-anchor_.y)<GetSystemMetrics(SM_CYDRAG)) return;
            drag_=1;snap_hover_=snap_pending_={};
        }
        if(drag_==1) selection_=normalize_rect(anchor_,p);
        else if(drag_==2) {
            const LONG dx=std::clamp(p.x-anchor_.x,-drag_start_.left,LONG(image_.width)-drag_start_.right);
            const LONG dy=std::clamp(p.y-anchor_.y,-drag_start_.top,LONG(image_.height)-drag_start_.bottom);
            selection_=drag_start_;OffsetRect(&selection_,dx,dy);
        } else if(drag_==3) {
            RECT next=drag_start_;
            const LONG dx=p.x-anchor_.x,dy=p.y-anchor_.y;
            if(resize_&1) next.left=std::clamp(drag_start_.left+dx,0l,next.right-1);
            if(resize_&2) next.right=std::clamp(drag_start_.right+dx,next.left+1,LONG(image_.width));
            if(resize_&4) next.top=std::clamp(drag_start_.top+dy,0l,next.bottom-1);
            if(resize_&8) next.bottom=std::clamp(drag_start_.bottom+dy,next.top+1,LONG(image_.height));
            selection_=next;
        } else if(drag_==4) {
            p.x=std::clamp(p.x,selection_.left,selection_.right-1);
            p.y=std::clamp(p.y,selection_.top,selection_.bottom-1);
            if(tool_==MarkTool::Pen || tool_==MarkTool::Marker) current_.points.push_back(p);
            else if(current_.points.size()==1) current_.points.push_back(p);
            else current_.points.back()=p;
        } else {
            const int previous_hover=hover_;
            const RECT previous_snap=snap_hover_;
            snap_hover_=valid_rect(selection_)?RECT{}:window_snap_at(snap_windows_,p);
            hover_=hit_button(p);
            static const wchar_t* names[]={L"Pencil",L"Line",L"Arrow",L"Rectangle",L"Marker",L"Text",
                L"Color",L"Undo (Ctrl+Z)",L"Copy (Ctrl+C)",L"Save (Ctrl+S)",L"Cancel (Esc)"};
            hint_=hover_>=0?names[hover_]:L"";
            const int handle=hit_handle(p);
            LPCWSTR cursor=IDC_CROSS;
            if(hover_>=0) cursor=IDC_HAND;
            else if(handle==5 || handle==10) cursor=IDC_SIZENWSE;
            else if(handle==6 || handle==9) cursor=IDC_SIZENESW;
            else if(handle&3) cursor=IDC_SIZEWE;
            else if(handle&12) cursor=IDC_SIZENS;
            else if(tool_==MarkTool::Select && PtInRect(&selection_,p)) cursor=IDC_SIZEALL;
            SetCursor(LoadCursorW(nullptr,cursor));
            if(hover_==previous_hover && EqualRect(&previous_snap,&snap_hover_)) return;
            if((hover_>=0 || valid_rect(snap_hover_)) && window_) {
                TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window_,0};TrackMouseEvent(&tracking);
            }
        }
        if(drag_ && drag_!=4 && EqualRect(&before,&selection_)) return;
        invalidate();
        if(drag_) UpdateWindow(window_);
    }
    void mouse_up(POINT p) {
        if(!drag_) return;
        mouse_move(p);
        const int previous=drag_;drag_=0;ReleaseCapture();
        if(previous==5) selection_=snap_pending_;
        snap_hover_=snap_pending_={};
        if(previous==2) {
            const LONG dx=selection_.left-drag_start_.left,dy=selection_.top-drag_start_.top;
            for(auto& mark:marks_) for(auto& point:mark.points) {point.x+=dx;point.y+=dy;}
        }
        if(previous==4) {marks_.push_back(std::move(current_));current_={};}
        if((previous==1 || previous==5) && quick_copy_ && valid_rect(selection_)) finish(SelectionAction::Copy);
        invalidate();
    }
    void key(UINT key) {
        const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        if(key==VK_ESCAPE || (ctrl && key=='X')) {finish(SelectionAction::Cancel);return;}
        if(ctrl && key=='C') {finish(SelectionAction::Copy);return;}
        if(ctrl && key=='S') {finish(SelectionAction::Save);return;}
        if(ctrl && key=='Z') {commit_text(true);if(!marks_.empty()) marks_.pop_back();invalidate();return;}
        if(ctrl && key=='A') {commit_text();selection_={0,0,LONG(image_.width),LONG(image_.height)};invalidate();return;}
        if(key=='V') {tool_=MarkTool::Select;invalidate();return;}
        if(!valid_rect(selection_) || (key<VK_LEFT || key>VK_DOWN)) return;
        commit_text();
        const LONG dx=key==VK_LEFT?-1:key==VK_RIGHT?1:0,dy=key==VK_UP?-1:key==VK_DOWN?1:0;
        if(shift) {
            selection_.right=std::clamp(selection_.right+dx,selection_.left+1,LONG(image_.width));
            selection_.bottom=std::clamp(selection_.bottom+dy,selection_.top+1,LONG(image_.height));
        } else if(ctrl) {
            selection_.left=std::clamp(selection_.left+dx,0l,selection_.right-1);
            selection_.top=std::clamp(selection_.top+dy,0l,selection_.bottom-1);
        } else {
            const LONG x=std::clamp(dx,-selection_.left,LONG(image_.width)-selection_.right);
            const LONG y=std::clamp(dy,-selection_.top,LONG(image_.height)-selection_.bottom);
            OffsetRect(&selection_,x,y);
            for(auto& mark:marks_) for(auto& point:mark.points) {point.x+=x;point.y+=y;}
        }
        invalidate();
    }
    // GDI strokes are aliased, so icons are drawn four times larger and averaged into coverage.
    static const std::vector<std::uint8_t>& icon_mask(int index) {
        constexpr int size=24,scale=4,canvas=size*scale;
        static std::vector<std::uint8_t> masks[11];
        auto& mask=masks[index];
        if(!mask.empty()) return mask;
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=canvas;info.bmiHeader.biHeight=-canvas;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* bits=nullptr;
        HDC dc=CreateCompatibleDC(nullptr);
        HBITMAP bitmap=dc?CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0):nullptr;
        if(!bitmap) {if(dc) DeleteDC(dc);throw std::runtime_error("Cannot draw selection toolbar.");}
        HGDIOBJ old_bitmap=SelectObject(dc,bitmap);
        const auto at=[&](double value){return int(std::lround((value+size/2.0)*scale));};
        const LOGBRUSH white_ink{BS_SOLID,RGB(255,255,255),0};
        const double width=index==1 || index==2 || index==10?2.2:index==5?2.4:1.8;
        HPEN pen=ExtCreatePen(PS_GEOMETRIC|PS_SOLID|PS_ENDCAP_ROUND|PS_JOIN_ROUND,DWORD(width*scale),&white_ink,0,nullptr);
        HBRUSH white=CreateSolidBrush(RGB(255,255,255)),black=CreateSolidBrush(RGB(0,0,0));
        HBRUSH hollow=static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        HGDIOBJ old_pen=SelectObject(dc,pen),old_brush=SelectObject(dc,hollow);
        const auto line=[&](double a,double b,double c,double d){MoveToEx(dc,at(a),at(b),nullptr);LineTo(dc,at(c),at(d));};
        const auto shape=[&](std::initializer_list<std::pair<double,double>> points,HBRUSH fill) {
            std::vector<POINT> polygon;for(const auto& point:points) polygon.push_back({at(point.first),at(point.second)});
            SelectObject(dc,fill);Polygon(dc,polygon.data(),int(polygon.size()));SelectObject(dc,hollow);
        };
        const auto box=[&](double l,double t,double r,double b,HBRUSH fill) {
            SelectObject(dc,fill);RoundRect(dc,at(l),at(t),at(r),at(b),2*scale,2*scale);SelectObject(dc,hollow);
        };
        if(index==0) {
            shape({{-7.5,7.5},{-2.9,6.4},{7.4,-3.8},{3.8,-7.4},{-6.4,2.9}},hollow);
            line(-2.9,6.4,-6.4,2.9);line(4.9,-1.3,1.3,-4.9);
        } else if(index==1) line(-7,7,7,-7);
        else if(index==2) {line(-7,7,3,-3);shape({{8,-8},{5.5,-0.6},{0.6,-5.5}},white);}
        else if(index==3) box(-8,-6,8,6,hollow);
        else if(index==4) {
            shape({{-0.2,4.8},{8.3,-3.7},{3.7,-8.3},{-4.8,0.2}},hollow);
            shape({{-0.2,4.8},{-3.5,5.7},{-5.7,3.5},{-4.8,0.2}},white);line(-8.5,9.5,-1,9.5);
        } else if(index==5) {line(-6,-7,6,-7);line(0,-7,0,8);}
        else if(index==7) {
            MoveToEx(dc,at(-4),at(-4),nullptr);
            const POINT curve[]={{at(3),at(-4)},{at(7),at(-1)},{at(7),at(3)},{at(7),at(7)},{at(3),at(8.5)},{at(-1),at(8.5)}};
            PolyBezierTo(dc,curve,6);shape({{-9,-4},{-4,-8.5},{-4,.5}},white);
        } else if(index==8) {box(-8,-8,3,4,hollow);box(-3,-3,8,9,black);}
        else if(index==9) {
            shape({{-8,-8},{5,-8},{8,-5},{8,8},{-8,8}},hollow);
            box(-4.5,-8,3.5,-3,white);box(-5,1,5,8,hollow);
        } else if(index==10) {line(-6,-6,6,6);line(6,-6,-6,6);}
        GdiFlush();
        mask.resize(size*size);
        const auto* pixels=static_cast<const std::uint8_t*>(bits);
        for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
            unsigned total=0;
            for(int v=0;v<scale;++v) for(int u=0;u<scale;++u) total+=pixels[((y*scale+v)*canvas+x*scale+u)*4+1];
            mask[std::size_t(y)*size+x]=std::uint8_t(total/(scale*scale));
        }
        SelectObject(dc,old_brush);SelectObject(dc,old_pen);SelectObject(dc,old_bitmap);
        DeleteObject(pen);DeleteObject(white);DeleteObject(black);DeleteObject(bitmap);DeleteDC(dc);
        return mask;
    }
    static void draw_icon(HDC dc,int index,POINT center,COLORREF background,COLORREF ink) {
        const auto& mask=icon_mask(index);
        std::vector<std::uint8_t> pixels(mask.size()*4);
        const auto mix=[](int from,int to,int amount){return std::uint8_t(from+(to-from)*amount/255);};
        for(std::size_t i=0;i<mask.size();++i) {
            pixels[i*4]=mix(GetBValue(background),GetBValue(ink),mask[i]);
            pixels[i*4+1]=mix(GetGValue(background),GetGValue(ink),mask[i]);
            pixels[i*4+2]=mix(GetRValue(background),GetRValue(ink),mask[i]);
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=24;info.bmiHeader.biHeight=-24;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        SetDIBitsToDevice(dc,center.x-12,center.y-12,24,24,0,0,0,24,pixels.data(),&info,DIB_RGB_COLORS);
    }
    void toolbar(HDC dc,RECT bounds,bool vertical) {
        const COLORREF face=RGB(244,244,244),hover=RGB(222,226,232),pressed=RGB(204,212,224),ink=RGB(51,51,51);
        HBRUSH face_brush=CreateSolidBrush(face),edge=CreateSolidBrush(RGB(150,150,150));
        FillRect(dc,&bounds,face_brush);FrameRect(dc,&bounds,edge);
        for(int i=0;i<(vertical?8:3);++i) {
            const int index=vertical?i:i+8;
            RECT r=button_bounds(index);
            const bool active=vertical && i<6 && int(tool_)==i+1;
            const COLORREF background=active?pressed:index==hover_?hover:face;
            if(background!=face) {
                HBRUSH fill=CreateSolidBrush(background);
                HPEN outline=CreatePen(PS_SOLID,1,active?RGB(160,172,190):background);
                HGDIOBJ old_brush=SelectObject(dc,fill),old_pen=SelectObject(dc,outline);
                RoundRect(dc,r.left+1,r.top+1,r.right-1,r.bottom-1,6,6);
                SelectObject(dc,old_pen);SelectObject(dc,old_brush);DeleteObject(outline);DeleteObject(fill);
            }
            const POINT center{(r.left+r.right)/2,(r.top+r.bottom)/2};
            if(index==6) {
                RECT swatch{center.x-8,center.y-8,center.x+8,center.y+8};
                HBRUSH color=CreateSolidBrush(color_),frame=CreateSolidBrush(ink);
                FillRect(dc,&swatch,color);FrameRect(dc,&swatch,frame);DeleteObject(frame);DeleteObject(color);
            } else draw_icon(dc,index,center,background,ink);
        }
        DeleteObject(edge);DeleteObject(face_brush);
    }
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<RegionSelector*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            self=static_cast<RegionSelector*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->window_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(!self) return DefWindowProcW(window,message,wp,lp);
        try {
            switch(message) {
            case WM_PAINT: {PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);self->paint(dc);EndPaint(window,&paint);diagnostic_paint();return 0;}
            case WM_ERASEBKGND:return 1;
            case WM_PRINTCLIENT:self->paint(reinterpret_cast<HDC>(wp));return 0;
            case WM_LBUTTONDOWN:self->mouse_down({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;
            case WM_MOUSEMOVE:self->mouse_move({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;
            case WM_MOUSELEAVE:self->hint_.clear();self->hover_=-1;self->snap_hover_={};self->invalidate();return 0;
            case WM_LBUTTONUP:self->mouse_up({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;
            case WM_RBUTTONUP:self->commit_text(true);self->tool_=MarkTool::Select;self->invalidate();return 0;
            case WM_HOTKEY:if(wp==selection_escape_hotkey_id) self->finish(SelectionAction::Cancel);return 0;
            case WM_KEYDOWN:self->key(UINT(wp));return 0;
            case WM_CLOSE:self->finish(SelectionAction::Cancel);return 0;
            case WM_DISPLAYCHANGE:self->finish(SelectionAction::Cancel);return 0;
            }
        } catch(const std::exception& error) {
            MessageBoxA(window,error.what(),"LumenShot",MB_OK|MB_ICONERROR);
        }
        return DefWindowProcW(window,message,wp,lp);
    }
public:
    explicit RegionSelector(Image image):image_(std::move(image)) {
        if(image_.bgra.size()!=checked_pixels(image_.width,image_.height)*4)
            throw std::runtime_error("Invalid selection preview pixels.");
        font_=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    }
    ~RegionSelector() {if(window_) DestroyWindow(window_);if(font_) DeleteObject(font_);}
    void cancel() {finish(SelectionAction::Cancel);}
    void paint(HDC target) {
        if(!buffer_.dc()) {
            original_.create(target,image_.width,image_.height);dimmed_.create(target,image_.width,image_.height);
            buffer_.create(target,image_.width,image_.height);
            std::memcpy(original_.pixels(),image_.bgra.data(),image_.bgra.size());
            std::memcpy(dimmed_.pixels(),image_.bgra.data(),image_.bgra.size());
            for(std::size_t i=0;i<image_.bgra.size();i+=4)
                for(int channel=0;channel<3;++channel) dimmed_.pixels()[i+channel]=std::uint8_t(image_.bgra[i+channel]*.36);
        }
        RECT clip{},dirty{},area{0,0,LONG(image_.width),LONG(image_.height)};
        if(GetClipBox(target,&clip)==ERROR || !IntersectRect(&dirty,&clip,&area)) return;
        const HDC dc=buffer_.dc();
        const int state=SaveDC(dc);IntersectClipRect(dc,dirty.left,dirty.top,dirty.right,dirty.bottom);
        const int width=dirty.right-dirty.left,height=dirty.bottom-dirty.top;
        // Frozen pixels stay in GDI surfaces; pointer updates repaint only their damaged area.
        BitBlt(dc,dirty.left,dirty.top,width,height,dimmed_.dc(),dirty.left,dirty.top,SRCCOPY);
        if(!valid_rect(selection_) && valid_rect(snap_hover_)) {
            RECT visible{};
            if(IntersectRect(&visible,&snap_hover_,&dirty))
                BitBlt(dc,visible.left,visible.top,visible.right-visible.left,visible.bottom-visible.top,
                       original_.dc(),visible.left,visible.top,SRCCOPY);
            HBRUSH outline=CreateSolidBrush(RGB(100,190,255));FrameRect(dc,&snap_hover_,outline);DeleteObject(outline);
        }
        if(valid_rect(selection_)) {
            const int saved=SaveDC(dc);
            IntersectClipRect(dc,selection_.left,selection_.top,selection_.right,selection_.bottom);
            RECT visible{};
            if(IntersectRect(&visible,&selection_,&dirty))
                BitBlt(dc,visible.left,visible.top,visible.right-visible.left,visible.bottom-visible.top,
                       original_.dc(),visible.left,visible.top,SRCCOPY);
            const POINT offset=drag_==2?POINT{drag_start_.left-selection_.left,drag_start_.top-selection_.top}:POINT{};
            for(const auto& mark:marks_) draw_annotation(dc,mark,offset);
            if(drag_==4) draw_annotation(dc,current_);
            RestoreDC(dc,saved);
            HPEN pen=CreatePen(PS_DOT,1,RGB(255,255,255));
            HGDIOBJ old_pen=SelectObject(dc,pen),old_brush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
            Rectangle(dc,selection_.left-1,selection_.top-1,selection_.right+1,selection_.bottom+1);
            SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(pen);
            for(int y=0;y<3;++y) for(int x=0;x<3;++x) if(x!=1 || y!=1) {
                const LONG px=selection_.left+(selection_.right-selection_.left)*x/2;
                const LONG py=selection_.top+(selection_.bottom-selection_.top)*y/2;
                RECT handle{px-3,py-3,px+4,py+4};FillRect(dc,&handle,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            }
            SelectObject(dc,font_);SetBkMode(dc,OPAQUE);SetBkColor(dc,RGB(38,41,47));SetTextColor(dc,RGB(255,255,255));
            const auto size=std::to_wstring(selection_.right-selection_.left)+L" x "+std::to_wstring(selection_.bottom-selection_.top);
            TextOutW(dc,selection_.left,std::max(0l,selection_.top-25),size.c_str(),int(size.size()));
            if(!drag_) {toolbar(dc,vertical_,true);toolbar(dc,horizontal_,false);}
        }
        if(!hint_.empty() && valid_rect(hint_bounds_)) {
            HBRUSH face=CreateSolidBrush(RGB(35,39,47)),edge=CreateSolidBrush(RGB(94,104,119));
            FillRect(dc,&hint_bounds_,face);FrameRect(dc,&hint_bounds_,edge);DeleteObject(face);DeleteObject(edge);
            RECT label=hint_bounds_;InflateRect(&label,-8,-5);
            SelectObject(dc,font_);SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,TRANSPARENT);
            DrawTextW(dc,hint_.c_str(),int(hint_.size()),&label,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
        RestoreDC(dc,state);
        BitBlt(target,dirty.left,dirty.top,width,height,dc,dirty.left,dirty.top,SRCCOPY);
        last_visual_=visual_bounds();
    }
    SelectionResult run(HWND,RECT screen_bounds) {
        DiagnosticTimer timing(DiagnosticStage::Selection);
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=class_name;wc.hCursor=LoadCursorW(nullptr,IDC_CROSS);
        if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Cannot create selection interface.");
        snap_windows_=window_snap_rectangles(screen_bounds,GetCurrentProcessId());
        // Ownership can raise or reactivate background settings.
        window_=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,class_name,L"LumenShot selection",
            WS_POPUP,screen_bounds.left,screen_bounds.top,int(image_.width),int(image_.height),
            nullptr,nullptr,wc.hInstance,this);
        if(!window_) throw std::runtime_error("Cannot open selection interface.");
        SetWindowDisplayAffinity(window_,0x11);
        const bool escape_registered=RegisterHotKey(window_,selection_escape_hotkey_id,MOD_NOREPEAT,VK_ESCAPE)!=FALSE;
        struct ReleaseEscape {
            HWND window;bool registered;
            ~ReleaseEscape(){if(registered) UnregisterHotKey(window,selection_escape_hotkey_id);}
        } release_escape{window_,escape_registered};
        ShowWindow(window_,SW_SHOW);SetForegroundWindow(window_);SetFocus(window_);
        POINT pointer{};if(GetCursorPos(&pointer) && ScreenToClient(window_,&pointer)) mouse_move(pointer);
        MSG message{};
        while(!finished_) {
            const BOOL received=GetMessageW(&message,nullptr,0,0);
            if(received<=0) {if(received==0) PostQuitMessage(int(message.wParam));break;}
            if(message.message==WM_KEYDOWN && message.wParam==VK_ESCAPE) {finish(SelectionAction::Cancel);continue;}
            if(message.hwnd==edit_ && message.message==WM_KEYDOWN && (GetKeyState(VK_CONTROL)&0x8000)
               && (message.wParam=='C' || message.wParam=='S' || message.wParam=='X')) {
                key(UINT(message.wParam));continue;
            }
            if(message.hwnd==edit_ && message.message==WM_KEYDOWN && message.wParam==VK_RETURN) {
                commit_text();invalidate();continue;
            }
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        commit_text(true);
        return {action_,selection_,marks_};
    }
    const Image& image() const { return image_; }
    void preview_selection(RECT rect,std::vector<Annotation> marks={}) {
        selection_=rect;marks_=std::move(marks);layout();last_visual_=visual_bounds();
    }
};

} // namespace hdrshot
