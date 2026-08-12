#include "pch.h"
#include "WindowsDiagnostics.h"

#include <atomic>
#include <cstdlib>
#include <exception>
#include <filesystem>

#if defined(_DEBUG)
namespace cuelet::windows {
namespace {

std::atomic<int> activeShutdownState{static_cast<int>(ShutdownState::Running)};

std::wstring stateName(ShutdownState state)
{
    switch (state) {
    case ShutdownState::Running: return L"Running";
    case ShutdownState::HidingWindow: return L"HidingWindow";
    case ShutdownState::ShuttingDown: return L"ShuttingDown";
    case ShutdownState::Stopped: return L"Stopped";
    }
    return L"Unknown";
}

std::filesystem::path logPath()
{
    wchar_t localAppData[32768]{};
    const auto length = ::GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (length == 0 || length >= std::size(localAppData)) return {};
    return std::filesystem::path(localAppData) / L"Cuelet" / L"Logs" / L"cuelet.log";
}

} // namespace

void logDiagnostic(std::wstring_view event, std::wstring_view details) noexcept
{
    try {
        SYSTEMTIME time{};
        ::GetSystemTime(&time);
        wchar_t prefix[192]{};
        swprintf_s(
            prefix, L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ thread=%lu event=%.*s",
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
            time.wSecond, time.wMilliseconds, ::GetCurrentThreadId(),
            static_cast<int>(event.size()), event.data());
        std::wstring line(prefix);
        if (!details.empty()) {
            line += L" details=\"";
            for (const auto character : details) {
                if (character == L'\r' || character == L'\n') line += L' ';
                else if (character == L'"') line += L'\'';
                else line += character;
            }
            line += L'"';
        }
        line += L"\r\n";
        ::OutputDebugStringW(line.c_str());

        const auto path = logPath();
        if (path.empty()) return;
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return;
        const auto handle = ::CreateFileW(
            path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return;
        const auto bytesRequired = ::WideCharToMultiByte(
            CP_UTF8, 0, line.data(), static_cast<int>(line.size()), nullptr, 0,
            nullptr, nullptr);
        std::string utf8(static_cast<std::size_t>(bytesRequired), '\0');
        ::WideCharToMultiByte(
            CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
            utf8.data(), bytesRequired, nullptr, nullptr);
        DWORD written = 0;
        ::WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        ::CloseHandle(handle);
    } catch (...) {
        // Diagnostics are best-effort and must never participate in shutdown failure.
    }
}

void setDiagnosticShutdownState(ShutdownState state) noexcept
{
    activeShutdownState.store(static_cast<int>(state), std::memory_order_relaxed);
    logDiagnostic(L"shutdown.state", stateName(state));
}

void installDebugTerminateHandler() noexcept
{
#if defined(_DEBUG)
    std::set_terminate([] {
        const auto state = static_cast<ShutdownState>(
            activeShutdownState.load(std::memory_order_relaxed));
        std::wstring details =
            L"shutdownState=" + stateName(state) +
            L" currentException=" + (std::current_exception() ? L"present" : L"none");
        logDiagnostic(L"terminate", details);
        std::abort();
    });
#endif
}

} // namespace cuelet::windows
#else
namespace cuelet::windows {

void logDiagnostic(std::wstring_view, std::wstring_view) noexcept
{
}

void setDiagnosticShutdownState(ShutdownState) noexcept
{
}

void installDebugTerminateHandler() noexcept
{
}

} // namespace cuelet::windows
#endif
