#include "services/LinuxAudioService.h"

#include <gst/pbutils/pbutils.h>

#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

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

class MediaFile {
public:
    static std::optional<MediaFile> open(const std::string& path)
    {
        const int descriptor = ::open(
            path.c_str(),
            O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (descriptor < 0) {
            return std::nullopt;
        }

        struct stat status {};
        if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode)) {
            ::close(descriptor);
            return std::nullopt;
        }
        return MediaFile(descriptor, status);
    }

    MediaFile(MediaFile&& other) noexcept
        : descriptor_(other.descriptor_)
        , status_(other.status_)
    {
        other.descriptor_ = -1;
    }

    ~MediaFile()
    {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
    }

    MediaFile(const MediaFile&) = delete;
    MediaFile& operator=(const MediaFile&) = delete;
    MediaFile& operator=(MediaFile&&) = delete;

    int descriptor() const { return descriptor_; }
    const struct stat& status() const { return status_; }

    int release()
    {
        const int descriptor = descriptor_;
        descriptor_ = -1;
        return descriptor;
    }

    std::string procPath() const
    {
        return "/proc/self/fd/" + std::to_string(descriptor_);
    }

private:
    MediaFile(int descriptor, const struct stat& status)
        : descriptor_(descriptor)
        , status_(status)
    {
    }

    int descriptor_ = -1;
    struct stat status_ {};
};

double discoverDurationForOpenFile(const MediaFile& file)
{
    const auto uri = uriForPath(file.procPath());
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

    GstDiscovererInfo* info =
        gst_discoverer_discover_uri(discoverer, uri.c_str(), &error);
    if (!info) {
        if (error) {
            g_error_free(error);
        }
        g_object_unref(discoverer);
        return 0.0;
    }

    if (error) {
        g_error_free(error);
    }
    const auto result = gst_discoverer_info_get_result(info);
    const GstClockTime duration = gst_discoverer_info_get_duration(info);
    gst_discoverer_info_unref(info);
    g_object_unref(discoverer);

    if (result != GST_DISCOVERER_OK || !GST_CLOCK_TIME_IS_VALID(duration)) {
        return 0.0;
    }
    return static_cast<double>(duration) / static_cast<double>(GST_SECOND);
}

LinuxAudioService::DurationFingerprint fingerprintForOpenFile(
    const std::string& sourcePath,
    const MediaFile& file)
{
    return LinuxAudioService::DurationFingerprint{
        sourcePath,
        static_cast<std::uint64_t>(file.status().st_size),
        static_cast<std::int64_t>(file.status().st_mtim.tv_sec),
    };
}

bool hasWritableProperty(GstElement* element, const char* propertyName, GType propertyType)
{
    if (!element || !propertyName) {
        return false;
    }

    const GParamSpec* property = g_object_class_find_property(
        G_OBJECT_GET_CLASS(element), propertyName);
    return property &&
        (property->flags & G_PARAM_WRITABLE) != 0 &&
        G_PARAM_SPEC_VALUE_TYPE(property) == propertyType;
}

} // namespace

LinuxAudioService::LinuxAudioService()
    : LinuxAudioService(Configuration{})
{
}

LinuxAudioService::LinuxAudioService(Configuration configuration)
    : configuration_(std::move(configuration))
{
}

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
        if (player.speakerVolume) {
            g_object_set(player.speakerVolume, "volume", volume_, nullptr);
        } else if (routingConfiguration_.mode == PlaybackRoutingMode::SpeakersOnly) {
            g_object_set(player.element, "volume", volume_, nullptr);
        }
    }
}

bool LinuxAudioService::setRoutingConfiguration(
    RoutingConfiguration configuration)
{
    configuration.virtualMicrophoneLevel = std::isfinite(
        configuration.virtualMicrophoneLevel)
        ? std::clamp(configuration.virtualMicrophoneLevel, 0.0, 1.0)
        : 0.25;
    if (configuration.mode != PlaybackRoutingMode::SpeakersOnly &&
        configuration.virtualSinkNode.empty()) {
        reportError("Virtual microphone routing requires an exact PipeWire sink target.");
        return false;
    }
    if (configuration.mode == routingConfiguration_.mode &&
        configuration.virtualSinkNode == routingConfiguration_.virtualSinkNode) {
        setVirtualMicrophoneLevel(configuration.virtualMicrophoneLevel);
        return true;
    }
    if (!players_.empty()) {
        reportError("Stop playback before changing virtual microphone routing.");
        return false;
    }
    routingConfiguration_ = std::move(configuration);
    return true;
}

const LinuxAudioService::RoutingConfiguration&
LinuxAudioService::routingConfiguration() const
{
    return routingConfiguration_;
}

void LinuxAudioService::setVirtualMicrophoneLevel(double level)
{
    routingConfiguration_.virtualMicrophoneLevel = std::isfinite(level)
        ? std::clamp(level, 0.0, 1.0)
        : 0.25;
    for (const auto& [path, player] : players_) {
        if (player.virtualVolume) {
            g_object_set(
                player.virtualVolume,
                "volume",
                routingConfiguration_.virtualMicrophoneLevel,
                nullptr);
        }
    }
}

void LinuxAudioService::setAllowsSimultaneousPlayback(bool allows)
{
    allowsSimultaneousPlayback_ = allows;
}

bool LinuxAudioService::setOutputSelection(OutputSelection selection)
{
    if (selection.backend == OutputBackend::Automatic) {
        if (!selection.deviceId.empty()) {
            reportError("Automatic output selection cannot use a device identifier.");
            return false;
        }
    } else {
        if (selection.deviceId.empty()) {
            reportError("Select a valid audio output device.");
            return false;
        }
        if (!configuration_.audioSinkFactoryName.empty()) {
            reportError("A fixed audio sink cannot also select a desktop output device.");
            return false;
        }
        if (!outputBackendAvailable(selection.backend)) {
            reportError(
                selection.backend == OutputBackend::PipeWire
                    ? "The GStreamer PipeWire output plugin is unavailable."
                    : "The GStreamer PulseAudio output plugin is unavailable.");
            return false;
        }
    }

    if (!players_.empty()) {
        reportError("Stop playback before changing the audio output.");
        return false;
    }

    outputSelection_ = std::move(selection);
    return true;
}

const LinuxAudioService::OutputSelection& LinuxAudioService::outputSelection() const
{
    return outputSelection_;
}

bool LinuxAudioService::outputBackendAvailable(OutputBackend backend)
{
    if (backend == OutputBackend::Automatic) {
        return true;
    }

    const char* factoryName = outputSinkFactory(backend);
    const char* propertyName = outputDeviceProperty(backend);
    if (!factoryName || !propertyName) {
        return false;
    }

    GstElement* sink = gst_element_factory_make(factoryName, nullptr);
    if (!sink) {
        return false;
    }
    const bool available = hasWritableProperty(sink, propertyName, G_TYPE_STRING);
    gst_object_unref(sink);
    return available;
}

bool LinuxAudioService::play(const cuelet::SoundClip& clip)
{
    if (clip.absolutePath.empty() || clip.missing) {
        reportError("This sound is missing from disk.");
        return false;
    }

    if (clip.relativePath.empty()) {
        reportError("This sound does not have a valid library path.");
        return false;
    }

    const bool dualOutput =
        routingConfiguration_.mode ==
            PlaybackRoutingMode::SpeakersAndVirtualMicrophone;
    int sourceFd = -1;
    GstElement* playbin = createPlayerElement(
        clip,
        sourceFd,
        dualOutput
            ? PlaybackRoutingMode::SpeakersOnly
            : routingConfiguration_.mode);
    if (!playbin) {
        return false;
    }

    int secondarySourceFd = -1;
    GstElement* secondary = dualOutput
        ? createPlayerElement(
              clip,
              secondarySourceFd,
              PlaybackRoutingMode::VirtualMicrophoneOnly)
        : nullptr;
    if (dualOutput && !secondary) {
        gst_object_unref(playbin);
        ::close(sourceFd);
        return false;
    }

    GstBus* bus = gst_element_get_bus(playbin);
    if (!bus) {
        if (secondary) {
            gst_object_unref(secondary);
            ::close(secondarySourceFd);
        }
        gst_object_unref(playbin);
        ::close(sourceFd);
        reportError("GStreamer could not monitor playback.");
        return false;
    }
    const guint watchId = gst_bus_add_watch(bus, &LinuxAudioService::onBusMessage, this);
    gst_object_unref(bus);
    if (watchId == 0) {
        if (secondary) {
            gst_object_unref(secondary);
            ::close(secondarySourceFd);
        }
        gst_object_unref(playbin);
        ::close(sourceFd);
        reportError("GStreamer could not monitor playback.");
        return false;
    }
    guint secondaryWatchId = 0;
    if (secondary) {
        GstBus* secondaryBus = gst_element_get_bus(secondary);
        secondaryWatchId = secondaryBus
            ? gst_bus_add_watch(
                  secondaryBus, &LinuxAudioService::onBusMessage, this)
            : 0;
        if (secondaryBus) {
            gst_object_unref(secondaryBus);
        }
        if (secondaryWatchId == 0) {
            g_source_remove(watchId);
            gst_object_unref(secondary);
            gst_object_unref(playbin);
            ::close(secondarySourceFd);
            ::close(sourceFd);
            reportError("GStreamer could not monitor virtual microphone playback.");
            return false;
        }
    }

    if (!allowsSimultaneousPlayback_) {
        stopAll();
    } else if (players_.count(clip.relativePath) > 0) {
        stop(clip.relativePath);
    }

    const auto stateChange = gst_element_set_state(playbin, GST_STATE_PLAYING);
    const auto secondaryStateChange = secondary
        ? gst_element_set_state(secondary, GST_STATE_PLAYING)
        : GST_STATE_CHANGE_SUCCESS;
    if (stateChange == GST_STATE_CHANGE_FAILURE ||
        secondaryStateChange == GST_STATE_CHANGE_FAILURE) {
        g_source_remove(watchId);
        if (secondaryWatchId != 0) {
            g_source_remove(secondaryWatchId);
        }
        if (secondary) {
            gst_element_set_state(secondary, GST_STATE_NULL);
            gst_object_unref(secondary);
            ::close(secondarySourceFd);
        }
        gst_element_set_state(playbin, GST_STATE_NULL);
        gst_object_unref(playbin);
        ::close(sourceFd);
        reportError("GStreamer could not start playback.");
        return false;
    }
    GstElement* speakerVolume = nullptr;
    GstElement* virtualVolume = nullptr;
    std::size_t branchCount = 1;
    speakerVolume = static_cast<GstElement*>(
        g_object_get_data(G_OBJECT(playbin), "cuelet-speaker-volume"));
    if (dualOutput) {
        speakerVolume = playbin;
    }
    virtualVolume = static_cast<GstElement*>(
        g_object_get_data(
            G_OBJECT(secondary ? secondary : playbin),
            "cuelet-virtual-volume"));
    const auto storedBranchCount = reinterpret_cast<std::uintptr_t>(
        g_object_get_data(G_OBJECT(playbin), "cuelet-branch-count"));
    if (storedBranchCount > 0) {
        branchCount = storedBranchCount;
    }
    players_[clip.relativePath] = Player{
        playbin,
        watchId,
        secondary,
        secondaryWatchId,
        PlaybackState::Playing,
        sourceFd,
        secondarySourceFd,
        speakerVolume,
        virtualVolume,
        dualOutput ? 2U : branchCount,
    };
    return true;
}

bool LinuxAudioService::pause(const std::string& relativePath)
{
    return changePlaybackState(
        relativePath,
        PlaybackState::Playing,
        PlaybackState::Paused,
        GST_STATE_PAUSED);
}

bool LinuxAudioService::resume(const std::string& relativePath)
{
    return changePlaybackState(
        relativePath,
        PlaybackState::Paused,
        PlaybackState::Playing,
        GST_STATE_PLAYING);
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
    if (found->second.secondaryBusWatchId != 0) {
        g_source_remove(found->second.secondaryBusWatchId);
    }
    if (found->second.secondaryElement) {
        gst_element_set_state(found->second.secondaryElement, GST_STATE_NULL);
        gst_object_unref(found->second.secondaryElement);
    }
    gst_element_set_state(found->second.element, GST_STATE_NULL);
    gst_object_unref(found->second.element);
    if (found->second.sourceFd >= 0) {
        ::close(found->second.sourceFd);
    }
    if (found->second.secondarySourceFd >= 0) {
        ::close(found->second.secondarySourceFd);
    }
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

bool LinuxAudioService::isPaused(const std::string& relativePath) const
{
    return playbackState(relativePath) == PlaybackState::Paused;
}

LinuxAudioService::PlaybackState LinuxAudioService::playbackState(
    const std::string& relativePath) const
{
    const auto found = players_.find(relativePath);
    return found == players_.end() ? PlaybackState::Stopped : found->second.state;
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
        std::isfinite(static_cast<double>(position))
            ? std::max(0.0, static_cast<double>(position) /
                    static_cast<double>(GST_SECOND))
            : 0.0,
        std::isfinite(static_cast<double>(duration))
            ? std::max(0.0, static_cast<double>(duration) /
                    static_cast<double>(GST_SECOND))
            : 0.0,
    };
}

std::size_t LinuxAudioService::activePlaybackBranchCount(
    const std::string& relativePath) const
{
    const auto found = players_.find(relativePath);
    return found == players_.end() ? 0 : found->second.branchCount;
}

std::optional<double> LinuxAudioService::activeVirtualMicrophoneLevel(
    const std::string& relativePath) const
{
    const auto found = players_.find(relativePath);
    if (found == players_.end() || !found->second.virtualVolume) {
        return std::nullopt;
    }
    gdouble level = 1.0;
    g_object_get(found->second.virtualVolume, "volume", &level, nullptr);
    return static_cast<double>(level);
}

double LinuxAudioService::discoverDurationSeconds(const std::string& absolutePath)
{
    auto file = MediaFile::open(absolutePath);
    if (!file) {
        return 0.0;
    }
    return discoverDurationForOpenFile(*file);
}

std::optional<LinuxAudioService::DurationFingerprint>
LinuxAudioService::durationFingerprint(const std::string& absolutePath)
{
    if (absolutePath.empty()) {
        return std::nullopt;
    }

    const auto file = MediaFile::open(absolutePath);
    if (!file) {
        return std::nullopt;
    }
    return fingerprintForOpenFile(absolutePath, *file);
}

bool LinuxAudioService::durationCacheIsValid(
    const cuelet::SoundClip& clip,
    const DurationFingerprint& fingerprint)
{
    return clip.durationKnown &&
        clip.durationSourcePath == fingerprint.sourcePath &&
        clip.durationFileSize == fingerprint.fileSize &&
        clip.durationModifiedSeconds == fingerprint.modifiedSeconds;
}

bool LinuxAudioService::updateDurationMetadata(cuelet::SoundClip& clip)
{
    auto file = MediaFile::open(clip.absolutePath);
    if (!file) {
        clip.durationSeconds = 0.0;
        clip.durationKnown = false;
        clip.durationFileSize = 0;
        clip.durationModifiedSeconds = 0;
        clip.durationSourcePath.clear();
        return false;
    }
    const auto fingerprint = fingerprintForOpenFile(clip.absolutePath, *file);
    if (durationCacheIsValid(clip, fingerprint)) {
        return true;
    }

    const double duration = discoverDurationForOpenFile(*file);
    if (!std::isfinite(duration) || duration <= 0.0) {
        clip.durationSeconds = 0.0;
        clip.durationKnown = false;
        clip.durationFileSize = 0;
        clip.durationModifiedSeconds = 0;
        clip.durationSourcePath.clear();
        return false;
    }

    clip.durationSeconds = duration;
    clip.durationKnown = true;
    clip.durationFileSize = fingerprint.fileSize;
    clip.durationModifiedSeconds = fingerprint.modifiedSeconds;
    clip.durationSourcePath = fingerprint.sourcePath;
    return true;
}

gboolean LinuxAudioService::onBusMessage(GstBus*, GstMessage* message, gpointer userData)
{
    auto* self = static_cast<LinuxAudioService*>(userData);
    GstObject* source = GST_MESSAGE_SRC(message);
    const auto player = std::find_if(self->players_.begin(), self->players_.end(), [&](const auto& item) {
        GstObject* pipeline = GST_OBJECT(item.second.element);
        GstObject* secondary = item.second.secondaryElement
            ? GST_OBJECT(item.second.secondaryElement)
            : nullptr;
        return source == pipeline || gst_object_has_as_ancestor(source, pipeline) ||
            (secondary &&
             (source == secondary || gst_object_has_as_ancestor(source, secondary)));
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

const char* LinuxAudioService::outputSinkFactory(OutputBackend backend)
{
    switch (backend) {
    case OutputBackend::PipeWire:
        return "pipewiresink";
    case OutputBackend::PulseAudio:
        return "pulsesink";
    case OutputBackend::Automatic:
        return nullptr;
    }
    return nullptr;
}

const char* LinuxAudioService::outputDeviceProperty(OutputBackend backend)
{
    switch (backend) {
    case OutputBackend::PipeWire:
        return "target-object";
    case OutputBackend::PulseAudio:
        return "device";
    case OutputBackend::Automatic:
        return nullptr;
    }
    return nullptr;
}

GstElement* LinuxAudioService::createAudioSink(bool forceAutomatic)
{
    const char* factoryName = nullptr;
    const char* deviceProperty = nullptr;
    if (!configuration_.audioSinkFactoryName.empty()) {
        factoryName = configuration_.audioSinkFactoryName.c_str();
    } else if (outputSelection_.backend != OutputBackend::Automatic) {
        factoryName = outputSinkFactory(outputSelection_.backend);
        deviceProperty = outputDeviceProperty(outputSelection_.backend);
    } else if (!forceAutomatic) {
        return nullptr;
    } else {
        factoryName = "autoaudiosink";
    }

    GstElement* sink = factoryName
        ? gst_element_factory_make(factoryName, nullptr)
        : nullptr;
    if (!sink) {
        reportError("GStreamer could not create the selected audio output.");
        return nullptr;
    }
    gst_object_ref_sink(sink);

    if (hasWritableProperty(sink, "sync", G_TYPE_BOOLEAN)) {
        g_object_set(sink, "sync", configuration_.synchronizeSink, nullptr);
    }
    if (forceAutomatic && hasWritableProperty(sink, "async", G_TYPE_BOOLEAN)) {
        // A speaker branch must not hold the whole tee in preroll while the
        // independent virtual branch is already live.
        g_object_set(sink, "async", FALSE, nullptr);
    }
    if (deviceProperty) {
        if (!hasWritableProperty(sink, deviceProperty, G_TYPE_STRING)) {
            gst_object_unref(sink);
            reportError("The selected audio output does not support device routing.");
            return nullptr;
        }
        g_object_set(sink, deviceProperty, outputSelection_.deviceId.c_str(), nullptr);
    }
    return sink;
}

GstElement* LinuxAudioService::createVirtualAudioSink()
{
    const char* factoryName = configuration_.virtualAudioSinkFactoryName.empty()
        ? "pipewiresink"
        : configuration_.virtualAudioSinkFactoryName.c_str();
    GstElement* sink = gst_element_factory_make(factoryName, nullptr);
    if (!sink) {
        reportError("GStreamer could not create the virtual microphone output.");
        return nullptr;
    }
    gst_object_ref_sink(sink);
    if (hasWritableProperty(sink, "sync", G_TYPE_BOOLEAN)) {
        g_object_set(sink, "sync", configuration_.synchronizeSink, nullptr);
    }
    if (hasWritableProperty(sink, "async", G_TYPE_BOOLEAN)) {
        g_object_set(sink, "async", FALSE, nullptr);
    }
    if (configuration_.virtualAudioSinkFactoryName.empty()) {
        if (!hasWritableProperty(sink, "target-object", G_TYPE_STRING)) {
            gst_object_unref(sink);
            reportError("The PipeWire output cannot target Cuelet Virtual Microphone.");
            return nullptr;
        }
        GstStructure* properties = gst_structure_new(
            "props",
            "node.name", G_TYPE_STRING, "cuelet.soundboard-output",
            "node.description", G_TYPE_STRING, "Cuelet Soundboard Output",
            "media.role", G_TYPE_STRING, "Communication",
            "node.dont-reconnect", G_TYPE_BOOLEAN, TRUE,
            "node.dont-fallback", G_TYPE_BOOLEAN, TRUE,
            "state.restore-props", G_TYPE_BOOLEAN, FALSE,
            nullptr);
        g_object_set(
            sink,
            "target-object", routingConfiguration_.virtualSinkNode.c_str(),
            "stream-properties", properties,
            nullptr);
        gst_structure_free(properties);
    }
    return sink;
}

GstElement* LinuxAudioService::createPlayerElement(
    const cuelet::SoundClip& clip,
    int& sourceFd,
    PlaybackRoutingMode mode)
{
    sourceFd = -1;
    auto file = MediaFile::open(clip.absolutePath);
    if (!file) {
        reportError("This sound is missing or is not a safe regular file.");
        return nullptr;
    }

    GstElement* playbin = gst_element_factory_make("playbin", nullptr);
    if (!playbin) {
        reportError("GStreamer could not create a playbin element.");
        return nullptr;
    }
    gst_object_ref_sink(playbin);

    const auto uri = uriForPath(file->procPath());
    if (uri.empty()) {
        gst_object_unref(playbin);
        reportError("Could not create a file URI for playback.");
        return nullptr;
    }
    g_object_set(
        playbin,
        "uri", uri.c_str(),
        "volume",
        mode == PlaybackRoutingMode::SpeakersOnly
            ? volume_
            : routingConfiguration_.virtualMicrophoneLevel,
        nullptr);

    const bool virtualRouting =
        mode != PlaybackRoutingMode::SpeakersOnly;
    const bool needsExplicitSink = virtualRouting ||
        !configuration_.audioSinkFactoryName.empty() ||
        outputSelection_.backend != OutputBackend::Automatic;
    GstElement* audioSink = virtualRouting
        ? createVirtualAudioSink()
        : createAudioSink();
    if (needsExplicitSink && !audioSink) {
        gst_object_unref(playbin);
        return nullptr;
    }
    if (audioSink) {
        g_object_set(playbin, "audio-sink", audioSink, nullptr);
        gst_object_unref(audioSink);
    }
    g_object_set_data(
        G_OBJECT(playbin),
        "cuelet-speaker-volume",
        virtualRouting ? nullptr : playbin);
    g_object_set_data(
        G_OBJECT(playbin),
        "cuelet-virtual-volume",
        virtualRouting ? playbin : nullptr);
    g_object_set_data(
        G_OBJECT(playbin),
        "cuelet-branch-count",
        reinterpret_cast<gpointer>(static_cast<std::uintptr_t>(1)));
    sourceFd = file->release();
    return playbin;
}

bool LinuxAudioService::changePlaybackState(
    const std::string& relativePath,
    PlaybackState expected,
    PlaybackState requested,
    GstState gstState)
{
    auto found = players_.find(relativePath);
    if (found == players_.end() || found->second.state != expected) {
        return false;
    }

    const auto stateChange = gst_element_set_state(found->second.element, gstState);
    const auto secondaryStateChange = found->second.secondaryElement
        ? gst_element_set_state(found->second.secondaryElement, gstState)
        : GST_STATE_CHANGE_SUCCESS;
    if (stateChange == GST_STATE_CHANGE_FAILURE ||
        secondaryStateChange == GST_STATE_CHANGE_FAILURE) {
        reportError(
            requested == PlaybackState::Paused
                ? "GStreamer could not pause playback."
                : "GStreamer could not resume playback.");
        return false;
    }
    found->second.state = requested;
    return true;
}

void LinuxAudioService::finishPath(const std::string& relativePath)
{
    auto found = players_.find(relativePath);
    if (found == players_.end()) {
        return;
    }

    if (found->second.busWatchId != 0) {
        g_source_remove(found->second.busWatchId);
    }
    found->second.busWatchId = 0;
    if (found->second.secondaryBusWatchId != 0) {
        g_source_remove(found->second.secondaryBusWatchId);
    }
    found->second.secondaryBusWatchId = 0;
    if (found->second.secondaryElement) {
        gst_element_set_state(found->second.secondaryElement, GST_STATE_NULL);
        gst_object_unref(found->second.secondaryElement);
    }
    gst_element_set_state(found->second.element, GST_STATE_NULL);
    gst_object_unref(found->second.element);
    if (found->second.sourceFd >= 0) {
        ::close(found->second.sourceFd);
    }
    if (found->second.secondarySourceFd >= 0) {
        ::close(found->second.secondarySourceFd);
    }
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
