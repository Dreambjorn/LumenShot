#pragma once
#include "platform.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

namespace hdrshot {

struct ThemeColors {COLORREF background,input,text,muted,border,accent,selection;};
class NativeTheme {
public:
    bool dark=true;
    ThemeColors colors{};
    HBRUSH background=nullptr,input=nullptr;
    NativeTheme(){set_colors(true);}
    ~NativeTheme(){DeleteObject(background);DeleteObject(input);}
    static bool is(HWND window,const wchar_t* type) {
        wchar_t name[64]{};GetClassNameW(window,name,64);return _wcsicmp(name,type)==0;
    }
    void set_colors(bool value) {
        dark=value;
        colors=dark?ThemeColors{RGB(18,22,29),RGB(30,36,46),RGB(233,238,246),RGB(159,173,193),RGB(64,76,94),RGB(96,190,255),RGB(43,73,106)}
                   :ThemeColors{RGB(247,249,252),RGB(255,255,255),RGB(23,35,52),RGB(91,105,125),RGB(160,172,188),RGB(28,111,177),RGB(212,231,249)};
        if(background)DeleteObject(background);
        if(input)DeleteObject(input);
        background=CreateSolidBrush(colors.background);input=CreateSolidBrush(colors.input);
    }
    static void fill(HDC dc,RECT rect,COLORREF color) {
        HBRUSH brush=CreateSolidBrush(color);FillRect(dc,&rect,brush);DeleteObject(brush);
    }
    void border(HDC dc,RECT rect,COLORREF color) const {
        HBRUSH brush=CreateSolidBrush(color);FrameRect(dc,&rect,brush);DeleteObject(brush);
    }
    struct CheckboxLayout {RECT box,label;};
    static CheckboxLayout checkbox_layout(HDC dc,const RECT& rect,const wchar_t* title) {
        SIZE size{};GetTextExtentPoint32W(dc,title,lstrlenW(title),&size);
        return {{0,(rect.bottom-16)/2,16,(rect.bottom+16)/2},
                {24,(rect.bottom-size.cy)/2,24+size.cx,(rect.bottom-size.cy)/2+size.cy}};
    }
    static bool checkbox_hit(HWND window,LPARAM point) {
        RECT rect{};GetClientRect(window,&rect);wchar_t title[1024]{};GetWindowTextW(window,title,1024);
        HDC dc=GetDC(window);
        HGDIOBJ font=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0)));
        const auto layout=checkbox_layout(dc,rect,title);
        SelectObject(dc,font);ReleaseDC(window,dc);
        const POINT position{GET_X_LPARAM(point),GET_Y_LPARAM(point)};
        return PtInRect(&layout.box,position) || PtInRect(&layout.label,position);
    }
    void paint(HWND window,HDC dc) const {
        RECT rect{};GetClientRect(window,&rect);SetBkMode(dc,TRANSPARENT);
        SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0)));
        SetTextColor(dc,colors.text);FillRect(dc,&rect,background);
        if(is(window,WC_TABCONTROLW)) {
            for(int i=0;i<TabCtrl_GetItemCount(window);++i) {
                RECT tab{};TabCtrl_GetItemRect(window,i,&tab);
                const bool selected=TabCtrl_GetCurSel(window)==i;
                if(selected)fill(dc,tab,colors.input);
                wchar_t title[64]{};TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=title;item.cchTextMax=64;
                TabCtrl_GetItem(window,i,&item);
                SetTextColor(dc,selected?colors.text:colors.muted);
                DrawTextW(dc,title,-1,&tab,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                if(selected)fill(dc,{tab.left,tab.bottom-3,tab.right,tab.bottom},colors.accent);
                if(GetFocus()==window && selected){InflateRect(&tab,-5,-5);DrawFocusRect(dc,&tab);}
            }
            return;
        }
        if(is(window,TRACKBAR_CLASSW)) {
            RECT channel{},thumb{};
            SendMessageW(window,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));
            SendMessageW(window,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
            channel.top=(rect.bottom-4)/2;channel.bottom=channel.top+4;
            fill(dc,channel,colors.border);
            RECT active=channel;active.right=std::clamp((thumb.left+thumb.right)/2,channel.left,channel.right);
            fill(dc,active,colors.accent);fill(dc,thumb,colors.accent);
            if(GetFocus()==window){RECT focus=rect;InflateRect(&focus,-1,-1);DrawFocusRect(dc,&focus);}
            return;
        }
        wchar_t title[1024]{};GetWindowTextW(window,title,1024);
        if(is(window,L"COMBOBOX")) {
            FillRect(dc,&rect,input);border(dc,rect,GetFocus()==window?colors.accent:colors.border);
            RECT label=rect;label.left+=9;label.right-=30;
            DrawTextW(dc,title,-1,&label,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            const int x=rect.right-15,y=(rect.bottom-rect.top)/2;
            HPEN pen=CreatePen(PS_SOLID,2,colors.text);HGDIOBJ old=SelectObject(dc,pen);
            MoveToEx(dc,x-4,y-2,nullptr);LineTo(dc,x,y+2);LineTo(dc,x+4,y-2);
            SelectObject(dc,old);DeleteObject(pen);return;
        }
        const LONG_PTR style=GetWindowLongPtrW(window,GWL_STYLE)&BS_TYPEMASK;
        if(style==BS_AUTOCHECKBOX) {
            auto layout=checkbox_layout(dc,rect,title);const RECT box=layout.box;
            const bool checked=SendMessageW(window,BM_GETCHECK,0,0)==BST_CHECKED;
            fill(dc,box,checked?colors.accent:colors.input);border(dc,box,checked?colors.accent:colors.border);
            if(checked) {
                HPEN pen=CreatePen(PS_SOLID,2,dark?colors.background:RGB(255,255,255));HGDIOBJ old=SelectObject(dc,pen);
                MoveToEx(dc,box.left+3,box.top+8,nullptr);LineTo(dc,box.left+7,box.top+12);LineTo(dc,box.left+13,box.top+4);
                SelectObject(dc,old);DeleteObject(pen);
            }
            DrawTextW(dc,title,-1,&layout.label,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        } else {
            const bool pressed=SendMessageW(window,BM_GETSTATE,0,0)&BST_PUSHED;
            fill(dc,rect,pressed?colors.selection:colors.input);
            border(dc,rect,GetFocus()==window || style==BS_DEFPUSHBUTTON?colors.accent:colors.border);
            DrawTextW(dc,title,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        if(GetFocus()==window) {
            RECT focus=rect;
            if(style==BS_AUTOCHECKBOX) {
                focus=checkbox_layout(dc,rect,title).box;InflateRect(&focus,-2,-2);
                if(SendMessageW(window,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS) return;
            } else InflateRect(&focus,-4,-4);
            DrawFocusRect(dc,&focus);
        }
    }
    static LRESULT CALLBACK control_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
        auto& theme=*reinterpret_cast<NativeTheme*>(data);
        const bool drawn=is(window,L"BUTTON") || is(window,L"COMBOBOX") || is(window,WC_TABCONTROLW) || is(window,TRACKBAR_CLASSW);
        const bool checkbox=is(window,L"BUTTON") && (GetWindowLongPtrW(window,GWL_STYLE)&BS_TYPEMASK)==BS_AUTOCHECKBOX;
        if(checkbox && (message==WM_LBUTTONDOWN || message==WM_LBUTTONDBLCLK || message==WM_LBUTTONUP)) {
            if(!checkbox_hit(window,lp)) {
                if(message==WM_LBUTTONUP) {DefSubclassProc(window,WM_CANCELMODE,0,0);InvalidateRect(window,nullptr,TRUE);}
                return 0;
            }
            if(message!=WM_LBUTTONUP) SendMessageW(window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEFOCUS),0);
        }
        if(drawn && message==WM_PAINT) {PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);theme.paint(window,dc);EndPaint(window,&paint);return 0;}
        if(drawn && (message==WM_PRINT || message==WM_PRINTCLIENT)) {theme.paint(window,reinterpret_cast<HDC>(wp));return 0;}
        if(drawn && message==WM_ERASEBKGND) {RECT rect{};GetClientRect(window,&rect);FillRect(reinterpret_cast<HDC>(wp),&rect,theme.background);return 1;}
        if(message==WM_NCDESTROY)RemoveWindowSubclass(window,control_proc,id);
        if(message==WM_NCPAINT && is(window,L"EDIT")) {
            const auto result=DefSubclassProc(window,message,wp,lp);
            RECT rect{};GetWindowRect(window,&rect);OffsetRect(&rect,-rect.left,-rect.top);
            HDC dc=GetWindowDC(window);theme.border(dc,rect,GetFocus()==window?theme.colors.accent:theme.colors.border);
            ReleaseDC(window,dc);return result;
        }
        const auto result=DefSubclassProc(window,message,wp,lp);
        if(message==WM_SETFOCUS || message==WM_KILLFOCUS || message==BM_SETCHECK || message==BM_SETSTATE || message==WM_KEYUP || message==WM_LBUTTONUP || message==WM_UPDATEUISTATE || message==TCM_SETCURSEL)
            RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME);
        return result;
    }
    void apply(HWND window,bool value) {
        set_colors(value);const BOOL enabled=dark;
        DwmSetWindowAttribute(window,20,&enabled,sizeof(enabled));
        DwmSetWindowAttribute(window,35,&colors.background,sizeof(COLORREF));
        DwmSetWindowAttribute(window,36,&colors.text,sizeof(COLORREF));
        for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
            SetWindowTheme(child,dark?L"DarkMode_Explorer":L"Explorer",nullptr);
            SetWindowSubclass(child,control_proc,0x4853,reinterpret_cast<DWORD_PTR>(this));
            if(is(child,L"COMBOBOX")) {COMBOBOXINFO info{};info.cbSize=sizeof(info);if(GetComboBoxInfo(child,&info))SetWindowTheme(info.hwndList,dark?L"DarkMode_Explorer":L"Explorer",nullptr);}
        }
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME);
    }
    HBRUSH color_control(HDC dc,HWND child) const {
        const bool field=is(child,L"EDIT") || is(child,L"LISTBOX");
        SetTextColor(dc,colors.text);SetBkColor(dc,field?colors.input:colors.background);SetBkMode(dc,OPAQUE);
        return field?input:background;
    }
    void combo_item(const DRAWITEMSTRUCT& item) const {
        const bool selected=item.itemState&ODS_SELECTED;
        fill(item.hDC,item.rcItem,selected?colors.selection:colors.input);
        SetTextColor(item.hDC,colors.text);SetBkMode(item.hDC,TRANSPARENT);
        if(item.itemID!=UINT(-1)) {
            wchar_t title[1024]{};SendMessageW(item.hwndItem,CB_GETLBTEXT,item.itemID,reinterpret_cast<LPARAM>(title));
            RECT rect=item.rcItem;rect.left+=9;DrawTextW(item.hDC,title,-1,&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
        if(item.itemState&ODS_FOCUS)DrawFocusRect(item.hDC,&item.rcItem);
    }
};

} // namespace hdrshot
