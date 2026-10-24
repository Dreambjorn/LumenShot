#include "app/workflow.hpp"
#include "image/scaling.hpp"

namespace hdrshot {

CapturedInput capture_input(const Settings& settings,HMONITOR foreground) {
    const auto displays=monitors();
    if(settings.monitor==L"*") return capture_desktop(displays,settings.backend);
    auto found=displays.end();
    if(!settings.monitor.empty()) found=std::find_if(displays.begin(),displays.end(),
        [&](const Monitor& monitor){return monitor.device_name==settings.monitor;});
    else found=std::find_if(displays.begin(),displays.end(),
        [&](const Monitor& monitor){return monitor.handle==foreground;});
    if(found==displays.end()) throw std::runtime_error("Selected monitor is no longer connected. Restart settings.");
    CapturedInput input;input.bounds=found->bounds;
    input.frame=capture(*found,settings.backend,&input.sdr);
    return input;
}
// Sharpening waits for the crop; a full-screen pass delayed the selector by over 100 ms.
void prepare_preview(WorkResult& result,const Settings& settings,Image sdr) {
    DiagnosticTimer timing(DiagnosticStage::Preview);
    if(active_diagnostics) active_diagnostics->dimensions(result.frame.width,result.frame.height,result.frame.hdr,
                                                         unsigned(result.frame.regions.size()));
    result.preview=sdr.bgra.empty()?render_sdr(result.frame,settings.color):std::move(sdr);
}
// Sharpening reads each neighbour, so a one-pixel margin keeps a crop equal to a full-image pass.
RECT sharpen_margin(RECT rect,const Image& image,const Settings& settings) {
    if(settings.scale!=100 || !settings.sharpness) return rect;
    return {std::max(0l,rect.left-1),std::max(0l,rect.top-1),
            std::min(LONG(image.width),rect.right+1),std::min(LONG(image.height),rect.bottom+1)};
}
WorkResult process_output(const Settings& settings,const Frame& frame,const Image& converted,
                          const std::filesystem::path& imported,bool clipboard,const Image* hdr_base) {
    std::filesystem::create_directories(settings.folder);
    const auto name=screenshot_name();
    const auto output=settings.folder/(name+L".png");
    if(!clipboard) save_image(output,converted);
    std::wstring warning;
    std::filesystem::path original;
    bool jpeg=false;
    if(settings.keep_original && frame.hdr) {
        try {
            jpeg=settings.hdr_format==HdrFormat::UltraJpeg && frame.width>=8 && frame.height>=8;
            const auto originals=settings.folder/L"HDR images";
            std::filesystem::create_directories(originals);
            const auto destination=originals/(name+(jpeg?L".jpg":L".jxr"));
            if(jpeg) save_hdr_jpeg(destination,frame,hdr_base?*hdr_base:converted);
            else if(imported.empty() || settings.sharpness || settings.scale!=100) save_master(destination,frame);
            else copy_master(imported,destination,frame.width,frame.height);
            original=destination;
            if(!jpeg && settings.hdr_format==HdrFormat::UltraJpeg)
                warning=L"HDR JPEG requires at least 8 x 8 pixels. Saved JPEG XR instead.";
        } catch(const std::exception& error) {
            warning=L"HDR image could not be saved: "+widen(error.what());
        }
    }
    WorkResult result;
    result.success=true;
    if(clipboard) result.clipboard=converted;
    result.status=clipboard?L"Copied screenshot to clipboard":L"Saved screenshot";
    std::wostringstream details;
    details<<L"Size: "<<converted.width<<L" x "<<converted.height
           <<(clipboard?L"\nClipboard color: ":L"\nPNG color: ")<<converted.description;
    if(frame.hdr && settings.color.exposure!=0)
        details<<L"\nHDR exposure setting: "<<std::showpos<<std::fixed<<std::setprecision(2)<<settings.color.exposure<<L" stops";
    if(settings.sharpness) details<<L"\nSharpness: "<<settings.sharpness<<L"%";
    if(settings.scale!=100) details<<L"\nImage size: "<<settings.scale<<L"%";
    if(!clipboard) details<<L"\n\nPNG: "<<output.wstring();
    if(!original.empty()) details<<(jpeg?L"\n\nHDR JPEG: ":L"\n\nHDR JPEG XR: ")<<original.wstring();
    if(!warning.empty()) details<<L"\n\n"<<warning;
    result.details=details.str();
    return result;
}
WorkResult process_capture(const Settings& settings,HMONITOR foreground,const std::filesystem::path& imported) {
    Runtime runtime;
    WorkResult input;
    Image sdr;
    if(imported.empty()) {auto captured=capture_input(settings,foreground);input.frame=std::move(captured.frame);sdr=std::move(captured.sdr);}
    else input.frame=load_master(imported);
    Settings snapshot=settings;
    if(!imported.empty() && snapshot.color.mode==ContentMode::Auto) snapshot.color.mode=ContentMode::Hdr;
    prepare_preview(input,snapshot,std::move(sdr));
    Image converted=std::move(input.preview);
    if(settings.scale!=100) scale_image(converted,settings.scale,settings.sharpness);
    else sharpen_image(converted,settings.sharpness);
    if(settings.keep_original && input.frame.hdr) enhance_hdr(input.frame,settings.scale,settings.sharpness);
    return process_output(settings,input.frame,converted,imported);
}

} // namespace hdrshot
