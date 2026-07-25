#include "pch.h"
#include "VirtualAudioInstallerClient.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace cuelet::windows {
namespace {

std::filesystem::path executableDirectory()
{
    std::wstring buffer(32768, L'\0');
    const auto length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path resultPath()
{
    wchar_t localAppData[32768]{};
    const auto length = ::GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (length == 0 || length >= std::size(localAppData)) return {};
    GUID id{};
    ::CoCreateGuid(&id);
    const auto name = L"cuelet-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
        std::to_wstring(::GetTickCount64()) + L"-" +
        std::to_wstring(static_cast<unsigned long>(id.Data1)) + L".json";
    return std::filesystem::path(localAppData) / L"Cuelet" / L"InstallerResults" / name;
}

winrt::hstring errorJson(DWORD code, std::wstring_view message)
{
    std::wstring escaped(message);
    std::replace(escaped.begin(), escaped.end(), L'"', L'\'');
    return winrt::hstring(
        L"{\"schema\":1,\"exitCode\":" + std::to_wstring(code) +
        L",\"operation\":\"\",\"message\":\"" + escaped +
        L"\",\"diagnostic\":\"\",\"packageInstalled\":false,"
        L"\"signatureTrusted\":false,\"renderEndpointPresent\":false,"
        L"\"captureEndpointPresent\":false,\"endpointPairValid\":false,"
        L"\"restartRequired\":false,\"updateAvailable\":false,"
        L"\"newerDriverInstalled\":false,\"publishedInf\":\"\","
        L"\"installedVersion\":\"\",\"bundledVersion\":\"\","
        L"\"renderEndpointId\":\"\",\"captureEndpointId\":\"\"}");
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    std::ostringstream text;
    text << stream.rdbuf();
    auto result = text.str();
    if (result.size() >= 3 &&
        static_cast<unsigned char>(result[0]) == 0xEF &&
        static_cast<unsigned char>(result[1]) == 0xBB &&
        static_cast<unsigned char>(result[2]) == 0xBF) {
        result.erase(0, 3);
    }
    return result;
}

winrt::hstring utf8ToHstring(std::string_view value)
{
    if (value.empty()) return {};
    const auto required = ::MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0);
    if (required <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    ::MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        wide.data(), required);
    return winrt::hstring(wide);
}

} // namespace

winrt::Windows::Foundation::IAsyncOperation<winrt::hstring>
VirtualAudioInstallerClient::runAsync(
    std::wstring_view operation,
    bool elevate,
    std::shared_ptr<std::atomic_bool> const& cancellation)
{
    const auto installer =
        executableDirectory() / L"Cuelet.VirtualAudio.Installer.exe";
    const auto result = resultPath();
    if (!std::filesystem::is_regular_file(installer) || result.empty()) {
        co_return errorJson(
            ERROR_FILE_NOT_FOUND,
            L"The Cuelet virtual-audio installer helper is missing from this build.");
    }
    std::error_code error;
    std::filesystem::create_directories(result.parent_path(), error);
    if (error) {
        co_return errorJson(
            ERROR_CANNOT_MAKE,
            L"Cuelet could not create its private installer-result directory.");
    }
    std::filesystem::remove(result, error);
    std::wstring parameters =
        std::wstring(operation) + L" --result-file \"" + result.wstring() + L"\"";
#if defined(_DEBUG)
    wchar_t developerFlag[4]{};
    if ((operation == L"install" || operation == L"repair") &&
        ::GetEnvironmentVariableW(
            L"CUELET_ALLOW_TEST_DRIVER", developerFlag,
            static_cast<DWORD>(std::size(developerFlag))) > 0 &&
        std::wstring_view(developerFlag) == L"1") {
        parameters += L" --allow-test-package";
    }
#endif

    co_await winrt::resume_background();
    auto const installerDirectory = installer.parent_path();

    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    execute.lpVerb = elevate ? L"runas" : L"open";
    execute.lpFile = installer.c_str();
    execute.lpParameters = parameters.c_str();
    execute.lpDirectory = installerDirectory.c_str();
    execute.nShow = SW_HIDE;
    if (!::ShellExecuteExW(&execute)) {
        const auto code = ::GetLastError();
        co_return errorJson(
            code, code == ERROR_CANCELLED
                ? L"Administrator permission was canceled. Local playback is unchanged."
                : L"Windows could not start the Cuelet installer helper.");
    }
    DWORD wait = WAIT_TIMEOUT;
    for (int attempt = 0; attempt < 5 * 60 * 10; ++attempt) {
        wait = ::WaitForSingleObject(execute.hProcess, 100);
        if (wait != WAIT_TIMEOUT) break;
        if (cancellation && cancellation->load(std::memory_order_acquire)) {
            // The elevated helper owns SetupAPI rollback and verification. Do
            // not terminate it mid-transaction; only release the closing UI.
            ::CloseHandle(execute.hProcess);
            co_return errorJson(
                ERROR_CANCELLED,
                L"Cuelet stopped waiting because the window is closing. "
                L"The approved driver operation will finish safely in the helper.");
        }
    }
    DWORD processExitCode = ERROR_GEN_FAILURE;
    ::GetExitCodeProcess(execute.hProcess, &processExitCode);
    ::CloseHandle(execute.hProcess);
    if (wait != WAIT_OBJECT_0) {
        co_return errorJson(
            wait == WAIT_TIMEOUT ? WAIT_TIMEOUT : ::GetLastError(),
            L"The Cuelet installer did not complete in time.");
    }
    const auto json = readFile(result);
    std::filesystem::remove(result, error);
    const auto converted = utf8ToHstring(json);
    if (converted.empty()) {
        co_return errorJson(
            processExitCode,
            L"The Cuelet installer did not return a valid structured result.");
    }
    co_return converted;
}

} // namespace cuelet::windows
