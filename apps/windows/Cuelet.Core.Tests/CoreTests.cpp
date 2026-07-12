#include "cuelet/LibraryScanner.h"
#include "cuelet/SoundSearch.h"
#include "WindowsUtf8.h"
#include "WindowsHotkeyModel.h"
#include "WindowsAudioRoutingModel.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void touch(const std::filesystem::path& path)
{
    std::ofstream stream(path, std::ios::binary);
    stream << "test";
}

class FakeGlobalShortcutBackend final : public cuelet::windows::IGlobalShortcutBackend {
public:
    cuelet::windows::NativeShortcutResult registerShortcut(
        int id, unsigned int key, unsigned int nativeModifiers) override
    {
        if (key == failingKey) return {false, ERROR_HOTKEY_ALREADY_REGISTERED};
        const auto combination = std::pair{key, nativeModifiers};
        const auto duplicate = std::find_if(registrations.begin(), registrations.end(), [&](auto const& item) {
            return item.second == combination;
        });
        if (duplicate != registrations.end()) return {false, ERROR_HOTKEY_ALREADY_REGISTERED};
        registrations[id] = combination;
        ++registerCalls;
        return {true, ERROR_SUCCESS};
    }

    void unregisterShortcut(int id) noexcept override
    {
        if (registrations.erase(id) != 0) ++unregisterCalls;
    }

    cuelet::windows::NativeShortcutResult probe(unsigned int key, unsigned int nativeModifiers) override
    {
        ++probeCalls;
        if (claimed.count({key, nativeModifiers}) != 0) return {false, ERROR_HOTKEY_ALREADY_REGISTERED};
        return {true, ERROR_SUCCESS};
    }

    unsigned int failingKey = 0;
    int registerCalls = 0;
    int unregisterCalls = 0;
    int probeCalls = 0;
    std::map<int, std::pair<unsigned int, unsigned int>> registrations;
    std::set<std::pair<unsigned int, unsigned int>> claimed;
};

cuelet::Shortcut shortcut(unsigned int key, unsigned int modifiers, bool global = true)
{
    cuelet::Shortcut value;
    value.keyval = key;
    value.modifiers = modifiers;
    value.global = global;
    value = cuelet::windows::normalizeShortcut(value);
    value.label = cuelet::windows::wideToUtf8(cuelet::windows::formatShortcut(value));
    return value;
}

void runTests()
{
    const auto root = std::filesystem::temp_directory_path()
        / ("cuelet-windows-tests-" + std::to_string(static_cast<unsigned long long>(::GetCurrentProcessId())));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "nested");
    try {
        touch(root / "Air Horn.wav");
        touch(root / "nested" / "theme.MP3");
        touch(root / "notes.txt");

        cuelet::LibraryScanner scanner;
        auto shallow = scanner.scan(root, false);
        require(shallow.warning.empty(), "valid folders should scan without warnings");
        require(shallow.clips.size() == 1, "non-recursive scan should only include root audio");
        require(shallow.unsupportedFiles.size() == 1, "unsupported root files should be reported");

        auto recursive = scanner.scan(root, true);
        require(recursive.clips.size() == 2, "recursive scan should include nested audio");
        require(recursive.clips[0].id == cuelet::stableIdForPath(recursive.clips[0].relativePath), "sound IDs must be stable");

        recursive.clips[0].favorite = true;
        recursive.clips[0].notes = "meeting intro";
        cuelet::FilterOptions filter;
        filter.scope = cuelet::LibraryScope::Favorites;
        filter.searchText = "meeting";
        auto filtered = cuelet::filterAndSortSounds(recursive.clips, {cuelet::uncategorizedCategory()}, filter);
        require(filtered.size() == 1 && filtered[0].favorite, "favorite and text filters should compose");

        require(cuelet::displayNameFromFilename("door.knock.wav") == "door.knock", "only the final extension should be removed");
        require(cuelet::stableCategoryIdForName("Sound FX") == cuelet::stableCategoryIdForName("Sound FX"), "category IDs must be deterministic");

        const auto& colors = cuelet::availableCategoryColors();
        require(colors.size() == 9, "the portable category palette must contain all supported colors");
        require(colors.front().colorHex == "#8E8E93" && colors.back().colorHex == "#AF52DE", "category palette values must remain stable for metadata");
        require(cuelet::canonicalCategoryIconId("music.note") == "music-note", "macOS icon aliases must map to portable IDs");
        require(cuelet::canonicalCategoryIconId("audio-speakers-symbolic") == "audio-speakers", "Linux icon aliases must map to portable IDs");
        require(cuelet::canonicalCategoryIconId("unknown-platform-glyph") == "tag", "unknown icons must use the portable tag fallback");

        const std::string unicode = u8"jönu · Geräusche · música · 日本語";
        const auto wide = cuelet::windows::utf8ToWide(unicode);
        require(cuelet::windows::wideToUtf8(wide) == unicode, "UTF-8 and Unicode conversion must round-trip");
        require(!cuelet::windows::containsMojibakeMarker(L"New category\u2026"), "the correct ellipsis must not be flagged as mojibake");
        require(cuelet::windows::containsMojibakeMarker(L"New Categoryå€:"), "corrupted labels must be detected by regression tests");

        const auto unicodeFolder = root / std::filesystem::path(L"Geräusche 日本語");
        std::filesystem::create_directories(unicodeFolder);
        touch(unicodeFolder / std::filesystem::path(L"jönu música.wav"));
        auto unicodeScan = scanner.scan(root, true);
        const auto unicodeClip = std::find_if(unicodeScan.clips.begin(), unicodeScan.clips.end(), [](auto const& clip) {
            return clip.filename == u8"jönu música.wav";
        });
        require(unicodeClip != unicodeScan.clips.end(), "Unicode sound paths must scan as UTF-8");
        require(std::filesystem::exists(std::filesystem::u8path(unicodeClip->absolutePath)), "stored UTF-8 sound paths must resolve to native Windows paths");

        using namespace cuelet::windows;
        const auto normalizedLetter = normalizeShortcut(shortcut('S', shortcutModifierCtrl | shortcutModifierAlt | 0x80u, false));
        require(normalizedLetter.keyval == 'S' && normalizedLetter.modifiers == (shortcutModifierCtrl | shortcutModifierAlt),
                "shortcut normalization must preserve canonical letter virtual keys and mask modifier bits");
        require(formatShortcut(shortcut('S', shortcutModifierCtrl | shortcutModifierAlt | shortcutModifierShift | shortcutModifierWin)) ==
                    L"Ctrl+Alt+Shift+Win+S",
                "shortcut formatting must use canonical Windows modifier order");
        require(formatShortcut(shortcut(VK_NUMPAD1, shortcutModifierCtrl | shortcutModifierAlt)) == L"Ctrl+Alt+NumPad1",
                "numpad keys must remain distinct in display text");
        require(shortcutEquals(shortcut('A', shortcutModifierCtrl | 0x80u), shortcut('A', shortcutModifierCtrl)),
                "shortcut equality must compare canonical virtual keys and normalized modifier bits");
        require(isModifierKey(VK_LCONTROL) && isModifierKey(VK_RSHIFT) && isModifierKey(VK_LWIN),
                "left and right modifier keys must be recognized as modifiers");
        require(!isShortcutSupported(shortcut(VK_CONTROL, 0, false)), "modifier-only shortcuts must be rejected");
        require(!isShortcutSupported(shortcut('A', 0, true)), "unmodified global typing keys must be rejected");
        require(isShortcutSupported(shortcut(VK_F13, 0, true)), "unmodified F13-F24 global shortcuts must be supported");
        require(isShortcutSupported(shortcut(VK_F8, shortcutModifierAlt, true)), "modified function keys must be supported");

        const std::vector<cuelet::Shortcut> reserved = {
            shortcut(VK_DELETE, shortcutModifierCtrl | shortcutModifierAlt),
            shortcut(VK_TAB, shortcutModifierAlt),
            shortcut(VK_ESCAPE, shortcutModifierAlt),
            shortcut(VK_F4, shortcutModifierAlt),
            shortcut(VK_ESCAPE, shortcutModifierCtrl | shortcutModifierShift),
            shortcut('L', shortcutModifierWin), shortcut('D', shortcutModifierWin),
            shortcut('E', shortcutModifierWin), shortcut('R', shortcutModifierWin),
            shortcut('I', shortcutModifierWin), shortcut(VK_TAB, shortcutModifierWin),
            shortcut('X', shortcutModifierWin), shortcut('V', shortcutModifierWin),
            shortcut(VK_SPACE, shortcutModifierWin),
            shortcut('D', shortcutModifierWin | shortcutModifierCtrl),
            shortcut(VK_LEFT, shortcutModifierWin | shortcutModifierCtrl),
            shortcut(VK_RIGHT, shortcutModifierWin | shortcutModifierCtrl),
            shortcut(VK_F12, shortcutModifierCtrl),
        };
        require(std::all_of(reserved.begin(), reserved.end(), isWindowsReservedShortcut),
                "all documented Windows-reserved shortcut rules must be enforced");

        cuelet::SoundClip airHorn;
        airHorn.id = "air-horn";
        airHorn.displayName = u8"Air Hørn 日本語";
        airHorn.shortcut = shortcut('1', shortcutModifierCtrl | shortcutModifierAlt, false);
        cuelet::SoundClip secondSound;
        secondSound.id = "second";
        secondSound.displayName = "Second";
        secondSound.shortcut = shortcut('1', shortcutModifierCtrl | shortcutModifierAlt, false);
        auto localConflict = findShortcutConflict({airHorn, secondSound}, *secondSound.shortcut, secondSound.id);
        require(localConflict && localConflict->clipId == airHorn.id, "local duplicates in the same Cuelet context must conflict");
        require(localConflict->soundName == u8"Air Hørn 日本語", "conflict results must preserve Unicode sound names");
        secondSound.shortcut->global = true;
        require(findShortcutConflict({airHorn, secondSound}, *secondSound.shortcut, secondSound.id).has_value(),
                "global and local shortcuts with the same combination must conflict");
        const auto crossScopePlan = makeHotkeyRegistrationPlan({airHorn, secondSound});
        require(crossScopePlan.entries.empty() && crossScopePlan.errors.count(secondSound.id) == 1,
                "a loaded local/global duplicate must suppress the global registration to prevent double firing");
        require(!findShortcutConflict({airHorn}, *airHorn.shortcut, airHorn.id).has_value(),
                "an unchanged assignment on the sound being edited must not conflict with itself");

        const auto storedShortcut = shortcutToStorage(shortcut('Q', shortcutModifierCtrl | shortcutModifierAlt | 0x40u, true));
        const auto loadedShortcut = shortcutFromStorage(storedShortcut);
        require(storedShortcut.virtualKey == 'Q' && storedShortcut.modifiers == (shortcutModifierCtrl | shortcutModifierAlt) && storedShortcut.global,
                "shortcut serialization must store normalized virtual key, modifier mask, and scope");
        require(shortcutEquals(loadedShortcut, shortcut('Q', shortcutModifierCtrl | shortcutModifierAlt, true)) && loadedShortcut.global,
                "shortcut deserialization must restore the structured binding without parsing display text");

        cuelet::SoundClip firstHotkey;
        firstHotkey.id = "first";
        firstHotkey.shortcut = cuelet::Shortcut{'1', 1 | 2, "Ctrl+Shift+1", true};
        cuelet::SoundClip duplicateHotkey;
        duplicateHotkey.id = "duplicate";
        duplicateHotkey.shortcut = cuelet::Shortcut{'1', 1 | 2, "Ctrl+Shift+1", true};
        cuelet::SoundClip localHotkey;
        localHotkey.id = "local";
        localHotkey.shortcut = cuelet::Shortcut{'2', 1, "Ctrl+2", false};
        cuelet::SoundClip uniqueHotkey;
        uniqueHotkey.id = "unique";
        uniqueHotkey.shortcut = shortcut('3', shortcutModifierCtrl | shortcutModifierAlt, true);
        const auto hotkeyPlan = cuelet::windows::makeHotkeyRegistrationPlan({firstHotkey, duplicateHotkey, localHotkey, uniqueHotkey}, 100);
        require(hotkeyPlan.entries.size() == 1, "only unique global shortcuts should be registered");
        require(hotkeyPlan.entries.front().id == 100 && hotkeyPlan.entries.front().clipId == "unique", "hotkey IDs must map deterministically to sound IDs");
        require((hotkeyPlan.entries.front().nativeModifiers & MOD_NOREPEAT) != 0, "global hotkeys must use MOD_NOREPEAT");
        require(hotkeyPlan.errors.count("first") == 1 && hotkeyPlan.errors.count("duplicate") == 1,
                "every global member of a duplicate shortcut group must report a conflict");

        auto fakeBackend = std::make_shared<FakeGlobalShortcutBackend>();
        GlobalShortcutRegistry registry(fakeBackend);
        cuelet::SoundClip registeredClip;
        registeredClip.id = "registered";
        registeredClip.shortcut = shortcut('1', shortcutModifierCtrl | shortcutModifierAlt, true);
        require(registry.update({registeredClip}, true), "a valid global registration transaction must succeed");
        require(registry.statusFor("registered").registered && fakeBackend->registrations.size() == 1,
                "a successful transaction must publish the real registration status");

        fakeBackend->failingKey = '2';
        auto failedReplacement = registeredClip;
        failedReplacement.shortcut = shortcut('2', shortcutModifierCtrl | shortcutModifierAlt, true);
        require(!registry.update({failedReplacement}, true), "a failed replacement registration must fail the transaction");
        require(registry.statusFor("registered").registered && fakeBackend->registrations.size() == 1 &&
                    fakeBackend->registrations.begin()->second.first == '1',
                "registration failure must roll back new work and preserve the old shortcut");

        fakeBackend->failingKey = 0;
        cuelet::SoundClip replacementOwner;
        replacementOwner.id = "replacement";
        replacementOwner.shortcut = registeredClip.shortcut;
        const auto registerCallsBeforeTransfer = fakeBackend->registerCalls;
        require(registry.update({replacementOwner}, true), "approved Cuelet replacement must transfer the registration");
        require(registry.statusFor("replacement").registered &&
                    fakeBackend->registerCalls == registerCallsBeforeTransfer,
                "replacement of an identical global combination must reuse the existing registration atomically");

        require(registry.update({}, true), "clearing an assignment must update the registration plan");
        require(fakeBackend->registrations.empty() && !registry.clipForHotkeyId(0x4350).has_value(),
                "clearing an assignment must immediately unregister it");

        const auto probed = shortcut('P', shortcutModifierCtrl | shortcutModifierShift, true);
        fakeBackend->claimed.insert({probed.keyval, nativeShortcutModifiers(probed)});
        const auto probedUnavailable = registry.probe(probed);
        require(probedUnavailable.availability == ShortcutAvailability::RegisteredByAnotherApplication &&
                    probedUnavailable.errorCode == ERROR_HOTKEY_ALREADY_REGISTERED,
                "availability probing must report Windows or another application without naming an owner");

        cuelet::SoundClip libraryOne = registeredClip;
        libraryOne.id = "library-one";
        cuelet::SoundClip libraryDuplicate = libraryOne;
        libraryDuplicate.id = "library-duplicate";
        cuelet::SoundClip libraryReserved = registeredClip;
        libraryReserved.id = "library-reserved";
        libraryReserved.shortcut = shortcut(VK_F12, shortcutModifierCtrl, true);
        cuelet::SoundClip libraryValid = registeredClip;
        libraryValid.id = "library-valid";
        libraryValid.shortcut = shortcut('4', shortcutModifierCtrl | shortcutModifierAlt, true);
        const auto switchPlan = makeHotkeyRegistrationPlan({libraryOne, libraryDuplicate, libraryReserved, libraryValid}, 500);
        require(switchPlan.entries.size() == 1 && switchPlan.entries.front().id == 500,
                "library switching must produce a deterministic registration plan for valid assignments");
        require(switchPlan.errors.count("library-one") == 1 && switchPlan.errors.count("library-duplicate") == 1 &&
                    switchPlan.errors.count("library-reserved") == 1,
                "library switching must retain visible errors for duplicate and reserved assignments");
        require(cuelet::windows::volumeToSetting(0.75) == 750, "audio routing volumes must serialize deterministically");
        require(cuelet::windows::volumeFromSetting(750) == 0.75, "audio routing volumes must deserialize deterministically");
        require(cuelet::windows::volumeToSetting(4.0) == 1000 && cuelet::windows::volumeFromSetting(4000) == 1.0, "persisted routing volumes must clamp safely");
        require(cuelet::windows::looksLikeVirtualAudioEndpoint(L"CABLE Input (VB-Audio Virtual Cable)"), "installed virtual cable render endpoints must be detected");
        require(!cuelet::windows::looksLikeVirtualAudioEndpoint(L"Speakers (USB Audio)"), "normal speakers must not be presented as virtual endpoints");

        const auto halfway = cuelet::makePlaybackProgress(2.5, 5.0);
        require(halfway.positionSeconds == 2.5 && halfway.durationSeconds == 5.0 && halfway.fraction == 0.5, "playback progress must use the real position and duration");
        const auto finished = cuelet::makePlaybackProgress(8.0, 5.0);
        require(finished.positionSeconds == 5.0 && finished.fraction == 1.0, "playback progress must clamp at the end");
        const auto unavailable = cuelet::makePlaybackProgress(-1.0, 0.0);
        require(unavailable.positionSeconds == 0.0 && unavailable.durationSeconds == 0.0 && unavailable.fraction == 0.0, "invalid playback progress must reset cleanly");
        require(cuelet::shouldShowSelectionOutline(true, false), "selected sounds must show a selection outline");
        require(cuelet::shouldShowSelectionOutline(true, true), "selected playing sounds must remain selected");
        require(!cuelet::shouldShowSelectionOutline(false, true), "playback must not create a selection outline");
    } catch (...) {
        std::filesystem::remove_all(root, error);
        throw;
    }
    std::filesystem::remove_all(root, error);
}

} // namespace

int wmain()
{
    try {
        runTests();
        std::wcout << L"Cuelet Windows core tests passed.\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << "Cuelet Windows core tests failed: " << error.what() << '\n';
        return 1;
    }
}
