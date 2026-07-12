#include "WindowsAudioRoutingModel.h"

#include <algorithm>
#include <cwctype>
#include <string>

namespace cuelet::windows {

unsigned long volumeToSetting(double value) noexcept
{
    return static_cast<unsigned long>(std::clamp(value, 0.0, 1.0) * 1000.0);
}

double volumeFromSetting(unsigned long value) noexcept
{
    return std::clamp(static_cast<double>(value) / 1000.0, 0.0, 1.0);
}

bool looksLikeVirtualAudioEndpoint(std::wstring_view name)
{
    std::wstring normalized(name);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return normalized.find(L"virtual") != std::wstring::npos ||
           normalized.find(L"cable") != std::wstring::npos ||
           normalized.find(L"voicemeeter") != std::wstring::npos;
}

} // namespace cuelet::windows
