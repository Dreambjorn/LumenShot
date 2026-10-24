#pragma once
#include "app/settings.hpp"

namespace hdrshot {

struct WorkResult {
    std::wstring status, details;
    bool success = false, canceled = false;
    Image preview, clipboard;
    Frame frame;
    RECT bounds{};
    bool needs_selection=false;
};

CapturedInput capture_input(const Settings& settings,HMONITOR foreground);
void prepare_preview(WorkResult& result,const Settings& settings,Image sdr={});
RECT sharpen_margin(RECT rect,const Image& image,const Settings& settings);
WorkResult process_output(const Settings& settings,const Frame& frame,const Image& converted,
                          const std::filesystem::path& imported={},bool clipboard=false,const Image* hdr_base=nullptr);
WorkResult process_capture(const Settings& settings,HMONITOR foreground,const std::filesystem::path& imported);

} // namespace hdrshot
