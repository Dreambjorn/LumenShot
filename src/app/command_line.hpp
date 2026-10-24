#pragma once

namespace hdrshot {

struct LaunchOptions {
    bool open_settings=false,command_line_mode=false,diagnostics=false;
};

bool handle_command_line(int argc,wchar_t** argv,LaunchOptions& options);

} // namespace hdrshot
