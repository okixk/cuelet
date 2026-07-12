#include "pch.h"
#include "WindowsTrayIcon.h"

namespace cuelet::windows {
namespace {
constexpr UINT iconId = 1;
constexpr UINT openCommand = 0x5101;
constexpr UINT stopCommand = 0x5102;
constexpr UINT exitCommand = 0x5103;
}

WindowsTrayIcon::~WindowsTrayIcon()
{
    remove();
}

void WindowsTrayIcon::attach(HWND window) noexcept
{
    m_window = window;
}

bool WindowsTrayIcon::add(bool shortcutsActive, bool showBackgroundHint) noexcept
{
    if (!m_window) return false;
    m_shortcutsActive = shortcutsActive;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = m_window;
    data.uID = iconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = callbackMessage;
    data.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(data.szTip, L"Cuelet");
    if (showBackgroundHint) {
        data.uFlags |= NIF_INFO;
        wcscpy_s(data.szInfoTitle, L"Cuelet is still running");
        wcscpy_s(data.szInfo, L"Global shortcuts and playback remain active. Use the notification-area menu to exit.");
        data.dwInfoFlags = NIIF_INFO;
    }
    const auto operation = m_visible ? NIM_MODIFY : NIM_ADD;
    if (!::Shell_NotifyIconW(operation, &data)) return false;
    m_visible = true;
    data.uVersion = NOTIFYICON_VERSION_4;
    ::Shell_NotifyIconW(NIM_SETVERSION, &data);
    return true;
}

void WindowsTrayIcon::remove() noexcept
{
    if (!m_visible || !m_window) return;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = m_window;
    data.uID = iconId;
    ::Shell_NotifyIconW(NIM_DELETE, &data);
    m_visible = false;
}

std::optional<TrayCommand> WindowsTrayIcon::handleCallback(LPARAM event) const noexcept
{
    const auto notification = LOWORD(event);
    if (notification == WM_LBUTTONDBLCLK || notification == NIN_SELECT || notification == NIN_KEYSELECT) {
        return TrayCommand::Open;
    }
    if (notification == WM_CONTEXTMENU || notification == WM_RBUTTONUP) return showMenu();
    return std::nullopt;
}

std::optional<TrayCommand> WindowsTrayIcon::showMenu() const noexcept
{
    const auto menu = ::CreatePopupMenu();
    if (!menu) return std::nullopt;
    ::AppendMenuW(menu, MF_STRING, openCommand, L"Open Cuelet");
    ::AppendMenuW(menu, MF_STRING, stopCommand, L"Stop all sounds");
    ::AppendMenuW(menu, MF_STRING | MF_DISABLED, 0,
                  m_shortcutsActive ? L"Background shortcuts: On" : L"Background shortcuts: Unavailable");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, exitCommand, L"Exit Cuelet");
    POINT point{};
    ::GetCursorPos(&point);
    ::SetForegroundWindow(m_window);
    const auto command = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                           point.x, point.y, 0, m_window, nullptr);
    ::DestroyMenu(menu);
    ::PostMessageW(m_window, WM_NULL, 0, 0);
    if (command == openCommand) return TrayCommand::Open;
    if (command == stopCommand) return TrayCommand::StopAll;
    if (command == exitCommand) return TrayCommand::Exit;
    return std::nullopt;
}

} // namespace cuelet::windows
