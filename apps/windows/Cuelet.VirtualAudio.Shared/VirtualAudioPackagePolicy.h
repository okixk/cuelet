#pragma once

#include <string_view>

namespace cuelet::virtual_audio {

// This exact build was involved in a confirmed Cuelet kernel crash and is
// retained only as evidence. It must never be staged again, even when a Debug
// caller explicitly permits a locally test-signed package.
inline bool isKnownUnsafeDriverVersion(std::wstring_view version) noexcept
{
    return version == L"20.37.42.726";
}

} // namespace cuelet::virtual_audio
