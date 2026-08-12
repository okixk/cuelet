#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "WindowsDiagnostics.h"
#include "WindowsText.h"

#include <fstream>
#include <sstream>

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
        cuelet::windows::installDebugTerminateHandler();
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& args)
        {
            cuelet::windows::logDiagnostic(
                L"application.unhandled_exception", args.Message().c_str());
        });
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const&)
        {
            if (IsDebuggerPresent()) { __debugbreak(); }
        });
#endif
    }

    App::~App()
    {
        cuelet::windows::logDiagnostic(L"application.exit");
        if (instanceMutex) ::CloseHandle(instanceMutex);
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        using namespace Microsoft::Windows::AppLifecycle;
        try
        {
            auto activation = AppInstance::GetCurrent().GetActivatedEventArgs();
            std::error_code currentDirectoryError;
            const auto currentDirectory = std::filesystem::current_path(currentDirectoryError);
            const auto cliRequest = cuelet::windows::parseCommandLine(
                processCommandLineArguments(), currentDirectoryError ? std::filesystem::path{} : currentDirectory);
            if (cliRequest.command == cuelet::windows::CliCommand::Help ||
                cliRequest.command == cuelet::windows::CliCommand::Invalid) {
                cuelet::windows::CliExecutionResult result;
                if (cliRequest.command == cuelet::windows::CliCommand::Help) {
                    result.standardOutput = cuelet::windows::wideToUtf8(cuelet::windows::cliHelpText());
                } else {
                    result.exitCode = 2;
                    result.standardError = cuelet::windows::wideToUtf8(cliRequest.error) +
                        "\nRun Cuelet.exe --help for usage.\n";
                }
                writeConsole(result.standardOutput, false);
                writeConsole(result.standardError, true);
                ::ExitProcess(static_cast<UINT>(result.exitCode));
            }
            ::SetLastError(ERROR_SUCCESS);
            instanceMutex = ::CreateMutexW(nullptr, FALSE, L"Local\\Cuelet.WinUI.SingleInstance");
            const auto mutexResult = ::GetLastError();
            if (instanceMutex && mutexResult == ERROR_ALREADY_EXISTS)
            {
                if (cliRequest.isCliCommand()) {
                    const auto forwarded = redirectCliToRunningInstance();
                    const auto result = forwarded.value_or(cuelet::windows::CliExecutionResult{
                        5, {}, "The running Cuelet instance did not accept the command.\n", false});
                    writeConsole(result.standardOutput, false);
                    writeConsole(result.standardError, true);
                    ::ExitProcess(static_cast<UINT>(result.exitCode));
                } else {
                    redirectToRunningInstance(activationFolder(activation));
                    Exit();
                    return;
                }
            }
            window = make<MainWindow>();
            if (cliRequest.isCliCommand()) {
                const auto projected = window.as<winrt::Cuelet::WinUI::MainWindow>();
                auto result = get_self<implementation::MainWindow>(projected)->ExecuteCli(cliRequest);
                writeConsole(result.standardOutput, false);
                writeConsole(result.standardError, true);
                if (!result.keepRunning || result.exitCode != 0) {
                    ::ExitProcess(static_cast<UINT>(result.exitCode));
                }
                window.Activate();
                if (cliRequest.command == cuelet::windows::CliCommand::PlayId ||
                    cliRequest.command == cuelet::windows::CliCommand::PlayName ||
                    cliRequest.command == cuelet::windows::CliCommand::PlayFile ||
                    cliRequest.command == cuelet::windows::CliCommand::Hide) {
                    HWND hwnd{};
                    check_hresult(projected.as<::IWindowNative>()->get_WindowHandle(&hwnd));
                    ::ShowWindow(hwnd, SW_HIDE);
                }
                return;
            }
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

    std::vector<std::wstring> App::processCommandLineArguments() const
    {
        int count = 0;
        auto values = ::CommandLineToArgvW(::GetCommandLineW(), &count);
        std::vector<std::wstring> arguments;
        if (!values) return arguments;
        for (int index = 1; index < count; ++index) arguments.emplace_back(values[index]);
        ::LocalFree(values);
        return arguments;
    }

    void App::writeConsole(std::string const& text, bool errorStream)
    {
        if (text.empty()) return;
        const auto standardHandle = errorStream ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;
        auto handle = ::GetStdHandle(standardHandle);
        if (!handle || handle == INVALID_HANDLE_VALUE) {
            ::AttachConsole(ATTACH_PARENT_PROCESS);
            handle = ::GetStdHandle(standardHandle);
        }
        if (!handle || handle == INVALID_HANDLE_VALUE) return;
        DWORD mode = 0;
        if (::GetConsoleMode(handle, &mode)) {
            const auto wide = cuelet::windows::utf8ToWide(text);
            DWORD written = 0;
            ::WriteConsoleW(handle, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr);
        } else {
            DWORD written = 0;
            ::WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        }
    }

    std::optional<cuelet::windows::CliExecutionResult> App::redirectCliToRunningInstance() const
    {
        constexpr ULONG_PTR cliMessageId = 0x434C4931;
        HWND target{};
        for (int attempt = 0; attempt < 40 && !target; ++attempt) {
            target = findCueletWindow();
            if (!target) ::Sleep(50);
        }
        if (!target) return std::nullopt;

        wchar_t temporaryFolder[MAX_PATH]{};
        if (::GetTempPathW(static_cast<DWORD>(std::size(temporaryFolder)), temporaryFolder) == 0) {
            return std::nullopt;
        }
        const auto responsePath = std::filesystem::path(temporaryFolder) /
            (L"cuelet-cli-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
             std::to_wstring(::GetTickCount64()) + L".response");
        std::error_code error;
        std::filesystem::remove(responsePath, error);

        const auto payload = responsePath.wstring() + L"\n" +
            std::filesystem::current_path(error).wstring() + L"\n" + ::GetCommandLineW();
        COPYDATASTRUCT data{};
        data.dwData = cliMessageId;
        data.cbData = static_cast<DWORD>((payload.size() + 1) * sizeof(wchar_t));
        data.lpData = const_cast<wchar_t*>(payload.c_str());
        DWORD_PTR messageResult{};
        if (!::SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                                   SMTO_ABORTIFHUNG, 10000, &messageResult)) {
            return std::nullopt;
        }

        std::ifstream stream(responsePath, std::ios::binary);
        if (!stream) return std::nullopt;
        std::string line;
        cuelet::windows::CliExecutionResult result;
        std::getline(stream, line);
        result.exitCode = line.empty() ? 5 : std::stoi(line);
        std::getline(stream, line);
        const auto outputSize = line.empty() ? 0ull : std::stoull(line);
        std::getline(stream, line);
        const auto errorSize = line.empty() ? 0ull : std::stoull(line);
        result.standardOutput.resize(static_cast<std::size_t>(outputSize));
        result.standardError.resize(static_cast<std::size_t>(errorSize));
        stream.read(result.standardOutput.data(), static_cast<std::streamsize>(result.standardOutput.size()));
        stream.read(result.standardError.data(), static_cast<std::streamsize>(result.standardError.size()));
        stream.close();
        std::filesystem::remove(responsePath, error);
        return result;
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
