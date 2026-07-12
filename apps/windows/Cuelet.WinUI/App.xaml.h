#pragma once
#include "App.xaml.g.h"

#include <filesystem>
#include <optional>

namespace winrt::Cuelet::WinUI::implementation
{
    struct App : AppT<App>
    {
        App();
        ~App();
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
    private:
        std::optional<std::filesystem::path> activationFolder(
            Microsoft::Windows::AppLifecycle::AppActivationArguments const& args) const;
        void processActivation(Microsoft::Windows::AppLifecycle::AppActivationArguments const& args);
        void redirectToRunningInstance(std::optional<std::filesystem::path> const& folder) const;
        Microsoft::UI::Xaml::Window window{ nullptr };
        HANDLE instanceMutex = nullptr;
    };
}
