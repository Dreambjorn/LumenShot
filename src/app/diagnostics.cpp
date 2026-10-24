#include "app/diagnostics.hpp"
#include "image/image_io.hpp"
#include <atomic>

namespace hdrshot {

bool save_capture_diagnostics(const std::shared_ptr<DiagnosticRecord>& record,bool success,bool canceled) noexcept {
    if(!record) return true;
    try {
        record->finish(success,canceled);
        DiagnosticContext pause(nullptr);
        static std::atomic<unsigned> sequence{0};
        const auto folder=executable_directory()/L"Diagnostics";
        std::filesystem::create_directories(folder);
        const auto name=L"Capture_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64())
                       +L"_"+std::to_wstring(++sequence)+L".json";
        OutputTransaction transaction(folder/name);
        std::ofstream output(transaction.path(),std::ios::binary|std::ios::trunc);
        output<<"{\"release\":\"" LUMENSHOT_VERSION "\",\"capture\":"<<diagnostic_json(record->snapshot())<<"}\n";
        output.close();if(!output) return false;
        transaction.commit();return true;
    } catch(...) {return false;}
}

} // namespace hdrshot
