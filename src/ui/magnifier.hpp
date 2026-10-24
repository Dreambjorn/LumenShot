#pragma once
#include "image/color.hpp"
#include <array>

namespace hdrshot {

inline RECT magnifier_rect(RECT area,POINT pointer,SIZE size) {
    constexpr LONG margin=4,gap=18;
    if(size.cx<=0 || size.cy<=0 || area.right-area.left<size.cx+2*margin || area.bottom-area.top<size.cy+2*margin) return {};
    // Only monitor edges constrain the pointer-following panel.
    const LONG x=std::clamp(pointer.x+gap,area.left+margin,area.right-margin-size.cx);
    const LONG y=std::clamp(pointer.y+gap,area.top+margin,area.bottom-margin-size.cy);
    return {x,y,x+size.cx,y+size.cy};
}
inline void draw_magnifier(HDC dc,const Image& image,POINT pointer,RECT bounds,int cell,const std::wstring& label,HFONT font) {
    if(bounds.right<=bounds.left || bounds.bottom<=bounds.top) return;
    constexpr int count=11,radius=count/2;
    std::array<std::uint32_t,count*count> pixels{};
    // Read frozen source pixels so the grid never magnifies its own overlay.
    for(int y=0;y<count;++y) for(int x=0;x<count;++x) {
        const LONG sx=std::clamp(pointer.x+x-radius,0l,LONG(image.width)-1);
        const LONG sy=std::clamp(pointer.y+y-radius,0l,LONG(image.height)-1);
        std::memcpy(&pixels[y*count+x],image.bgra.data()+(std::size_t(sy)*image.width+sx)*4,4);
    }
    const int saved=SaveDC(dc),grid=count*cell;
    const LONG left=bounds.left+(bounds.right-bounds.left-grid)/2,top=bounds.top+6;
    HBRUSH face=CreateSolidBrush(RGB(35,39,47)),edge=CreateSolidBrush(RGB(94,104,119));
    FillRect(dc,&bounds,face);FrameRect(dc,&bounds,edge);DeleteObject(face);DeleteObject(edge);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=count;info.bmiHeader.biHeight=-count;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    SetStretchBltMode(dc,COLORONCOLOR);
    StretchDIBits(dc,left,top,grid,grid,0,0,count,count,pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
    HPEN pen=CreatePen(PS_SOLID,1,RGB(70,75,85));SelectObject(dc,pen);
    for(int i=0;i<=count;++i) {
        MoveToEx(dc,left+i*cell,top,nullptr);LineTo(dc,left+i*cell,top+grid);
        MoveToEx(dc,left,top+i*cell,nullptr);LineTo(dc,left+grid,top+i*cell);
    }
    RECT target{left+radius*cell,top+radius*cell,left+(radius+1)*cell,top+(radius+1)*cell};
    RECT outer=target;InflateRect(&outer,1,1);
    FrameRect(dc,&outer,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    FrameRect(dc,&target,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    const LONG cx=(target.left+target.right)/2,cy=(target.top+target.bottom)/2;
    RECT arms[]={{left,cy-1,left+5,cy+2},{left+grid-4,cy-1,left+grid+1,cy+2},
        {cx-1,top,cx+2,top+5},{cx-1,top+grid-4,cx+2,top+grid+1}};
    for(const auto& arm:arms) FillRect(dc,&arm,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    if(!label.empty()) {
        RECT text{bounds.left+6,top+grid+6,bounds.right-6,bounds.bottom-4};
        SelectObject(dc,font);SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,TRANSPARENT);
        DrawTextW(dc,label.c_str(),int(label.size()),&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
    RestoreDC(dc,saved);DeleteObject(pen);
}

} // namespace hdrshot
