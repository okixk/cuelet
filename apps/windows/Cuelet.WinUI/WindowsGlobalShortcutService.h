#pragma once

#include "WindowsHotkeyModel.h"

#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cuelet::windows {

class Win32GlobalShortcutBackend final : public IGlobalShortcutBackend {
public:
    void attach(HWND window) noexcept { m_window = window; }
    NativeShortcutResult registerShortcut(int id, unsigned int key, unsigned int nativeModifiers) override;
    void unregisterShortcut(int id) noexcept override;
    NativeShortcutResult probe(unsigned int key, unsigned int nativeModifiers) override;

private:
    static constexpr int probeId = 0x7FFE;
    HWND m_window = nullptr;
};

class WindowsGlobalShortcutService {
public:
    using Callback = std::function<void(std::string const&)>;

    WindowsGlobalShortcutService();
    void attach(HWND window, Callback callback);
    void update(std::vector<SoundClip> const& clips);
    bool tryUpdate(std::vector<SoundClip> const& clips);
    ShortcutCheckResult probe(cuelet::Shortcut const& shortcut) const;
    void unregisterAll() noexcept;
    bool handleHotKey(WPARAM id) const;
    GlobalShortcutStatus statusFor(std::string const& clipId) const;
    bool isRegistered(std::string const& clipId) const;
    std::size_t failureCount() const noexcept;
    std::wstring lastFailureReason() const;

private:
    HWND m_window = nullptr;
    Callback m_callback;
    std::shared_ptr<Win32GlobalShortcutBackend> m_backend;
    GlobalShortcutRegistry m_registry;
};

} // namespace cuelet::windows
