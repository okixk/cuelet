#pragma once

#include <windows.h>
#include <optional>

namespace cuelet::windows {

enum class TrayCommand { Open, StopAll, Exit };

class WindowsTrayIcon {
public:
    static constexpr UINT callbackMessage = WM_APP + 0x143;

    ~WindowsTrayIcon();
    void attach(HWND window) noexcept;
    bool add(bool shortcutsActive, bool showBackgroundHint = false) noexcept;
    void remove() noexcept;
    std::optional<TrayCommand> handleCallback(LPARAM event) const noexcept;
    bool visible() const noexcept { return m_visible; }

private:
    std::optional<TrayCommand> showMenu() const noexcept;

    HWND m_window = nullptr;
    bool m_visible = false;
    bool m_shortcutsActive = false;
};

} // namespace cuelet::windows
