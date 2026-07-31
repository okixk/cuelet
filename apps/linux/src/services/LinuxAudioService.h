#pragma once

#include "cuelet/SoundTypes.h"

#include <gst/gst.h>

#include <cstdint>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

class LinuxAudioService {
public:
    enum class PlaybackState {
        Stopped,
        Playing,
        Paused,
    };

    enum class OutputBackend {
        Automatic,
        PipeWire,
        PulseAudio,
    };

    enum class PlaybackRoutingMode {
        SpeakersOnly,
        VirtualMicrophoneOnly,
        SpeakersAndVirtualMicrophone,
    };

    struct PlaybackProgress {
        double positionSeconds = 0.0;
        double durationSeconds = 0.0;
    };

    struct DurationFingerprint {
        std::string sourcePath;
        std::uint64_t fileSize = 0;
        std::int64_t modifiedSeconds = 0;
    };

    struct OutputSelection {
        OutputBackend backend = OutputBackend::Automatic;
        std::string deviceId;
    };

    struct RoutingConfiguration {
        PlaybackRoutingMode mode = PlaybackRoutingMode::SpeakersOnly;
        std::string virtualSinkNode;
        double virtualMicrophoneLevel = 0.25;
    };

    struct Configuration {
        // Primarily intended for deterministic tests. An empty value lets
        // playbin use the desktop's normal auto-selected audio sink.
        std::string audioSinkFactoryName;
        std::string virtualAudioSinkFactoryName;
        bool synchronizeSink = true;
    };

    using FinishCallback = std::function<void(const std::string&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    LinuxAudioService();
    explicit LinuxAudioService(Configuration configuration);
    ~LinuxAudioService();

    LinuxAudioService(const LinuxAudioService&) = delete;
    LinuxAudioService& operator=(const LinuxAudioService&) = delete;
    LinuxAudioService(LinuxAudioService&&) = delete;
    LinuxAudioService& operator=(LinuxAudioService&&) = delete;

    void setFinishCallback(FinishCallback callback);
    void setErrorCallback(ErrorCallback callback);
    void setVolume(double volume);
    void setAllowsSimultaneousPlayback(bool allows);
    bool setOutputSelection(OutputSelection selection);
    const OutputSelection& outputSelection() const;
    static bool outputBackendAvailable(OutputBackend backend);
    bool setRoutingConfiguration(RoutingConfiguration configuration);
    const RoutingConfiguration& routingConfiguration() const;
    void setVirtualMicrophoneLevel(double level);

    bool play(const cuelet::SoundClip& clip);
    bool pause(const std::string& relativePath);
    bool resume(const std::string& relativePath);
    void stop(const std::string& relativePath);
    void stopAll();
    bool isPlaying(const std::string& relativePath) const;
    bool isPaused(const std::string& relativePath) const;
    PlaybackState playbackState(const std::string& relativePath) const;
    std::vector<std::string> playingPaths() const;
    std::optional<PlaybackProgress> playbackProgress(const std::string& relativePath) const;
    std::size_t activePlaybackBranchCount(const std::string& relativePath) const;
    std::optional<double> activeVirtualMicrophoneLevel(
        const std::string& relativePath) const;

    static double discoverDurationSeconds(const std::string& absolutePath);
    static std::optional<DurationFingerprint> durationFingerprint(
        const std::string& absolutePath);
    static bool durationCacheIsValid(
        const cuelet::SoundClip& clip,
        const DurationFingerprint& fingerprint);
    static bool updateDurationMetadata(cuelet::SoundClip& clip);

private:
    struct Player {
        GstElement* element = nullptr;
        guint busWatchId = 0;
        GstElement* secondaryElement = nullptr;
        guint secondaryBusWatchId = 0;
        PlaybackState state = PlaybackState::Stopped;
        int sourceFd = -1;
        int secondarySourceFd = -1;
        GstElement* speakerVolume = nullptr;
        GstElement* virtualVolume = nullptr;
        std::size_t branchCount = 1;
    };

    static gboolean onBusMessage(GstBus* bus, GstMessage* message, gpointer userData);
    static const char* outputSinkFactory(OutputBackend backend);
    static const char* outputDeviceProperty(OutputBackend backend);

    GstElement* createAudioSink(bool forceAutomatic = false);
    GstElement* createVirtualAudioSink();
    GstElement* createPlayerElement(
        const cuelet::SoundClip& clip,
        int& sourceFd,
        PlaybackRoutingMode mode);
    bool changePlaybackState(
        const std::string& relativePath,
        PlaybackState expected,
        PlaybackState requested,
        GstState gstState);
    void finishPath(const std::string& relativePath);
    void reportError(const std::string& message);

    std::map<std::string, Player> players_;
    Configuration configuration_;
    OutputSelection outputSelection_;
    RoutingConfiguration routingConfiguration_;
    double volume_ = 0.8;
    bool allowsSimultaneousPlayback_ = true;
    FinishCallback finishCallback_;
    ErrorCallback errorCallback_;
};
