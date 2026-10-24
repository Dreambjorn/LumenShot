#include "app/settings.hpp"
#include "image/scaling.hpp"

namespace hdrshot {

std::filesystem::path config_path() { return executable_directory()/L"LumenShot.ini"; }
namespace {
std::wstring ini_string(const wchar_t* key,const std::wstring& fallback) {
    std::wstring buffer(32768,L'\0');
    const DWORD length=GetPrivateProfileStringW(L"Screenshot",key,fallback.c_str(),buffer.data(),
                                               DWORD(buffer.size()),config_path().c_str());
    buffer.resize(length);return buffer;
}
}
Settings load_settings() {
    Settings settings;
    settings.folder=ini_string(L"Folder",settings.folder.wstring());
    settings.monitor=ini_string(L"Monitor",L"");
    const auto value=[&](const wchar_t* key,int fallback){
        return GetPrivateProfileIntW(L"Screenshot",key,fallback,config_path().c_str());
    };
    settings.backend=CaptureBackend(std::min(value(L"Backend",0),2u));
    settings.color.mode=ContentMode(std::min(value(L"Content",0),2u));
    settings.color.peak_method=PeakMethod(std::min(value(L"PeakMethod",unsigned(settings.color.peak_method)),1u));
    try { settings.color.paper_white=std::stod(ini_string(L"PaperWhite",L"0")); }
    catch (...) { settings.color.paper_white=0; }
    try { settings.color.exposure=std::stod(ini_string(L"Exposure",L"0")); }
    catch (...) { settings.color.exposure=0; }
    if (!std::isfinite(settings.color.paper_white) || (settings.color.paper_white!=0 && settings.color.paper_white<40) || settings.color.paper_white>2000)
        settings.color.paper_white=0;
    try {settings.color.peak_nits=std::stod(ini_string(L"PeakNits",L"0"));}catch(...) {settings.color.peak_nits=0;}
    if(!std::isfinite(settings.color.peak_nits) || settings.color.peak_nits<0 || settings.color.peak_nits>10000
       || (settings.color.peak_nits>0 && settings.color.peak_nits<40)) settings.color.peak_nits=0;
    if (!std::isfinite(settings.color.exposure) || std::abs(settings.color.exposure)>4) settings.color.exposure=0;
    const auto sharpness=value(L"Sharpness",0);
    settings.sharpness=sharpness<=100?sharpness:0;
    const auto scale=value(L"Scale",100);settings.scale=valid_scale(scale)?scale:100;
    settings.keep_original=value(L"KeepOriginal",1)!=0;
    settings.dark_mode=value(L"DarkMode",1)!=0;
    settings.hdr_format=value(L"HdrFormat",0)==1?HdrFormat::JpegXr:HdrFormat::UltraJpeg;
    settings.modifiers=value(L"Modifiers",0)&(MOD_CONTROL|MOD_SHIFT|MOD_ALT);
    settings.key=value(L"Key",VK_SNAPSHOT);
    if (settings.key!=VK_SNAPSHOT && (settings.key<VK_F1 || settings.key>VK_F11)) settings.key=VK_SNAPSHOT;
    return settings;
}
void save_settings(const Settings& settings) {
    const auto write=[&](const wchar_t* key,const std::wstring& value){
        if (!WritePrivateProfileStringW(L"Screenshot",key,value.c_str(),config_path().c_str()))
            throw std::runtime_error("Cannot save settings beside the application. Move it to a writable folder.");
    };
    write(L"Folder",settings.folder.wstring());write(L"Monitor",settings.monitor);
    write(L"Backend",std::to_wstring(int(settings.backend)));
    write(L"Content",std::to_wstring(int(settings.color.mode)));
    write(L"PeakMethod",std::to_wstring(int(settings.color.peak_method)));
    write(L"PaperWhite",std::to_wstring(settings.color.paper_white));
    write(L"PeakNits",std::to_wstring(settings.color.peak_nits));
    write(L"Exposure",std::to_wstring(settings.color.exposure));
    write(L"Sharpness",std::to_wstring(settings.sharpness));
    write(L"Scale",std::to_wstring(settings.scale));
    write(L"KeepOriginal",settings.keep_original?L"1":L"0");
    write(L"HdrFormat",std::to_wstring(int(settings.hdr_format)));
    write(L"DarkMode",settings.dark_mode?L"1":L"0");
    write(L"Modifiers",std::to_wstring(settings.modifiers));write(L"Key",std::to_wstring(settings.key));
}
std::wstring hotkey_label(const Settings& settings) {
    std::wstring label;
    if (settings.modifiers&MOD_CONTROL) label+=L"Ctrl + ";
    if (settings.modifiers&MOD_SHIFT) label+=L"Shift + ";
    if (settings.modifiers&MOD_ALT) label+=L"Alt + ";
    return label+(settings.key==VK_SNAPSHOT?L"Print Screen":L"F"+std::to_wstring(settings.key-VK_F1+1));
}

} // namespace hdrshot
