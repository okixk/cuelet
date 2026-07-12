#pragma once

#include <string>
#include <string_view>

namespace cuelet::windows {

std::wstring utf8ToWide(std::string_view value);
std::string wideToUtf8(std::wstring_view value);
bool containsMojibakeMarker(std::wstring_view value);

} // namespace cuelet::windows
