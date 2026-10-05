#include "services/LinuxVirtualMicrophoneService.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace cuelet_linux;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

PipeWireNodeInfo node(
    std::uint32_t id,
    std::string name,
    std::string description,
    std::string mediaClass = "Audio/Source",
    std::string deviceClass = {},
    bool virtualNode = false,
    bool available = true)
{
    return {
        id,
        std::move(name),
        std::move(description),
        std::move(mediaClass),
        std::move(deviceClass),
        virtualNode,
        available,
    };
}

class FakeBackend final : public VirtualMicrophoneBackend {
public:
    VirtualMicrophoneCapabilities capabilitiesValue{true, true, true, true};
    std::vector<PipeWireNodeInfo> nodes;
    std::deque<VirtualMicrophoneBackendEvent> events;
    bool startBridgeSucceeds = true;
    bool startPhysicalSucceeds = true;
    bool startProcessedSucceeds = true;
    bool bridgeRunningValue = false;
    bool physicalRunningValue = false;
    bool physicalHandleRetained = false;
    int startBridgeCalls = 0;
    int stopBridgeCalls = 0;
    int startPhysicalCalls = 0;
    int stopPhysicalCalls = 0;
    int setPhysicalLevelCalls = 0;
    std::string selectedPhysicalNode;
    std::string physicalTarget;
    double physicalLevel = 0.0;
    bool physicalNoiseSuppression = false;
    std::uint64_t bridgeGeneration = 0;
    std::uint64_t physicalGeneration = 0;

    VirtualMicrophoneCapabilities capabilities() override
    {
        return capabilitiesValue;
    }

    bool startBridge(const std::string&, std::uint64_t generation) override
    {
        ++startBridgeCalls;
        bridgeGeneration = generation;
        bridgeRunningValue = startBridgeSucceeds;
        return startBridgeSucceeds;
    }

    void stopBridge() override
    {
        ++stopBridgeCalls;
        bridgeRunningValue = false;
    }

    bool bridgeRunning() override
    {
        return bridgeRunningValue;
    }

    std::string virtualSinkNode() const override
    {
        return "cuelet.soundboard-input";
    }

    std::string virtualSourceNode() const override
    {
        return "cuelet.virtual-microphone";
    }

    std::vector<PipeWireNodeInfo> enumerateNodes() override
    {
        return nodes;
    }

    bool startPhysicalMix(
        const std::string& sourceNode,
        const std::string& sinkNode,
        double level,
        bool noiseSuppression,
        std::uint64_t generation) override
    {
        ++startPhysicalCalls;
        if (physicalHandleRetained) {
            return false;
        }
        selectedPhysicalNode = sourceNode;
        physicalTarget = sinkNode;
        physicalLevel = level;
        physicalNoiseSuppression = noiseSuppression;
        physicalGeneration = generation;
        const bool started = startPhysicalSucceeds &&
            (!noiseSuppression || startProcessedSucceeds);
        physicalRunningValue = started;
        physicalHandleRetained = started;
        return started;
    }

    void setPhysicalMixLevel(double level) override
    {
        ++setPhysicalLevelCalls;
        physicalLevel = level;
    }

    void stopPhysicalMix() override
    {
        ++stopPhysicalCalls;
        physicalRunningValue = false;
        physicalHandleRetained = false;
        selectedPhysicalNode.clear();
    }

    bool physicalMixRunning() override
    {
        return physicalRunningValue;
    }

    std::vector<VirtualMicrophoneBackendEvent> takeEvents() override
    {
        std::vector<VirtualMicrophoneBackendEvent> result(
            events.begin(), events.end());
        events.clear();
        return result;
    }
};

VirtualMicrophoneConfiguration virtualOnly()
{
    VirtualMicrophoneConfiguration configuration;
    configuration.mode = VirtualMicrophoneRoutingMode::VirtualMicrophoneOnly;
    return configuration;
}

void makeVirtualSourceVisible(FakeBackend& backend)
{
    backend.nodes.push_back(node(
        20,
        "cuelet.virtual-microphone",
        "Cuelet Virtual Microphone",
        "Audio/Source",
        {},
        true));
}

void initialStateIsDisabledAndSideEffectFree()
{
    require(std::string(virtualMicrophoneSinkNodeName()) ==
                "cuelet.soundboard-input"
                && std::string(virtualMicrophoneSourceNodeName()) ==
                    "cuelet.virtual-microphone",
            "stable endpoint names must be available before graph creation");

    FakeBackend backend;
    LinuxVirtualMicrophoneService service(backend, "test-session", 1000);

    require(service.status().state == VirtualMicrophoneState::Off,
            "the service must start off");
    require(service.configuration().mode == VirtualMicrophoneRoutingMode::SpeakersOnly,
            "speakers-only must be the default routing mode");
    require(backend.startBridgeCalls == 0 && backend.startPhysicalCalls == 0,
            "constructing the service must not open a microphone or create nodes");
}

void physicalSourcesAreFilteredAndStableAcrossNumericIds()
{
    const std::vector<PipeWireNodeInfo> first{
        node(61, "alsa_input.internal", "Built-in Microphone"),
        node(62, "alsa_input.internal", "Duplicate Alias"),
        node(70, "alsa_output.speakers.monitor", "Monitor of Speakers", "Audio/Source", "monitor"),
        node(71, "cuelet.virtual-microphone", "Cuelet Virtual Microphone", "Audio/Source", {}, true),
        node(72, "cuelet.microphone-mix.capture", "Cuelet Mix"),
        node(73, "video.input", "Camera", "Video/Source"),
        node(74, "bluez_input.gone", "Disconnected Headset", "Audio/Source", {}, false, false),
    };
    const auto filtered = physicalMicrophonesFromNodes(first);
    require(filtered.size() == 1, "only one real available microphone must remain");
    require(filtered.front().stableId == "alsa_input.internal",
            "node.name must be the stable microphone identity");
    require(filtered.front().numericId == 61,
            "the current numeric ID may be retained only as diagnostic data");

    const auto afterRestart = physicalMicrophonesFromNodes({
        node(1061, "alsa_input.internal", "Renamed Built-in Microphone"),
    });
    require(afterRestart.size() == 1
                && afterRestart.front().stableId == filtered.front().stableId
                && afterRestart.front().numericId != filtered.front().numericId,
            "a PipeWire node-ID change must not change the selected stable identity");
}

void physicalLoopbackArgumentsAreScopedAndShellFree()
{
    const auto arguments = physicalMicrophoneLoopbackArguments(
        "alsa_input.usb-safe", "cuelet.soundboard-input", 0.35);
    require(arguments.size() == 9 && arguments.front() == "pw-loopback",
            "the physical mix must use one directly executed pw-loopback plan");
    require(std::find(arguments.begin(), arguments.end(),
                      "--capture=alsa_input.usb-safe") != arguments.end(),
            "the selected stable source must be an exact argv element");
    require(std::find(arguments.begin(), arguments.end(),
                      "--playback=cuelet.soundboard-input") != arguments.end(),
            "the Cuelet-owned sink must be an exact argv element");
    for (const auto& argument : arguments) {
        require(argument != "sh" && argument != "bash" && argument != "-c",
                "physical mixing must never introduce a shell");
        require(argument.find("set-default") == std::string::npos,
                "physical mixing must never change a default device");
    }
    require(arguments.back().find("channelVolumes") != std::string::npos &&
                arguments.back().find("0.350000") != std::string::npos,
            "the owned playback stream must carry its conservative mix level");
    require(physicalMicrophoneLoopbackArguments(
                "cuelet.virtual-microphone", "cuelet.soundboard-input", 0.5).empty(),
            "Cuelet's own source must be rejected before spawning");
    require(physicalMicrophoneLoopbackArguments(
                "alsa_output.monitor", "cuelet.soundboard-input", 0.5).empty(),
            "monitor sources must be rejected before spawning");
}

void successfulCreationAndIdempotentApply()
{
    FakeBackend backend;
    makeVirtualSourceVisible(backend);
    LinuxVirtualMicrophoneService service(backend, "test-session", 1000);

    const auto configuration = virtualOnly();
    require(service.apply(configuration, 10), "the virtual route must start");
    require(service.status().state == VirtualMicrophoneState::Ready,
            "a running visible source must be ready");
    require(service.status().virtualSourceVisible,
            "readiness must include source visibility");
    require(backend.startBridgeCalls == 1, "one bridge must be created");

    require(service.apply(configuration, 20), "reapplying the same mode must succeed");
    service.poll(30);
    require(backend.startBridgeCalls == 1,
            "repeated activation and polling must not duplicate the bridge");
}

void creationFailureAndStartupTimeoutFailClosed()
{
    FakeBackend failedBackend;
    failedBackend.startBridgeSucceeds = false;
    LinuxVirtualMicrophoneService failed(failedBackend, "failed", 1000);
    require(!failed.apply(virtualOnly(), 0), "a bridge start failure must be reported");
    require(failed.status().state == VirtualMicrophoneState::Failed,
            "a failed graph creation must be visible");
    require(failedBackend.startPhysicalCalls == 0,
            "partial creation failure must not open the physical microphone");

    FakeBackend timeoutBackend;
    LinuxVirtualMicrophoneService timeout(timeoutBackend, "timeout", 1000);
    require(timeout.apply(virtualOnly(), 100), "a live helper may enter starting state");
    require(timeout.status().state == VirtualMicrophoneState::Starting,
            "source discovery must remain pending before the timeout");
    timeout.poll(1099);
    require(timeout.status().state == VirtualMicrophoneState::Starting,
            "the startup timeout must be bounded but not premature");
    timeout.poll(1100);
    require(timeout.status().state == VirtualMicrophoneState::Failed,
            "an invisible source at the deadline must fail");
    require(timeoutBackend.stopBridgeCalls == 1,
            "a timed-out partial graph must be cleaned up");
}

void failedRequestRecoversUnlessExplicitlyDisabled()
{
    FakeBackend recoveringBackend;
    makeVirtualSourceVisible(recoveringBackend);
    recoveringBackend.capabilitiesValue.pipeWireSessionReachable = false;
    LinuxVirtualMicrophoneService recovering(recoveringBackend, "recovering", 1000);
    require(!recovering.apply(virtualOnly(), 0),
            "an unavailable PipeWire session must reject initial activation");
    recoveringBackend.capabilitiesValue.pipeWireSessionReachable = true;
    recovering.poll(1);
    require(recoveringBackend.startBridgeCalls == 1
                && recovering.status().state == VirtualMicrophoneState::Ready,
            "a still-requested persisted route must recover when PipeWire returns");

    FakeBackend rolledBackBackend;
    makeVirtualSourceVisible(rolledBackBackend);
    rolledBackBackend.capabilitiesValue.pipeWireSessionReachable = false;
    LinuxVirtualMicrophoneService rolledBack(rolledBackBackend, "rolled-back", 1000);
    require(!rolledBack.apply(virtualOnly(), 0),
            "the user-requested activation failure must be observable");
    VirtualMicrophoneConfiguration speakersOnly;
    require(rolledBack.apply(speakersOnly, 1),
            "the UI must be able to roll a failed request back to off");
    rolledBackBackend.capabilitiesValue.pipeWireSessionReachable = true;
    rolledBack.poll(2);
    require(rolledBackBackend.startBridgeCalls == 0
                && rolledBack.status().state == VirtualMicrophoneState::Off,
            "a rolled-back request must stay off after capability recovery");
}

void modeTransitionsAndCleanupAreScoped()
{
    FakeBackend backend;
    makeVirtualSourceVisible(backend);
    LinuxVirtualMicrophoneService service(backend, "modes", 1000);
    require(service.apply(virtualOnly(), 0), "virtual-only must activate");

    auto both = virtualOnly();
    both.mode = VirtualMicrophoneRoutingMode::SpeakersAndVirtualMicrophone;
    require(service.apply(both, 1), "speaker mirroring must reuse the graph");
    require(backend.startBridgeCalls == 1 && backend.stopBridgeCalls == 0,
            "a playback-only mode transition must not recreate PipeWire nodes");

    VirtualMicrophoneConfiguration speakers;
    require(service.apply(speakers, 2), "speakers-only must disable the graph");
    require(service.status().state == VirtualMicrophoneState::Off,
            "speakers-only must report off");
    require(backend.stopBridgeCalls == 1,
            "disable must stop exactly the owned bridge");

    require(service.apply(virtualOnly(), 3), "the graph must enable again");
    service.shutdown();
    service.shutdown();
    require(backend.stopBridgeCalls == 2,
            "shutdown must clean the active graph exactly once");
}

void physicalMixUsesOnlyTheSelectedStableSource()
{
    FakeBackend backend;
    makeVirtualSourceVisible(backend);
    backend.nodes.push_back(node(61, "alsa_input.mic-a", "Microphone A"));
    backend.nodes.push_back(node(62, "alsa_input.mic-b", "Microphone B"));
    LinuxVirtualMicrophoneService service(backend, "physical", 1000);

    auto configuration = virtualOnly();
    configuration.mixPhysicalMicrophone = true;
    configuration.physicalMicrophoneId = "alsa_input.mic-b";
    configuration.physicalMicrophoneLevel = 0.45;
    require(service.apply(configuration, 0), "selected microphone mixing must start");
    require(service.status().state == VirtualMicrophoneState::Ready,
            "a selected available microphone must be ready");
    require(service.status().physicalMicrophoneActive,
            "the UI state must expose active microphone capture");
    require(backend.startPhysicalCalls == 1
                && backend.selectedPhysicalNode == "alsa_input.mic-b"
                && backend.physicalTarget == "cuelet.soundboard-input"
                && !backend.physicalNoiseSuppression,
            "only the selected stable source may feed the Cuelet-owned sink");

    service.poll(1);
    require(backend.startPhysicalCalls == 1,
            "polling must not create duplicate microphone links");

    configuration.physicalMicrophoneLevel = 0.3;
    require(service.apply(configuration, 2), "level updates must be accepted");
    require(backend.setPhysicalLevelCalls == 1 && backend.physicalLevel == 0.3,
            "physical level changes must update the existing mixer");

    backend.physicalRunningValue = false;
    backend.events.push_back({
        service.generation(),
        VirtualMicrophoneBackendEventKind::PhysicalMixExited,
        "injected helper exit",
    });
    service.poll(3);
    require(backend.startPhysicalCalls == 2 &&
                backend.selectedPhysicalNode == "alsa_input.mic-b",
            "an exited physical helper must be reconstructed for the exact selected source");
}

void noiseSuppressionSwitchesOnlyThePhysicalMicrophoneRoute()
{
    FakeBackend backend;
    makeVirtualSourceVisible(backend);
    backend.nodes.push_back(node(61, "alsa_input.internal", "Built-in Microphone"));
    LinuxVirtualMicrophoneService service(backend, "noise-switch", 1000);
    auto configuration = virtualOnly();
    configuration.mixPhysicalMicrophone = true;
    configuration.physicalMicrophoneId = "alsa_input.internal";

    require(!configuration.noiseSuppression,
            "noise suppression must be off in the default configuration");
    require(service.apply(configuration, 0), "default microphone mixing must start");
    require(!backend.physicalNoiseSuppression && backend.startPhysicalCalls == 1,
            "the default microphone route must retain pw-loopback behavior");

    configuration.noiseSuppression = true;
    require(service.apply(configuration, 1), "suppression must enable without bridge restart");
    require(backend.physicalNoiseSuppression && backend.startPhysicalCalls == 2
                && backend.startBridgeCalls == 1,
            "enabling must restart only the physical microphone path");
    require(service.configuration().noiseSuppression,
            "the effective configuration must report suppression on");

    configuration.noiseSuppression = false;
    require(service.apply(configuration, 2), "suppression must disable immediately");
    require(!backend.physicalNoiseSuppression && backend.startPhysicalCalls == 3
                && backend.startBridgeCalls == 1,
            "disabling must restore the ordinary microphone loopback path");
}

void missingRnnoisePluginPreservesVirtualSourceAndMicrophone()
{
    FakeBackend backend;
    backend.capabilitiesValue.rnnoiseAvailable = false;
    makeVirtualSourceVisible(backend);
    backend.nodes.push_back(node(61, "alsa_input.internal", "Built-in Microphone"));
    LinuxVirtualMicrophoneService service(backend, "missing-rnnoise", 1000);
    auto configuration = virtualOnly();
    configuration.mixPhysicalMicrophone = true;
    configuration.physicalMicrophoneId = "alsa_input.internal";
    configuration.noiseSuppression = true;

    require(service.apply(configuration, 0),
            "missing optional plugin must not prevent normal virtual microphone use");
    require(backend.bridgeRunningValue && backend.physicalRunningValue
                && !backend.physicalNoiseSuppression,
            "missing plugin must use the existing physical microphone loopback");
    require(!service.configuration().noiseSuppression,
            "the effective suppression setting must be disabled");
    require(service.status().message.find("audiornnoise") != std::string::npos,
            "the status must explain which optional plugin is missing");
    service.poll(1);
    require(service.status().message.find("audiornnoise") != std::string::npos,
            "the diagnostic must remain visible during polling");

    backend.capabilitiesValue.rnnoiseAvailable = true;
    service.poll(2);
    require(!service.configuration().noiseSuppression
                && !backend.physicalNoiseSuppression
                && service.status().message.find("audiornnoise") == std::string::npos,
            "installing the plugin must clear the stale diagnostic without enabling suppression");
}

void rnnoiseLossRestartsOnlyPhysicalRouteWithoutStateCarryover()
{
    FakeBackend backend;
    makeVirtualSourceVisible(backend);
    backend.nodes.push_back(node(61, "alsa_input.internal", "Built-in Microphone"));
    LinuxVirtualMicrophoneService service(backend, "rnnoise-loss", 1000);
    auto configuration = virtualOnly();
    configuration.mixPhysicalMicrophone = true;
    configuration.physicalMicrophoneId = "alsa_input.internal";
    configuration.noiseSuppression = true;
    require(service.apply(configuration, 0), "processed route must start");
    require(backend.physicalNoiseSuppression, "processed route must be selected");

    backend.capabilitiesValue.rnnoiseAvailable = false;
    service.poll(1);
    require(backend.bridgeRunningValue && backend.startBridgeCalls == 1
                && backend.startPhysicalCalls == 2 && !backend.physicalNoiseSuppression,
            "losing the plugin must replace only the microphone route with loopback");
    require(!service.configuration().noiseSuppression,
            "the effective setting must become off when the plugin disappears");
    require(service.status().message.find("audiornnoise") != std::string::npos,
            "plugin loss must report a useful diagnostic");
}

void processedStartupFailureFallsBackToOrdinaryMicrophoneMix()
{
    FakeBackend backend;
    backend.startProcessedSucceeds = false;
    makeVirtualSourceVisible(backend);
    backend.nodes.push_back(node(61, "alsa_input.internal", "Built-in Microphone"));
    LinuxVirtualMicrophoneService service(backend, "processed-start-failure", 1000);
    auto configuration = virtualOnly();
    configuration.mixPhysicalMicrophone = true;
    configuration.physicalMicrophoneId = "alsa_input.internal";
    configuration.noiseSuppression = true;

    require(service.apply(configuration, 0),
            "failed optional suppression startup must preserve the virtual source");
    require(backend.startPhysicalCalls == 2 && backend.physicalRunningValue
                && !backend.physicalNoiseSuppression && backend.startBridgeCalls == 1,
            "processed startup failure must immediately start ordinary microphone loopback");
    require(!service.configuration().noiseSuppression,
            "startup failure must disable effective suppression");
    require(service.status().message.find("failed to start") != std::string::npos,
            "startup failure must have a concise diagnostic");
    service.poll(1);
    require(backend.startPhysicalCalls == 2 &&
                service.status().message.find("failed to start") != std::string::npos,
            "polling must neither retry suppression nor erase its diagnostic");

    backend.startProcessedSucceeds = true;
    require(service.apply(configuration, 2),
            "an explicit later On request must be able to retry suppression");
    require(service.configuration().noiseSuppression &&
                backend.physicalNoiseSuppression && backend.startPhysicalCalls == 3,
            "the later On request must replace bypass with a newly started processed route");
}

void processedRuntimeFailureFallsBackAndReportsBackendError()
{
    FakeBackend backend;
    makeVirtualSourceVisible(backend);
    backend.nodes.push_back(node(61, "alsa_input.internal", "Built-in Microphone"));
    LinuxVirtualMicrophoneService service(backend, "processed-runtime-failure", 1000);
    auto configuration = virtualOnly();
    configuration.mixPhysicalMicrophone = true;
    configuration.physicalMicrophoneId = "alsa_input.internal";
    configuration.noiseSuppression = true;
    require(service.apply(configuration, 0), "processed route must start");

    backend.physicalRunningValue = false;
    backend.events.push_back({
        service.generation(),
        VirtualMicrophoneBackendEventKind::PhysicalMixExited,
        "injected GStreamer pipeline error \xC3\xA9",
    });
    service.poll(1);
    require(backend.startPhysicalCalls == 2 && backend.physicalRunningValue
                && !backend.physicalNoiseSuppression && backend.startBridgeCalls == 1,
            "processed runtime failure must restore ordinary microphone loopback");
    require(!service.configuration().noiseSuppression,
            "runtime failure must disable effective suppression");
    require(service.status().message.find("injected GStreamer pipeline error") !=
                std::string::npos,
            "runtime failure must expose the backend error");
    require(service.status().message.find("\xC3") == std::string::npos,
            "the bounded diagnostic must not expose partial UTF-8 to the GTK subtitle");
    service.poll(2);
    require(backend.startPhysicalCalls == 2,
            "a later poll must not retry the failed processed route");
}

void suppressionNeverProcessesSoundboardOnlyRoute()
{
    FakeBackend backend;
    makeVirtualSourceVisible(backend);
    LinuxVirtualMicrophoneService service(backend, "soundboard-only", 1000);
    auto configuration = virtualOnly();
    configuration.noiseSuppression = true;

    require(service.apply(configuration, 0),
            "virtual microphone can run without a physical microphone mix");
    require(backend.bridgeRunningValue && backend.startPhysicalCalls == 0,
            "the soundboard-only bridge must never enter the suppressor");
}

void processedMicrophoneDeviceChangeStartsFreshRoute()
{
    FakeBackend backend;
    makeVirtualSourceVisible(backend);
    backend.nodes.push_back(node(61, "alsa_input.mic-a", "Microphone A"));
    backend.nodes.push_back(node(62, "alsa_input.mic-b", "Microphone B"));
    LinuxVirtualMicrophoneService service(backend, "processed-device-change", 1000);
    auto configuration = virtualOnly();
    configuration.mixPhysicalMicrophone = true;
    configuration.noiseSuppression = true;
    configuration.physicalMicrophoneId = "alsa_input.mic-a";
    require(service.apply(configuration, 0), "first processed microphone must start");

    configuration.physicalMicrophoneId = "alsa_input.mic-b";
    require(service.apply(configuration, 1), "second processed microphone must start");
    require(backend.stopPhysicalCalls >= 1 && backend.startPhysicalCalls == 2
                && backend.selectedPhysicalNode == "alsa_input.mic-b"
                && backend.physicalNoiseSuppression && backend.startBridgeCalls == 1,
            "changing devices must stop the old processed route before starting a new one");
}

void missingPhysicalSourceDegradesAndReconnectsExactly()
{
    FakeBackend backend;
    makeVirtualSourceVisible(backend);
    backend.nodes.push_back(node(61, "alsa_input.usb", "USB Microphone"));
    LinuxVirtualMicrophoneService service(backend, "hotplug", 1000);
    auto configuration = virtualOnly();
    configuration.mixPhysicalMicrophone = true;
    configuration.physicalMicrophoneId = "alsa_input.usb";
    require(service.apply(configuration, 0), "the initial microphone must start");

    backend.nodes.erase(
        std::remove_if(backend.nodes.begin(), backend.nodes.end(), [](const auto& item) {
            return item.stableName == "alsa_input.usb";
        }),
        backend.nodes.end());
    service.poll(10);
    require(service.status().state ==
                VirtualMicrophoneState::DegradedMicrophoneUnavailable,
            "removing the selected microphone must degrade without stopping sound injection");
    require(backend.bridgeRunningValue && !backend.physicalRunningValue,
            "the bridge must survive while microphone capture stops");

    backend.nodes.push_back(node(1061, "alsa_input.usb", "USB Microphone Renamed"));
    service.poll(20);
    require(service.status().state == VirtualMicrophoneState::Ready,
            "the exact stable source returning must recover mixing");
    require(backend.startPhysicalCalls == 2
                && backend.selectedPhysicalNode == "alsa_input.usb",
            "recovery must not silently substitute another microphone");
}

void disableReapsAnExitedPhysicalHelperBeforeReenable()
{
    FakeBackend backend;
    makeVirtualSourceVisible(backend);
    backend.nodes.push_back(node(61, "alsa_input.internal", "Built-in Microphone"));
    LinuxVirtualMicrophoneService service(backend, "dead-physical-helper", 1000);

    auto configuration = virtualOnly();
    configuration.mixPhysicalMicrophone = true;
    configuration.physicalMicrophoneId = "alsa_input.internal";
    require(service.apply(configuration, 0), "the initial physical mix must start");
    require(backend.physicalHandleRetained,
            "the backend must retain its exact helper handle while active");

    backend.physicalRunningValue = false;
    VirtualMicrophoneConfiguration speakersOnly;
    require(service.apply(speakersOnly, 1), "disabling must remain safe after helper exit");
    require(!backend.physicalHandleRetained,
            "disabling must reap an exited helper handle");

    require(service.apply(configuration, 2),
            "physical mixing must restart without restarting Cuelet");
    require(service.status().state == VirtualMicrophoneState::Ready,
            "re-enabled physical mixing must become ready");
}

void pipeWireDisconnectReconnectAndStaleEventsAreSafe()
{
    FakeBackend backend;
    makeVirtualSourceVisible(backend);
    LinuxVirtualMicrophoneService service(backend, "reconnect", 1000);
    require(service.apply(virtualOnly(), 0), "the initial route must start");
    const auto oldGeneration = service.generation();

    backend.capabilitiesValue.pipeWireSessionReachable = false;
    service.poll(10);
    require(service.status().state == VirtualMicrophoneState::PipeWireUnavailable,
            "a PipeWire disconnect must be visible");
    require(!backend.bridgeRunningValue && backend.stopBridgeCalls == 1,
            "disconnect must discard an alive but potentially stale bridge");

    backend.capabilitiesValue.pipeWireSessionReachable = true;
    service.poll(20);
    require(backend.startBridgeCalls == 2,
            "a returning PipeWire session must reconstruct the owned graph once");
    require(service.status().state == VirtualMicrophoneState::Ready,
            "a visible reconstructed source must become ready");

    backend.events.push_back({oldGeneration, VirtualMicrophoneBackendEventKind::BridgeExited,
                              "stale helper exit"});
    service.poll(30);
    require(backend.startBridgeCalls == 2,
            "an event from a replaced generation must not tear down the current graph");

    backend.events.push_back({service.generation(),
                              VirtualMicrophoneBackendEventKind::BridgeExited,
                              "current helper exited"});
    backend.bridgeRunningValue = false;
    service.poll(40);
    require(backend.startBridgeCalls == 3,
            "a current helper exit must trigger one controlled reconstruction");
}

} // namespace

int main()
{
    try {
        initialStateIsDisabledAndSideEffectFree();
        physicalSourcesAreFilteredAndStableAcrossNumericIds();
        physicalLoopbackArgumentsAreScopedAndShellFree();
        successfulCreationAndIdempotentApply();
        creationFailureAndStartupTimeoutFailClosed();
        failedRequestRecoversUnlessExplicitlyDisabled();
        modeTransitionsAndCleanupAreScoped();
        physicalMixUsesOnlyTheSelectedStableSource();
        noiseSuppressionSwitchesOnlyThePhysicalMicrophoneRoute();
        missingRnnoisePluginPreservesVirtualSourceAndMicrophone();
        rnnoiseLossRestartsOnlyPhysicalRouteWithoutStateCarryover();
        processedStartupFailureFallsBackToOrdinaryMicrophoneMix();
        processedRuntimeFailureFallsBackAndReportsBackendError();
        suppressionNeverProcessesSoundboardOnlyRoute();
        processedMicrophoneDeviceChangeStartsFreshRoute();
        missingPhysicalSourceDegradesAndReconnectsExactly();
        disableReapsAnExitedPhysicalHelperBeforeReenable();
        pipeWireDisconnectReconnectAndStaleEventsAreSafe();
    } catch (const std::exception& error) {
        std::cerr << "cuelet virtual microphone tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "cuelet virtual microphone tests passed\n";
    return EXIT_SUCCESS;
}
