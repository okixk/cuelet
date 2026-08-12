#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "cuelet/LibraryScanner.h"
#include "cuelet/SoundSearch.h"
#include "WindowsUtf8.h"
#include "WindowsHotkeyModel.h"
#include "WindowsAudioRoutingModel.h"
#include "WindowsLifecycleModel.h"
#include "WindowsVirtualAudioModel.h"
#include "WindowsWorkflowModel.h"
#include "VirtualAudioRingBufferModel.h"
#include "VirtualAudioPackagePolicy.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

void runVirtualAudioFifoExtendedTests();

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

std::vector<std::uint8_t> pcmStereoFrame(
    std::int16_t left,
    std::int16_t right)
{
    return {
        static_cast<std::uint8_t>(left),
        static_cast<std::uint8_t>(
            static_cast<std::uint16_t>(left) >> 8),
        static_cast<std::uint8_t>(right),
        static_cast<std::uint8_t>(
            static_cast<std::uint16_t>(right) >> 8)};
}

void appendBytes(
    std::vector<std::uint8_t>& destination,
    std::vector<std::uint8_t> const& source)
{
    destination.insert(
        destination.end(), source.begin(), source.end());
}

void runVirtualAudioRingBufferTests()
{
    using cuelet::windows::VirtualAudioRingBufferModel;
    constexpr std::size_t frameBytes = 4;

    VirtualAudioRingBufferModel underflow(64, frameBytes, 8);
    VirtualAudioRingBufferModel::Reader underflowReader;
    require(
        underflow.read(underflowReader, frameBytes) ==
            std::vector<std::uint8_t>(frameBytes),
        "a new bridge reader must begin with silence");
    underflow.publish(pcmStereoFrame(1, -1));
    require(
        underflow.read(underflowReader, frameBytes) ==
            std::vector<std::uint8_t>(frameBytes),
        "startup buffering must not emit a partial reserve");
    underflow.publish(pcmStereoFrame(2, -2));
    require(
        underflow.read(underflowReader, frameBytes) ==
            pcmStereoFrame(1, -1),
        "startup buffering must preserve the oldest live frame");
    require(
        underflow.read(underflowReader, frameBytes * 2) ==
            std::vector<std::uint8_t>(frameBytes * 2),
        "underflow must return whole-frame silence without consuming data");
    std::vector<std::uint8_t> refill;
    appendBytes(refill, pcmStereoFrame(3, -3));
    appendBytes(refill, pcmStereoFrame(4, -4));
    underflow.publish(refill);
    const auto recovered = underflow.read(
        underflowReader, frameBytes * 2);
    std::vector<std::uint8_t> expectedRecovered;
    appendBytes(expectedRecovered, pcmStereoFrame(2, -2));
    appendBytes(expectedRecovered, pcmStereoFrame(3, -3));
    require(
        recovered == expectedRecovered,
        "underflow recovery must resume the FIFO without stale replay");

    VirtualAudioRingBufferModel wrap(32, frameBytes, 8);
    VirtualAudioRingBufferModel::Reader wrapReader;
    require(
        wrap.read(wrapReader, frameBytes) ==
            std::vector<std::uint8_t>(frameBytes),
        "wrap test reader must initialize at the live edge");
    std::vector<std::vector<std::uint8_t>> frames;
    for (std::int16_t value = 0; value < 50; ++value) {
        frames.push_back(pcmStereoFrame(
            static_cast<std::int16_t>(value * 101),
            static_cast<std::int16_t>(-value * 103)));
    }
    std::vector<std::uint8_t> startup;
    appendBytes(startup, frames[0]);
    appendBytes(startup, frames[1]);
    wrap.publish(startup);
    require(
        wrap.read(wrapReader, frameBytes) == frames[0],
        "ring startup must return the first complete stereo frame");
    for (std::size_t index = 2; index < frames.size(); ++index) {
        wrap.publish(frames[index]);
        require(
            wrap.read(wrapReader, frameBytes) ==
                frames[index - 1],
            "every ring wrap boundary must preserve stereo frame order");
    }

    VirtualAudioRingBufferModel crossing(64, frameBytes, 8);
    VirtualAudioRingBufferModel::Reader crossingReader;
    crossing.read(crossingReader, frameBytes);
    std::vector<std::uint8_t> crossingBytes;
    for (std::size_t index = 0; index < 12; ++index) {
        appendBytes(crossingBytes, frames[index]);
    }
    crossing.publish(crossingBytes);
    const auto crossingRead =
        crossing.read(crossingReader, frameBytes * 7);
    require(
        std::equal(
            crossingRead.begin(), crossingRead.end(),
            crossingBytes.begin()),
        "a large read crossing the ring end must remain contiguous");

    VirtualAudioRingBufferModel overflow(32, frameBytes, 8);
    VirtualAudioRingBufferModel::Reader overflowReader;
    overflow.read(overflowReader, frameBytes);
    std::vector<std::uint8_t> overflowBytes;
    for (std::size_t index = 0; index < 12; ++index) {
        appendBytes(overflowBytes, frames[index]);
    }
    overflow.publish(overflowBytes);
    require(
        overflow.read(overflowReader, frameBytes) == frames[10],
        "overflow recovery must resume at the bounded target reserve");

    VirtualAudioRingBufferModel reset(64, frameBytes, 8);
    VirtualAudioRingBufferModel::Reader resetReader;
    reset.read(resetReader, frameBytes);
    reset.publish(startup);
    require(
        reset.read(resetReader, frameBytes) == frames[0],
        "reset fixture must start normally");
    reset.reset();
    require(
        reset.read(resetReader, frameBytes) ==
            std::vector<std::uint8_t>(frameBytes),
        "reset must invalidate reader cursors and return silence");

    const auto extrema = pcmStereoFrame(
        std::numeric_limits<std::int16_t>::min(),
        std::numeric_limits<std::int16_t>::max());
    VirtualAudioRingBufferModel integrity(64, frameBytes, 8);
    VirtualAudioRingBufferModel::Reader integrityReader;
    integrity.read(integrityReader, frameBytes);
    std::vector<std::uint8_t> integrityInput;
    appendBytes(integrityInput, extrema);
    appendBytes(integrityInput, pcmStereoFrame(0x1234, -0x1234));
    integrity.publish(integrityInput);
    require(
        integrity.read(integrityReader, frameBytes) == extrema,
        "int16 minimum/maximum values must survive without sign corruption");

    bool rejectedPartialFrame = false;
    try {
        integrity.publish({1, 2, 3});
    } catch (std::invalid_argument const&) {
        rejectedPartialFrame = true;
    }
    require(
        rejectedPartialFrame,
        "the bridge model must reject partial-frame writes");

    constexpr std::size_t sineFrames = 48'000;
    std::vector<std::uint8_t> sineBytes;
    sineBytes.reserve(sineFrames * frameBytes);
    for (std::size_t frame = 0; frame < sineFrames; ++frame) {
        const auto sample = static_cast<std::int16_t>(std::lround(
            0.8 * 32767.0 *
            std::sin(
                2.0 * 3.14159265358979323846 * 40.0 *
                static_cast<double>(frame) / 48'000.0)));
        appendBytes(sineBytes, pcmStereoFrame(sample, sample));
    }
    VirtualAudioRingBufferModel sine(256, frameBytes, 64);
    VirtualAudioRingBufferModel::Reader sineReader;
    sine.read(sineReader, frameBytes);
    sine.publish(std::vector<std::uint8_t>(
        sineBytes.begin(), sineBytes.begin() + 64));
    std::vector<std::uint8_t> sineCapture;
    constexpr std::size_t chunkBytes = 16;
    std::size_t writeOffset = 64;
    while (sineCapture.size() < sineBytes.size() - 64) {
        if (writeOffset < sineBytes.size()) {
            const auto writeEnd = std::min(
                writeOffset + chunkBytes, sineBytes.size());
            sine.publish(std::vector<std::uint8_t>(
                sineBytes.begin() +
                    static_cast<std::ptrdiff_t>(writeOffset),
                sineBytes.begin() +
                    static_cast<std::ptrdiff_t>(writeEnd)));
            writeOffset = writeEnd;
        }
        const auto block = sine.read(sineReader, chunkBytes);
        sineCapture.insert(
            sineCapture.end(), block.begin(), block.end());
    }
    require(
        std::equal(
            sineCapture.begin(), sineCapture.end(),
            sineBytes.begin()),
        "a continuous 40 Hz sine must remain exact across repeated wraps");
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

class FakeInstallerProcessBackend final
    : public cuelet::windows::IVirtualAudioInstallerProcessBackend {
public:
    cuelet::windows::InstallerInvocation invoke(
        cuelet::windows::VirtualAudioInstallerAction requestedAction) override
    {
        action = requestedAction;
        ++calls;
        return result;
    }

    cuelet::windows::InstallerInvocation result;
    cuelet::windows::VirtualAudioInstallerAction action =
        cuelet::windows::VirtualAudioInstallerAction::Install;
    int calls = 0;
};

class FakeDriverVerificationBackend final
    : public cuelet::windows::IVirtualAudioDriverVerificationBackend {
public:
    cuelet::windows::VirtualAudioVerification verify() override
    {
        ++calls;
        return result;
    }

    cuelet::windows::VirtualAudioVerification result;
    int calls = 0;
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
    runVirtualAudioRingBufferTests();
    runVirtualAudioFifoExtendedTests();
    require(
        cuelet::virtual_audio::isKnownUnsafeDriverVersion(
            L"20.37.42.726"),
        "the preserved crashing driver package must be blocked");
    require(
        !cuelet::virtual_audio::isKnownUnsafeDriverVersion(
            L"20.43.0.721"),
        "a separately versioned candidate must not match the crash denylist");
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

        require(std::string_view(u8"…") == "\xE2\x80\xA6",
                "UTF-8 source literals must compile to UTF-8 bytes");
        require(std::wstring_view(L"…") == L"\u2026",
                "Unicode source literals must preserve their code points");

        const std::string unicode = u8"jönu · Geräusche · música · 日本語";
        const auto wide = cuelet::windows::utf8ToWide(unicode);
        require(cuelet::windows::wideToUtf8(wide) == unicode, "UTF-8 and Unicode conversion must round-trip");
        require(!cuelet::windows::containsMojibakeMarker(L"New category\u2026"), "the correct ellipsis must not be flagged as mojibake");
        require(cuelet::windows::containsMojibakeMarker(L"New Category\u00E5\u20AC:"), "corrupted labels must be detected by regression tests");

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
        require(!isShortcutSupported(shortcut('A', 0, false)), "legacy local typing shortcuts must be validated as global during migration");
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
        const AudioEndpointDescriptor physicalMic{
            "physical", L"Microphone Array (AMD Audio Device)", L"{physical}", L"AMD",
            L"HDAUDIO\\FUNC_01", L"AMD", {}, true, true};
        const AudioEndpointDescriptor virtualMic{
            "virtual", L"CABLE Output (VB-Audio Virtual Cable)", L"{cable}", L"VB-Audio",
            L"ROOT\\VB-CABLE", L"VB-Audio", {}, true, true};
        const AudioEndpointDescriptor virtualRender{
            "render", L"CABLE Input (VB-Audio Virtual Cable)", L"{cable}", L"VB-Audio",
            L"ROOT\\VB-CABLE", L"VB-Audio", {}, false, true};
        require(isPhysicalMicrophone(physicalMic) && !isPhysicalMicrophone(virtualMic),
                "physical microphone classification must exclude virtual capture endpoints");
        const std::vector<AudioEndpointDescriptor> fakeCaptures{virtualMic, physicalMic};
        require(choosePhysicalMicrophone(fakeCaptures, {}, "physical", "virtual").value() == 1,
                "automatic microphone selection must prefer a physical communications microphone");
        require(choosePhysicalMicrophone(fakeCaptures, "virtual", "physical", {}).value() == 1,
                "a persisted virtual capture choice must not become the physical microphone");
        require(findBestVirtualCapture(virtualRender, fakeCaptures).value() == 0 &&
                    virtualAudioPairScore(virtualRender, virtualMic) >
                        virtualAudioPairScore(virtualRender, physicalMic),
                "virtual render/capture pairing must use endpoint identity without conflating a physical mic");
        const AudioEndpointDescriptor speakers{
            "speakers", L"Speakers (Realtek Audio)", L"{realtek}", L"Realtek",
            L"HDAUDIO\\FUNC_01", L"Realtek", {}, false, true};
        require(classifyAudioEndpoint(speakers) == AudioEndpointKind::LocalPlayback &&
                    !isSupportedVirtualEndpoint(speakers),
                "ordinary speakers must remain local playback and never appear as a compatible virtual route");
        const AudioEndpointDescriptor cueletRender{
            "cuelet-render", L"Cuelet Virtual Microphone Input", L"{cuelet-container}",
            L"Cuelet", L"ROOT\\CUELETVIRTUALAUDIO\\0000", L"Cuelet",
            L"{8B9D3BB9-8C4E-4EF5-94D5-4BE741D4D892}", false, true,
            L"ROOT\\CUELETVIRTUALAUDIO\\0000"};
        const AudioEndpointDescriptor cueletCapture{
            "cuelet-capture", L"Cuelet Virtual Microphone", L"{cuelet-container}",
            L"Cuelet", L"ROOT\\CUELETVIRTUALAUDIO\\0000", L"Cuelet",
            L"{8B9D3BB9-8C4E-4EF5-94D5-4BE741D4D892}", true, true,
            L"ROOT\\CUELETVIRTUALAUDIO\\0000"};
        require(classifyAudioEndpoint(cueletRender) == AudioEndpointKind::CueletVirtualRender &&
                    classifyAudioEndpoint(cueletCapture) == AudioEndpointKind::CueletVirtualCapture &&
                    isCompatibleVirtualPair(cueletRender, cueletCapture),
                "Cuelet endpoints must be identified by the stable root-parent identity");
        AudioEndpointDescriptor projectedCueletRender{
            "projected-render", L"Speakers (Cuelet Virtual Audio Device)", {},
            L"Microsoft", L"SWD\\MMDEVAPI\\render", L"Microsoft", {},
            false, true};
        projectedCueletRender.parentInstanceId =
            L"ROOT\\CUELETVIRTUALAUDIO\\0000";
        AudioEndpointDescriptor projectedCueletCapture{
            "projected-capture",
            L"Cuelet Virtual Microphone (Cuelet Virtual Audio Device)", {},
            L"Microsoft", L"SWD\\MMDEVAPI\\capture", L"Microsoft", {},
            true, true};
        projectedCueletCapture.parentInstanceId =
            L"ROOT\\CUELETVIRTUALAUDIO\\0000";
        require(
            classifyAudioEndpoint(projectedCueletRender) ==
                    AudioEndpointKind::CueletVirtualRender &&
                classifyAudioEndpoint(projectedCueletCapture) ==
                    AudioEndpointKind::CueletVirtualCapture &&
                isCompatibleVirtualPair(
                    projectedCueletRender, projectedCueletCapture),
            "Windows-projected Cuelet endpoints must use their verified root parent identity");
        auto wrongParentCapture = projectedCueletCapture;
        wrongParentCapture.parentInstanceId =
            L"ROOT\\CUELETVIRTUALAUDIO\\0001";
        require(
            !isCompatibleVirtualPair(projectedCueletRender, wrongParentCapture),
            "Cuelet endpoint directions with different root parents must not pair");
        auto spoofedCueletRender = cueletRender;
        spoofedCueletRender.providerName = L"Realtek";
        spoofedCueletRender.parentInstanceId.clear();
        require(classifyAudioEndpoint(spoofedCueletRender) ==
                    AudioEndpointKind::LocalPlayback,
                "display-name, provider, and pairing spoofing without a Cuelet root parent must be rejected");
        auto missingParentCapture = cueletCapture;
        missingParentCapture.parentInstanceId.clear();
        require(
            classifyAudioEndpoint(missingParentCapture) ==
                    AudioEndpointKind::PhysicalMicrophone &&
                !isCompatibleVirtualPair(cueletRender, missingParentCapture),
            "missing root-parent identity must not be accepted automatically");
        require(
            !findBestVirtualCapture(cueletRender, {}).has_value(),
            "one missing Cuelet endpoint direction must remain an incomplete pair");
        require(!isCompatibleVirtualPair(speakers, physicalMic),
                "an arbitrary speaker plus physical microphone must never form a virtual pair");

        ShutdownCoordinator shutdown;
        const auto initialGeneration = shutdown.generation();
        require(shutdown.request(ShutdownReason::WindowClose, true) ==
                    ShutdownDecision::HideWindow &&
                    shutdown.state() == ShutdownState::HidingWindow &&
                    shutdown.acceptsUiWork(initialGeneration),
                "background close must hide without beginning final cleanup");
        require(shutdown.request(ShutdownReason::TrayExit, true) ==
                    ShutdownDecision::BeginFinalShutdown &&
                    shutdown.state() == ShutdownState::ShuttingDown &&
                    !shutdown.acceptsUiWork(initialGeneration),
                "tray Exit after hide must override background mode and cancel outstanding UI work");
        require(shutdown.request(ShutdownReason::WindowClose, false) ==
                    ShutdownDecision::AlreadyShuttingDown,
                "repeated final shutdown requests must be idempotent");
        shutdown.stopped();
        require(shutdown.request(ShutdownReason::TrayExit, false) ==
                    ShutdownDecision::AlreadyStopped,
                "a stopped coordinator must ignore later cleanup requests");

        VirtualAudioVerification absentDriver;
        require(driverStatus(absentDriver) == VirtualAudioDriverStatus::NotInstalled,
                "an absent package must report Not installed");
        VirtualAudioVerification partialDriver{
            true, true, true, false, false, false, false};
        require(driverStatus(partialDriver) == VirtualAudioDriverStatus::RepairRequired,
                "partial endpoint creation must report Repair required");
        VirtualAudioVerification connectedDriver{
            true, true, true, true, true, false, false};
        require(driverStatus(connectedDriver) == VirtualAudioDriverStatus::Connected &&
                    isCompleteCueletEndpointPair(connectedDriver),
                "trusted package plus the verified endpoint pair must report Connected");
        auto developmentDriver = connectedDriver;
        developmentDriver.signatureTrusted = false;
        require(driverStatus(developmentDriver) ==
                    VirtualAudioDriverStatus::RepairRequired &&
                    !isCompleteCueletEndpointPair(developmentDriver),
                "an untrusted package must not be accepted by the production status path");
        require(driverStatus(developmentDriver, true) ==
                    VirtualAudioDriverStatus::Connected &&
                    isCompleteCueletEndpointPair(developmentDriver, true),
                "an explicit development build may accept its verified test package");
        auto updateDriver = connectedDriver;
        updateDriver.updateAvailable = true;
        require(driverStatus(updateDriver) ==
                    VirtualAudioDriverStatus::UpdateAvailable,
                "an older installed package must report Update available");
        require(mayRemoveDriverPackage(L"Cuelet", L"ROOT\\CueletVirtualAudio") &&
                    !mayRemoveDriverPackage(L"VB-Audio", L"ROOT\\VB-CABLE"),
                "uninstall ownership checks must never authorize unrelated packages");

        FakeInstallerProcessBackend installerProcess;
        FakeDriverVerificationBackend driverVerification;
        installerProcess.result = {
            InstallerLaunchOutcome::UacCanceled, -1, false};
        auto installerWorkflow = runVirtualAudioInstallerWorkflow(
            VirtualAudioInstallerAction::Install,
            installerProcess,
            driverVerification);
        require(installerWorkflow.uacCanceled && !installerWorkflow.succeeded &&
                    installerWorkflow.status == VirtualAudioDriverStatus::NotInstalled,
                "UAC cancellation must preserve the absent-driver state");

        installerProcess.result = {
            InstallerLaunchOutcome::Completed, 13, false};
        installerWorkflow = runVirtualAudioInstallerWorkflow(
            VirtualAudioInstallerAction::Install,
            installerProcess,
            driverVerification);
        require(!installerWorkflow.succeeded &&
                    installerWorkflow.status ==
                        VirtualAudioDriverStatus::InstallationFailed,
                "signature validation or installer process failure must not report success");

        installerProcess.result = {
            InstallerLaunchOutcome::Completed, 0, false};
        driverVerification.result = connectedDriver;
        installerWorkflow = runVirtualAudioInstallerWorkflow(
            VirtualAudioInstallerAction::Install,
            installerProcess,
            driverVerification);
        require(installerWorkflow.succeeded &&
                    installerWorkflow.status == VirtualAudioDriverStatus::Connected,
                "a successful installer still requires complete endpoint verification");

        driverVerification.result = partialDriver;
        installerWorkflow = runVirtualAudioInstallerWorkflow(
            VirtualAudioInstallerAction::Repair,
            installerProcess,
            driverVerification);
        require(!installerWorkflow.succeeded &&
                    installerWorkflow.status ==
                        VirtualAudioDriverStatus::RepairRequired,
                "partial endpoint creation after repair must remain Repair required");

        driverVerification.result = {};
        installerWorkflow = runVirtualAudioInstallerWorkflow(
            VirtualAudioInstallerAction::Uninstall,
            installerProcess,
            driverVerification);
        require(installerWorkflow.succeeded &&
                    installerWorkflow.status ==
                        VirtualAudioDriverStatus::NotInstalled,
                "uninstall succeeds only after the Cuelet package is absent");

        driverVerification.result = connectedDriver;
        installerWorkflow = runVirtualAudioInstallerWorkflow(
            VirtualAudioInstallerAction::Uninstall,
            installerProcess,
            driverVerification);
        require(!installerWorkflow.succeeded &&
                    installerWorkflow.status ==
                        VirtualAudioDriverStatus::InstallationFailed,
                "uninstall must fail verification while the Cuelet package remains");

        require(importBehaviorFromSetting(importBehaviorSetting(ImportBehavior::Copy)) == ImportBehavior::Copy,
                "copy import behavior must round-trip through the persisted setting");
        require(importBehaviorFromSetting(importBehaviorSetting(ImportBehavior::Link)) == ImportBehavior::Link,
                "link import behavior must round-trip through the persisted setting");
        require(categoryIdForNavigationTag(L"category:user-memes") == "user-memes" &&
                    categoryIdForNavigationTag(L"category:uncategorized") == "uncategorized" &&
                    categoryIdForNavigationTag(L"library") == "uncategorized",
                "category drop identity must come from the complete row tag, independent of the child under the pointer");
        std::vector<cuelet::SoundClip> dropClips(1);
        dropClips.front().id = "existing-drop";
        dropClips.front().categoryId = "uncategorized";
        require(reassignExistingSound(dropClips, "existing-drop", "user-memes") &&
                    dropClips.size() == 1 && dropClips.front().categoryId == "user-memes",
                "dropping an existing Cuelet sound must reassign it without adding a duplicate");

        const auto importLibrary = root / "import-library";
        const auto importSources = root / "import-sources";
        std::filesystem::create_directories(importLibrary);
        std::filesystem::create_directories(importSources / "nested");
        touch(importLibrary / "tone.wav");
        touch(importSources / "tone.wav");
        touch(importSources / "nested" / "second.flac");
        touch(importSources / "readme.txt");
        const auto managedPlan = makeImportPlan(
            {importSources}, importLibrary, ImportBehavior::Copy, true, "user-memes", {});
        require(managedPlan.entries.size() == 2, "managed copy planning must expand supported files in subfolders");
        require(managedPlan.unsupported.size() == 1, "managed copy planning must retain unsupported-file details");
        require(managedPlan.entries.front().categoryId == "user-memes",
                "drop/import planning must carry the target category");
        const auto conflictDestination = std::find_if(managedPlan.entries.begin(), managedPlan.entries.end(), [](auto const& entry) {
            return entry.source.filename() == "tone.wav";
        });
        require(conflictDestination != managedPlan.entries.end() &&
                    conflictDestination->destination.filename() == "tone (2).wav",
                "managed copy planning must generate a non-overwriting destination");

        cuelet::SoundClip existingLinked;
        existingLinked.id = "linked-existing";
        existingLinked.storageMode = cuelet::SoundStorageMode::Linked;
        existingLinked.externalPath = wideToUtf8((importSources / "tone.wav").wstring());
        const auto linkedPlan = makeImportPlan(
            {importSources / "tone.wav", importSources / "nested" / "second.flac"},
            importLibrary, ImportBehavior::Link, false, "uncategorized", {existingLinked});
        require(linkedPlan.entries.size() == 2 && linkedPlan.entries.front().destination.empty(),
                "linked-reference planning must not create managed destinations");
        require(linkedPlan.entries.front().duplicateClipId == existingLinked.id,
                "duplicate detection must match an existing linked source by file identity");
        require(linkedMetadataKey(importSources / "tone.wav").rfind("@linked/", 0) == 0,
                "linked metadata keys must remain distinct from managed relative paths");
        require(cuelet::soundStorageModeFromName(cuelet::soundStorageModeName(cuelet::SoundStorageMode::Linked)) ==
                    cuelet::SoundStorageMode::Linked,
                "linked storageMode values must serialize and deserialize");
        require(cuelet::soundStorageModeFromName("") == cuelet::SoundStorageMode::Managed,
                "metadata without storageMode must remain a managed library entry");

        const auto renameOld = root / "rename-old.wav";
        const auto renameNew = root / "rename-new.wav";
        touch(renameOld);
        std::string renameError;
        require(!renameFileTransaction(renameOld, renameNew, [] { return false; }, &renameError) &&
                    std::filesystem::exists(renameOld) && !std::filesystem::exists(renameNew),
                "file rename transactions must roll back when metadata commit fails");
        require(renameFileTransaction(renameOld, renameNew, [] { return true; }, &renameError) &&
                    !std::filesystem::exists(renameOld) && std::filesystem::exists(renameNew),
                "file rename transactions must keep the new path after metadata commit succeeds");
        const auto collisionOld = root / "collision-old.wav";
        const auto collisionNew = root / "collision-new.wav";
        touch(collisionOld);
        touch(collisionNew);
        require(!renameFileTransaction(collisionOld, collisionNew, [] { return true; }, &renameError) &&
                    std::filesystem::exists(collisionOld) && std::filesystem::exists(collisionNew),
                "rename transactions must never overwrite an existing file");
        require(renamedSoundPath(root / "old.flac", L"Unicode \u65E5\u672C\u8A9E").filename() ==
                    std::filesystem::path(L"Unicode \u65E5\u672C\u8A9E.flac"),
                "the unified rename workflow must preserve the original extension and Unicode stem");
        cuelet::SoundClip managedRename;
        managedRename.id = "managed-rename";
        managedRename.filename = "old.wav";
        managedRename.displayName = "Legacy custom display name";
        managedRename.categoryId = "user-memes";
        managedRename.favorite = true;
        managedRename.notes = "preserve";
        managedRename.aliases = {"alias"};
        managedRename.durationKnown = true;
        managedRename.durationSeconds = 42;
        managedRename.durationSourcePath = "old";
        applyRenamedSoundMetadata(managedRename, importLibrary / L"Renamed.wav", importLibrary);
        require(managedRename.id == "managed-rename" && managedRename.filename == "Renamed.wav" &&
                    managedRename.relativePath == "Renamed.wav" && managedRename.displayName == "Renamed" &&
                    managedRename.categoryId == "user-memes" && managedRename.favorite &&
                    managedRename.notes == "preserve" && managedRename.aliases == std::vector<std::string>{"alias"} &&
                    managedRename.durationSeconds == 42 && managedRename.durationSourcePath.empty(),
                "managed rename must synchronize the filename/display name while preserving all other metadata");
        cuelet::SoundClip linkedRename = managedRename;
        linkedRename.storageMode = cuelet::SoundStorageMode::Linked;
        linkedRename.relativePath = "@linked/stable";
        applyRenamedSoundMetadata(linkedRename, importSources / L"External renamed.wav", importLibrary);
        require(linkedRename.relativePath == "@linked/stable" &&
                    linkedRename.externalPath == linkedRename.absolutePath &&
                    linkedRename.filename == "External renamed.wav" &&
                    linkedRename.displayName == "External renamed",
                "linked rename must update the original external path without changing its stable metadata key");

        cuelet::SoundClip exactName;
        exactName.id = "exact-name";
        exactName.displayName = "Door";
        cuelet::SoundClip exactFile;
        exactFile.id = "exact-file";
        exactFile.displayName = "Other";
        exactFile.filename = "door.wav";
        cuelet::SoundClip exactAlias;
        exactAlias.id = "exact-alias";
        exactAlias.displayName = "Third";
        exactAlias.aliases = {"door"};
        require(cuelet::bestMatchingSound({exactAlias, exactFile, exactName}, {cuelet::uncategorizedCategory()}, "door")->id ==
                    exactName.id,
                "Enter-to-play scoring must prioritize an exact display name over filename and alias");
        require(cuelet::soundSearchRankingScore(exactFile, {cuelet::uncategorizedCategory()}, "door.wav") >
                    cuelet::soundSearchRankingScore(exactAlias, {cuelet::uncategorizedCategory()}, "door.wav"),
                "exact filename scoring must remain deterministic");

        cuelet::SoundClip legacyLocal;
        legacyLocal.id = "legacy-local";
        legacyLocal.shortcut = shortcut('7', shortcutModifierCtrl | shortcutModifierAlt, false);
        const auto migratedPlan = makeHotkeyRegistrationPlan({legacyLocal});
        require(migratedPlan.entries.size() == 1 && migratedPlan.entries.front().clipId == legacyLocal.id,
                "old local shortcut assignments must be planned as global registrations without being deleted");
        const auto persistedLegacy = shortcutToStorage(*legacyLocal.shortcut);
        require(persistedLegacy.global, "saving a migrated shortcut must always use the global-only format");

        const auto cliImport = parseCommandLine(
            {L"--import", L"one.wav", L"--import", L"folder two", L"--mode", L"link",
             L"--category", L"Memes", L"--json"},
            root);
        require(cliImport.command == CliCommand::Import && cliImport.importPaths.size() == 2 &&
                    cliImport.importBehavior == ImportBehavior::Link && cliImport.categoryName == L"Memes" &&
                    cliImport.json,
                "CLI import parsing must support multiple paths, link mode, category, and JSON");
        const auto cliLibrary = parseCommandLine({L"--list-sounds", L"--library", L"library with spaces"}, root);
        require(cliLibrary.command == CliCommand::ListSounds &&
                    cliLibrary.library == (root / "library with spaces").lexically_normal(),
                "CLI library paths with spaces must resolve without replacing the primary command");
        require(parseCommandLine({L"--mode", L"copy"}).command == CliCommand::Invalid,
                "CLI import-only modifiers must fail outside an import command");
        require(parseCommandLine({L"--demo"}).command == CliCommand::Invalid,
                "the removed demo mode must not remain available through the CLI");
        require(cliHelpText().find(L"--reveal-id") != std::wstring::npos &&
                    cliHelpText().find(L"--demo") == std::wstring::npos &&
                    cliHelpText().find(L"local shortcut") == std::wstring::npos,
                "CLI help must match release functionality and omit removed development options");

        const auto hiddenMetadata = root / ".cuelet-metadata.json";
        touch(hiddenMetadata);
        DWORD attributeError = ERROR_SUCCESS;
        require(addHiddenFileAttribute(hiddenMetadata, &attributeError) &&
                    hasHiddenFileAttribute(hiddenMetadata),
                "the Windows metadata attribute wrapper must add FILE_ATTRIBUTE_HIDDEN");
        require(attributeError == ERROR_SUCCESS, "successful hidden-attribute updates must clear the diagnostic code");

        require(libraryStartupState({}) == LibraryStartupState::NeedsOnboarding,
                "an empty library setting must enter startup onboarding");
        require(libraryStartupState(root / "missing-library") == LibraryStartupState::ConfiguredLibraryMissing,
                "a missing configured library must not silently become an empty library");
        require(libraryStartupState(importLibrary) == LibraryStartupState::Ready,
                "an accessible configured library must skip onboarding");
        require(notificationDismissDelay(NotificationKind::Success).value() == std::chrono::milliseconds(2750) &&
                    notificationDismissDelay(NotificationKind::Warning).value() == std::chrono::milliseconds(6000) &&
                    !notificationDismissDelay(NotificationKind::Error).has_value(),
                "transient notification replacement policy must auto-dismiss success/warning but retain errors");

        const auto halfway = cuelet::makePlaybackProgress(2.5, 5.0);
        require(halfway.positionSeconds == 2.5 && halfway.durationSeconds == 5.0 && halfway.fraction == 0.5, "playback progress must use the real position and duration");
        const auto finished = cuelet::makePlaybackProgress(8.0, 5.0);
        require(finished.positionSeconds == 5.0 && finished.fraction == 1.0, "playback progress must clamp at the end");
        const auto unavailable = cuelet::makePlaybackProgress(-1.0, 0.0);
        require(unavailable.positionSeconds == 0.0 && unavailable.durationSeconds == 0.0 && unavailable.fraction == 0.0, "invalid playback progress must reset cleanly");
        cuelet::SoundClip cachedDuration;
        cachedDuration.durationKnown = true;
        cachedDuration.durationSeconds = 65;
        cachedDuration.durationFileSize = 1234;
        cachedDuration.durationModifiedSeconds = 500;
        cachedDuration.durationSourcePath = "C:\\sounds\\tone.wav";
        require(durationCacheIsValid(cachedDuration, "C:\\sounds\\tone.wav", 1234, 500) &&
                    !durationCacheIsValid(cachedDuration, "C:\\sounds\\tone.wav", 1234, 501) &&
                    !durationCacheIsValid(cachedDuration, "C:\\sounds\\tone.wav", 1235, 500),
                "duration cache validity must include source path, file size, and modification timestamp");
        require(formatDurationLabel(0, false) == L"\u2014" &&
                    formatDurationLabel(0, true) == L"0:00" &&
                    formatDurationLabel(90061, true) == L"1501:01",
                "duration labels must distinguish unknown, zero-length, and very long files");
        cuelet::SoundClip shortDuration;
        shortDuration.id = "short";
        shortDuration.displayName = "Short";
        shortDuration.durationKnown = true;
        shortDuration.durationSeconds = 2;
        cuelet::SoundClip longDuration = shortDuration;
        longDuration.id = "long";
        longDuration.displayName = "Long";
        longDuration.durationSeconds = 200;
        cuelet::SoundClip unknownDuration = shortDuration;
        unknownDuration.id = "unknown";
        unknownDuration.displayName = "Unknown";
        unknownDuration.durationKnown = false;
        cuelet::FilterOptions durationSort;
        durationSort.sort = cuelet::SortOption::DurationLongest;
        const auto durationSorted = cuelet::filterAndSortSounds(
            {unknownDuration, shortDuration, longDuration},
            {cuelet::uncategorizedCategory()}, durationSort);
        require(durationSorted[0].id == "long" && durationSorted[1].id == "short" &&
                    durationSorted[2].id == "unknown",
                "duration sorting must use indexed values and keep unknown durations last");
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
