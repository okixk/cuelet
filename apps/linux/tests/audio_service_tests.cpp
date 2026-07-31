#include "services/LinuxAudioService.h"

#include <gst/gst.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TestDirectory {
public:
    TestDirectory()
    {
        GError* error = nullptr;
        gchar* created = g_dir_make_tmp("cuelet-audio-service-tests-XXXXXX", &error);
        if (!created) {
            const std::string message =
                error && error->message ? error->message : "unknown temporary-directory error";
            if (error) {
                g_error_free(error);
            }
            throw std::runtime_error("could not create test directory: " + message);
        }
        path_ = created;
        g_free(created);
    }

    ~TestDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void writeLittleEndian16(std::ofstream& output, std::uint16_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
    };
    output.write(bytes, sizeof(bytes));
}

void writeLittleEndian32(std::ofstream& output, std::uint32_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff),
    };
    output.write(bytes, sizeof(bytes));
}

void writeSilentWav(const std::filesystem::path& path, double durationSeconds)
{
    constexpr std::uint32_t sampleRate = 8'000;
    constexpr std::uint16_t channelCount = 1;
    constexpr std::uint16_t bitsPerSample = 16;
    const auto sampleCount = static_cast<std::uint32_t>(
        std::llround(durationSeconds * static_cast<double>(sampleRate)));
    const std::uint32_t dataSize = sampleCount * channelCount * (bitsPerSample / 8);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(output.good(), "temporary WAV fixture must be writable");
    output.write("RIFF", 4);
    writeLittleEndian32(output, 36 + dataSize);
    output.write("WAVE", 4);
    output.write("fmt ", 4);
    writeLittleEndian32(output, 16);
    writeLittleEndian16(output, 1);
    writeLittleEndian16(output, channelCount);
    writeLittleEndian32(output, sampleRate);
    writeLittleEndian32(output, sampleRate * channelCount * (bitsPerSample / 8));
    writeLittleEndian16(output, channelCount * (bitsPerSample / 8));
    writeLittleEndian16(output, bitsPerSample);
    output.write("data", 4);
    writeLittleEndian32(output, dataSize);

    const std::vector<char> silence(dataSize, '\0');
    output.write(silence.data(), static_cast<std::streamsize>(silence.size()));
    require(output.good(), "temporary WAV fixture must be complete");
}

LinuxAudioService::Configuration fakeSinkConfiguration()
{
    LinuxAudioService::Configuration configuration;
    configuration.audioSinkFactoryName = "fakesink";
    configuration.virtualAudioSinkFactoryName = "fakesink";
    configuration.synchronizeSink = true;
    return configuration;
}

cuelet::SoundClip clipFor(
    const std::filesystem::path& path,
    std::string relativePath);

void testVirtualMicrophoneRoutingModesAndLevels(const TestDirectory& temporary)
{
    const auto wavPath = temporary.path() / "routing.wav";
    writeSilentWav(wavPath, 1.0);
    const auto clip = clipFor(wavPath, "routing.wav");
    LinuxAudioService audio(fakeSinkConfiguration());

    require(audio.routingConfiguration().mode ==
                LinuxAudioService::PlaybackRoutingMode::SpeakersOnly,
            "audio must start in speakers-only mode");
    require(audio.setRoutingConfiguration({
                LinuxAudioService::PlaybackRoutingMode::VirtualMicrophoneOnly,
                "cuelet.soundboard-input",
                0.5,
            }),
            "a stable virtual target must be accepted");
    require(audio.play(clip), "virtual-only playback must start with an injected sink");
    require(audio.activePlaybackBranchCount(clip.relativePath) == 1,
            "virtual-only playback must have exactly one output branch");
    require(audio.activeVirtualMicrophoneLevel(clip.relativePath).has_value() &&
                std::abs(*audio.activeVirtualMicrophoneLevel(clip.relativePath) - 0.5) <
                    0.000001,
            "the actual virtual playback pipeline must apply the configured level");
    audio.setVirtualMicrophoneLevel(0.3);
    require(audio.activeVirtualMicrophoneLevel(clip.relativePath).has_value() &&
                std::abs(*audio.activeVirtualMicrophoneLevel(clip.relativePath) - 0.3) <
                    0.000001,
            "live virtual level changes must reach the playback pipeline");
    require(!audio.setRoutingConfiguration({
                LinuxAudioService::PlaybackRoutingMode::SpeakersAndVirtualMicrophone,
                "cuelet.soundboard-input",
                0.3,
            }),
            "topology changes must not race active playback");
    audio.stopAll();

    require(audio.setRoutingConfiguration({
                LinuxAudioService::PlaybackRoutingMode::SpeakersAndVirtualMicrophone,
                "cuelet.soundboard-input",
                0.4,
            }),
            "dual routing must be accepted after playback stops");
    require(audio.play(clip), "dual-output playback must start");
    require(audio.activePlaybackBranchCount(clip.relativePath) == 2,
            "dual-output playback must isolate two output branches");
    require(audio.activeVirtualMicrophoneLevel(clip.relativePath).has_value() &&
                std::abs(*audio.activeVirtualMicrophoneLevel(clip.relativePath) - 0.4) <
                    0.000001,
            "dual-output playback must apply its virtual level independently");
    require(audio.pause(clip.relativePath) && audio.resume(clip.relativePath),
            "pause and resume must update both dual-output players");
    audio.setVolume(0.6);
    audio.setVirtualMicrophoneLevel(0.25);
    audio.stopAll();

    require(!audio.setRoutingConfiguration({
                LinuxAudioService::PlaybackRoutingMode::VirtualMicrophoneOnly,
                {},
                0.5,
            }),
            "virtual routing must reject an empty exact target");
    require(audio.setRoutingConfiguration({}),
            "speakers-only must disable virtual routing without a target");
}

void pumpMainContext()
{
    while (g_main_context_iteration(nullptr, false)) {
    }
}

bool waitUntil(const std::function<bool()>& predicate,
               std::chrono::milliseconds timeout = 2s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        pumpMainContext();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    pumpMainContext();
    return predicate();
}

cuelet::SoundClip clipFor(const std::filesystem::path& path, std::string relativePath)
{
    cuelet::SoundClip clip;
    clip.id = relativePath;
    clip.absolutePath = path.string();
    clip.relativePath = std::move(relativePath);
    clip.filename = path.filename().string();
    clip.displayName = path.stem().string();
    return clip;
}

void testMissingAndMalformedFiles(const TestDirectory& temporary)
{
    LinuxAudioService audio(fakeSinkConfiguration());
    std::vector<std::string> errors;
    audio.setErrorCallback([&](const std::string& message) {
        errors.push_back(message);
    });

    cuelet::SoundClip markedMissing;
    markedMissing.relativePath = "marked-missing.wav";
    markedMissing.missing = true;
    require(!audio.play(markedMissing), "a clip marked missing must not play");
    require(!errors.empty(), "a missing clip must report a useful error");
    require(!audio.playbackProgress(markedMissing.relativePath),
            "a missing clip must not expose playback progress");

    const auto absent = clipFor(temporary.path() / "absent.wav", "absent.wav");
    require(!audio.play(absent), "a nonexistent file must fail before starting playback");
    require(audio.playbackState(absent.relativePath) ==
                LinuxAudioService::PlaybackState::Stopped,
            "a nonexistent file must remain stopped");

    const auto malformedPath = temporary.path() / "malformed.wav";
    {
        std::ofstream malformed(malformedPath, std::ios::binary);
        malformed << "not a WAV";
    }
    const auto malformed = clipFor(malformedPath, "malformed.wav");
    const auto errorCount = errors.size();
    if (audio.play(malformed)) {
        require(waitUntil([&] {
            return audio.playbackState(malformed.relativePath) ==
                LinuxAudioService::PlaybackState::Stopped;
        }), "a malformed file must be cleaned up after GStreamer reports its error");
    } else {
        require(audio.playbackState(malformed.relativePath) ==
                    LinuxAudioService::PlaybackState::Stopped,
                "a synchronously rejected malformed file must remain stopped");
    }
    require(errors.size() > errorCount, "a malformed file must report its decoder error");
}

void testSymlinkMediaIsRejectedAndActivePlaybackKeepsItsFile(
    const TestDirectory& temporary)
{
    const auto source = temporary.path() / "stable-source.wav";
    const auto replacement = temporary.path() / "replacement.wav";
    const auto symlink = temporary.path() / "linked-source.wav";
    writeSilentWav(source, 1.0);
    writeSilentWav(replacement, 0.2);

    std::error_code error;
    std::filesystem::create_symlink(replacement, symlink, error);
    require(!error, "the playback symlink fixture must be creatable");

    LinuxAudioService audio(fakeSinkConfiguration());
    std::vector<std::string> errors;
    audio.setErrorCallback([&](const std::string& message) {
        errors.push_back(message);
    });
    const auto symlinkClip = clipFor(symlink, "linked-source.wav");
    require(!audio.play(symlinkClip),
            "playback must reject a path whose final component is a symlink");
    require(!LinuxAudioService::durationFingerprint(symlink.string()),
            "duration fingerprinting must reject symlinks");
    require(LinuxAudioService::discoverDurationSeconds(symlink.string()) == 0.0,
            "duration discovery must not follow a symlink");

    const auto fifo = temporary.path() / "media-fifo.wav";
    require(::mkfifo(fifo.c_str(), 0600) == 0,
            "the non-regular media fixture must be creatable");
    const auto fifoClip = clipFor(fifo, "media-fifo.wav");
    require(!audio.play(fifoClip),
            "playback must reject a FIFO without blocking");
    require(!LinuxAudioService::durationFingerprint(fifo.string()),
            "duration fingerprinting must reject a FIFO without blocking");
    require(LinuxAudioService::discoverDurationSeconds(fifo.string()) == 0.0,
            "duration discovery must reject a FIFO without blocking");

    const auto stableClip = clipFor(source, "stable-source.wav");
    require(audio.play(stableClip), "the stable fixture must begin playback");
    require(std::filesystem::remove(source, error) && !error,
            "the active source path must be replaceable for the race fixture");
    std::filesystem::create_symlink(replacement, source, error);
    require(!error, "the active source must be replaceable by a symlink");
    require(waitUntil([&] {
        const auto progress = audio.playbackProgress(stableClip.relativePath);
        return progress && progress->positionSeconds > 0.04
            && progress->durationSeconds > 0.9;
    }), "active playback must remain bound to the safely opened original file");
    audio.stopAll();
}

void testDurationDiscoveryAndFingerprint(const TestDirectory& temporary)
{
    const auto wavPath = temporary.path() / "duration.wav";
    writeSilentWav(wavPath, 0.4);

    const double duration = LinuxAudioService::discoverDurationSeconds(wavPath.string());
    require(duration > 0.35 && duration < 0.45,
            "duration discovery must read the generated WAV duration");

    const auto fingerprint = LinuxAudioService::durationFingerprint(wavPath.string());
    require(fingerprint.has_value(), "a readable sound must have a duration fingerprint");
    require(fingerprint->sourcePath == wavPath.string(),
            "the duration fingerprint must retain the exact source path");
    require(fingerprint->fileSize == std::filesystem::file_size(wavPath),
            "the duration fingerprint must include file size");

    auto clip = clipFor(wavPath, "duration.wav");
    require(LinuxAudioService::updateDurationMetadata(clip),
            "duration metadata indexing must succeed for a supported WAV");
    require(clip.durationKnown && clip.durationSeconds > 0.35 &&
                clip.durationSeconds < 0.45,
            "duration indexing must populate the known duration");
    require(LinuxAudioService::durationCacheIsValid(clip, *fingerprint),
            "fresh duration metadata must match its file fingerprint");
    auto wrongPath = *fingerprint;
    wrongPath.sourcePath += ".moved";
    require(!LinuxAudioService::durationCacheIsValid(clip, wrongPath),
            "duration cache validity must include the source path");
    auto wrongTimestamp = *fingerprint;
    ++wrongTimestamp.modifiedSeconds;
    require(!LinuxAudioService::durationCacheIsValid(clip, wrongTimestamp),
            "duration cache validity must include the modification timestamp");

    {
        std::ofstream append(wavPath, std::ios::binary | std::ios::app);
        append.put('\0');
    }
    const auto changedFingerprint = LinuxAudioService::durationFingerprint(wavPath.string());
    require(changedFingerprint.has_value(),
            "a modified sound must still have a duration fingerprint");
    require(!LinuxAudioService::durationCacheIsValid(clip, *changedFingerprint),
            "duration cache validity must detect a changed file size");

    auto absent = clipFor(temporary.path() / "no-duration.wav", "no-duration.wav");
    require(!LinuxAudioService::updateDurationMetadata(absent),
            "duration indexing must fail safely for a missing file");
    require(!absent.durationKnown && absent.durationSourcePath.empty(),
            "failed duration indexing must clear stale cache identity");
}

void testRenamedFileDurationMetadataCanBeRefreshed(const TestDirectory& temporary)
{
    const auto oldPath = temporary.path() / "before-rename.wav";
    const auto newPath = temporary.path() / "after-rename.wav";
    writeSilentWav(oldPath, 0.3);

    auto clip = clipFor(oldPath, "before-rename.wav");
    require(LinuxAudioService::updateDurationMetadata(clip) && clip.durationKnown,
            "the pre-rename fixture must have indexed duration metadata");

    std::error_code error;
    std::filesystem::rename(oldPath, newPath, error);
    require(!error, "the duration regression fixture must be renameable");
    clip.absolutePath = newPath.string();
    clip.relativePath = "after-rename.wav";
    clip.filename = newPath.filename().string();
    clip.durationKnown = false;
    clip.durationSeconds = 0.0;
    clip.durationFileSize = 0;
    clip.durationModifiedSeconds = 0;
    clip.durationSourcePath.clear();

    require(LinuxAudioService::updateDurationMetadata(clip),
            "a successfully renamed sound must be re-indexable before persistence");
    require(clip.durationKnown
                && clip.durationSeconds > 0.25
                && clip.durationSeconds < 0.35
                && clip.durationSourcePath == newPath.string(),
            "post-rename duration metadata must identify and describe the new path");
    const auto fingerprint = LinuxAudioService::durationFingerprint(newPath.string());
    require(fingerprint.has_value()
                && LinuxAudioService::durationCacheIsValid(clip, *fingerprint),
            "post-rename duration metadata must be immediately cache-valid");
}

void testPauseResumeProgressAndStop(const TestDirectory& temporary)
{
    const auto wavPath = temporary.path() / "state.wav";
    writeSilentWav(wavPath, 1.0);
    const auto clip = clipFor(wavPath, "state.wav");

    LinuxAudioService audio(fakeSinkConfiguration());
    require(audio.play(clip), "a valid generated WAV must start");
    require(audio.playbackState(clip.relativePath) ==
                LinuxAudioService::PlaybackState::Playing,
            "play must expose the requested playing state");
    require(audio.isPlaying(clip.relativePath), "isPlaying must reflect the playing state");
    require(!audio.isPaused(clip.relativePath), "a new player must not be paused");

    require(waitUntil([&] {
        const auto progress = audio.playbackProgress(clip.relativePath);
        return progress && progress->positionSeconds > 0.04 &&
            progress->durationSeconds > 0.9;
    }), "playback must expose advancing position and decoded duration");

    require(audio.pause(clip.relativePath), "an active player must pause");
    require(!audio.pause(clip.relativePath), "an already-paused player must reject pause");
    require(audio.playbackState(clip.relativePath) ==
                LinuxAudioService::PlaybackState::Paused,
            "pause must expose the paused state");
    require(audio.isPlaying(clip.relativePath) && audio.isPaused(clip.relativePath),
            "a paused player must remain an active player for API compatibility");

    const auto pausedProgress = audio.playbackProgress(clip.relativePath);
    require(pausedProgress.has_value(), "a paused player must retain progress");
    std::this_thread::sleep_for(100ms);
    pumpMainContext();
    const auto stillPaused = audio.playbackProgress(clip.relativePath);
    require(stillPaused.has_value(), "a paused player must remain queryable");
    require(std::abs(stillPaused->positionSeconds - pausedProgress->positionSeconds) < 0.04,
            "position must not advance materially while paused");

    require(audio.resume(clip.relativePath), "a paused player must resume");
    require(!audio.resume(clip.relativePath), "an already-playing player must reject resume");
    require(audio.playbackState(clip.relativePath) ==
                LinuxAudioService::PlaybackState::Playing,
            "resume must restore the playing state");
    require(waitUntil([&] {
        const auto resumed = audio.playbackProgress(clip.relativePath);
        return resumed && resumed->positionSeconds > stillPaused->positionSeconds + 0.04;
    }), "position must advance after resume");

    audio.stop(clip.relativePath);
    require(audio.playbackState(clip.relativePath) ==
                LinuxAudioService::PlaybackState::Stopped,
            "stop must remove the player");
    require(!audio.playbackProgress(clip.relativePath),
            "a stopped player must not expose stale progress");
    require(audio.playingPaths().empty(), "stop must remove the active path");
    require(!audio.pause(clip.relativePath) && !audio.resume(clip.relativePath),
            "pause and resume must reject a stopped path");
}

void testCompletionSimultaneousPolicyAndCleanup(const TestDirectory& temporary)
{
    const auto shortPath = temporary.path() / "short.wav";
    const auto longPath = temporary.path() / "long.wav";
    writeSilentWav(shortPath, 0.12);
    writeSilentWav(longPath, 1.0);
    const auto shortClip = clipFor(shortPath, "short.wav");
    const auto longClip = clipFor(longPath, "long.wav");

    LinuxAudioService audio(fakeSinkConfiguration());
    std::vector<std::string> finished;
    audio.setFinishCallback([&](const std::string& path) {
        finished.push_back(path);
    });
    require(audio.play(shortClip), "the short fixture must start");
    require(waitUntil([&] {
        return !audio.isPlaying(shortClip.relativePath);
    }), "end-of-stream must clean up the completed player");
    require(finished.size() == 1 && finished.front() == shortClip.relativePath,
            "end-of-stream must identify the completed sound once");

    audio.setAllowsSimultaneousPlayback(false);
    require(audio.play(longClip), "the long fixture must start");
    require(audio.play(shortClip), "the replacement fixture must start");
    require(audio.playbackState(longClip.relativePath) ==
                LinuxAudioService::PlaybackState::Stopped,
            "single-playback mode must stop the prior sound");
    require(audio.playbackState(shortClip.relativePath) ==
                LinuxAudioService::PlaybackState::Playing,
            "single-playback mode must retain the replacement sound");
    audio.stopAll();
    require(audio.playingPaths().empty(), "stopAll must clean up every player");
    audio.stopAll();
    require(audio.playingPaths().empty(), "repeated stopAll calls must remain safe");

    {
        LinuxAudioService scoped(fakeSinkConfiguration());
        require(scoped.play(longClip), "the destructor-cleanup fixture must start");
    }
    pumpMainContext();
}

void testOutputSelectionValidation()
{
    LinuxAudioService audio(fakeSinkConfiguration());
    std::vector<std::string> errors;
    audio.setErrorCallback([&](const std::string& message) {
        errors.push_back(message);
    });

    require(audio.outputSelection().backend ==
                LinuxAudioService::OutputBackend::Automatic,
            "playback must default to the desktop-selected output");
    require(!audio.setOutputSelection({
                LinuxAudioService::OutputBackend::Automatic,
                "unexpected-device",
            }),
            "automatic output must reject an ambiguous device identifier");
    require(!errors.empty(), "invalid output selection must report its reason");

    require(audio.setOutputSelection({
                LinuxAudioService::OutputBackend::Automatic,
                {},
            }),
            "the automatic output selection must always be available");
    require(audio.outputSelection().deviceId.empty(),
            "automatic output must not retain a device identifier");

    for (const auto backend : {
             LinuxAudioService::OutputBackend::PipeWire,
             LinuxAudioService::OutputBackend::PulseAudio,
         }) {
        if (!LinuxAudioService::outputBackendAvailable(backend)) {
            continue;
        }
        LinuxAudioService selectable;
        require(selectable.setOutputSelection({backend, "cuelet-test-device"}),
                "an available explicit output backend must accept a device identifier");
        require(selectable.outputSelection().backend == backend &&
                    selectable.outputSelection().deviceId == "cuelet-test-device",
                "explicit output selection must retain its backend and device");
    }
}

} // namespace

int main(int argc, char** argv)
{
    gst_init(&argc, &argv);

    try {
        TestDirectory temporary;
        testMissingAndMalformedFiles(temporary);
        testSymlinkMediaIsRejectedAndActivePlaybackKeepsItsFile(temporary);
        testDurationDiscoveryAndFingerprint(temporary);
        testRenamedFileDurationMetadataCanBeRefreshed(temporary);
        testPauseResumeProgressAndStop(temporary);
        testCompletionSimultaneousPolicyAndCleanup(temporary);
        testOutputSelectionValidation();
        testVirtualMicrophoneRoutingModesAndLevels(temporary);
    } catch (const std::exception& error) {
        std::cerr << "cuelet audio service tests failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "cuelet audio service tests passed\n";
    return 0;
}
