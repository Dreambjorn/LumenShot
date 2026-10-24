#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include "platform/diagnostics.hpp"
#include <functional>
#include <stdexcept>
#include <utility>

namespace hdrshot {

class NativeWork {
    PTP_WORK work_ = nullptr;
    std::function<void()> function_;
    static void CALLBACK run(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) {
        static_cast<NativeWork*>(context)->function_();
    }
public:
    NativeWork() = default;
    NativeWork(const NativeWork&) = delete;
    NativeWork& operator=(const NativeWork&) = delete;
    ~NativeWork() { join(); }
    bool joinable() const { return work_ != nullptr; }
    void start(std::function<void()> function) {
        if (work_) throw std::runtime_error("Background work is already active.");
        if(active_diagnostics) {
            auto record=active_diagnostics;const auto stage=active_diagnostic_stage;
            function_=[record=std::move(record),stage,body=std::move(function)] {
                DiagnosticContext context(record,stage);body();
            };
        } else function_ = std::move(function);
        work_ = CreateThreadpoolWork(run, this, nullptr);
        if (!work_) throw std::runtime_error("Cannot create background work.");
        SubmitThreadpoolWork(work_);
    }
    void join() noexcept {
        if (!work_) return;
        WaitForThreadpoolWorkCallbacks(work_, FALSE);
        CloseThreadpoolWork(work_);
        work_ = nullptr;
        function_ = {};
    }
};

} // namespace hdrshot
