#include "app/command_line.hpp"
#include "app/workflow.hpp"
#include "ui/notices.hpp"
#include <iostream>

namespace hdrshot {

bool handle_command_line(int argc,wchar_t** argv,LaunchOptions& options) {
    if (argc>1 && wcscmp(argv[1],L"--licenses")==0) {
        options.command_line_mode=true;std::cout<<embedded_notices();return true;
    }
    if (argc>1 && (wcscmp(argv[1],L"--help")==0 || wcscmp(argv[1],L"--version")==0)) {
        std::cout<<"LumenShot " LUMENSHOT_VERSION "\nUsage: LumenShot.exe [--settings | --tray | --capture | --import <file.jxr> | --licenses]\n"
                 <<"External Windows capture only. No game injection or game-process access.\n";return true;
    }
    options.open_settings=argc>1 && wcscmp(argv[1],L"--settings")==0;
    options.command_line_mode=argc>1 && wcscmp(argv[1],L"--tray")!=0 && !options.open_settings;
    if (argc>1 && (wcscmp(argv[1],L"--capture")==0 || wcscmp(argv[1],L"--import")==0)) {
        Settings settings=load_settings();
        std::filesystem::path imported;
        if(wcscmp(argv[1],L"--import")==0) {
            if(argc<3) throw std::runtime_error("Specify a floating-point .jxr file to import.");
            imported=argv[2];
        }
        const auto result=process_capture(settings,MonitorFromWindow(GetForegroundWindow(),MONITOR_DEFAULTTOPRIMARY),imported);
        std::wcout<<result.status<<L"\n\n"<<result.details<<L"\n";return true;
    }
    return false;
}

} // namespace hdrshot
