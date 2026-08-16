#include "services/LinuxGlobalShortcutsPortal.h"
#include "TestSupport.h"

#include <gtk/gtk.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace cuelet_linux;

PortalOperationResult success()
{
    return {PortalResponseOutcome::Success, {}};
}

PortalOperationResult cancelled()
{
    return {PortalResponseOutcome::Cancelled, "User cancelled"};
}

PortalOperationResult failed(const std::string& error)
{
    return {PortalResponseOutcome::Failed, error};
}

class FakePortal final : public GlobalShortcutsPortalAdapter {
public:
    DetectionCallback detectionCallback;
    PortalEventSink sink;
    SessionCallback createCallback;
    BindingsCallback listCallback;
    BindingsCallback bindCallback;
    OperationCallback configureCallback;
    std::vector<PortalShortcutSpec> lastBound;
    std::vector<std::string> closedSessions;
    std::string lastListSession;
    std::string lastBindSession;
    std::string lastParentWindow;
    int startCalls = 0;
    int createCalls = 0;
    int listCalls = 0;
    int bindCalls = 0;
    int configureCalls = 0;
    int cancelCalls = 0;
    int stopCalls = 0;

    void start(DetectionCallback callback, PortalEventSink eventSink) override
    {
        ++startCalls;
        detectionCallback = std::move(callback);
        sink = std::move(eventSink);
    }

    void createSession(SessionCallback callback) override
    {
        ++createCalls;
        createCallback = std::move(callback);
    }

    void listShortcuts(
        const std::string& sessionHandle,
        BindingsCallback callback) override
    {
        ++listCalls;
        lastListSession = sessionHandle;
        listCallback = std::move(callback);
    }

    void bindShortcuts(
        const std::string& sessionHandle,
        const std::vector<PortalShortcutSpec>& shortcuts,
        const std::string& parentWindow,
        BindingsCallback callback) override
    {
        ++bindCalls;
        lastBindSession = sessionHandle;
        lastBound = shortcuts;
        lastParentWindow = parentWindow;
        bindCallback = std::move(callback);
    }

    void configureShortcuts(
        const std::string&,
        const std::string&,
        OperationCallback callback) override
    {
        ++configureCalls;
        configureCallback = std::move(callback);
    }

    void closeSession(const std::string& sessionHandle) override
    {
        closedSessions.push_back(sessionHandle);
    }

    void cancelOutstandingRequests() override
    {
        ++cancelCalls;
    }

    void stop() override
    {
        ++stopCalls;
    }

    void detect(bool available, unsigned int version, std::string error = {})
    {
        detectionCallback(PortalDetectionResult{available, version, std::move(error)});
    }

    void create(PortalOperationResult operation, const std::string& session)
    {
        auto callback = std::move(createCallback);
        callback(PortalSessionResult{std::move(operation), session});
    }

    void list(
        PortalOperationResult operation,
        std::vector<PortalShortcutBinding> bindings = {})
    {
        auto callback = std::move(listCallback);
        callback(PortalBindingsResult{std::move(operation), std::move(bindings)});
    }

    void bind(
        PortalOperationResult operation,
        std::vector<PortalShortcutBinding> bindings = {})
    {
        auto callback = std::move(bindCallback);
        callback(PortalBindingsResult{std::move(operation), std::move(bindings)});
    }
};

PortalShortcutSpec spec(
    const std::string& soundId,
    const std::string& trigger,
    const std::string& description = {})
{
    return {
        soundId,
        portalShortcutId(soundId),
        description.empty() ? "Play " + soundId : description,
        trigger,
    };
}

std::shared_ptr<LinuxGlobalShortcutsController> controllerFor(
    const std::shared_ptr<FakePortal>& portal,
    std::vector<std::string>& activations)
{
    return LinuxGlobalShortcutsController::create(
        portal,
        [&](const std::string& id) { activations.push_back(id); },
        {},
        60'000);
}

void stableIdsAndTriggersDoNotDependOnNames()
{
    cuelet::SoundClip clip;
    clip.id = "00000000-0000-4000-8000-abc123abc123";
    clip.displayName = "Original Name";
    clip.shortcut = cuelet::Shortcut{
        GDK_KEY_9,
        GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK | GDK_SUPER_MASK,
        "Ctrl+Alt+Super+Shift+9",
        true,
    };

    const auto specs = portalShortcutSpecs({clip});
    CUELET_REQUIRE(specs.size() == 1);
    CUELET_REQUIRE(specs[0].portalId == "sound." + clip.id);
    CUELET_REQUIRE(specs[0].preferredTrigger == "CTRL+ALT+SHIFT+LOGO+9");

    clip.displayName = "Renamed Sound";
    const auto renamed = portalShortcutSpecs({clip});
    CUELET_REQUIRE(renamed[0].portalId == specs[0].portalId);
    CUELET_REQUIRE(renamed[0].preferredTrigger == specs[0].preferredTrigger);

    clip.shortcut->global = false;
    CUELET_REQUIRE(portalShortcutSpecs({clip}).empty());

    const PortalShortcutStatus active{PortalShortcutState::Active, "Ctrl+Alt+9", {}};
    const PortalShortcutStatus denied{PortalShortcutState::Denied, {}, {}};
    CUELET_REQUIRE(shouldHandleShortcutLocally(*clip.shortcut, active));
    clip.shortcut->global = true;
    CUELET_REQUIRE(!shouldHandleShortcutLocally(*clip.shortcut, active));
    CUELET_REQUIRE(shouldHandleShortcutLocally(*clip.shortcut, denied));
    CUELET_REQUIRE(shouldHandleShortcutLocally(*clip.shortcut, std::nullopt));
}

void unavailablePortalExposesFallbackState()
{
    auto portal = std::make_shared<FakePortal>();
    std::vector<std::string> activations;
    auto controller = controllerFor(portal, activations);
    controller->setDesiredShortcuts({spec("one", "CTRL+ALT+1")});
    controller->start();
    portal->detect(false, 0, "GlobalShortcuts is unavailable");

    CUELET_REQUIRE(controller->overallState() == PortalOverallState::Unavailable);
    CUELET_REQUIRE(!controller->portalAvailable());
    const auto status = controller->statusForSound("one");
    CUELET_REQUIRE(status.has_value());
    CUELET_REQUIRE(status->state == PortalShortcutState::Unavailable);
    CUELET_REQUIRE(portal->createCalls == 0);
}

void versionOneCreatesListsAndBindsExactlyOnce()
{
    auto portal = std::make_shared<FakePortal>();
    std::vector<std::string> activations;
    auto controller = controllerFor(portal, activations);
    controller->setDesiredShortcuts(
        {spec("one", "CTRL+ALT+1"), spec("two", "CTRL+ALT+2")},
        "wayland:parent");
    controller->start();
    portal->detect(true, 1);
    CUELET_REQUIRE(controller->portalVersion() == 1);
    CUELET_REQUIRE(portal->createCalls == 1);

    portal->create(success(), "/session/one");
    CUELET_REQUIRE(portal->listCalls == 1);
    CUELET_REQUIRE(portal->lastListSession == "/session/one");

    portal->list(success(), {{"sound.one", "Play one", "Ctrl+Alt+1"}});
    CUELET_REQUIRE(portal->bindCalls == 1);
    CUELET_REQUIRE(portal->lastBound.size() == 2);
    CUELET_REQUIRE(portal->lastParentWindow == "wayland:parent");

    portal->bind(success(), {
        {"sound.one", "Play one", "Ctrl+Alt+1"},
        {"sound.two", "Play two", "Ctrl+Alt+2"},
    });
    CUELET_REQUIRE(controller->overallState() == PortalOverallState::Active);
    CUELET_REQUIRE(controller->statusForSound("one")->triggerDescription == "Ctrl+Alt+1");
    CUELET_REQUIRE(controller->statusForSound("two")->triggerDescription == "Ctrl+Alt+2");
    CUELET_REQUIRE(portal->bindCalls == 1);
}

void creationFailureAndDenialAreVisible()
{
    auto portal = std::make_shared<FakePortal>();
    std::vector<std::string> activations;
    auto controller = controllerFor(portal, activations);
    controller->setDesiredShortcuts({spec("one", "CTRL+ALT+1")});
    controller->start();
    portal->detect(true, 1);
    portal->create(failed("CreateSession failed"), {});
    CUELET_REQUIRE(controller->overallState() == PortalOverallState::Error);
    CUELET_REQUIRE(controller->statusForSound("one")->state == PortalShortcutState::Error);

    auto deniedPortal = std::make_shared<FakePortal>();
    auto denied = controllerFor(deniedPortal, activations);
    denied->setDesiredShortcuts({spec("one", "CTRL+ALT+1")});
    denied->start();
    deniedPortal->detect(true, 1);
    deniedPortal->create(success(), "/session/denied");
    deniedPortal->list(success());
    deniedPortal->bind(cancelled());
    CUELET_REQUIRE(denied->overallState() == PortalOverallState::Denied);
    CUELET_REQUIRE(denied->statusForSound("one")->state == PortalShortcutState::Denied);
}

void partialApprovalUsesPortalReturnedTrigger()
{
    auto portal = std::make_shared<FakePortal>();
    std::vector<std::string> activations;
    auto controller = controllerFor(portal, activations);
    controller->setDesiredShortcuts(
        {spec("one", "CTRL+ALT+1"), spec("two", "CTRL+ALT+2")});
    controller->start();
    portal->detect(true, 1);
    portal->create(success(), "/session/partial");
    portal->list(success());
    portal->bind(success(), {{"sound.one", "Play one", "Super+F9"}});

    CUELET_REQUIRE(controller->overallState() == PortalOverallState::Partial);
    CUELET_REQUIRE(controller->statusForSound("one")->state == PortalShortcutState::Active);
    CUELET_REQUIRE(controller->statusForSound("one")->triggerDescription == "Super+F9");
    CUELET_REQUIRE(controller->statusForSound("two")->state == PortalShortcutState::Denied);
}

void activationMapsByStableIdAndSuppressesHeldDuplicates()
{
    auto portal = std::make_shared<FakePortal>();
    std::vector<std::string> activations;
    auto controller = controllerFor(portal, activations);
    controller->setDesiredShortcuts({spec("one", "CTRL+ALT+1")});
    controller->start();
    portal->detect(true, 1);
    portal->create(success(), "/session/active");
    portal->list(success());
    portal->bind(success(), {{"sound.one", "Play one", "Ctrl+Alt+1"}});

    portal->sink.activated("/session/active", "sound.one", 10);
    portal->sink.activated("/session/active", "sound.one", 11);
    portal->sink.activated("/session/active", "sound.unknown", 12);
    CUELET_REQUIRE(activations == std::vector<std::string>{"one"});

    portal->sink.deactivated("/session/active", "sound.one", 13);
    portal->sink.activated("/session/active", "sound.one", 14);
    CUELET_REQUIRE(activations == std::vector<std::string>({"one", "one"}));
}

void deletedAndRenamedSoundsRemainSafe()
{
    cuelet::SoundClip clip;
    clip.id = "stable-id";
    clip.relativePath = "before.wav";
    clip.displayName = "Before";
    std::vector<cuelet::SoundClip> clips{clip};
    CUELET_REQUIRE(soundByStableId(clips, "stable-id") == &clips[0]);
    clips[0].displayName = "After Rename";
    clips[0].relativePath = "after.wav";
    CUELET_REQUIRE(soundByStableId(clips, "stable-id") == &clips[0]);
    clips.clear();
    CUELET_REQUIRE(soundByStableId(clips, "stable-id") == nullptr);
}

void recreationDebouncesChangesAndRejectsStaleCallbacks()
{
    auto portal = std::make_shared<FakePortal>();
    std::vector<std::string> activations;
    auto controller = controllerFor(portal, activations);
    controller->setDesiredShortcuts({spec("one", "CTRL+ALT+1")});
    controller->start();
    portal->detect(true, 1);
    portal->create(success(), "/session/old");
    portal->list(success());
    portal->bind(success(), {{"sound.one", "Play one", "Ctrl+Alt+1"}});

    controller->setDesiredShortcuts(
        {spec("one", "CTRL+ALT+1"), spec("two", "CTRL+ALT+2")});
    controller->setDesiredShortcuts(
        {spec("one", "CTRL+ALT+1"), spec("two", "CTRL+ALT+3")});
    CUELET_REQUIRE(portal->createCalls == 1);
    controller->flushReconfigurationForTesting();
    CUELET_REQUIRE(portal->closedSessions == std::vector<std::string>{"/session/old"});
    CUELET_REQUIRE(portal->createCalls == 2);

    portal->sink.activated("/session/old", "sound.one", 20);
    CUELET_REQUIRE(activations.empty());
    portal->create(success(), "/session/new");
    portal->list(success());
    portal->bind(success(), {
        {"sound.one", "Play one", "Ctrl+Alt+1"},
        {"sound.two", "Play two", "Ctrl+Alt+3"},
    });
    portal->sink.activated("/session/new", "sound.two", 21);
    CUELET_REQUIRE(activations == std::vector<std::string>{"two"});
}

void descriptionChangesReplaceThePortalSession()
{
    auto portal = std::make_shared<FakePortal>();
    std::vector<std::string> activations;
    auto controller = controllerFor(portal, activations);
    controller->setDesiredShortcuts({spec("one", "CTRL+ALT+1", "Play Old Name")});
    controller->start();
    portal->detect(true, 1);
    portal->create(success(), "/session/old-description");
    portal->list(success());
    portal->bind(success(), {{"sound.one", "Play Old Name", "Ctrl+Alt+1"}});

    controller->setDesiredShortcuts({
        spec("one", "CTRL+ALT+1", "Play Renamed Sound")});
    controller->flushReconfigurationForTesting();
    CUELET_REQUIRE(portal->closedSessions ==
        std::vector<std::string>{"/session/old-description"});
    CUELET_REQUIRE(portal->createCalls == 2);
}

void replacedSessionIgnoresOutstandingCreation()
{
    auto portal = std::make_shared<FakePortal>();
    std::vector<std::string> activations;
    auto controller = controllerFor(portal, activations);
    controller->setDesiredShortcuts({spec("one", "CTRL+ALT+1")});
    controller->start();
    portal->detect(true, 1);
    CUELET_REQUIRE(portal->createCalls == 1);

    auto staleCreate = std::move(portal->createCallback);
    controller->setDesiredShortcuts(
        {spec("one", "CTRL+ALT+1"), spec("two", "CTRL+ALT+2")});
    controller->flushReconfigurationForTesting();
    CUELET_REQUIRE(portal->createCalls == 2);

    staleCreate(PortalSessionResult{success(), "/session/stale"});
    CUELET_REQUIRE(!controller->hasLiveSession());
    CUELET_REQUIRE(portal->lastListSession.empty());

    portal->create(success(), "/session/current");
    CUELET_REQUIRE(controller->hasLiveSession());
    CUELET_REQUIRE(portal->lastListSession == "/session/current");
}

void closedSessionRejectsFurtherActivation()
{
    auto portal = std::make_shared<FakePortal>();
    std::vector<std::string> activations;
    auto controller = controllerFor(portal, activations);
    controller->setDesiredShortcuts({spec("one", "CTRL+ALT+1")});
    controller->start();
    portal->detect(true, 1);
    portal->create(success(), "/session/closed");
    portal->list(success());
    portal->bind(success(), {{"sound.one", "Play one", "Ctrl+Alt+1"}});

    portal->sink.sessionClosed("/session/closed");
    CUELET_REQUIRE(controller->overallState() == PortalOverallState::Disconnected);
    CUELET_REQUIRE(!controller->hasLiveSession());
    portal->sink.activated("/session/closed", "sound.one", 30);
    CUELET_REQUIRE(activations.empty());
}

void configurationCompletionAfterShutdownIsIgnored()
{
    auto portal = std::make_shared<FakePortal>();
    std::vector<std::string> activations;
    auto controller = controllerFor(portal, activations);
    controller->setDesiredShortcuts({spec("one", "CTRL+ALT+1")});
    controller->start();
    portal->detect(true, 2);
    portal->create(success(), "/session/configure");
    portal->list(success());
    portal->bind(success(), {{"sound.one", "Play one", "Ctrl+Alt+1"}});

    int completions = 0;
    controller->configureShortcuts(
        [&](const PortalOperationResult&) { ++completions; });
    CUELET_REQUIRE(portal->configureCalls == 1);
    auto lateCompletion = std::move(portal->configureCallback);

    controller->shutdown();
    lateCompletion(success());
    CUELET_REQUIRE(completions == 0);
}

void disconnectAndShutdownCancelOutstandingWork()
{
    auto portal = std::make_shared<FakePortal>();
    std::vector<std::string> activations;
    auto controller = controllerFor(portal, activations);
    controller->setDesiredShortcuts({spec("one", "CTRL+ALT+1")});
    controller->start();
    portal->detect(true, 1);
    CUELET_REQUIRE(portal->createCalls == 1);

    portal->sink.disconnected();
    CUELET_REQUIRE(controller->overallState() == PortalOverallState::Disconnected);
    CUELET_REQUIRE(controller->statusForSound("one")->state == PortalShortcutState::Disconnected);

    portal->detect(true, 1);
    CUELET_REQUIRE(controller->portalAvailable());
    CUELET_REQUIRE(portal->createCalls == 2);

    controller->shutdown();
    CUELET_REQUIRE(controller->overallState() == PortalOverallState::Stopped);
    CUELET_REQUIRE(portal->cancelCalls >= 1);
    CUELET_REQUIRE(portal->stopCalls == 1);

    // Completion after shutdown must be ignored rather than reviving a session.
    if (portal->createCallback) {
        portal->create(success(), "/session/too-late");
    }
    CUELET_REQUIRE(!controller->hasLiveSession());
}

} // namespace

int main()
{
    return cuelet_linux::tests::run("cuelet global shortcuts tests", [] {
        stableIdsAndTriggersDoNotDependOnNames();
        unavailablePortalExposesFallbackState();
        versionOneCreatesListsAndBindsExactlyOnce();
        creationFailureAndDenialAreVisible();
        partialApprovalUsesPortalReturnedTrigger();
        activationMapsByStableIdAndSuppressesHeldDuplicates();
        deletedAndRenamedSoundsRemainSafe();
        recreationDebouncesChangesAndRejectsStaleCallbacks();
        descriptionChangesReplaceThePortalSession();
        replacedSessionIgnoresOutstandingCreation();
        closedSessionRejectsFurtherActivation();
        configurationCompletionAfterShutdownIsIgnored();
        disconnectAndShutdownCancelOutstandingWork();
    });
}
