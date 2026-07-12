#include "WindowsUtf8.h"

#include <windows.h>
#include <stdexcept>

namespace cuelet::windows {

std::wstring utf8ToWide(std::string_view value)
{
    if (value.empty()) return {};
    const auto length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                               static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                              result.data(), length) != length) {
        throw std::runtime_error("Could not convert UTF-8 text");
    }
    return result;
}

std::string wideToUtf8(std::wstring_view value)
{
    if (value.empty()) return {};
    const auto length = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                               static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) throw std::runtime_error("Invalid Unicode text");
    std::string result(static_cast<std::size_t>(length), '\0');
    if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                              result.data(), length, nullptr, nullptr) != length) {
        throw std::runtime_error("Could not convert Unicode text");
    }
    return result;
}

bool containsMojibakeMarker(std::wstring_view value)
{
    return value.find(L"å€") != std::wstring_view::npos ||
           value.find(L"â€") != std::wstring_view::npos ||
           value.find(L"Ã") != std::wstring_view::npos ||
           value.find(L'\uFFFD') != std::wstring_view::npos;
}

} // namespace cuelet::windows
