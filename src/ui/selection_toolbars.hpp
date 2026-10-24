#pragma once
#include "image/color.hpp"
#include <utility>

namespace hdrshot {

inline constexpr std::array<int,11> selection_tool_order{0,1,2,3,4,5,11,12,13,6,7};
inline int selection_tool_slot(int index) {
    return int(std::find(selection_tool_order.begin(),selection_tool_order.end(),index)-selection_tool_order.begin());
}

inline std::pair<RECT,RECT> selection_toolbars(RECT area,RECT selection) {
    constexpr LONG width=36,height=11*32+8,actions_width=3*42+8,actions_height=38,gap=7;
    const LONG x=std::clamp(selection.right+gap,area.left,std::max(area.left,area.right-width));
    const LONG y=std::clamp(selection.bottom-height,area.top,std::max(area.top,area.bottom-height));
    const LONG bx=std::clamp(selection.right-actions_width,area.left,std::max(area.left,area.right-actions_width));
    const LONG below=selection.bottom+gap;
    LONG by=std::clamp(below+actions_height<=area.bottom?below:selection.bottom-gap-actions_height,
                       area.top,std::max(area.top,area.bottom-actions_height));
    RECT vertical{x,y,x+width,y+height};
    RECT horizontal{bx,by,bx+actions_width,by+actions_height},overlap{};
    // Keep both bars by the same corner when an edge forces them inside the selection.
    if(IntersectRect(&overlap,&vertical,&horizontal)) {
        if(vertical.left-gap-actions_width>=area.left) {
            OffsetRect(&horizontal,vertical.left-gap-horizontal.right,0);
            return {vertical,horizontal};
        }
        if(vertical.bottom+gap+actions_height<=area.bottom) by=vertical.bottom+gap;
        else {
            const LONG bottom=by-gap;
            OffsetRect(&vertical,0,bottom-vertical.bottom);
        }
        horizontal={bx,by,bx+actions_width,by+actions_height};
    }
    return {vertical,horizontal};
}

} // namespace hdrshot
