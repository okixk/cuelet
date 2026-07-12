#include "pch.h"
#include "WindowsGlobalShortcutService.h"

namespace cuelet::windows {

NativeShortcutResult Win32GlobalShortcutBackend::registerShortcut(
    int id, unsigned int key, unsigned int nativeModifiers)
{
    if (!m_window) return {false, ERROR_INVALID_WINDOW_HANDLE};
    if (::RegisterHotKey(m_window, id, nativeModifiers, key)) return {true, ERROR_SUCCESS};
    return {false, ::GetLastError()};
}

void Win32GlobalShortcutBackend::unregisterShortcut(int id) noexcept
{
    if (m_window) ::UnregisterHotKey(m_window, id);
}

NativeShortcutResult Win32GlobalShortcutBackend::probe(unsigned int key, unsigned int nativeModifiers)
{
    if (!m_window) return {false, ERROR_INVALID_WINDOW_HANDLE};
    if (!::RegisterHotKey(m_window, probeId, nativeModifiers, key)) return {false, ::GetLastError()};
    ::UnregisterHotKey(m_window, probeId);
    return {true, ERROR_SUCCESS};
}

WindowsGlobalShortcutService::WindowsGlobalShortcutService()
    : m_backend(std::make_shared<Win32GlobalShortcutBackend>()), m_registry(m_backend)
{
}

void WindowsGlobalShortcutService::attach(HWND window, Callback callback)
{
    unregisterAll();
    m_window = window;
    m_backend->attach(window);
    m_callback = std::move(callback);
}

void WindowsGlobalShortcutService::update(std::vector<SoundClip> const& clips)
{
    m_registry.update(clips, false);
}

bool WindowsGlobalShortcutService::tryUpdate(std::vector<SoundClip> const& clips)
{
    return m_registry.update(clips, true);
}

ShortcutCheckResult WindowsGlobalShortcutService::probe(cuelet::Shortcut const& shortcut) const
{
    return m_registry.probe(shortcut);
}

void WindowsGlobalShortcutService::unregisterAll() noexcept
{
    m_registry.unregisterAll();
}

bool WindowsGlobalShortcutService::handleHotKey(WPARAM id) const
{
    const auto clipId = m_registry.clipForHotkeyId(static_cast<int>(id));
    if (!clipId || !m_callback) return false;
    m_callback(*clipId);
    return true;
}

GlobalShortcutStatus WindowsGlobalShortcutService::statusFor(std::string const& clipId) const
{
    return m_registry.statusFor(clipId);
}

bool WindowsGlobalShortcutService::isRegistered(std::string const& clipId) const
{
    return statusFor(clipId).registered;
}

std::size_t WindowsGlobalShortcutService::failureCount() const noexcept
{
    return m_registry.failureCount();
}

std::wstring WindowsGlobalShortcutService::lastFailureReason() const
{
    return m_registry.lastFailureReason();
}

} // namespace cuelet::windows
