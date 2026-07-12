#pragma once

#include "cuelet/SoundTypes.h"

#include <windows.h>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cuelet::windows {

constexpr unsigned int shortcutModifierCtrl = 1u;
constexpr unsigned int shortcutModifierShift = 2u;
constexpr unsigned int shortcutModifierAlt = 4u;
constexpr unsigned int shortcutModifierWin = 8u;
constexpr unsigned int shortcutModifierMask = shortcutModifierCtrl | shortcutModifierShift |
                                                shortcutModifierAlt | shortcutModifierWin;

enum class ShortcutAvailability {
    Available,
    CueletConflict,
    ReservedBySystem,
    RegisteredByAnotherApplication,
    Unsupported,
    RegistrationError,
};

struct ShortcutCheckResult {
    ShortcutAvailability availability = ShortcutAvailability::Available;
    std::string conflictingClipId;
    DWORD errorCode = ERROR_SUCCESS;

    bool available() const noexcept { return availability == ShortcutAvailability::Available; }
};

struct ShortcutConflict {
    std::string clipId;
    std::string soundName;
    bool global = false;
};

struct ShortcutStorageRecord {
    unsigned int virtualKey = 0;
    unsigned int modifiers = 0;
    bool global = false;
};

cuelet::Shortcut normalizeShortcut(cuelet::Shortcut shortcut);
ShortcutStorageRecord shortcutToStorage(cuelet::Shortcut const& shortcut) noexcept;
cuelet::Shortcut shortcutFromStorage(ShortcutStorageRecord const& stored);
std::wstring formatShortcut(cuelet::Shortcut const& shortcut);
bool shortcutEquals(cuelet::Shortcut const& left, cuelet::Shortcut const& right) noexcept;
bool shortcutScopesConflict(cuelet::Shortcut const& left, cuelet::Shortcut const& right) noexcept;
bool isModifierKey(unsigned int virtualKey) noexcept;
bool isShortcutSupported(cuelet::Shortcut const& shortcut) noexcept;
bool isWindowsReservedShortcut(cuelet::Shortcut const& shortcut) noexcept;
unsigned int nativeShortcutModifiers(cuelet::Shortcut const& shortcut) noexcept;
std::optional<ShortcutConflict> findShortcutConflict(
    std::vector<SoundClip> const& clips,
    cuelet::Shortcut const& shortcut,
    std::optional<std::string> const& ignoredClipId = std::nullopt);

struct PlannedHotkey {
    int id = 0;
    std::string clipId;
    unsigned int key = 0;
    unsigned int nativeModifiers = 0;
};

struct HotkeyRegistrationPlan {
    std::vector<PlannedHotkey> entries;
    std::map<std::string, std::wstring> errors;
};

HotkeyRegistrationPlan makeHotkeyRegistrationPlan(std::vector<SoundClip> const& clips, int firstId = 0x4350);

struct NativeShortcutResult {
    bool succeeded = false;
    DWORD errorCode = ERROR_SUCCESS;
};

class IGlobalShortcutBackend {
public:
    virtual ~IGlobalShortcutBackend() = default;
    virtual NativeShortcutResult registerShortcut(int id, unsigned int key, unsigned int nativeModifiers) = 0;
    virtual void unregisterShortcut(int id) noexcept = 0;
    virtual NativeShortcutResult probe(unsigned int key, unsigned int nativeModifiers) = 0;
};

struct GlobalShortcutStatus {
    bool registered = false;
    std::wstring reason;
    DWORD errorCode = ERROR_SUCCESS;
};

// Owns only Cuelet's registrations. A transactional update registers every new
// combination first and changes the active map only if they all succeed.
class GlobalShortcutRegistry {
public:
    explicit GlobalShortcutRegistry(std::shared_ptr<IGlobalShortcutBackend> backend);
    ~GlobalShortcutRegistry();

    bool update(std::vector<SoundClip> const& clips, bool transactional);
    ShortcutCheckResult probe(cuelet::Shortcut const& shortcut) const;
    void unregisterAll() noexcept;
    std::optional<std::string> clipForHotkeyId(int id) const;
    GlobalShortcutStatus statusFor(std::string const& clipId) const;
    std::size_t failureCount() const noexcept;
    std::wstring lastFailureReason() const;

private:
    struct ActiveRegistration {
        int id = 0;
        std::string clipId;
        unsigned int key = 0;
        unsigned int nativeModifiers = 0;
    };

    int allocateId() noexcept;
    std::shared_ptr<IGlobalShortcutBackend> m_backend;
    std::vector<ActiveRegistration> m_active;
    std::map<std::string, GlobalShortcutStatus> m_statusByClip;
    int m_nextId = 0x4350;
    std::wstring m_lastFailureReason;
};

} // namespace cuelet::windows
