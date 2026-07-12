#include "pch.h"
#include "CategoryIconMapper.h"
#include "WindowsText.h"

#include <cwchar>

using namespace winrt;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

namespace cuelet::windows {
namespace {

std::wstring glyphForId(const std::string& value)
{
    const auto id = cuelet::canonicalCategoryIconId(value);
    if (id == "folder") return L"\xE8B7";
    if (id == "music-note") return L"\xE8D6";
    if (id == "audio-speakers") return L"\xE767";
    if (id == "waveform") return L"\xE9D2";
    if (id == "bell") return L"\xEA8F";
    if (id == "sparkles") return L"\xE735";
    if (id == "weather-showers") return L"\xE9CA";
    if (id == "applications-games") return L"\xE7FC";
    if (id == "microphone") return L"\xE720";
    if (id == "chat-message") return L"\xE8BD";
    if (id == "star") return L"\xE734";
    if (id == "heart") return L"\xEB51";
    if (id == "bolt") return L"\xE945";
    if (id == "flame") return L"\xE9D9";
    if (id == "face-smile") return L"\xE899";
    return L"\xE8EC"; // Tag
}

unsigned char hexByte(const std::string& value, std::size_t offset, unsigned char fallback)
{
    if (value.size() < offset + 2) return fallback;
    unsigned int parsed = 0;
    if (sscanf_s(value.substr(offset, 2).c_str(), "%02x", &parsed) != 1) return fallback;
    return static_cast<unsigned char>(parsed);
}

} // namespace

IconSource iconForCategoryId(const std::string& iconId)
{
    FontIconSource source;
    source.FontFamily(FontFamily(L"Segoe Fluent Icons"));
    source.Glyph(glyphForId(iconId));
    return source;
}

std::vector<WindowsCategoryIconChoice> availableWindowsCategoryIcons()
{
    std::vector<WindowsCategoryIconChoice> choices;
    choices.reserve(cuelet::availableCategoryIcons().size());
    for (const auto& choice : cuelet::availableCategoryIcons()) {
        choices.push_back({choice.id, utf8ToWide(choice.name), iconForCategoryId(choice.id)});
    }
    return choices;
}

SolidColorBrush categoryColorBrush(const std::string& colorHex)
{
    const auto fallback = std::string{"#8E8E93"};
    const auto& value = colorHex.size() == 7 && colorHex.front() == '#' ? colorHex : fallback;
    Windows::UI::Color color{255, hexByte(value, 1, 0x8E), hexByte(value, 3, 0x8E), hexByte(value, 5, 0x93)};
    return SolidColorBrush(color);
}

} // namespace cuelet::windows
