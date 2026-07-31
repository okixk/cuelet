#include "services/LinuxGlobalShortcutsPortal.h"

#include <gtk/gtk.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cuelet_linux {
namespace {

bool shortcutIdentityEqual(
    const std::vector<PortalShortcutSpec>& left,
    const std::vector<PortalShortcutSpec>& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].portalId != right[index].portalId
            || left[index].preferredTrigger != right[index].preferredTrigger) {
            return false;
        }
    }
    return true;
}

std::vector<PortalShortcutSpec> normalizedSpecs(
    std::vector<PortalShortcutSpec> shortcuts)
{
    shortcuts.erase(
        std::remove_if(shortcuts.begin(), shortcuts.end(), [](const auto& shortcut) {
            return shortcut.soundId.empty() || shortcut.portalId.empty();
        }),
        shortcuts.end());
    std::sort(shortcuts.begin(), shortcuts.end(), [](const auto& left, const auto& right) {
        return left.portalId < right.portalId;
    });
    shortcuts.erase(
        std::unique(shortcuts.begin(), shortcuts.end(), [](const auto& left, const auto& right) {
            return left.portalId == right.portalId;
        }),
        shortcuts.end());
    return shortcuts;
}

std::string portalIdSoundId(const std::string& portalId)
{
    constexpr const char* prefix = "sound.";
    return portalId.rfind(prefix, 0) == 0
        ? portalId.substr(std::char_traits<char>::length(prefix))
        : std::string{};
}

} // namespace

std::string portalShortcutId(const std::string& soundId)
{
    return soundId.empty() ? std::string{} : "sound." + soundId;
}

std::string portalPreferredTrigger(const cuelet::Shortcut& shortcut)
{
    if (shortcut.empty()) {
        return {};
    }

    std::string trigger;
    if ((shortcut.modifiers & GDK_CONTROL_MASK) != 0) {
        trigger += "CTRL+";
    }
    if ((shortcut.modifiers & GDK_ALT_MASK) != 0) {
        trigger += "ALT+";
    }
    if ((shortcut.modifiers & GDK_SHIFT_MASK) != 0) {
        trigger += "SHIFT+";
    }
    if ((shortcut.modifiers & GDK_SUPER_MASK) != 0) {
        trigger += "LOGO+";
    }

    const guint normalizedKey = gdk_keyval_to_lower(shortcut.keyval);
    const char* keyName = gdk_keyval_name(normalizedKey);
    if (!keyName || !*keyName) {
        return {};
    }
    for (const unsigned char character : std::string(keyName)) {
        if (std::isalnum(character) == 0 && character != '_') {
            return {};
        }
    }
    trigger += keyName;
    return trigger;
}

std::vector<PortalShortcutSpec> portalShortcutSpecs(
    const std::vector<cuelet::SoundClip>& clips)
{
    std::vector<PortalShortcutSpec> shortcuts;
    std::unordered_set<std::string> seenSoundIds;
    for (const auto& clip : clips) {
        if (clip.id.empty()
            || !clip.shortcut
            || clip.shortcut->empty()
            || !clip.shortcut->global
            || !seenSoundIds.insert(clip.id).second) {
            continue;
        }
        shortcuts.push_back(PortalShortcutSpec{
            clip.id,
            portalShortcutId(clip.id),
            "Play “" + clip.searchableName() + "” in Cuelet",
            portalPreferredTrigger(*clip.shortcut),
        });
    }
    return normalizedSpecs(std::move(shortcuts));
}

const cuelet::SoundClip* soundByStableId(
    const std::vector<cuelet::SoundClip>& clips,
    const std::string& soundId)
{
    const auto found = std::find_if(clips.begin(), clips.end(), [&](const auto& clip) {
        return !soundId.empty() && clip.id == soundId;
    });
    return found == clips.end() ? nullptr : &*found;
}

cuelet::SoundClip* soundByStableId(
    std::vector<cuelet::SoundClip>& clips,
    const std::string& soundId)
{
    const auto found = std::find_if(clips.begin(), clips.end(), [&](const auto& clip) {
        return !soundId.empty() && clip.id == soundId;
    });
    return found == clips.end() ? nullptr : &*found;
}

bool shouldHandleShortcutLocally(
    const cuelet::Shortcut& shortcut,
    const std::optional<PortalShortcutStatus>& portalStatus)
{
    return !shortcut.global
        || !portalStatus
        || portalStatus->state != PortalShortcutState::Active;
}

std::string portalShortcutStateLabel(PortalShortcutState state)
{
    switch (state) {
    case PortalShortcutState::Pending: return "Pending portal confirmation";
    case PortalShortcutState::Active: return "Active";
    case PortalShortcutState::Denied: return "Denied or not approved";
    case PortalShortcutState::Unavailable: return "Portal unavailable";
    case PortalShortcutState::Disconnected: return "Portal disconnected";
    case PortalShortcutState::Error: return "Portal registration failed";
    }
    return "Unknown";
}

struct LinuxGlobalShortcutsController::Impl {
    std::weak_ptr<LinuxGlobalShortcutsController> owner;
    std::shared_ptr<GlobalShortcutsPortalAdapter> adapter;
    ActivationCallback activationCallback;
    StatusChangedCallback statusChangedCallback;
    unsigned int reconfigureDelayMilliseconds = 750;
    unsigned int version = 0;
    guint reconfigureSourceId = 0;
    std::uint64_t generation = 0;
    PortalOverallState overallState = PortalOverallState::NotStarted;
    std::vector<PortalShortcutSpec> desired;
    std::map<std::string, PortalShortcutStatus> statusesBySoundId;
    std::map<std::string, PortalShortcutBinding> bindingsByPortalId;
    std::unordered_set<std::string> heldPortalIds;
    std::string parentWindow;
    std::string sessionHandle;
    bool started = false;
    bool available = false;
    bool sessionAttemptInFlight = false;
    bool shuttingDown = false;

    void notifyStatusChanged()
    {
        if (statusChangedCallback) {
            statusChangedCallback();
        }
    }

    void cancelReconfigureTimer()
    {
        if (reconfigureSourceId != 0) {
            g_source_remove(reconfigureSourceId);
            reconfigureSourceId = 0;
        }
    }

    void setEveryDesiredStatus(
        PortalShortcutState state,
        const std::string& detail,
        bool retainTrigger = false)
    {
        std::map<std::string, PortalShortcutStatus> updated;
        for (const auto& shortcut : desired) {
            PortalShortcutStatus status;
            status.state = state;
            status.detail = detail;
            const auto previous = statusesBySoundId.find(shortcut.soundId);
            if (retainTrigger && previous != statusesBySoundId.end()) {
                status.triggerDescription = previous->second.triggerDescription;
            }
            updated[shortcut.soundId] = std::move(status);
        }
        statusesBySoundId = std::move(updated);
    }

    void failAttempt(
        const PortalOperationResult& operation,
        const std::string& defaultMessage)
    {
        sessionAttemptInFlight = false;
        const bool denied = operation.outcome == PortalResponseOutcome::Cancelled;
        overallState = denied ? PortalOverallState::Denied : PortalOverallState::Error;
        setEveryDesiredStatus(
            denied ? PortalShortcutState::Denied : PortalShortcutState::Error,
            operation.error.empty() ? defaultMessage : operation.error);
        notifyStatusChanged();
    }

    void applyBindings(const std::vector<PortalShortcutBinding>& bindings)
    {
        bindingsByPortalId.clear();
        for (const auto& binding : bindings) {
            const auto desiredShortcut = std::find_if(
                desired.begin(), desired.end(), [&](const auto& shortcut) {
                    return shortcut.portalId == binding.portalId;
                });
            if (desiredShortcut != desired.end()) {
                bindingsByPortalId[binding.portalId] = binding;
            }
        }

        std::size_t activeCount = 0;
        std::map<std::string, PortalShortcutStatus> updated;
        for (const auto& shortcut : desired) {
            PortalShortcutStatus status;
            const auto binding = bindingsByPortalId.find(shortcut.portalId);
            if (binding == bindingsByPortalId.end()) {
                status.state = PortalShortcutState::Denied;
                status.detail = "The desktop did not approve this shortcut.";
            } else {
                ++activeCount;
                status.state = PortalShortcutState::Active;
                status.triggerDescription = binding->second.triggerDescription;
                status.detail = "Confirmed by the desktop portal.";
            }
            updated[shortcut.soundId] = std::move(status);
        }
        statusesBySoundId = std::move(updated);
        if (activeCount == desired.size() && activeCount > 0) {
            overallState = PortalOverallState::Active;
        } else if (activeCount > 0) {
            overallState = PortalOverallState::Partial;
        } else {
            overallState = PortalOverallState::Denied;
        }
        notifyStatusChanged();
    }

    void bindCurrentSession(std::uint64_t expectedGeneration)
    {
        if (shuttingDown
            || expectedGeneration != generation
            || sessionHandle.empty()) {
            return;
        }
        const std::string expectedSession = sessionHandle;
        auto weakOwner = owner;
        adapter->bindShortcuts(
            expectedSession,
            desired,
            parentWindow,
            [weakOwner, expectedGeneration, expectedSession](const PortalBindingsResult& result) {
                const auto owner = weakOwner.lock();
                if (!owner) {
                    return;
                }
                auto& impl = *owner->impl_;
                if (impl.shuttingDown
                    || expectedGeneration != impl.generation
                    || expectedSession != impl.sessionHandle) {
                    return;
                }
                impl.sessionAttemptInFlight = false;
                if (result.operation.outcome != PortalResponseOutcome::Success) {
                    impl.failAttempt(result.operation, "The portal did not register the shortcuts.");
                    return;
                }
                impl.applyBindings(result.bindings);
            });
    }

    void listRestoredShortcuts(std::uint64_t expectedGeneration)
    {
        const std::string expectedSession = sessionHandle;
        auto weakOwner = owner;
        adapter->listShortcuts(
            expectedSession,
            [weakOwner, expectedGeneration, expectedSession](const PortalBindingsResult& result) {
                const auto owner = weakOwner.lock();
                if (!owner) {
                    return;
                }
                auto& impl = *owner->impl_;
                if (impl.shuttingDown
                    || expectedGeneration != impl.generation
                    || expectedSession != impl.sessionHandle) {
                    return;
                }

                // Restored values are useful display hints, but remain pending
                // until this session's single BindShortcuts request succeeds.
                if (result.operation.outcome == PortalResponseOutcome::Success) {
                    for (const auto& binding : result.bindings) {
                        const std::string soundId = portalIdSoundId(binding.portalId);
                        auto status = impl.statusesBySoundId.find(soundId);
                        if (status != impl.statusesBySoundId.end()) {
                            status->second.triggerDescription = binding.triggerDescription;
                        }
                    }
                    impl.notifyStatusChanged();
                }
                impl.bindCurrentSession(expectedGeneration);
            });
    }

    void beginSession()
    {
        if (shuttingDown
            || !started
            || !available
            || desired.empty()
            || sessionAttemptInFlight
            || !sessionHandle.empty()) {
            return;
        }
        ++generation;
        const std::uint64_t expectedGeneration = generation;
        sessionAttemptInFlight = true;
        overallState = PortalOverallState::Connecting;
        setEveryDesiredStatus(
            PortalShortcutState::Pending,
            "Creating a desktop portal shortcut session.",
            true);
        notifyStatusChanged();

        auto weakOwner = owner;
        adapter->createSession([weakOwner, expectedGeneration](const PortalSessionResult& result) {
            const auto owner = weakOwner.lock();
            if (!owner) {
                return;
            }
            auto& impl = *owner->impl_;
            if (impl.shuttingDown || expectedGeneration != impl.generation) {
                return;
            }
            if (result.operation.outcome != PortalResponseOutcome::Success
                || result.sessionHandle.empty()) {
                impl.failAttempt(result.operation, "Could not create a portal shortcut session.");
                return;
            }
            impl.sessionHandle = result.sessionHandle;
            impl.overallState = PortalOverallState::Pending;
            impl.setEveryDesiredStatus(
                PortalShortcutState::Pending,
                "Waiting for desktop portal confirmation.",
                true);
            impl.notifyStatusChanged();
            impl.listRestoredShortcuts(expectedGeneration);
        });
    }

    void replaceSession()
    {
        cancelReconfigureTimer();
        if (shuttingDown) {
            return;
        }
        ++generation;
        adapter->cancelOutstandingRequests();
        if (!sessionHandle.empty()) {
            adapter->closeSession(sessionHandle);
        }
        sessionHandle.clear();
        sessionAttemptInFlight = false;
        bindingsByPortalId.clear();
        heldPortalIds.clear();
        if (desired.empty()) {
            statusesBySoundId.clear();
            overallState = available
                ? PortalOverallState::NotStarted
                : PortalOverallState::Unavailable;
            notifyStatusChanged();
            return;
        }
        beginSession();
    }

    void scheduleReplacement()
    {
        if (reconfigureSourceId != 0 || shuttingDown) {
            return;
        }
        auto* weakOwner = new std::weak_ptr<LinuxGlobalShortcutsController>(owner);
        reconfigureSourceId = g_timeout_add_full(
            G_PRIORITY_DEFAULT,
            reconfigureDelayMilliseconds,
            +[](gpointer data) -> gboolean {
                auto* weak = static_cast<std::weak_ptr<LinuxGlobalShortcutsController>*>(data);
                if (const auto owner = weak->lock()) {
                    owner->impl_->reconfigureSourceId = 0;
                    owner->impl_->replaceSession();
                }
                return G_SOURCE_REMOVE;
            },
            weakOwner,
            +[](gpointer data) {
                delete static_cast<std::weak_ptr<LinuxGlobalShortcutsController>*>(data);
            });
    }

    void handleDetection(const PortalDetectionResult& result)
    {
        if (shuttingDown) {
            return;
        }
        version = result.version;
        available = result.available;
        if (!available) {
            ++generation;
            sessionAttemptInFlight = false;
            sessionHandle.clear();
            bindingsByPortalId.clear();
            heldPortalIds.clear();
            overallState = PortalOverallState::Unavailable;
            setEveryDesiredStatus(
                PortalShortcutState::Unavailable,
                result.error.empty()
                    ? "The GlobalShortcuts portal is unavailable."
                    : result.error);
            notifyStatusChanged();
            return;
        }
        if (desired.empty()) {
            overallState = PortalOverallState::NotStarted;
            notifyStatusChanged();
            return;
        }
        beginSession();
    }

    void handleDisconnected()
    {
        if (shuttingDown) {
            return;
        }
        ++generation;
        available = false;
        version = 0;
        sessionAttemptInFlight = false;
        sessionHandle.clear();
        bindingsByPortalId.clear();
        heldPortalIds.clear();
        overallState = PortalOverallState::Disconnected;
        setEveryDesiredStatus(
            PortalShortcutState::Disconnected,
            "The desktop portal disconnected. Use the GNOME command fallback until it returns.");
        notifyStatusChanged();
    }

    void handleActivated(
        const std::string& eventSession,
        const std::string& portalId)
    {
        if (shuttingDown
            || eventSession != sessionHandle
            || bindingsByPortalId.count(portalId) == 0
            || !heldPortalIds.insert(portalId).second) {
            return;
        }
        const auto desiredShortcut = std::find_if(
            desired.begin(), desired.end(), [&](const auto& shortcut) {
                return shortcut.portalId == portalId;
            });
        if (desiredShortcut != desired.end() && activationCallback) {
            activationCallback(desiredShortcut->soundId);
        }
    }

    void handleDeactivated(
        const std::string& eventSession,
        const std::string& portalId)
    {
        if (eventSession == sessionHandle) {
            heldPortalIds.erase(portalId);
        }
    }

    void handleShortcutsChanged(
        const std::string& eventSession,
        const std::vector<PortalShortcutBinding>& bindings)
    {
        if (!shuttingDown && eventSession == sessionHandle) {
            heldPortalIds.clear();
            applyBindings(bindings);
        }
    }

    void handleSessionClosed(const std::string& eventSession)
    {
        if (shuttingDown || eventSession != sessionHandle) {
            return;
        }
        ++generation;
        sessionHandle.clear();
        sessionAttemptInFlight = false;
        bindingsByPortalId.clear();
        heldPortalIds.clear();
        overallState = PortalOverallState::Disconnected;
        setEveryDesiredStatus(
            PortalShortcutState::Disconnected,
            "The desktop closed the shortcut session.");
        notifyStatusChanged();
    }
};

LinuxGlobalShortcutsController::LinuxGlobalShortcutsController(
    std::shared_ptr<GlobalShortcutsPortalAdapter> adapter,
    ActivationCallback activationCallback,
    StatusChangedCallback statusChangedCallback,
    unsigned int reconfigureDelayMilliseconds)
    : impl_(std::make_unique<Impl>())
{
    impl_->adapter = std::move(adapter);
    impl_->activationCallback = std::move(activationCallback);
    impl_->statusChangedCallback = std::move(statusChangedCallback);
    impl_->reconfigureDelayMilliseconds = reconfigureDelayMilliseconds;
}

std::shared_ptr<LinuxGlobalShortcutsController> LinuxGlobalShortcutsController::create(
    std::shared_ptr<GlobalShortcutsPortalAdapter> adapter,
    ActivationCallback activationCallback,
    StatusChangedCallback statusChangedCallback,
    unsigned int reconfigureDelayMilliseconds)
{
    auto controller = std::shared_ptr<LinuxGlobalShortcutsController>(
        new LinuxGlobalShortcutsController(
            std::move(adapter),
            std::move(activationCallback),
            std::move(statusChangedCallback),
            reconfigureDelayMilliseconds));
    controller->impl_->owner = controller;
    return controller;
}

LinuxGlobalShortcutsController::~LinuxGlobalShortcutsController()
{
    shutdown();
}

void LinuxGlobalShortcutsController::start()
{
    if (impl_->started || impl_->shuttingDown || !impl_->adapter) {
        return;
    }
    impl_->started = true;
    auto weakOwner = impl_->owner;
    PortalEventSink sink;
    sink.activated = [weakOwner](const std::string& session,
                                const std::string& shortcutId,
                                std::uint64_t) {
        if (const auto owner = weakOwner.lock()) {
            owner->impl_->handleActivated(session, shortcutId);
        }
    };
    sink.deactivated = [weakOwner](const std::string& session,
                                  const std::string& shortcutId,
                                  std::uint64_t) {
        if (const auto owner = weakOwner.lock()) {
            owner->impl_->handleDeactivated(session, shortcutId);
        }
    };
    sink.shortcutsChanged = [weakOwner](
                                const std::string& session,
                                const std::vector<PortalShortcutBinding>& bindings) {
        if (const auto owner = weakOwner.lock()) {
            owner->impl_->handleShortcutsChanged(session, bindings);
        }
    };
    sink.sessionClosed = [weakOwner](const std::string& session) {
        if (const auto owner = weakOwner.lock()) {
            owner->impl_->handleSessionClosed(session);
        }
    };
    sink.disconnected = [weakOwner] {
        if (const auto owner = weakOwner.lock()) {
            owner->impl_->handleDisconnected();
        }
    };
    impl_->adapter->start(
        [weakOwner](const PortalDetectionResult& result) {
            if (const auto owner = weakOwner.lock()) {
                owner->impl_->handleDetection(result);
            }
        },
        std::move(sink));
}

void LinuxGlobalShortcutsController::setDesiredShortcuts(
    std::vector<PortalShortcutSpec> shortcuts,
    std::string parentWindow)
{
    shortcuts = normalizedSpecs(std::move(shortcuts));
    const bool identityChanged = !shortcutIdentityEqual(impl_->desired, shortcuts);
    impl_->desired = std::move(shortcuts);
    if (!parentWindow.empty()) {
        impl_->parentWindow = std::move(parentWindow);
    }

    std::map<std::string, PortalShortcutStatus> retained;
    for (const auto& shortcut : impl_->desired) {
        const auto status = impl_->statusesBySoundId.find(shortcut.soundId);
        if (status != impl_->statusesBySoundId.end()) {
            retained[shortcut.soundId] = status->second;
        } else {
            retained[shortcut.soundId] = PortalShortcutStatus{
                impl_->available
                    ? PortalShortcutState::Pending
                    : PortalShortcutState::Unavailable,
                {},
                impl_->available
                    ? "Pending portal registration."
                    : "The GlobalShortcuts portal is not connected.",
            };
        }
    }
    impl_->statusesBySoundId = std::move(retained);

    if (!identityChanged || impl_->shuttingDown) {
        impl_->notifyStatusChanged();
        return;
    }
    if (impl_->desired.empty()) {
        impl_->replaceSession();
        return;
    }
    if (!impl_->started || !impl_->available) {
        impl_->notifyStatusChanged();
        return;
    }
    if (!impl_->sessionHandle.empty() || impl_->sessionAttemptInFlight) {
        impl_->scheduleReplacement();
        impl_->notifyStatusChanged();
        return;
    }
    impl_->beginSession();
}

void LinuxGlobalShortcutsController::configureShortcuts(
    GlobalShortcutsPortalAdapter::OperationCallback callback)
{
    if (impl_->version < 2 || impl_->sessionHandle.empty()) {
        callback(PortalOperationResult{
            PortalResponseOutcome::Failed,
            "ConfigureShortcuts requires portal version 2 and an active session.",
        });
        return;
    }
    const std::uint64_t expectedGeneration = impl_->generation;
    auto weakOwner = impl_->owner;
    impl_->adapter->configureShortcuts(
        impl_->sessionHandle,
        impl_->parentWindow,
        [weakOwner, expectedGeneration, callback = std::move(callback)](
            const PortalOperationResult& result) {
            const auto owner = weakOwner.lock();
            if (!owner
                || owner->impl_->shuttingDown
                || owner->impl_->generation != expectedGeneration) {
                return;
            }
            callback(result);
        });
}

void LinuxGlobalShortcutsController::shutdown()
{
    if (!impl_ || impl_->shuttingDown) {
        return;
    }
    impl_->shuttingDown = true;
    ++impl_->generation;
    impl_->cancelReconfigureTimer();
    if (impl_->adapter) {
        impl_->adapter->cancelOutstandingRequests();
        if (!impl_->sessionHandle.empty()) {
            impl_->adapter->closeSession(impl_->sessionHandle);
        }
        impl_->adapter->stop();
    }
    impl_->sessionHandle.clear();
    impl_->sessionAttemptInFlight = false;
    impl_->bindingsByPortalId.clear();
    impl_->heldPortalIds.clear();
    impl_->overallState = PortalOverallState::Stopped;
}

PortalOverallState LinuxGlobalShortcutsController::overallState() const
{
    return impl_->overallState;
}

unsigned int LinuxGlobalShortcutsController::portalVersion() const
{
    return impl_->version;
}

bool LinuxGlobalShortcutsController::portalAvailable() const
{
    return impl_->available;
}

bool LinuxGlobalShortcutsController::hasLiveSession() const
{
    return !impl_->sessionHandle.empty();
}

std::optional<PortalShortcutStatus> LinuxGlobalShortcutsController::statusForSound(
    const std::string& soundId) const
{
    const auto found = impl_->statusesBySoundId.find(soundId);
    return found == impl_->statusesBySoundId.end()
        ? std::nullopt
        : std::optional<PortalShortcutStatus>(found->second);
}

std::vector<PortalShortcutBinding> LinuxGlobalShortcutsController::activeBindings() const
{
    std::vector<PortalShortcutBinding> bindings;
    bindings.reserve(impl_->bindingsByPortalId.size());
    for (const auto& [id, binding] : impl_->bindingsByPortalId) {
        (void)id;
        bindings.push_back(binding);
    }
    return bindings;
}

void LinuxGlobalShortcutsController::flushReconfigurationForTesting()
{
    if (impl_->reconfigureSourceId != 0) {
        g_source_remove(impl_->reconfigureSourceId);
        impl_->reconfigureSourceId = 0;
        impl_->replaceSession();
    }
}

} // namespace cuelet_linux
