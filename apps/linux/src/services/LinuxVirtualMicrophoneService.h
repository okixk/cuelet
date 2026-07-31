#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cuelet_linux {

struct PipeWireNodeInfo {
    std::uint32_t numericId = 0;
    std::string stableName;
    std::string description;
    std::string mediaClass;
    std::string deviceClass;
    bool virtualNode = false;
    bool available = true;
};

struct PhysicalMicrophoneInfo {
    std::string stableId;
    std::string description;
    std::uint32_t numericId = 0;
};

std::vector<PhysicalMicrophoneInfo> physicalMicrophonesFromNodes(
    const std::vector<PipeWireNodeInfo>& nodes);

// Returns direct argv for the app-owned physical-microphone bridge. Invalid
// or unsafe node names produce an empty vector; no shell syntax is generated.
std::vector<std::string> physicalMicrophoneLoopbackArguments(
    const std::string& sourceNode,
    const std::string& virtualSinkNode,
    double level);

enum class VirtualMicrophoneRoutingMode {
    SpeakersOnly,
    VirtualMicrophoneOnly,
    SpeakersAndVirtualMicrophone,
};

enum class VirtualMicrophoneState {
    Off,
    Starting,
    Ready,
    DegradedMicrophoneUnavailable,
    PipeWireUnavailable,
    Failed,
    Reconnecting,
};

struct VirtualMicrophoneConfiguration {
    VirtualMicrophoneRoutingMode mode = VirtualMicrophoneRoutingMode::SpeakersOnly;
    bool mixPhysicalMicrophone = false;
    std::string physicalMicrophoneId;
    double soundboardLevel = 0.25;
    double physicalMicrophoneLevel = 0.25;
};

struct VirtualMicrophoneStatus {
    VirtualMicrophoneState state = VirtualMicrophoneState::Off;
    bool virtualSourceVisible = false;
    bool physicalMicrophoneActive = false;
    std::string message = "Off";
};

struct VirtualMicrophoneCapabilities {
    bool pipeWireSessionReachable = false;
    bool pwLoopbackAvailable = false;
    bool pipeWireGStreamerAvailable = false;
};

enum class VirtualMicrophoneBackendEventKind {
    BridgeExited,
    PhysicalMixExited,
};

struct VirtualMicrophoneBackendEvent {
    std::uint64_t generation = 0;
    VirtualMicrophoneBackendEventKind kind =
        VirtualMicrophoneBackendEventKind::BridgeExited;
    std::string error;
};

class VirtualMicrophoneBackend {
public:
    virtual ~VirtualMicrophoneBackend() = default;

    virtual VirtualMicrophoneCapabilities capabilities() = 0;
    virtual bool startBridge(
        const std::string& sessionKey,
        std::uint64_t generation) = 0;
    virtual void stopBridge() = 0;
    virtual bool bridgeRunning() = 0;
    virtual std::string virtualSinkNode() const = 0;
    virtual std::string virtualSourceNode() const = 0;
    virtual std::vector<PipeWireNodeInfo> enumerateNodes() = 0;
    virtual bool startPhysicalMix(
        const std::string& sourceNode,
        const std::string& sinkNode,
        double level,
        std::uint64_t generation) = 0;
    virtual void setPhysicalMixLevel(double level) = 0;
    virtual void stopPhysicalMix() = 0;
    virtual bool physicalMixRunning() = 0;
    virtual std::vector<VirtualMicrophoneBackendEvent> takeEvents() = 0;
};

class LinuxVirtualMicrophoneService {
public:
    LinuxVirtualMicrophoneService(
        VirtualMicrophoneBackend& backend,
        std::string sessionKey,
        std::uint64_t startupTimeoutMilliseconds = 3000);
    ~LinuxVirtualMicrophoneService();

    LinuxVirtualMicrophoneService(const LinuxVirtualMicrophoneService&) = delete;
    LinuxVirtualMicrophoneService& operator=(const LinuxVirtualMicrophoneService&) = delete;

    bool apply(
        const VirtualMicrophoneConfiguration& configuration,
        std::uint64_t monotonicMilliseconds);
    void poll(std::uint64_t monotonicMilliseconds);
    void shutdown();

    const VirtualMicrophoneConfiguration& configuration() const;
    const VirtualMicrophoneStatus& status() const;
    std::vector<PhysicalMicrophoneInfo> physicalMicrophones();
    std::uint64_t generation() const;

private:
    bool startGraph(std::uint64_t monotonicMilliseconds, bool reconnecting);
    void refreshGraph(std::uint64_t monotonicMilliseconds);
    void refreshPhysicalMix();
    bool virtualSourceIsVisible(const std::vector<PipeWireNodeInfo>& nodes) const;
    void stopGraph();

    VirtualMicrophoneBackend& backend_;
    std::string sessionKey_;
    std::uint64_t startupTimeoutMilliseconds_ = 3000;
    std::uint64_t startupDeadlineMilliseconds_ = 0;
    std::uint64_t generation_ = 0;
    VirtualMicrophoneConfiguration configuration_;
    VirtualMicrophoneStatus status_;
    std::string activePhysicalMicrophoneId_;
    double activePhysicalMicrophoneLevel_ = 0.25;
    bool shutdown_ = false;
};

// Creates the live GStreamer/PipeWire implementation. Keeping this behind the
// interface lets ordinary tests remain completely independent of the desktop
// audio graph.
std::unique_ptr<VirtualMicrophoneBackend> makePipeWireVirtualMicrophoneBackend();

} // namespace cuelet_linux
