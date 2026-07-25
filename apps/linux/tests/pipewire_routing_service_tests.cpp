#include "services/LinuxPipeWireRoutingService.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <csignal>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <unistd.h>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct FakeChildState {
    std::size_t stopCalls = 0;
    std::vector<LinuxPipeWireRoutingPlan::ProcessStop> stopRequests;
    std::deque<bool> stopOutcomes;
    bool running = true;
};

class FakeChildProcess final : public LinuxPipeWireRoutingService::ChildProcess {
public:
    explicit FakeChildProcess(std::shared_ptr<FakeChildState> state)
        : state_(std::move(state))
    {
    }

    LinuxPipeWireRoutingService::StopResult stop(
        const LinuxPipeWireRoutingPlan::ProcessStop& request) override
    {
        ++state_->stopCalls;
        state_->stopRequests.push_back(request);
        const bool succeeds =
            state_->stopOutcomes.empty() || state_->stopOutcomes.front();
        if (!state_->stopOutcomes.empty()) {
            state_->stopOutcomes.pop_front();
        }
        if (succeeds) {
            state_->running = false;
        }
        return {
            succeeds,
            !succeeds,
            succeeds ? std::string{} : std::string("injected stop failure"),
        };
    }

    bool isRunning() const override
    {
        return state_->running;
    }

private:
    std::shared_ptr<FakeChildState> state_;
};

class FakeProcessRunner final : public LinuxPipeWireRoutingService::ProcessRunner {
public:
    LinuxPipeWireRoutingPlan::ToolState tools{true, true};
    std::optional<std::size_t> failingSpawnIndex;
    std::deque<std::deque<bool>> handleStopOutcomes;
    std::size_t probeCalls = 0;
    std::vector<std::vector<std::string>> spawnArguments;
    std::vector<std::shared_ptr<FakeChildState>> childStates;

    LinuxPipeWireRoutingPlan::ToolState probeCapabilities() override
    {
        ++probeCalls;
        return tools;
    }

    LinuxPipeWireRoutingService::SpawnResult spawn(
        const std::vector<std::string>& argv) override
    {
        const auto spawnIndex = spawnArguments.size();
        spawnArguments.push_back(argv);
        if (failingSpawnIndex.has_value() && *failingSpawnIndex == spawnIndex) {
            return {nullptr, "injected spawn failure"};
        }

        const auto state = std::make_shared<FakeChildState>();
        if (!handleStopOutcomes.empty()) {
            state->stopOutcomes = std::move(handleStopOutcomes.front());
            handleStopOutcomes.pop_front();
        }
        childStates.push_back(state);
        return {std::make_unique<FakeChildProcess>(state), {}};
    }
};

LinuxPipeWireRoutingPlan::Plan enabledPlan(const std::string& sessionKey)
{
    LinuxPipeWireRoutingPlan::Request request;
    request.enabled = true;
    request.sessionKey = sessionKey;
    return LinuxPipeWireRoutingPlan::create(request);
}

LinuxPipeWireRoutingPlan::Plan twoProcessPlan()
{
    const auto first = enabledPlan("first-process");
    const auto second = enabledPlan("second-process");
    return {
        true,
        LinuxPipeWireRoutingPlan::Scope::UserSession,
        {"pw-loopback"},
        {first.startProcesses.front(), second.startProcesses.front()},
        {first.stopProcesses.front(), second.stopProcesses.front()},
        {},
        first.virtualSinkNode,
        first.virtualSourceNode,
        first.bridgesVirtualSinkMonitorToSource,
        false,
        false,
        false,
        false,
    };
}

LinuxPipeWireRoutingPlan::Plan planWithGracePeriod(
    const LinuxPipeWireRoutingPlan::Plan& plan,
    unsigned int milliseconds)
{
    const auto& stop = plan.stopProcesses.front();
    const LinuxPipeWireRoutingPlan::ProcessStop replacementStop{
        stop.ownershipToken,
        stop.mode,
        milliseconds,
        stop.requiresTrackedChildHandle,
        stop.allowNameBasedFallback,
        stop.forceAfterGracePeriod,
    };
    return {
        plan.enabled,
        plan.scope,
        plan.requiredExecutables,
        plan.startProcesses,
        {replacementStop},
        plan.configurationWrites,
        plan.virtualSinkNode,
        plan.virtualSourceNode,
        plan.bridgesVirtualSinkMonitorToSource,
        plan.changesDefaultInput,
        plan.changesDefaultOutput,
        plan.routesLocalPlayback,
        plan.writesSystemConfiguration,
    };
}

LinuxPipeWireRoutingPlan::Plan planWithStartProcess(
    const LinuxPipeWireRoutingPlan::Plan& plan,
    LinuxPipeWireRoutingPlan::ProcessStart start)
{
    return {
        plan.enabled,
        plan.scope,
        plan.requiredExecutables,
        {std::move(start)},
        plan.stopProcesses,
        plan.configurationWrites,
        plan.virtualSinkNode,
        plan.virtualSourceNode,
        plan.bridgesVirtualSinkMonitorToSource,
        plan.changesDefaultInput,
        plan.changesDefaultOutput,
        plan.routesLocalPlayback,
        plan.writesSystemConfiguration,
    };
}

class ProductionRunnerFixture {
public:
    ProductionRunnerFixture()
        : oldPath_(environmentValue("PATH"))
        , oldRuntimeDirectory_(environmentValue("PIPEWIRE_RUNTIME_DIR"))
        , oldRemote_(environmentValue("PIPEWIRE_REMOTE"))
        , oldHelper_(environmentValue("CUELET_PIPEWIRE_TEST_HELPER"))
        , oldIgnoreTerm_(environmentValue("CUELET_PIPEWIRE_TEST_IGNORE_TERM"))
    {
        GError* error = nullptr;
        gchar* directory = g_dir_make_tmp("cuelet-pipewire-service-XXXXXX", &error);
        if (!directory) {
            const std::string message = error && error->message
                ? error->message
                : "could not make a routing test directory";
            if (error) {
                g_error_free(error);
            }
            throw std::runtime_error(message);
        }
        directory_ = directory;
        g_free(directory);

        gchar* executable = g_file_read_link("/proc/self/exe", &error);
        if (!executable) {
            const std::string message = error && error->message
                ? error->message
                : "could not resolve the test executable";
            if (error) {
                g_error_free(error);
            }
            throw std::runtime_error(message);
        }
        helperPath_ = directory_ + "/pw-loopback";
        const int symlinkResult = symlink(executable, helperPath_.c_str());
        g_free(executable);
        if (symlinkResult != 0) {
            throw std::runtime_error("could not create the temporary pw-loopback helper");
        }

        socketPath_ = directory_ + "/pipewire-0";
        if (!g_file_set_contents(socketPath_.c_str(), "", 0, &error)) {
            const std::string message = error && error->message
                ? error->message
                : "could not create the temporary PipeWire probe fixture";
            if (error) {
                g_error_free(error);
            }
            throw std::runtime_error(message);
        }

        g_setenv("PATH", directory_.c_str(), TRUE);
        g_setenv("PIPEWIRE_RUNTIME_DIR", directory_.c_str(), TRUE);
        g_unsetenv("PIPEWIRE_REMOTE");
        g_setenv("CUELET_PIPEWIRE_TEST_HELPER", "1", TRUE);
        g_unsetenv("CUELET_PIPEWIRE_TEST_IGNORE_TERM");
    }

    ~ProductionRunnerFixture()
    {
        restoreEnvironment("PATH", oldPath_);
        restoreEnvironment("PIPEWIRE_RUNTIME_DIR", oldRuntimeDirectory_);
        restoreEnvironment("PIPEWIRE_REMOTE", oldRemote_);
        restoreEnvironment("CUELET_PIPEWIRE_TEST_HELPER", oldHelper_);
        restoreEnvironment("CUELET_PIPEWIRE_TEST_IGNORE_TERM", oldIgnoreTerm_);
        if (!socketPath_.empty()) {
            g_remove(socketPath_.c_str());
        }
        if (!helperPath_.empty()) {
            g_remove(helperPath_.c_str());
        }
        if (!directory_.empty()) {
            g_rmdir(directory_.c_str());
        }
    }

    void setIgnoresTerminate(bool ignores)
    {
        if (ignores) {
            g_setenv("CUELET_PIPEWIRE_TEST_IGNORE_TERM", "1", TRUE);
        } else {
            g_unsetenv("CUELET_PIPEWIRE_TEST_IGNORE_TERM");
        }
    }

private:
    static std::optional<std::string> environmentValue(const char* name)
    {
        const char* value = g_getenv(name);
        return value ? std::optional<std::string>(value) : std::nullopt;
    }

    static void restoreEnvironment(
        const char* name,
        const std::optional<std::string>& value)
    {
        if (value.has_value()) {
            g_setenv(name, value->c_str(), TRUE);
        } else {
            g_unsetenv(name);
        }
    }

    std::optional<std::string> oldPath_;
    std::optional<std::string> oldRuntimeDirectory_;
    std::optional<std::string> oldRemote_;
    std::optional<std::string> oldHelper_;
    std::optional<std::string> oldIgnoreTerm_;
    std::string directory_;
    std::string helperPath_;
    std::string socketPath_;
};

void testCapabilityFailureDoesNotSpawn()
{
    FakeProcessRunner runner;
    runner.tools = {true, false};
    LinuxPipeWireRoutingService service(runner);

    const auto tools = service.probeCapabilities();
    require(tools.pipeWireSessionReachable, "the injected PipeWire session must be reported");
    require(!tools.pwLoopbackAvailable, "the injected missing tool must be reported");
    require(!service.start(enabledPlan("missing-tool")), "start must fail without pw-loopback");
    require(runner.spawnArguments.empty(), "capability failure must not spawn a process");
    require(
        service.diagnostic().status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::MissingTool,
        "the service must expose a missing-tool diagnostic");

    runner.tools = {false, true};
    require(
        !service.start(enabledPlan("missing-session")),
        "start must fail when the PipeWire user session is unavailable");
    require(runner.spawnArguments.empty(), "session failure must not spawn a process");
    require(
        service.diagnostic().status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::SessionUnavailable,
        "the service must expose a session-unavailable diagnostic");
}

void testStartAndStopAreIdempotentAndHandleScoped()
{
    FakeProcessRunner runner;
    LinuxPipeWireRoutingService service(runner);
    const auto plan = enabledPlan("idempotent");

    require(service.start(plan), "the injected process should start");
    require(service.start(plan), "starting an already active identical plan must succeed");
    require(runner.spawnArguments.size() == 1, "idempotent start must not spawn twice");
    require(service.isActive(), "the service must expose active state");
    require(service.ownedProcessCount() == 1, "one exact child handle must be owned");
    require(service.virtualSinkNode() == plan.virtualSinkNode, "the sink must be exposed to UI code");
    require(
        service.virtualSourceNode() == plan.virtualSourceNode,
        "the source must be exposed to UI code");
    require(
        service.diagnostic().status == LinuxPipeWireRoutingPlan::DiagnosticStatus::Active,
        "successful start must expose an active diagnostic");

    const auto& argv = runner.spawnArguments.front();
    require(!argv.empty() && argv.front() == "pw-loopback", "the runner must receive direct argv");
    for (const auto& argument : argv) {
        require(argument != "sh" && argument != "bash" && argument != "-c",
                "the runtime must not add a shell");
        require(argument.find("set-default") == std::string::npos,
                "the runtime must not add a default-device command");
    }

    const auto state = runner.childStates.front();
    require(service.stop(), "owned process cleanup must succeed");
    require(service.stop(), "stopping an already stopped service must succeed");
    require(state->stopCalls == 1, "idempotent stop must terminate the child once");
    require(state->stopRequests.size() == 1, "one exact stop request must be issued");
    require(
        state->stopRequests.front().ownershipToken ==
            plan.startProcesses.front().ownershipToken,
        "cleanup must use the start action's exact ownership token");
    require(
        state->stopRequests.front().requiresTrackedChildHandle,
        "cleanup must require a tracked child handle");
    require(
        !state->stopRequests.front().allowNameBasedFallback,
        "cleanup must prohibit name-based killing");
    require(!service.isActive(), "the service must no longer be active");
    require(service.ownedProcessCount() == 0, "no child handle may linger after stop");
    require(
        service.diagnostic().status == LinuxPipeWireRoutingPlan::DiagnosticStatus::Stopped,
        "successful cleanup must expose a stopped diagnostic");
}

void testRestartStopsTheOldHandleBeforeStartingTheNewPlan()
{
    FakeProcessRunner runner;
    const auto firstPlan = enabledPlan("restart-first");
    const auto secondPlan = enabledPlan("restart-second");
    std::shared_ptr<FakeChildState> firstState;
    std::shared_ptr<FakeChildState> secondState;

    {
        LinuxPipeWireRoutingService service(runner);
        require(service.start(firstPlan), "the first plan must start");
        firstState = runner.childStates.front();

        require(service.restart(secondPlan), "restart must stop and replace the plan");
        require(firstState->stopCalls == 1, "restart must stop the old exact child");
        require(runner.spawnArguments.size() == 2, "restart must spawn the replacement once");
        require(service.virtualSinkNode() == secondPlan.virtualSinkNode,
                "restart must expose the replacement endpoint");
        secondState = runner.childStates.back();
        require(secondState->stopCalls == 0, "the replacement must remain active");
    }

    require(firstState->stopCalls == 1, "destruction must not stop an old handle twice");
    require(secondState->stopCalls == 1, "destruction must clean up the active replacement");
}

void testRestartDoesNotSpawnWhenOldCleanupFails()
{
    FakeProcessRunner runner;
    runner.handleStopOutcomes.push_back({false, true});
    LinuxPipeWireRoutingService service(runner);

    require(service.start(enabledPlan("restart-cleanup-first")), "the first plan must start");
    require(
        !service.restart(enabledPlan("restart-cleanup-second")),
        "restart must fail closed when the old child cannot be cleaned up");
    require(
        runner.spawnArguments.size() == 1,
        "restart must not spawn a replacement beside an uncleaned old route");
    require(service.ownedProcessCount() == 1, "the exact old handle must remain tracked");
    require(service.stop(), "a later cleanup retry must stop the retained old handle");
}

void testRejectedReplacementPreservesOperationDiagnostic()
{
    FakeProcessRunner runner;
    LinuxPipeWireRoutingService service(runner);
    require(service.start(enabledPlan("preserved-active")), "the initial plan must start");

    require(
        !service.start(enabledPlan("rejected-replacement")),
        "start must reject a different plan while a route is owned");
    require(
        service.diagnostic().status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::InvalidRuntimeState,
        "the rejected-operation reason must remain available to preferences");
    require(service.isActive(), "the unchanged old route must still report healthy");
    require(
        service.diagnostic().status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::InvalidRuntimeState,
        "health polling must not erase the rejected-operation reason");

    require(service.stop(), "the original exact child must still be cleanly stoppable");
    require(
        service.diagnostic().status == LinuxPipeWireRoutingPlan::DiagnosticStatus::Stopped,
        "a successful later operation may clear the prior rejection");
}

void testPartialStartRollsBackAlreadyStartedChildren()
{
    FakeProcessRunner runner;
    runner.failingSpawnIndex = 1;
    LinuxPipeWireRoutingService service(runner);

    require(!service.start(twoProcessPlan()), "a partial process start must fail atomically");
    require(runner.spawnArguments.size() == 2, "both planned starts must be attempted in order");
    require(runner.childStates.size() == 1, "only the first child should have started");
    require(
        runner.childStates.front()->stopCalls == 1,
        "a started child must be rolled back after a later spawn failure");
    require(service.ownedProcessCount() == 0, "successful rollback must retain no child handles");
    require(!service.isActive(), "a rolled-back plan must not report active");
    require(
        service.diagnostic().status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::StartFailed,
        "a fully rolled-back partial start must be classified as a start failure");
}

void testUnexpectedExitIsDiagnosedAndCanBeRestarted()
{
    FakeProcessRunner runner;
    LinuxPipeWireRoutingService service(runner);
    const auto plan = enabledPlan("unexpected-exit");

    require(service.start(plan), "the route must initially start");
    runner.childStates.front()->running = false;
    require(!service.isActive(), "an exited process must no longer report active");
    require(
        service.diagnostic().status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::RuntimeFailed,
        "an exited owned process must expose a runtime-failure diagnostic");

    require(service.start(plan), "starting the same failed plan must clean up and restart it");
    require(runner.spawnArguments.size() == 2, "recovery must spawn one replacement process");
    require(runner.childStates.front()->stopCalls == 1, "the exited child handle must be reaped");
    require(service.isActive(), "the replacement process must report active");
}

void testFailedRollbackRetainsOnlyTheExactChildForRetry()
{
    FakeProcessRunner runner;
    runner.failingSpawnIndex = 1;
    runner.handleStopOutcomes.push_back({false, true});
    LinuxPipeWireRoutingService service(runner);

    require(!service.start(twoProcessPlan()), "the injected second spawn must fail");
    require(service.ownedProcessCount() == 1, "failed rollback must retain the exact child handle");
    require(
        service.diagnostic().status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::StopFailed,
        "failed rollback cleanup must be visible in diagnostics");

    require(service.stop(), "a later scoped cleanup retry must succeed");
    require(runner.childStates.front()->stopCalls == 2, "cleanup must retry only the retained handle");
    require(service.ownedProcessCount() == 0, "the successful retry must release the handle");
}

void testUnsafePlansAreRejectedWithoutExecution()
{
    FakeProcessRunner runner;
    LinuxPipeWireRoutingService service(runner);
    const auto safe = enabledPlan("unsafe-plan-base");
    const LinuxPipeWireRoutingPlan::Plan unsafe{
        safe.enabled,
        safe.scope,
        safe.requiredExecutables,
        safe.startProcesses,
        safe.stopProcesses,
        {{"/etc/pipewire/pipewire.conf.d/cuelet.conf", "unsafe", true}},
        safe.virtualSinkNode,
        safe.virtualSourceNode,
        safe.bridgesVirtualSinkMonitorToSource,
        true,
        false,
        false,
        true,
    };

    require(!service.start(unsafe), "persistent/default-changing plans must be rejected");
    require(runner.probeCalls == 0, "unsafe plans must be rejected before capability probing");
    require(runner.spawnArguments.empty(), "unsafe plans must never execute");
    require(
        service.diagnostic().status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::InvalidRuntimeState,
        "unsafe plans must expose an invalid-plan diagnostic");

    require(
        !service.start(planWithGracePeriod(safe, 6000)),
        "an unbounded shutdown grace period must be rejected");

    const auto& safeStart = safe.startProcesses.front();
    const LinuxPipeWireRoutingPlan::ProcessStart targetedStart{
        safeStart.ownershipToken,
        {
            "pw-loopback",
            safeStart.argv[1],
            safeStart.argv[2],
            safeStart.argv[3],
            safeStart.argv[4],
            "--capture=default",
            safeStart.argv[6],
        },
        false,
        true,
    };
    require(
        !service.start(planWithStartProcess(safe, targetedStart)),
        "a plan that targets an existing/default endpoint must be rejected");

    const LinuxPipeWireRoutingPlan::ProcessStart shellStart{
        safeStart.ownershipToken,
        safeStart.argv,
        true,
        true,
    };
    require(
        !service.start(planWithStartProcess(safe, shellStart)),
        "a plan that requests shell execution must be rejected");
    require(runner.probeCalls == 0, "all unsafe variants must fail before probing");
    require(runner.spawnArguments.empty(), "all unsafe variants must fail before execution");
}

void testDisabledPlanDoesNotExecute()
{
    FakeProcessRunner runner;
    LinuxPipeWireRoutingService service(runner);
    LinuxPipeWireRoutingPlan::Request request;

    require(service.start(LinuxPipeWireRoutingPlan::create(request)),
            "a disabled plan must be a successful no-op");
    require(runner.probeCalls == 0, "disabled routing must not probe external tools");
    require(runner.spawnArguments.empty(), "disabled routing must not spawn");
    require(
        service.diagnostic().status == LinuxPipeWireRoutingPlan::DiagnosticStatus::Disabled,
        "disabled routing must expose disabled state");
}

void testProductionCapabilityProbeIsReadOnly()
{
    LinuxPipeWireRoutingService service;
    const auto tools = service.probeCapabilities();
    static_cast<void>(tools);
    require(service.ownedProcessCount() == 0, "capability probing must not own a process");
    require(!service.isActive(), "capability probing alone must not activate routing");
    require(
        service.diagnostic().status == LinuxPipeWireRoutingPlan::DiagnosticStatus::Disabled,
        "read-only capability probing must leave routing disabled");
}

void testProductionRunnerUsesDirectOwnedProcessCleanup()
{
    ProductionRunnerFixture fixture;

    {
        LinuxPipeWireRoutingService service;
        require(service.start(enabledPlan("production-graceful")),
                "the temporary non-PipeWire helper must start through GSubprocess");
        g_usleep(20000);
        require(service.isActive(), "the exact production child must be observed running");
        require(service.stop(), "SIGTERM must gracefully stop the exact production child");
        require(service.ownedProcessCount() == 0, "graceful cleanup must release the handle");
    }

    fixture.setIgnoresTerminate(true);
    {
        LinuxPipeWireRoutingService service;
        const auto shortGrace = planWithGracePeriod(enabledPlan("production-force"), 10);
        require(service.start(shortGrace), "the force-fallback helper must start");
        g_usleep(20000);
        require(service.isActive(), "the force-fallback helper must receive valid direct argv");
        require(
            service.stop(),
            "an owned child that ignores SIGTERM must be force-stopped after the grace period");
        require(service.ownedProcessCount() == 0, "forced cleanup must release the exact handle");
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (g_getenv("CUELET_PIPEWIRE_TEST_HELPER")) {
        const bool validArguments =
            argc == 7 &&
            g_str_has_prefix(argv[1], "--name=cuelet-pipewire-session-") &&
            g_str_has_prefix(argv[2], "--group=cuelet-pipewire-session-") &&
            std::string(argv[3]) == "--channels=2" &&
            std::string(argv[4]) == "--channel-map=[ FL, FR ]" &&
            g_str_has_prefix(argv[5], "--capture-props=") &&
            g_str_has_prefix(argv[6], "--playback-props=");
        if (!validArguments) {
            _exit(64);
        }
        if (g_getenv("CUELET_PIPEWIRE_TEST_IGNORE_TERM")) {
            std::signal(SIGTERM, SIG_IGN);
        }
        while (true) {
            pause();
        }
    }

    try {
        testCapabilityFailureDoesNotSpawn();
        testStartAndStopAreIdempotentAndHandleScoped();
        testRestartStopsTheOldHandleBeforeStartingTheNewPlan();
        testRestartDoesNotSpawnWhenOldCleanupFails();
        testRejectedReplacementPreservesOperationDiagnostic();
        testPartialStartRollsBackAlreadyStartedChildren();
        testUnexpectedExitIsDiagnosedAndCanBeRestarted();
        testFailedRollbackRetainsOnlyTheExactChildForRetry();
        testUnsafePlansAreRejectedWithoutExecution();
        testDisabledPlanDoesNotExecute();
        testProductionCapabilityProbeIsReadOnly();
        testProductionRunnerUsesDirectOwnedProcessCleanup();
    } catch (const std::exception& error) {
        std::cerr << "cuelet PipeWire routing service tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "cuelet PipeWire routing service tests passed\n";
    return EXIT_SUCCESS;
}
