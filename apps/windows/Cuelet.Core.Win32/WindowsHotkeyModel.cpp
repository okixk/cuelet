#include "WindowsHotkeyModel.h"

#include <algorithm>
#include <cwctype>
#include <set>
#include <sstream>

namespace cuelet::windows {
namespace {

constexpr int probeHotkeyId = 0x7FFE;

bool hasExactModifiers(cuelet::Shortcut const& shortcut, unsigned int modifiers) noexcept
{
    return (shortcut.modifiers & shortcutModifierMask) == modifiers;
}

bool isFunctionKey(unsigned int key) noexcept
{
    return key >= VK_F1 && key <= VK_F24;
}

unsigned int modifierCount(unsigned int modifiers) noexcept
{
    unsigned int count = 0;
    for (unsigned int bit = 1; bit <= shortcutModifierWin; bit <<= 1) {
        if ((modifiers & bit) != 0) ++count;
    }
    return count;
}

std::wstring keyLabel(unsigned int key)
{
    if (key >= 'A' && key <= 'Z') return std::wstring(1, static_cast<wchar_t>(key));
    if (key >= '0' && key <= '9') return std::wstring(1, static_cast<wchar_t>(key));
    if (key >= VK_F1 && key <= VK_F24) return L"F" + std::to_wstring(key - VK_F1 + 1);
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) return L"NumPad" + std::to_wstring(key - VK_NUMPAD0);

    switch (key) {
    case VK_LEFT: return L"Left";
    case VK_RIGHT: return L"Right";
    case VK_UP: return L"Up";
    case VK_DOWN: return L"Down";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"PageUp";
    case VK_NEXT: return L"PageDown";
    case VK_INSERT: return L"Insert";
    case VK_DELETE: return L"Delete";
    case VK_SPACE: return L"Space";
    case VK_RETURN: return L"Enter";
    case VK_ESCAPE: return L"Escape";
    case VK_BACK: return L"Backspace";
    case VK_TAB: return L"Tab";
    case VK_DECIMAL: return L"NumPadDecimal";
    case VK_ADD: return L"NumPad+";
    case VK_SUBTRACT: return L"NumPad-";
    case VK_MULTIPLY: return L"NumPad*";
    case VK_DIVIDE: return L"NumPad/";
    default: break;
    }

    const auto scanCode = ::MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    wchar_t name[64]{};
    if (scanCode != 0 && ::GetKeyNameTextW(static_cast<LONG>(scanCode << 16), name, 64) > 0) return name;
    std::wstringstream fallback;
    fallback << L"Key 0x" << std::hex << std::uppercase << key;
    return fallback.str();
}

std::wstring registrationFailure(DWORD error)
{
    if (error == ERROR_HOTKEY_ALREADY_REGISTERED) {
        return L"This shortcut is already being used by Windows or another application.";
    }
    return L"Windows rejected this shortcut (error " + std::to_wstring(error) + L").";
}

} // namespace

cuelet::Shortcut normalizeShortcut(cuelet::Shortcut shortcut)
{
    // Win32 reports letter virtual-key codes as 'A'-'Z' regardless of Shift or
    // Caps Lock. Codes in the lowercase ASCII range are real VK_NUMPAD*, VK_F*,
    // and other distinct keys, so folding those numeric values would corrupt them.
    shortcut.modifiers &= shortcutModifierMask;
    return shortcut;
}

ShortcutStorageRecord shortcutToStorage(cuelet::Shortcut const& value) noexcept
{
    const auto shortcut = normalizeShortcut(value);
    return {shortcut.keyval, shortcut.modifiers, true};
}

cuelet::Shortcut shortcutFromStorage(ShortcutStorageRecord const& stored)
{
    cuelet::Shortcut shortcut;
    shortcut.keyval = stored.virtualKey;
    shortcut.modifiers = stored.modifiers;
    shortcut.global = stored.global;
    shortcut = normalizeShortcut(shortcut);
    const auto formatted = formatShortcut(shortcut);
    shortcut.label.reserve(formatted.size());
    for (const auto character : formatted) {
        // Shortcut labels are made only from ASCII names. Keeping this helper
        // platform-core avoids making the storage record depend on WinRT UTF-8 utilities.
        shortcut.label.push_back(static_cast<char>(character));
    }
    return shortcut;
}

std::wstring formatShortcut(cuelet::Shortcut const& value)
{
    const auto shortcut = normalizeShortcut(value);
    std::wstring label;
    const auto append = [&](wchar_t const* part) {
        if (!label.empty()) label += L"+";
        label += part;
    };
    if ((shortcut.modifiers & shortcutModifierCtrl) != 0) append(L"Ctrl");
    if ((shortcut.modifiers & shortcutModifierAlt) != 0) append(L"Alt");
    if ((shortcut.modifiers & shortcutModifierShift) != 0) append(L"Shift");
    if ((shortcut.modifiers & shortcutModifierWin) != 0) append(L"Win");
    if (shortcut.keyval != 0 && !isModifierKey(shortcut.keyval)) {
        const auto key = keyLabel(shortcut.keyval);
        if (!label.empty()) label += L"+";
        label += key;
    }
    return label;
}

bool shortcutEquals(cuelet::Shortcut const& left, cuelet::Shortcut const& right) noexcept
{
    const auto normalizedLeft = normalizeShortcut(left);
    const auto normalizedRight = normalizeShortcut(right);
    return normalizedLeft.keyval == normalizedRight.keyval &&
           normalizedLeft.modifiers == normalizedRight.modifiers;
}

bool shortcutScopesConflict(cuelet::Shortcut const& left, cuelet::Shortcut const& right) noexcept
{
    return shortcutEquals(left, right);
}

bool isModifierKey(unsigned int key) noexcept
{
    switch (key) {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_LWIN: case VK_RWIN:
        return true;
    default:
        return false;
    }
}

bool isShortcutSupported(cuelet::Shortcut const& value) noexcept
{
    const auto shortcut = normalizeShortcut(value);
    if (shortcut.keyval == 0 || isModifierKey(shortcut.keyval)) return false;

    // Avoid taking normal typing/navigation keys globally. Function keys with a
    // modifier and F13-F24 are intentionally supported; other keys need at least
    // two modifiers. There is no hidden "unsafe shortcut" mode.
    if (shortcut.keyval >= VK_F13 && shortcut.keyval <= VK_F24) return true;
    if (isFunctionKey(shortcut.keyval)) return shortcut.modifiers != 0;
    return modifierCount(shortcut.modifiers) >= 2;
}

bool isWindowsReservedShortcut(cuelet::Shortcut const& value) noexcept
{
    const auto shortcut = normalizeShortcut(value);
    const auto key = shortcut.keyval;
    if (key == VK_F12) return true; // Reserved for the debugger by RegisterHotKey.
    if (key == VK_DELETE && hasExactModifiers(shortcut, shortcutModifierCtrl | shortcutModifierAlt)) return true;
    if (key == VK_TAB && hasExactModifiers(shortcut, shortcutModifierAlt)) return true;
    if (key == VK_ESCAPE && hasExactModifiers(shortcut, shortcutModifierAlt)) return true;
    if (key == VK_F4 && hasExactModifiers(shortcut, shortcutModifierAlt)) return true;
    if (key == VK_ESCAPE && hasExactModifiers(shortcut, shortcutModifierCtrl | shortcutModifierShift)) return true;

    if (hasExactModifiers(shortcut, shortcutModifierWin)) {
        switch (key) {
        case 'L': case 'D': case 'E': case 'R': case 'I': case VK_TAB:
        case 'X': case 'V': case VK_SPACE:
            return true;
        default:
            break;
        }
    }
    if (hasExactModifiers(shortcut, shortcutModifierWin | shortcutModifierCtrl)) {
        if (key == 'D' || key == VK_LEFT || key == VK_RIGHT) return true;
    }
    return false;
}

unsigned int nativeShortcutModifiers(cuelet::Shortcut const& value) noexcept
{
    const auto shortcut = normalizeShortcut(value);
    unsigned int modifiers = MOD_NOREPEAT;
    if ((shortcut.modifiers & shortcutModifierCtrl) != 0) modifiers |= MOD_CONTROL;
    if ((shortcut.modifiers & shortcutModifierShift) != 0) modifiers |= MOD_SHIFT;
    if ((shortcut.modifiers & shortcutModifierAlt) != 0) modifiers |= MOD_ALT;
    if ((shortcut.modifiers & shortcutModifierWin) != 0) modifiers |= MOD_WIN;
    return modifiers;
}

std::optional<ShortcutConflict> findShortcutConflict(
    std::vector<SoundClip> const& clips,
    cuelet::Shortcut const& shortcut,
    std::optional<std::string> const& ignoredClipId)
{
    for (auto const& clip : clips) {
        if (ignoredClipId && clip.id == *ignoredClipId) continue;
        if (!clip.shortcut || clip.shortcut->empty()) continue;
        if (!shortcutScopesConflict(*clip.shortcut, shortcut)) continue;
        return ShortcutConflict{clip.id, clip.searchableName(), clip.shortcut->global};
    }
    return std::nullopt;
}

HotkeyRegistrationPlan makeHotkeyRegistrationPlan(std::vector<SoundClip> const& clips, int firstId)
{
    HotkeyRegistrationPlan plan;
    std::map<std::pair<unsigned int, unsigned int>, std::size_t> combinationCounts;
    for (auto const& clip : clips) {
        if (!clip.shortcut || clip.shortcut->empty()) continue;
        const auto shortcut = normalizeShortcut(*clip.shortcut);
        ++combinationCounts[{shortcut.keyval, shortcut.modifiers}];
    }
    auto id = firstId;
    for (auto const& clip : clips) {
        if (!clip.shortcut || clip.shortcut->empty()) continue;
        auto shortcut = normalizeShortcut(*clip.shortcut);
        shortcut.global = true;
        const auto combination = std::pair{shortcut.keyval, shortcut.modifiers};
        if (combinationCounts[combination] > 1) {
            plan.errors[clip.id] = L"Another Cuelet sound uses the same shortcut.";
            continue;
        }
        if (isWindowsReservedShortcut(shortcut)) {
            plan.errors[clip.id] = L"This combination is reserved by Windows and cannot be used.";
            continue;
        }
        if (!isShortcutSupported(shortcut)) {
            plan.errors[clip.id] = L"This shortcut is unsupported or unsafe as a global shortcut.";
            continue;
        }
        plan.entries.push_back({id++, clip.id, shortcut.keyval, nativeShortcutModifiers(shortcut)});
    }
    return plan;
}

GlobalShortcutRegistry::GlobalShortcutRegistry(std::shared_ptr<IGlobalShortcutBackend> backend)
    : m_backend(std::move(backend))
{
}

GlobalShortcutRegistry::~GlobalShortcutRegistry()
{
    unregisterAll();
}

int GlobalShortcutRegistry::allocateId() noexcept
{
    while (m_nextId == probeHotkeyId || std::any_of(m_active.begin(), m_active.end(), [&](auto const& active) {
        return active.id == m_nextId;
    })) {
        ++m_nextId;
    }
    return m_nextId++;
}

bool GlobalShortcutRegistry::update(std::vector<SoundClip> const& clips, bool transactional)
{
    m_lastFailureReason.clear();
    const auto plan = makeHotkeyRegistrationPlan(clips, 0);
    if (transactional && !plan.errors.empty()) {
        m_lastFailureReason = plan.errors.begin()->second;
        return false;
    }

    std::map<std::string, GlobalShortcutStatus> nextStatuses;
    for (auto const& [clipId, reason] : plan.errors) nextStatuses[clipId].reason = reason;
    std::vector<ActiveRegistration> nextActive;
    std::vector<int> newlyRegistered;
    std::set<int> reusedIds;

    for (auto const& entry : plan.entries) {
        const auto reused = std::find_if(m_active.begin(), m_active.end(), [&](auto const& active) {
            return active.key == entry.key && active.nativeModifiers == entry.nativeModifiers;
        });
        if (reused != m_active.end() && reusedIds.insert(reused->id).second) {
            nextActive.push_back({reused->id, entry.clipId, entry.key, entry.nativeModifiers});
            nextStatuses[entry.clipId].registered = true;
            continue;
        }

        const auto id = allocateId();
        const auto result = m_backend->registerShortcut(id, entry.key, entry.nativeModifiers);
        if (!result.succeeded) {
            const auto reason = registrationFailure(result.errorCode);
            m_lastFailureReason = reason;
            if (transactional) {
                for (const auto registeredId : newlyRegistered) m_backend->unregisterShortcut(registeredId);
                return false;
            }
            auto& status = nextStatuses[entry.clipId];
            status.reason = reason;
            status.errorCode = result.errorCode;
            continue;
        }
        newlyRegistered.push_back(id);
        nextActive.push_back({id, entry.clipId, entry.key, entry.nativeModifiers});
        nextStatuses[entry.clipId].registered = true;
    }

    for (auto const& active : m_active) {
        if (reusedIds.count(active.id) == 0) m_backend->unregisterShortcut(active.id);
    }
    m_active = std::move(nextActive);
    m_statusByClip = std::move(nextStatuses);
    return true;
}

ShortcutCheckResult GlobalShortcutRegistry::probe(cuelet::Shortcut const& shortcut) const
{
    const auto result = m_backend->probe(normalizeShortcut(shortcut).keyval, nativeShortcutModifiers(shortcut));
    if (result.succeeded) return {};
    if (result.errorCode == ERROR_HOTKEY_ALREADY_REGISTERED) {
        return {ShortcutAvailability::RegisteredByAnotherApplication, {}, result.errorCode};
    }
    return {ShortcutAvailability::RegistrationError, {}, result.errorCode};
}

void GlobalShortcutRegistry::unregisterAll() noexcept
{
    if (m_backend) {
        for (auto const& active : m_active) m_backend->unregisterShortcut(active.id);
    }
    m_active.clear();
}

std::optional<std::string> GlobalShortcutRegistry::clipForHotkeyId(int id) const
{
    const auto found = std::find_if(m_active.begin(), m_active.end(), [&](auto const& active) { return active.id == id; });
    return found == m_active.end() ? std::nullopt : std::optional<std::string>{found->clipId};
}

GlobalShortcutStatus GlobalShortcutRegistry::statusFor(std::string const& clipId) const
{
    const auto found = m_statusByClip.find(clipId);
    return found == m_statusByClip.end() ? GlobalShortcutStatus{} : found->second;
}

std::size_t GlobalShortcutRegistry::failureCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(m_statusByClip.begin(), m_statusByClip.end(), [](auto const& item) {
        return !item.second.registered;
    }));
}

std::wstring GlobalShortcutRegistry::lastFailureReason() const
{
    return m_lastFailureReason;
}

} // namespace cuelet::windows
