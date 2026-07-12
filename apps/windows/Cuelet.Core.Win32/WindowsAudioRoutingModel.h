#pragma once

#include <string_view>

namespace cuelet::windows {

unsigned long volumeToSetting(double value) noexcept;
double volumeFromSetting(unsigned long value) noexcept;
bool looksLikeVirtualAudioEndpoint(std::wstring_view name);

} // namespace cuelet::windows
