#pragma once

#include "cuelet/SoundTypes.h"

#include <gst/gst.h>

#include <functional>
#include <map>
#include <optional>
#include <string>

class LinuxAudioService {
public:
    struct PlaybackProgress {
        double positionSeconds = 0.0;
        double durationSeconds = 0.0;
    };

    using FinishCallback = std::function<void(const std::string&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    LinuxAudioService();
    ~LinuxAudioService();

    void setFinishCallback(FinishCallback callback);
    void setErrorCallback(ErrorCallback callback);
    void setVolume(double volume);
    void setAllowsSimultaneousPlayback(bool allows);

    bool play(const cuelet::SoundClip& clip);
    void stop(const std::string& relativePath);
    void stopAll();
    bool isPlaying(const std::string& relativePath) const;
    std::vector<std::string> playingPaths() const;
    std::optional<PlaybackProgress> playbackProgress(const std::string& relativePath) const;

    static double discoverDurationSeconds(const std::string& absolutePath);

private:
    struct Player {
        GstElement* element = nullptr;
        guint busWatchId = 0;
    };

    static gboolean onBusMessage(GstBus* bus, GstMessage* message, gpointer userData);

    void finishPath(const std::string& relativePath);
    void reportError(const std::string& message);

    std::map<std::string, Player> players_;
    double volume_ = 0.8;
    bool allowsSimultaneousPlayback_ = true;
    FinishCallback finishCallback_;
    ErrorCallback errorCallback_;
};
