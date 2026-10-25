#pragma once
#include "capture/capture.hpp"
#include "image/hdr_output.hpp"

namespace hdrshot {

struct Settings {
    std::filesystem::path folder = executable_directory()/L"Screenshots";
    std::wstring monitor;
    CaptureBackend backend = CaptureBackend::Auto;
    ColorSettings color{ContentMode::Auto,0,0,true};
    bool keep_original = true, dark_mode = true, show_magnifier = false, window_snapping = false, annotate_hdr = false;
    HdrFormat hdr_format = HdrFormat::UltraJpeg;
    HdrJpegQuality hdr_jpeg_quality = HdrJpegQuality::Full;
    UINT modifiers = 0, key = VK_SNAPSHOT;
    unsigned sharpness = 0, scale = 100;
};

std::filesystem::path config_path();
Settings load_settings();
void save_settings(const Settings& settings);
std::wstring hotkey_label(const Settings& settings);

} // namespace hdrshot
