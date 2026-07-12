#include "services/LinuxAudioService.h"

#include <gst/pbutils/pbutils.h>

#include <algorithm>

namespace {

std::string uriForPath(const std::string& path)
{
    GError* error = nullptr;
    char* uri = g_filename_to_uri(path.c_str(), nullptr, &error);
    if (!uri) {
        if (error) {
            g_error_free(error);
        }
        return {};
    }

    std::string result = uri;
    g_free(uri);
    return result;
}

} // namespace

LinuxAudioService::LinuxAudioService() = default;

LinuxAudioService::~LinuxAudioService()
{
    stopAll();
}

void LinuxAudioService::setFinishCallback(FinishCallback callback)
{
    finishCallback_ = std::move(callback);
}

void LinuxAudioService::setErrorCallback(ErrorCallback callback)
{
    errorCallback_ = std::move(callback);
}

void LinuxAudioService::setVolume(double volume)
{
    volume_ = std::clamp(volume, 0.0, 1.0);
    for (const auto& [path, player] : players_) {
        g_object_set(player.element, "volume", volume_, nullptr);
    }
}

void LinuxAudioService::setAllowsSimultaneousPlayback(bool allows)
{
    allowsSimultaneousPlayback_ = allows;
}

bool LinuxAudioService::play(const cuelet::SoundClip& clip)
{
    if (clip.absolutePath.empty() || clip.missing) {
        reportError("This sound is missing from disk.");
        return false;
    }

    if (!allowsSimultaneousPlayback_) {
        stopAll();
    } else if (isPlaying(clip.relativePath)) {
        stop(clip.relativePath);
    }

    GstElement* playbin = gst_element_factory_make("playbin", nullptr);
    if (!playbin) {
        reportError("GStreamer could not create a playbin element.");
        return false;
    }

    const auto uri = uriForPath(clip.absolutePath);
    if (uri.empty()) {
        gst_object_unref(playbin);
        reportError("Could not create a file URI for playback.");
        return false;
    }

    g_object_set(playbin, "uri", uri.c_str(), "volume", volume_, nullptr);
    GstBus* bus = gst_element_get_bus(playbin);
    const guint watchId = gst_bus_add_watch(bus, &LinuxAudioService::onBusMessage, this);
    gst_object_unref(bus);
    g_object_set_data_full(G_OBJECT(playbin), "cuelet-relative-path", g_strdup(clip.relativePath.c_str()), g_free);

    const auto stateChange = gst_element_set_state(playbin, GST_STATE_PLAYING);
    if (stateChange == GST_STATE_CHANGE_FAILURE) {
        if (watchId != 0) {
            g_source_remove(watchId);
        }
        gst_object_unref(playbin);
        reportError("GStreamer could not start playback.");
        return false;
    }

    players_[clip.relativePath] = Player{playbin, watchId};
    return true;
}

void LinuxAudioService::stop(const std::string& relativePath)
{
    auto found = players_.find(relativePath);
    if (found == players_.end()) {
        return;
    }

    if (found->second.busWatchId != 0) {
        g_source_remove(found->second.busWatchId);
    }
    gst_element_set_state(found->second.element, GST_STATE_NULL);
    gst_object_unref(found->second.element);
    players_.erase(found);
}

void LinuxAudioService::stopAll()
{
    auto paths = playingPaths();
    for (const auto& path : paths) {
        stop(path);
    }
}

bool LinuxAudioService::isPlaying(const std::string& relativePath) const
{
    return players_.count(relativePath) > 0;
}

std::vector<std::string> LinuxAudioService::playingPaths() const
{
    std::vector<std::string> paths;
    paths.reserve(players_.size());
    for (const auto& [path, player] : players_) {
        paths.push_back(path);
    }
    return paths;
}

std::optional<LinuxAudioService::PlaybackProgress> LinuxAudioService::playbackProgress(
    const std::string& relativePath) const
{
    const auto found = players_.find(relativePath);
    if (found == players_.end() || !found->second.element) {
        return std::nullopt;
    }

    gint64 position = 0;
    gint64 duration = 0;
    if (!gst_element_query_position(found->second.element, GST_FORMAT_TIME, &position)) {
        position = 0;
    }
    if (!gst_element_query_duration(found->second.element, GST_FORMAT_TIME, &duration)) {
        duration = 0;
    }

    return PlaybackProgress{
        std::max(0.0, static_cast<double>(position) / static_cast<double>(GST_SECOND)),
        std::max(0.0, static_cast<double>(duration) / static_cast<double>(GST_SECOND)),
    };
}

double LinuxAudioService::discoverDurationSeconds(const std::string& absolutePath)
{
    const auto uri = uriForPath(absolutePath);
    if (uri.empty()) {
        return 0.0;
    }

    GError* error = nullptr;
    GstDiscoverer* discoverer = gst_discoverer_new(2 * GST_SECOND, &error);
    if (!discoverer) {
        if (error) {
            g_error_free(error);
        }
        return 0.0;
    }

    GstDiscovererInfo* info = gst_discoverer_discover_uri(discoverer, uri.c_str(), &error);
    if (!info) {
        if (error) {
            g_error_free(error);
        }
        g_object_unref(discoverer);
        return 0.0;
    }

    const GstClockTime duration = gst_discoverer_info_get_duration(info);
    gst_discoverer_info_unref(info);
    g_object_unref(discoverer);

    if (!GST_CLOCK_TIME_IS_VALID(duration)) {
        return 0.0;
    }
    return static_cast<double>(duration) / static_cast<double>(GST_SECOND);
}

gboolean LinuxAudioService::onBusMessage(GstBus*, GstMessage* message, gpointer userData)
{
    auto* self = static_cast<LinuxAudioService*>(userData);
    GstObject* source = GST_MESSAGE_SRC(message);
    const auto player = std::find_if(self->players_.begin(), self->players_.end(), [&](const auto& item) {
        GstObject* pipeline = GST_OBJECT(item.second.element);
        return source == pipeline || gst_object_has_as_ancestor(source, pipeline);
    });
    if (player == self->players_.end()) {
        return G_SOURCE_CONTINUE;
    }
    const std::string path = player->first;

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_EOS:
        self->finishPath(path);
        return G_SOURCE_REMOVE;
    case GST_MESSAGE_ERROR: {
        GError* error = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(message, &error, &debug);
        std::string text = error && error->message ? error->message : "GStreamer playback failed.";
        if (debug) {
            g_free(debug);
        }
        if (error) {
            g_error_free(error);
        }
        self->finishPath(path);
        self->reportError(text);
        return G_SOURCE_REMOVE;
    }
    default:
        return G_SOURCE_CONTINUE;
    }
}

void LinuxAudioService::finishPath(const std::string& relativePath)
{
    auto found = players_.find(relativePath);
    if (found == players_.end()) {
        return;
    }

    found->second.busWatchId = 0;
    gst_element_set_state(found->second.element, GST_STATE_NULL);
    gst_object_unref(found->second.element);
    players_.erase(found);

    if (finishCallback_) {
        finishCallback_(relativePath);
    }
}

void LinuxAudioService::reportError(const std::string& message)
{
    if (errorCallback_) {
        errorCallback_(message);
    }
}
