#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "WindowsText.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Cuelet::WinUI::implementation
{
    namespace
    {
        constexpr ULONG_PTR activationMessageId = 0x4355454C; // CUEL

        HWND findCueletWindow()
        {
            struct Search { HWND result = nullptr; } search;
            ::EnumWindows([](HWND hwnd, LPARAM parameter) -> BOOL {
                auto data = reinterpret_cast<Search*>(parameter);
                wchar_t title[128]{};
                ::GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
                if (wcscmp(title, L"Cuelet") == 0) {
                    data->result = hwnd;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&search));
            return search.result;
        }
    }

    App::App()
    {
        InitializeComponent();
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const&)
        {
            if (IsDebuggerPresent()) { __debugbreak(); }
        });
#endif
    }

    App::~App()
    {
        if (instanceMutex) ::CloseHandle(instanceMutex);
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        using namespace Microsoft::Windows::AppLifecycle;
        try
        {
            auto activation = AppInstance::GetCurrent().GetActivatedEventArgs();
            ::SetLastError(ERROR_SUCCESS);
            instanceMutex = ::CreateMutexW(nullptr, FALSE, L"Local\\Cuelet.WinUI.SingleInstance");
            const auto mutexResult = ::GetLastError();
            if (instanceMutex && mutexResult == ERROR_ALREADY_EXISTS)
            {
                redirectToRunningInstance(activationFolder(activation));
                Exit();
                return;
            }
            window = make<MainWindow>();
            window.Activate();
            processActivation(activation);
        }
        catch (hresult_error const& error)
        {
            const auto message = L"Cuelet could not start.\n\n" + std::wstring(error.message());
            ::MessageBoxW(nullptr, message.c_str(), L"Cuelet", MB_OK | MB_ICONERROR);
        }
        catch (std::exception const& error)
        {
            const auto message = L"Cuelet could not start.\n\n" + cuelet::windows::utf8ToWide(error.what());
            ::MessageBoxW(nullptr, message.c_str(), L"Cuelet", MB_OK | MB_ICONERROR);
        }
    }

    std::optional<std::filesystem::path> App::activationFolder(
        Microsoft::Windows::AppLifecycle::AppActivationArguments const& args) const
    {
        hstring argumentText;
        std::filesystem::path currentDirectory;
        if (args.Kind() == Microsoft::Windows::AppLifecycle::ExtendedActivationKind::CommandLineLaunch)
        {
            if (auto commandLine = args.Data().try_as<Windows::ApplicationModel::Activation::ICommandLineActivatedEventArgs>())
            {
                argumentText = commandLine.Operation().Arguments();
                currentDirectory = std::filesystem::path(commandLine.Operation().CurrentDirectoryPath().c_str());
            }
        }
        else if (auto launch = args.Data().try_as<Windows::ApplicationModel::Activation::ILaunchActivatedEventArgs>())
        {
            argumentText = launch.Arguments();
        }
        if (argumentText.empty()) return std::nullopt;

        int count = 0;
        auto argv = ::CommandLineToArgvW(argumentText.c_str(), &count);
        if (!argv) return std::nullopt;
        std::optional<std::filesystem::path> folder;
        for (int index = 0; index < count; ++index)
        {
            std::filesystem::path candidate(argv[index]);
            if (candidate.empty() || candidate.wstring().starts_with(L"--")) continue;
            if (candidate.is_relative() && !currentDirectory.empty()) candidate = currentDirectory / candidate;
            std::error_code error;
            if (std::filesystem::is_directory(candidate, error))
            {
                folder = std::filesystem::absolute(candidate, error);
                break;
            }
        }
        ::LocalFree(argv);
        return folder;
    }

    void App::processActivation(Microsoft::Windows::AppLifecycle::AppActivationArguments const& args)
    {
        auto folder = activationFolder(args);
        if (!window || !folder) return;
        if (auto projected = window.try_as<winrt::Cuelet::WinUI::MainWindow>())
        {
            get_self<implementation::MainWindow>(projected)->OpenLibraryFromActivation(folder->wstring());
        }
    }

    void App::redirectToRunningInstance(std::optional<std::filesystem::path> const& folder) const
    {
        HWND target{};
        for (int attempt = 0; attempt < 20 && !target; ++attempt)
        {
            target = findCueletWindow();
            if (!target) ::Sleep(50);
        }
        if (!target) return;
        if (folder)
        {
            const auto text = folder->wstring();
            COPYDATASTRUCT data{};
            data.dwData = activationMessageId;
            data.cbData = static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t));
            data.lpData = const_cast<wchar_t*>(text.c_str());
            DWORD_PTR result{};
            ::SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                                  SMTO_ABORTIFHUNG, 2000, &result);
        }
        ::ShowWindow(target, SW_RESTORE);
        ::SetForegroundWindow(target);
    }
}
