#pragma once

#include "WindowsUtf8.h"

#include <filesystem>
#include <winrt/base.h>

namespace cuelet::windows {

inline winrt::hstring utf8ToHstring(std::string_view value)
{
    return winrt::hstring(utf8ToWide(value));
}

inline std::string hstringToUtf8(winrt::hstring const& value)
{
    return wideToUtf8(std::wstring_view(value.c_str(), value.size()));
}

inline std::filesystem::path pathFromUtf8(std::string_view value)
{
    return std::filesystem::path(utf8ToWide(value));
}

inline std::string pathToUtf8(std::filesystem::path const& value)
{
    return wideToUtf8(value.native());
}

} // namespace cuelet::windows
