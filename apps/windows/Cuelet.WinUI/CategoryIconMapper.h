#pragma once

#include "cuelet/SoundTypes.h"

namespace cuelet::windows {

struct WindowsCategoryIconChoice {
    std::string id;
    std::wstring displayName;
    winrt::Microsoft::UI::Xaml::Controls::IconSource iconSource{nullptr};
};

winrt::Microsoft::UI::Xaml::Controls::IconSource iconForCategoryId(const std::string& iconId);
std::vector<WindowsCategoryIconChoice> availableWindowsCategoryIcons();
winrt::Microsoft::UI::Xaml::Media::SolidColorBrush categoryColorBrush(const std::string& colorHex);

} // namespace cuelet::windows
