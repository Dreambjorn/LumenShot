#pragma once
#include "platform/platform.hpp"
#include <filesystem>

namespace hdrshot {

bool save_capture_diagnostics(const std::shared_ptr<DiagnosticRecord>& record,bool success,bool canceled=false) noexcept;

} // namespace hdrshot
