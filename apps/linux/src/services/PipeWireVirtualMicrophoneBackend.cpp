#include "services/LinuxVirtualMicrophoneService.h"

#include "services/LinuxPipeWireRoutingPlan.h"
#include "services/LinuxPipeWireRoutingService.h"

#include <gio/gio.h>
#include <gst/gst.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <sys/prctl.h>
#include <unistd.h>

namespace cuelet_linux {
namespace {

std::string structureString(const GstStructure* properties, const char* name)
{
    if (!properties || !name) {
        return {};
    }
    const char* value = gst_structure_get_string(properties, name);
    return value ? value : "";
}

bool structureBoolean(
    const GstStructure* properties,
    const char* name,
    bool fallback = false)
{
    gboolean result = fallback;
    return properties && gst_structure_get_boolean(properties, name, &result)
        ? result
        : fallback;
}

std::uint32_t nodeId(const GstStructure* properties)
{
    const auto text = structureString(properties, "object.id");
    std::uint32_t result = 0;
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()
        ? result
        : 0;
}

bool validNodeName(const std::string& value)
{
    return !value.empty() && value.size() <= 1024 &&
        std::none_of(value.begin(), value.end(), [](unsigned char character) {
            return character < 0x20 || character == 0x7f;
        });
}

bool writableProperty(GstElement* element, const char* name, GType type)
{
    const auto* property = g_object_class_find_property(
        G_OBJECT_GET_CLASS(element), name);
    return property && g_type_is_a(property->value_type, type) &&
        (property->flags & G_PARAM_WRITABLE);
}

std::optional<PipeWireNodeInfo> nodeInfo(GstDevice* device)
{
    if (!device) {
        return std::nullopt;
    }
    GstStructure* properties = gst_device_get_properties(device);
    const gchar* displayName = gst_device_get_display_name(device);
    const auto stableName = structureString(properties, "node.name");
    if (stableName.empty()) {
        if (properties) {
            gst_structure_free(properties);
        }
        return std::nullopt;
    }
    const auto propertyDescription = structureString(
        properties, "node.description");
    PipeWireNodeInfo result{
        nodeId(properties),
        stableName,
        !propertyDescription.empty()
            ? propertyDescription
            : std::string(displayName ? displayName : stableName),
        structureString(properties, "media.class"),
        structureString(properties, "device.class"),
        structureBoolean(properties, "node.virtual"),
        true,
    };
    if (properties) {
        gst_structure_free(properties);
    }
    return result;
}

class PipeWireVirtualMicrophoneBackend final : public VirtualMicrophoneBackend {
public:
    ~PipeWireVirtualMicrophoneBackend() override
    {
        stopPhysicalMix();
        if (physicalProcess_) {
            // The process is already force-terminated here. Retain its handle
            // during normal operation so no replacement can overlap a child
            // whose exit was not confirmed; object destruction is the final
            // ownership boundary and PR_SET_PDEATHSIG remains a backstop.
            g_subprocess_force_exit(physicalProcess_);
            g_object_unref(physicalProcess_);
            physicalProcess_ = nullptr;
            physicalChild_.reset();
        }
        stopBridge();
        if (deviceProvider_) {
            gst_device_provider_stop(deviceProvider_);
            gst_object_unref(deviceProvider_);
        }
    }

    VirtualMicrophoneCapabilities capabilities() override
    {
        const auto routeCapabilities = routing_.probeCapabilities();
        GstElementFactory* sourceFactory = gst_element_factory_find("pipewiresrc");
        GstElementFactory* sinkFactory = gst_element_factory_find("pipewiresink");
        GstElementFactory* rnnoiseFactory = gst_element_factory_find("audiornnoise");
        const bool gStreamerAvailable = sourceFactory && sinkFactory;
        if (sourceFactory) {
            gst_object_unref(sourceFactory);
        }
        if (sinkFactory) {
            gst_object_unref(sinkFactory);
        }
        if (rnnoiseFactory) {
            gst_object_unref(rnnoiseFactory);
        }
        return {
            routeCapabilities.pipeWireSessionReachable,
            routeCapabilities.pwLoopbackAvailable,
            gStreamerAvailable,
            rnnoiseFactory != nullptr,
        };
    }

    bool startBridge(
        const std::string& sessionKey,
        std::uint64_t generation) override
    {
        LinuxPipeWireRoutingPlan::Request request;
        request.enabled = true;
        request.sessionKey = sessionKey;
        request.sinkDescription = "Cuelet Soundboard Input";
        request.sourceDescription = "Cuelet Virtual Microphone";
        const auto plan = LinuxPipeWireRoutingPlan::create(request);
        if (!routing_.start(plan)) {
            return false;
        }
        bridgeGeneration_ = generation;
        bridgeWasRunning_ = true;
        return true;
    }

    void stopBridge() override
    {
        routing_.stop();
        bridgeWasRunning_ = false;
    }

    bool bridgeRunning() override
    {
        return routing_.isActive();
    }

    std::string virtualSinkNode() const override
    {
        return virtualMicrophoneSinkNodeName();
    }

    std::string virtualSourceNode() const override
    {
        return virtualMicrophoneSourceNodeName();
    }

    std::vector<PipeWireNodeInfo> enumerateNodes() override
    {
        std::vector<PipeWireNodeInfo> result;
        if (!deviceProvider_) {
            GstDeviceProviderFactory* factory =
                gst_device_provider_factory_find("pipewiredeviceprovider");
            deviceProvider_ = factory
                ? gst_device_provider_factory_get(factory)
                : nullptr;
            if (factory) {
                gst_object_unref(factory);
            }
            if (!deviceProvider_) {
                return result;
            }
            if (!gst_device_provider_start(deviceProvider_)) {
                gst_object_unref(deviceProvider_);
                deviceProvider_ = nullptr;
                return result;
            }
        }
        cachedNodes_.clear();
        GList* devices = gst_device_provider_get_devices(deviceProvider_);
        for (GList* item = devices; item; item = item->next) {
            if (const auto info = nodeInfo(GST_DEVICE(item->data))) {
                cachedNodes_.push_back(*info);
            }
        }
        g_list_free_full(devices, gst_object_unref);
        return cachedNodes_;
    }

    bool startPhysicalMix(
        const std::string& sourceNode,
        const std::string& sinkNode,
        double level,
        bool noiseSuppression,
        std::uint64_t generation) override
    {
        if (physicalProcess_ || processedPipeline_ || physicalStopping_ ||
            !validNodeName(sourceNode) ||
            !validNodeName(sinkNode) || sourceNode == sinkNode ||
            sourceNode.rfind("cuelet.", 0) == 0 ||
            sourceNode.find(".monitor") != std::string::npos) {
            return false;
        }
        if (noiseSuppression) {
            return startProcessedPhysicalMix(sourceNode, sinkNode, level, generation);
        }
        gchar* executable = g_find_program_in_path("pw-loopback");
        if (!executable) {
            return false;
        }
        physicalSourceNode_ = sourceNode;
        physicalSinkNode_ = sinkNode;
        physicalLevel_ = std::clamp(level, 0.0, 1.0);
        physicalGeneration_ = generation;
        physicalExitReported_ = false;
        auto arguments = physicalMicrophoneLoopbackArguments(
            sourceNode, sinkNode, physicalLevel_);
        if (arguments.empty()) {
            g_free(executable);
            return false;
        }
        arguments.front() = executable;
        g_free(executable);

        std::vector<const gchar*> directArguments;
        directArguments.reserve(arguments.size() + 1);
        for (const auto& argument : arguments) {
            directArguments.push_back(argument.c_str());
        }
        directArguments.push_back(nullptr);
        GSubprocessLauncher* launcher = g_subprocess_launcher_new(
            static_cast<GSubprocessFlags>(
                G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                G_SUBPROCESS_FLAGS_STDERR_PIPE));
        g_subprocess_launcher_set_child_setup(
            launcher,
            [](gpointer) {
                if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0 || getppid() == 1) {
                    _exit(127);
                }
            },
            nullptr,
            nullptr);
        GError* error = nullptr;
        physicalProcess_ = g_subprocess_launcher_spawnv(
            launcher, directArguments.data(), &error);
        g_object_unref(launcher);
        if (!physicalProcess_) {
            g_clear_error(&error);
            return false;
        }

        physicalChild_ = std::make_shared<PhysicalChildState>();
        physicalChild_->running = true;
        physicalChild_->generation = generation;
        g_subprocess_communicate_utf8_async(
            physicalProcess_,
            nullptr,
            nullptr,
            [](GObject* source, GAsyncResult* result, gpointer userData) {
                std::unique_ptr<std::shared_ptr<PhysicalChildState>> holder(
                    static_cast<std::shared_ptr<PhysicalChildState>*>(userData));
                gchar* standardOutput = nullptr;
                gchar* standardError = nullptr;
                GError* error = nullptr;
                g_subprocess_communicate_utf8_finish(
                    G_SUBPROCESS(source), result,
                    &standardOutput, &standardError, &error);
                {
                    std::lock_guard<std::mutex> lock((*holder)->mutex);
                    if (standardError) {
                        (*holder)->error = standardError;
                        if ((*holder)->error.size() > 8192) {
                            (*holder)->error.resize(8192);
                        }
                    } else if (error && error->message) {
                        (*holder)->error = error->message;
                    }
                }
                (*holder)->running = false;
                g_free(standardOutput);
                g_free(standardError);
                g_clear_error(&error);
            },
            new std::shared_ptr<PhysicalChildState>(physicalChild_));
        return true;
    }

    void setPhysicalMixLevel(double level) override
    {
        const auto normalized = std::isfinite(level)
            ? std::clamp(level, 0.0, 1.0)
            : 0.0;
        if (processedPipeline_) {
            if (processedVolume_ && normalized != physicalLevel_) {
                g_object_set(processedVolume_, "volume", normalized, nullptr);
            }
            physicalLevel_ = normalized;
            return;
        }
        if (!physicalProcess_ || normalized == physicalLevel_) {
            physicalLevel_ = normalized;
            return;
        }
        const auto source = physicalSourceNode_;
        const auto sink = physicalSinkNode_;
        const auto generation = physicalGeneration_;
        stopPhysicalMix();
        startPhysicalMix(source, sink, normalized, false, generation);
    }

    void stopPhysicalMix() override
    {
        if (processedPipeline_) {
            if (processedBusWatch_) {
                g_source_remove(processedBusWatch_);
                processedBusWatch_ = 0;
            }
            gst_element_set_state(processedPipeline_, GST_STATE_NULL);
            gst_object_unref(processedPipeline_);
            processedPipeline_ = nullptr;
            processedVolume_ = nullptr;
            processedFaulted_ = false;
            processedExitReported_ = false;
            physicalSourceNode_.clear();
            physicalSinkNode_.clear();
            return;
        }
        if (!physicalProcess_ || physicalStopping_) {
            return;
        }
        physicalStopping_ = true;
        if (physicalChild_ && physicalChild_->running) {
            g_subprocess_send_signal(physicalProcess_, SIGTERM);
            const gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
            while (physicalChild_->running && g_get_monotonic_time() < deadline) {
                while (g_main_context_iteration(nullptr, FALSE)) {
                }
                g_usleep(10000);
            }
            if (physicalChild_->running) {
                g_subprocess_force_exit(physicalProcess_);
                const gint64 forcedDeadline =
                    g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
                while (physicalChild_->running &&
                       g_get_monotonic_time() < forcedDeadline) {
                    while (g_main_context_iteration(nullptr, FALSE)) {
                    }
                    g_usleep(10000);
                }
                if (physicalChild_->running) {
                    // Keep the exact handle and prevent a replacement from
                    // starting until the asynchronous wait confirms exit.
                    physicalStopping_ = false;
                    return;
                }
            }
        }
        g_object_unref(physicalProcess_);
        physicalProcess_ = nullptr;
        physicalChild_.reset();
        physicalSourceNode_.clear();
        physicalSinkNode_.clear();
        physicalStopping_ = false;
    }

    bool physicalMixRunning() override
    {
        if (processedPipeline_) {
            return !processedFaulted_;
        }
        return physicalProcess_ && physicalChild_ && physicalChild_->running;
    }

    std::vector<VirtualMicrophoneBackendEvent> takeEvents() override
    {
        if (bridgeWasRunning_ && !routing_.isActive()) {
            bridgeWasRunning_ = false;
            events_.push_back({
                bridgeGeneration_,
                VirtualMicrophoneBackendEventKind::BridgeExited,
                "The owned pw-loopback helper exited unexpectedly.",
            });
        }
        if (physicalProcess_ && physicalChild_ &&
            !physicalChild_->running && !physicalExitReported_) {
            std::string error;
            {
                std::lock_guard<std::mutex> lock(physicalChild_->mutex);
                error = physicalChild_->error;
            }
            events_.push_back({
                physicalChild_->generation,
                VirtualMicrophoneBackendEventKind::PhysicalMixExited,
                error.empty()
                    ? "The physical microphone helper exited unexpectedly."
                    : error,
            });
            physicalExitReported_ = true;
        }
        if (processedPipeline_ && processedFaulted_ && !processedExitReported_) {
            events_.push_back({
                physicalGeneration_,
                VirtualMicrophoneBackendEventKind::PhysicalMixExited,
                processedError_.empty()
                    ? "The processed microphone path stopped unexpectedly."
                    : processedError_,
            });
            processedExitReported_ = true;
        }
        auto result = std::move(events_);
        events_.clear();
        return result;
    }

private:
    static gboolean onProcessedBusMessage(
        GstBus*, GstMessage* message, gpointer userData)
    {
        auto* self = static_cast<PipeWireVirtualMicrophoneBackend*>(userData);
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            GError* error = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            self->processedError_ = error && error->message
                ? error->message
                : "The processed microphone path stopped unexpectedly.";
            if (self->processedError_.size() > 8192) {
                self->processedError_.resize(8192);
            }
            self->processedFaulted_ = true;
            g_clear_error(&error);
            g_free(debug);
        } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
            self->processedError_ = "The selected microphone disconnected.";
            self->processedFaulted_ = true;
        }
        return G_SOURCE_CONTINUE;
    }

    bool startProcessedPhysicalMix(
        const std::string& sourceNode,
        const std::string& sinkNode,
        double level,
        std::uint64_t generation)
    {
        // All elements and caps are assembled before playback. GStreamer owns
        // the streaming threads; no application callback runs on audio data.
        GstElement* pipeline = gst_pipeline_new("cuelet-processed-microphone");
        GstElement* source = gst_element_factory_make("pipewiresrc", nullptr);
        GstElement* toFloat = gst_element_factory_make("audioconvert", nullptr);
        GstElement* resampler = gst_element_factory_make("audioresample", nullptr);
        GstElement* monoCaps = gst_element_factory_make("capsfilter", nullptr);
        GstElement* suppressor = gst_element_factory_make("audiornnoise", nullptr);
        GstElement* volume = gst_element_factory_make("volume", nullptr);
        GstElement* toStereo = gst_element_factory_make("audioconvert", nullptr);
        GstElement* outputCaps = gst_element_factory_make("capsfilter", nullptr);
        GstElement* sink = gst_element_factory_make("pipewiresink", nullptr);
        GstElement* elements[] = {
            source, toFloat, resampler, monoCaps, suppressor,
            volume, toStereo, outputCaps, sink,
        };
        const bool complete = pipeline && std::all_of(
            std::begin(elements), std::end(elements),
            [](GstElement* element) { return element != nullptr; });
        if (!complete || !writableProperty(source, "target-object", G_TYPE_STRING) ||
            !writableProperty(sink, "target-object", G_TYPE_STRING) ||
            !writableProperty(source, "stream-properties", GST_TYPE_STRUCTURE) ||
            !writableProperty(sink, "stream-properties", GST_TYPE_STRUCTURE) ||
            !writableProperty(volume, "volume", G_TYPE_DOUBLE)) {
            for (auto* element : elements) {
                if (element) {
                    gst_object_unref(element);
                }
            }
            if (pipeline) {
                gst_object_unref(pipeline);
            }
            return false;
        }

        GstCaps* inputCaps = gst_caps_new_simple(
            "audio/x-raw", "format", G_TYPE_STRING, "F32LE",
            "rate", G_TYPE_INT, 48000, "channels", G_TYPE_INT, 1,
            "layout", G_TYPE_STRING, "interleaved", nullptr);
        GstCaps* stereoCaps = gst_caps_new_simple(
            "audio/x-raw", "format", G_TYPE_STRING, "S16LE",
            "rate", G_TYPE_INT, 48000, "channels", G_TYPE_INT, 2,
            "layout", G_TYPE_STRING, "interleaved", nullptr);
        g_object_set(monoCaps, "caps", inputCaps, nullptr);
        g_object_set(outputCaps, "caps", stereoCaps, nullptr);
        gst_caps_unref(inputCaps);
        gst_caps_unref(stereoCaps);

        const double normalized = std::isfinite(level)
            ? std::clamp(level, 0.0, 1.0)
            : 0.0;
        g_object_set(source, "target-object", sourceNode.c_str(), nullptr);
        g_object_set(sink, "target-object", sinkNode.c_str(), nullptr);
        g_object_set(volume, "volume", normalized, nullptr);
        if (writableProperty(source, "max-buffers", G_TYPE_INT)) {
            g_object_set(source, "max-buffers", 8, nullptr);
        }
        if (writableProperty(source, "on-disconnect", G_TYPE_ENUM)) {
            g_object_set(source, "on-disconnect", 2, nullptr); // ERROR
        }
        GstStructure* sourceProperties = gst_structure_new(
            "props", "node.name", G_TYPE_STRING, "cuelet.processed-microphone-capture",
            "node.description", G_TYPE_STRING, "Cuelet Processed Microphone Capture",
            "node.dont-reconnect", G_TYPE_BOOLEAN, TRUE,
            "node.dont-fallback", G_TYPE_BOOLEAN, TRUE,
            "state.restore-props", G_TYPE_BOOLEAN, FALSE, nullptr);
        GstStructure* sinkProperties = gst_structure_new(
            "props", "node.name", G_TYPE_STRING, "cuelet.processed-microphone-output",
            "node.description", G_TYPE_STRING, "Cuelet Processed Microphone Output",
            "media.role", G_TYPE_STRING, "Communication",
            "node.dont-reconnect", G_TYPE_BOOLEAN, TRUE,
            "node.dont-fallback", G_TYPE_BOOLEAN, TRUE,
            "state.restore-props", G_TYPE_BOOLEAN, FALSE, nullptr);
        g_object_set(source, "stream-properties", sourceProperties, nullptr);
        g_object_set(sink, "stream-properties", sinkProperties, nullptr);
        gst_structure_free(sourceProperties);
        gst_structure_free(sinkProperties);

        gst_bin_add_many(GST_BIN(pipeline), source, toFloat, resampler,
            monoCaps, suppressor, volume, toStereo, outputCaps, sink, nullptr);
        if (!gst_element_link_many(source, toFloat, resampler, monoCaps,
                suppressor, volume, toStereo, outputCaps, sink, nullptr)) {
            gst_object_unref(pipeline);
            return false;
        }
        GstBus* bus = gst_element_get_bus(pipeline);
        const guint watch = gst_bus_add_watch(bus, onProcessedBusMessage, this);
        gst_object_unref(bus);
        if (!watch || gst_element_set_state(pipeline, GST_STATE_PLAYING) ==
                GST_STATE_CHANGE_FAILURE) {
            if (watch) {
                g_source_remove(watch);
            }
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
            return false;
        }

        processedPipeline_ = pipeline;
        processedVolume_ = volume;
        processedBusWatch_ = watch;
        processedFaulted_ = false;
        processedExitReported_ = false;
        processedError_.clear();
        physicalSourceNode_ = sourceNode;
        physicalSinkNode_ = sinkNode;
        physicalLevel_ = normalized;
        physicalGeneration_ = generation;
        return true;
    }

    struct PhysicalChildState {
        std::atomic<bool> running{false};
        std::uint64_t generation = 0;
        std::mutex mutex;
        std::string error;
    };

    LinuxPipeWireRoutingService routing_;
    GstDeviceProvider* deviceProvider_ = nullptr;
    std::vector<PipeWireNodeInfo> cachedNodes_;
    bool bridgeWasRunning_ = false;
    std::uint64_t bridgeGeneration_ = 0;
    GSubprocess* physicalProcess_ = nullptr;
    GstElement* processedPipeline_ = nullptr;
    GstElement* processedVolume_ = nullptr; // Owned by processedPipeline_.
    guint processedBusWatch_ = 0;
    bool processedFaulted_ = false;
    bool processedExitReported_ = false;
    std::string processedError_;
    std::shared_ptr<PhysicalChildState> physicalChild_;
    std::string physicalSourceNode_;
    std::string physicalSinkNode_;
    double physicalLevel_ = 0.5;
    bool physicalExitReported_ = false;
    bool physicalStopping_ = false;
    std::uint64_t physicalGeneration_ = 0;
    std::vector<VirtualMicrophoneBackendEvent> events_;
};

} // namespace

std::unique_ptr<VirtualMicrophoneBackend> makePipeWireVirtualMicrophoneBackend()
{
    return std::make_unique<PipeWireVirtualMicrophoneBackend>();
}

} // namespace cuelet_linux
