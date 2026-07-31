#pragma once

#include "cuelet/SoundTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cuelet_linux {

struct PortalShortcutSpec {
    std::string soundId;
    std::string portalId;
    std::string description;
    std::string preferredTrigger;
};

struct PortalShortcutBinding {
    std::string portalId;
    std::string description;
    std::string triggerDescription;
};

enum class PortalResponseOutcome {
    Success,
    Cancelled,
    Failed,
};

struct PortalOperationResult {
    PortalResponseOutcome outcome = PortalResponseOutcome::Failed;
    std::string error;
};

struct PortalDetectionResult {
    bool available = false;
    unsigned int version = 0;
    std::string error;
};

struct PortalSessionResult {
    PortalOperationResult operation;
    std::string sessionHandle;
};

struct PortalBindingsResult {
    PortalOperationResult operation;
    std::vector<PortalShortcutBinding> bindings;
};

struct PortalEventSink {
    std::function<void(const std::string&, const std::string&, std::uint64_t)> activated;
    std::function<void(const std::string&, const std::string&, std::uint64_t)> deactivated;
    std::function<void(const std::string&, const std::vector<PortalShortcutBinding>&)> shortcutsChanged;
    std::function<void(const std::string&)> sessionClosed;
    std::function<void()> disconnected;
};

class GlobalShortcutsPortalAdapter {
public:
    using DetectionCallback = std::function<void(const PortalDetectionResult&)>;
    using SessionCallback = std::function<void(const PortalSessionResult&)>;
    using BindingsCallback = std::function<void(const PortalBindingsResult&)>;
    using OperationCallback = std::function<void(const PortalOperationResult&)>;

    virtual ~GlobalShortcutsPortalAdapter() = default;

    virtual void start(DetectionCallback callback, PortalEventSink sink) = 0;
    virtual void createSession(SessionCallback callback) = 0;
    virtual void listShortcuts(
        const std::string& sessionHandle,
        BindingsCallback callback) = 0;
    virtual void bindShortcuts(
        const std::string& sessionHandle,
        const std::vector<PortalShortcutSpec>& shortcuts,
        const std::string& parentWindow,
        BindingsCallback callback) = 0;
    virtual void configureShortcuts(
        const std::string& sessionHandle,
        const std::string& parentWindow,
        OperationCallback callback) = 0;
    virtual void closeSession(const std::string& sessionHandle) = 0;
    virtual void cancelOutstandingRequests() = 0;
    virtual void stop() = 0;
};

enum class PortalShortcutState {
    Pending,
    Active,
    Denied,
    Unavailable,
    Disconnected,
    Error,
};

struct PortalShortcutStatus {
    PortalShortcutState state = PortalShortcutState::Unavailable;
    std::string triggerDescription;
    std::string detail;
};

enum class PortalOverallState {
    NotStarted,
    Unavailable,
    Connecting,
    Pending,
    Active,
    Partial,
    Denied,
    Disconnected,
    Error,
    Stopped,
};

std::string portalShortcutId(const std::string& soundId);
std::string portalPreferredTrigger(const cuelet::Shortcut& shortcut);
std::vector<PortalShortcutSpec> portalShortcutSpecs(
    const std::vector<cuelet::SoundClip>& clips);
const cuelet::SoundClip* soundByStableId(
    const std::vector<cuelet::SoundClip>& clips,
    const std::string& soundId);
cuelet::SoundClip* soundByStableId(
    std::vector<cuelet::SoundClip>& clips,
    const std::string& soundId);
bool shouldHandleShortcutLocally(
    const cuelet::Shortcut& shortcut,
    const std::optional<PortalShortcutStatus>& portalStatus);
std::string portalShortcutStateLabel(PortalShortcutState state);

class LinuxGlobalShortcutsController final
    : public std::enable_shared_from_this<LinuxGlobalShortcutsController> {
public:
    using ActivationCallback = std::function<void(const std::string& soundId)>;
    using StatusChangedCallback = std::function<void()>;

    static std::shared_ptr<LinuxGlobalShortcutsController> create(
        std::shared_ptr<GlobalShortcutsPortalAdapter> adapter,
        ActivationCallback activationCallback,
        StatusChangedCallback statusChangedCallback = {},
        unsigned int reconfigureDelayMilliseconds = 750);

    ~LinuxGlobalShortcutsController();

    LinuxGlobalShortcutsController(const LinuxGlobalShortcutsController&) = delete;
    LinuxGlobalShortcutsController& operator=(const LinuxGlobalShortcutsController&) = delete;

    void start();
    void setDesiredShortcuts(
        std::vector<PortalShortcutSpec> shortcuts,
        std::string parentWindow = {});
    void configureShortcuts(GlobalShortcutsPortalAdapter::OperationCallback callback);
    void shutdown();

    PortalOverallState overallState() const;
    unsigned int portalVersion() const;
    bool portalAvailable() const;
    bool hasLiveSession() const;
    std::optional<PortalShortcutStatus> statusForSound(
        const std::string& soundId) const;
    std::vector<PortalShortcutBinding> activeBindings() const;

    // Deterministic hook for unit tests; production uses the debounce timer.
    void flushReconfigurationForTesting();

private:
    LinuxGlobalShortcutsController(
        std::shared_ptr<GlobalShortcutsPortalAdapter> adapter,
        ActivationCallback activationCallback,
        StatusChangedCallback statusChangedCallback,
        unsigned int reconfigureDelayMilliseconds);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::shared_ptr<GlobalShortcutsPortalAdapter> makeGioGlobalShortcutsPortalAdapter();

} // namespace cuelet_linux
