#include "platform/diagnostics.hpp"

namespace hdrshot {

thread_local std::shared_ptr<DiagnosticRecord> active_diagnostics;
thread_local DiagnosticStage active_diagnostic_stage=DiagnosticStage::Count;

} // namespace hdrshot
