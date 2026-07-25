#pragma once
#include "App.xaml.g.h"
#include "WindowsWorkflowModel.h"

#include <filesystem>
#include <optional>
#include <vector>

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
        std::vector<std::wstring> processCommandLineArguments() const;
        std::optional<cuelet::windows::CliExecutionResult> redirectCliToRunningInstance() const;
        static void writeConsole(std::string const& text, bool errorStream);
        Microsoft::UI::Xaml::Window window{ nullptr };
        HANDLE instanceMutex = nullptr;
    };
}
