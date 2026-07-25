#pragma once

#include "WindowsLifecycleModel.h"

#include <string_view>

namespace cuelet::windows {

void logDiagnostic(std::wstring_view event, std::wstring_view details = {}) noexcept;
void setDiagnosticShutdownState(ShutdownState state) noexcept;
void installDebugTerminateHandler() noexcept;

} // namespace cuelet::windows
