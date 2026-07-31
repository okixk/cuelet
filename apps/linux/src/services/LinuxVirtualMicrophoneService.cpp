#include "services/LinuxVirtualMicrophoneService.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <utility>

namespace cuelet_linux {
namespace {

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool validStableName(const std::string& name)
{
    return !name.empty() && name.size() <= 1024 &&
        std::none_of(name.begin(), name.end(), [](unsigned char character) {
            return character < 0x20 || character == 0x7f;
        });
}

double safeLevel(double level)
{
    return std::isfinite(level) ? std::clamp(level, 0.0, 1.0) : 0.25;
}

bool needsVirtualGraph(VirtualMicrophoneRoutingMode mode)
{
    return mode != VirtualMicrophoneRoutingMode::SpeakersOnly;
}

} // namespace

std::vector<PhysicalMicrophoneInfo> physicalMicrophonesFromNodes(
    const std::vector<PipeWireNodeInfo>& nodes)
{
    std::vector<PhysicalMicrophoneInfo> microphones;
    std::set<std::string> stableNames;
    for (const auto& node : nodes) {
        const auto stableName = lower(node.stableName);
        const auto description = lower(node.description);
        const auto deviceClass = lower(node.deviceClass);
        if (!node.available || node.virtualNode ||
            node.mediaClass != "Audio/Source" ||
            !validStableName(node.stableName) ||
            stableName.rfind("cuelet.", 0) == 0 ||
            stableName.rfind("cuelet_", 0) == 0 ||
            stableName.find(".monitor") != std::string::npos ||
            deviceClass.find("monitor") != std::string::npos ||
            description.rfind("monitor of ", 0) == 0 ||
            !stableNames.insert(node.stableName).second) {
            continue;
        }
        microphones.push_back({
            node.stableName,
            node.description.empty() ? node.stableName : node.description,
            node.numericId,
        });
    }
    std::sort(microphones.begin(), microphones.end(), [](const auto& left, const auto& right) {
        if (left.description == right.description) {
            return left.stableId < right.stableId;
        }
        return left.description < right.description;
    });
    return microphones;
}

std::vector<std::string> physicalMicrophoneLoopbackArguments(
    const std::string& sourceNode,
    const std::string& virtualSinkNode,
    double level)
{
    const auto loweredSource = lower(sourceNode);
    if (!validStableName(sourceNode) ||
        !validStableName(virtualSinkNode) ||
        sourceNode == virtualSinkNode ||
        loweredSource.rfind("cuelet.", 0) == 0 ||
        loweredSource.rfind("cuelet_", 0) == 0 ||
        loweredSource.find(".monitor") != std::string::npos) {
        return {};
    }
    const auto normalizedLevel = std::to_string(safeLevel(level));
    return {
        "pw-loopback",
        "--name=cuelet.microphone-mix",
        "--group=cuelet.microphone-mix",
        "--channels=2",
        "--channel-map=[ FL, FR ]",
        "--capture=" + sourceNode,
        "--capture-props={\"node.name\":\"cuelet.microphone-mix.capture\",\"node.description\":\"Cuelet Physical Microphone Mix\",\"node.dont-reconnect\":true,\"node.dont-fallback\":true,\"object.linger\":false,\"state.restore-props\":false}",
        "--playback=" + virtualSinkNode,
        "--playback-props={\"node.name\":\"cuelet.microphone-mix.output\",\"node.description\":\"Cuelet Physical Microphone Mix Output\",\"node.dont-reconnect\":true,\"node.dont-fallback\":true,\"object.linger\":false,\"state.restore-props\":false,\"node.param.Props\":{\"channelVolumes\":[" +
            normalizedLevel + "," + normalizedLevel + "]}}",
    };
}

LinuxVirtualMicrophoneService::LinuxVirtualMicrophoneService(
    VirtualMicrophoneBackend& backend,
    std::string sessionKey,
    std::uint64_t startupTimeoutMilliseconds)
    : backend_(backend)
    , sessionKey_(std::move(sessionKey))
    , startupTimeoutMilliseconds_(std::max<std::uint64_t>(
          startupTimeoutMilliseconds, 1))
{
}

LinuxVirtualMicrophoneService::~LinuxVirtualMicrophoneService()
{
    shutdown();
}

bool LinuxVirtualMicrophoneService::apply(
    const VirtualMicrophoneConfiguration& requestedConfiguration,
    std::uint64_t monotonicMilliseconds)
{
    if (shutdown_) {
        return false;
    }

    auto next = requestedConfiguration;
    next.soundboardLevel = safeLevel(next.soundboardLevel);
    next.physicalMicrophoneLevel = safeLevel(next.physicalMicrophoneLevel);
    if (next.physicalMicrophoneId.size() > 1024 ||
        !validStableName(next.physicalMicrophoneId)) {
        next.physicalMicrophoneId.clear();
    }

    const bool levelChanged =
        next.physicalMicrophoneLevel != configuration_.physicalMicrophoneLevel;
    configuration_ = std::move(next);

    if (!needsVirtualGraph(configuration_.mode)) {
        stopGraph();
        status_ = {};
        return true;
    }

    if (!backend_.bridgeRunning()) {
        return startGraph(monotonicMilliseconds, false);
    }

    refreshGraph(monotonicMilliseconds);
    if (levelChanged && backend_.physicalMixRunning() &&
        activePhysicalMicrophoneId_ == configuration_.physicalMicrophoneId) {
        backend_.setPhysicalMixLevel(configuration_.physicalMicrophoneLevel);
        activePhysicalMicrophoneLevel_ = configuration_.physicalMicrophoneLevel;
    }
    refreshPhysicalMix();
    return status_.state != VirtualMicrophoneState::Failed &&
        status_.state != VirtualMicrophoneState::PipeWireUnavailable;
}

void LinuxVirtualMicrophoneService::poll(std::uint64_t monotonicMilliseconds)
{
    if (shutdown_ || !needsVirtualGraph(configuration_.mode)) {
        return;
    }

    bool currentBridgeExited = false;
    for (const auto& event : backend_.takeEvents()) {
        if (event.generation != generation_) {
            continue;
        }
        if (event.kind == VirtualMicrophoneBackendEventKind::BridgeExited) {
            currentBridgeExited = true;
            status_.message = event.error.empty()
                ? "The virtual microphone helper exited. Reconnecting."
                : event.error;
        } else if (event.kind == VirtualMicrophoneBackendEventKind::PhysicalMixExited) {
            backend_.stopPhysicalMix();
            activePhysicalMicrophoneId_.clear();
        }
    }

    const auto available = backend_.capabilities();
    if (!available.pipeWireSessionReachable) {
        // A helper may survive while its PipeWire connection is stale. Tear
        // down the complete owned graph so reconnect always starts from a new
        // session instead of trusting process liveness alone.
        stopGraph();
        status_ = {
            VirtualMicrophoneState::PipeWireUnavailable,
            false,
            false,
            "PipeWire is unavailable. Cuelet will reconnect when it returns.",
        };
        return;
    }
    if (!available.pwLoopbackAvailable ||
        !available.pipeWireGStreamerAvailable) {
        stopGraph();
        status_ = {
            VirtualMicrophoneState::Failed,
            false,
            false,
            !available.pwLoopbackAvailable
                ? "pw-loopback is unavailable."
                : "The GStreamer PipeWire plugin is unavailable.",
        };
        return;
    }

    if (currentBridgeExited || !backend_.bridgeRunning()) {
        backend_.stopPhysicalMix();
        activePhysicalMicrophoneId_.clear();
        backend_.stopBridge();
        status_.state = VirtualMicrophoneState::Reconnecting;
        if (!startGraph(monotonicMilliseconds, true)) {
            return;
        }
    }

    refreshGraph(monotonicMilliseconds);
    refreshPhysicalMix();
}

void LinuxVirtualMicrophoneService::shutdown()
{
    if (shutdown_) {
        return;
    }
    stopGraph();
    status_ = {};
    shutdown_ = true;
}

const VirtualMicrophoneConfiguration&
LinuxVirtualMicrophoneService::configuration() const
{
    return configuration_;
}

const VirtualMicrophoneStatus& LinuxVirtualMicrophoneService::status() const
{
    return status_;
}

std::vector<PhysicalMicrophoneInfo>
LinuxVirtualMicrophoneService::physicalMicrophones()
{
    return physicalMicrophonesFromNodes(backend_.enumerateNodes());
}

std::uint64_t LinuxVirtualMicrophoneService::generation() const
{
    return generation_;
}

bool LinuxVirtualMicrophoneService::startGraph(
    std::uint64_t monotonicMilliseconds,
    bool reconnecting)
{
    const auto available = backend_.capabilities();
    if (!available.pipeWireSessionReachable) {
        status_ = {
            VirtualMicrophoneState::PipeWireUnavailable,
            false,
            false,
            "PipeWire is unavailable.",
        };
        return false;
    }
    if (!available.pwLoopbackAvailable ||
        !available.pipeWireGStreamerAvailable) {
        status_ = {
            VirtualMicrophoneState::Failed,
            false,
            false,
            !available.pwLoopbackAvailable
                ? "pw-loopback is unavailable."
                : "The GStreamer PipeWire plugin is unavailable.",
        };
        return false;
    }

    ++generation_;
    if (!backend_.startBridge(sessionKey_, generation_)) {
        status_ = {
            VirtualMicrophoneState::Failed,
            false,
            false,
            "Could not create Cuelet Virtual Microphone.",
        };
        return false;
    }
    startupDeadlineMilliseconds_ =
        monotonicMilliseconds + startupTimeoutMilliseconds_;
    status_ = {
        reconnecting
            ? VirtualMicrophoneState::Reconnecting
            : VirtualMicrophoneState::Starting,
        false,
        false,
        reconnecting ? "Reconnecting" : "Starting",
    };
    refreshGraph(monotonicMilliseconds);
    refreshPhysicalMix();
    return true;
}

void LinuxVirtualMicrophoneService::refreshGraph(
    std::uint64_t monotonicMilliseconds)
{
    const auto nodes = backend_.enumerateNodes();
    status_.virtualSourceVisible = virtualSourceIsVisible(nodes);
    if (!status_.virtualSourceVisible) {
        status_.physicalMicrophoneActive = false;
        if (monotonicMilliseconds >= startupDeadlineMilliseconds_) {
            backend_.stopPhysicalMix();
            activePhysicalMicrophoneId_.clear();
            backend_.stopBridge();
            status_ = {
                VirtualMicrophoneState::Failed,
                false,
                false,
                "Cuelet Virtual Microphone did not appear before the startup timeout.",
            };
        } else {
            status_.state = VirtualMicrophoneState::Starting;
            status_.message = "Starting";
        }
        return;
    }

    status_.state = VirtualMicrophoneState::Ready;
    status_.message = "Ready";
}

void LinuxVirtualMicrophoneService::refreshPhysicalMix()
{
    if (!status_.virtualSourceVisible ||
        !configuration_.mixPhysicalMicrophone ||
        configuration_.physicalMicrophoneId.empty()) {
        if (backend_.physicalMixRunning()) {
            backend_.stopPhysicalMix();
        }
        activePhysicalMicrophoneId_.clear();
        status_.physicalMicrophoneActive = false;
        return;
    }

    const auto microphones = physicalMicrophones();
    const auto selected = std::find_if(
        microphones.begin(), microphones.end(), [&](const auto& microphone) {
            return microphone.stableId == configuration_.physicalMicrophoneId;
        });
    if (selected == microphones.end()) {
        if (backend_.physicalMixRunning()) {
            backend_.stopPhysicalMix();
        }
        activePhysicalMicrophoneId_.clear();
        status_.state = VirtualMicrophoneState::DegradedMicrophoneUnavailable;
        status_.physicalMicrophoneActive = false;
        status_.message = "Degraded: selected microphone unavailable";
        return;
    }

    if (backend_.physicalMixRunning() &&
        activePhysicalMicrophoneId_ == selected->stableId) {
        status_.physicalMicrophoneActive = true;
        return;
    }
    if (backend_.physicalMixRunning()) {
        backend_.stopPhysicalMix();
    }
    activePhysicalMicrophoneId_.clear();
    if (!backend_.startPhysicalMix(
            selected->stableId,
            backend_.virtualSinkNode(),
            configuration_.physicalMicrophoneLevel,
            generation_)) {
        status_.state = VirtualMicrophoneState::DegradedMicrophoneUnavailable;
        status_.physicalMicrophoneActive = false;
        status_.message = "Degraded: physical microphone could not be opened";
        return;
    }
    activePhysicalMicrophoneId_ = selected->stableId;
    activePhysicalMicrophoneLevel_ = configuration_.physicalMicrophoneLevel;
    status_.physicalMicrophoneActive = true;
    status_.state = VirtualMicrophoneState::Ready;
    status_.message = "Ready — physical microphone mixing is active";
}

bool LinuxVirtualMicrophoneService::virtualSourceIsVisible(
    const std::vector<PipeWireNodeInfo>& nodes) const
{
    const auto sourceName = backend_.virtualSourceNode();
    return std::any_of(nodes.begin(), nodes.end(), [&](const auto& node) {
        return node.available && node.mediaClass == "Audio/Source" &&
            node.stableName == sourceName;
    });
}

void LinuxVirtualMicrophoneService::stopGraph()
{
    if (backend_.physicalMixRunning()) {
        backend_.stopPhysicalMix();
    }
    activePhysicalMicrophoneId_.clear();
    if (backend_.bridgeRunning()) {
        backend_.stopBridge();
    }
}

} // namespace cuelet_linux
