#include "services/LinuxPipeWireRoutingPlan.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool contains(const std::vector<std::string>& values, const std::string& expected)
{
    for (const auto& value : values) {
        if (value == expected) {
            return true;
        }
    }
    return false;
}

const std::string& argumentStartingWith(
    const std::vector<std::string>& arguments,
    std::string_view prefix)
{
    for (const auto& argument : arguments) {
        if (argument.rfind(prefix, 0) == 0) {
            return argument;
        }
    }
    throw std::runtime_error("missing argument prefix: " + std::string(prefix));
}

void testDisabledPlanHasNoSideEffects()
{
    LinuxPipeWireRoutingPlan::Request request;
    request.enabled = false;
    request.sessionKey = "disabled-session";

    const auto plan = LinuxPipeWireRoutingPlan::create(request);

    require(!plan.enabled, "disabled request must remain disabled");
    require(plan.startProcesses.empty(), "disabled routing must start no processes");
    require(plan.stopProcesses.empty(), "disabled routing must stop no processes");
    require(plan.requiredExecutables.empty(), "disabled routing must require no tools");
    require(plan.configurationWrites.empty(), "disabled routing must write no configuration");
    require(!plan.changesDefaultInput, "routing must not change the default input");
    require(!plan.changesDefaultOutput, "routing must not change the default output");
    require(!plan.routesLocalPlayback, "routing must keep local playback separate");
    require(!plan.writesSystemConfiguration, "routing must not write system configuration");
}

void testEnabledPlanIsScopedAndSymmetric()
{
    LinuxPipeWireRoutingPlan::Request request;
    request.enabled = true;
    request.sessionKey = "window-42";

    const auto plan = LinuxPipeWireRoutingPlan::create(request);

    require(plan.enabled, "enabled request must produce an enabled plan");
    require(
        plan.scope == LinuxPipeWireRoutingPlan::Scope::UserSession,
        "routing must be scoped to the user session");
    require(
        plan.requiredExecutables == std::vector<std::string>{"pw-loopback"},
        "the PipeWire-native plan must only require pw-loopback");
    require(plan.startProcesses.size() == 1, "one loopback bridge must be started");
    require(plan.stopProcesses.size() == 1, "one owned loopback bridge must be stopped");
    require(
        plan.startProcesses.front().ownershipToken == plan.stopProcesses.front().ownershipToken,
        "start and stop actions must share an ownership token");
    require(plan.startProcesses.front().longRunning, "pw-loopback must be tracked as long-running");
    require(
        !plan.startProcesses.front().usesShell,
        "process arguments must be executed directly without a shell");
    require(
        plan.stopProcesses.front().mode ==
            LinuxPipeWireRoutingPlan::StopMode::TerminateOwnedProcess,
        "cleanup must terminate only the tracked child process");
    require(
        plan.stopProcesses.front().gracePeriodMilliseconds == 2000,
        "cleanup must allow bounded graceful termination");
    require(
        plan.stopProcesses.front().requiresTrackedChildHandle,
        "cleanup must require the exact tracked child handle");
    require(
        !plan.stopProcesses.front().allowNameBasedFallback,
        "cleanup must never fall back to broad name-based process matching");
    require(
        plan.stopProcesses.front().forceAfterGracePeriod,
        "an owned child may be forcibly cleaned up after its grace period");
    require(plan.bridgesVirtualSinkMonitorToSource, "the virtual monitor bridge must be explicit");
    require(!plan.changesDefaultInput, "routing must not change the default input");
    require(!plan.changesDefaultOutput, "routing must not change the default output");
    require(!plan.routesLocalPlayback, "local playback must remain a separate app path");
    require(plan.configurationWrites.empty(), "temporary routing must not write config files");
    require(!plan.writesSystemConfiguration, "temporary routing must never write /etc");

    const auto& arguments = plan.startProcesses.front().argv;
    require(!arguments.empty() && arguments.front() == "pw-loopback", "pw-loopback must be argv[0]");
    require(!contains(arguments, "sh"), "the plan must not invoke sh");
    require(!contains(arguments, "bash"), "the plan must not invoke bash");
    require(!contains(arguments, "-c"), "the plan must not contain a shell command argument");
    for (const auto& argument : arguments) {
        require(
            argument.find("set-default") == std::string::npos &&
                argument.find("@DEFAULT_") == std::string::npos &&
                argument.find("target.object") == std::string::npos,
            "the plan must not target or replace a system default device");
    }
    require(
        argumentStartingWith(arguments, "--capture-props=").find("\"media.class\":\"Audio/Sink\"") !=
            std::string::npos,
        "the capture side must expose a virtual sink");
    require(
        argumentStartingWith(arguments, "--playback-props=").find("\"media.class\":\"Audio/Source\"") !=
            std::string::npos,
        "the playback side must expose a virtual source");
    require(
        argumentStartingWith(arguments, "--capture-props=").find("\"state.restore-props\":false") !=
                std::string::npos &&
            argumentStartingWith(arguments, "--playback-props=").find("\"state.restore-props\":false") !=
                std::string::npos,
        "Cuelet-owned virtual nodes must not inherit stale session volumes");
    require(
        plan.virtualSinkNode == "cuelet.soundboard-input",
        "the render target must have Cuelet's stable internal node name");
    require(
        plan.virtualSourceNode == "cuelet.virtual-microphone",
        "the capture source must have Cuelet's stable public node name");
}

void testHostileStringsRemainSingleEscapedArguments()
{
    LinuxPipeWireRoutingPlan::Request request;
    request.enabled = true;
    request.sessionKey = "x;$(touch /tmp/not-owned) \"\n--playback=evil";
    request.sinkDescription = "Cuelet \"Input\"\n; rm -rf /";
    request.sourceDescription = "Cuelet $(id) \\\\ Microphone";

    const auto plan = LinuxPipeWireRoutingPlan::create(request);
    const auto& process = plan.startProcesses.front();
    const auto& captureProperties =
        argumentStartingWith(process.argv, "--capture-props=");
    const auto& playbackProperties =
        argumentStartingWith(process.argv, "--playback-props=");

    require(!process.usesShell, "hostile values must never enable shell execution");
    require(process.argv.front() == "pw-loopback", "hostile values must not replace argv[0]");
    require(
        captureProperties.find("\\\"Input\\\"") != std::string::npos,
        "quotes must be JSON escaped inside one argv element");
    require(
        captureProperties.find("\\n") != std::string::npos,
        "newlines must be JSON escaped inside one argv element");
    require(
        captureProperties.find('\n') == std::string::npos,
        "raw newlines must not reach the property argument");
    require(
        playbackProperties.find("$(id)") != std::string::npos,
        "literal shell syntax may remain data in a direct argv element");
    require(
        playbackProperties.find("\\\\\\\\") != std::string::npos,
        "backslashes must be JSON escaped");
    require(
        plan.virtualSinkNode.find(';') == std::string::npos &&
            plan.virtualSinkNode.find('$') == std::string::npos,
        "node names must not reuse hostile session text");
    require(
        plan.virtualSourceNode.find(';') == std::string::npos &&
            plan.virtualSourceNode.find('$') == std::string::npos,
        "source names must not reuse hostile session text");
    require(
        plan.startProcesses.front().ownershipToken == plan.stopProcesses.front().ownershipToken,
        "hostile input must not break scoped cleanup");
}

void testGenerationIsDeterministicAndSessionSpecific()
{
    LinuxPipeWireRoutingPlan::Request firstRequest;
    firstRequest.enabled = true;
    firstRequest.sessionKey = "session-a";

    LinuxPipeWireRoutingPlan::Request secondRequest;
    secondRequest.enabled = true;
    secondRequest.sessionKey = "session-b";

    const auto first = LinuxPipeWireRoutingPlan::create(firstRequest);
    const auto firstAgain = LinuxPipeWireRoutingPlan::create(firstRequest);
    const auto second = LinuxPipeWireRoutingPlan::create(secondRequest);

    require(
        first.startProcesses.front().argv == firstAgain.startProcesses.front().argv,
        "equal requests must generate identical argv");
    require(
        first.startProcesses.front().ownershipToken ==
            firstAgain.startProcesses.front().ownershipToken,
        "equal requests must generate identical ownership");
    require(
        first.startProcesses.front().ownershipToken !=
            second.startProcesses.front().ownershipToken,
        "different sessions must not share process ownership");
    require(
        first.virtualSinkNode == second.virtualSinkNode &&
            first.virtualSourceNode == second.virtualSourceNode,
        "the externally visible endpoints must remain stable across restarts");
}

void testDiagnosticsClassifyInjectedRuntimeState()
{
    LinuxPipeWireRoutingPlan::Request disabledRequest;
    const auto disabled = LinuxPipeWireRoutingPlan::create(disabledRequest);
    const auto disabledDiagnostic = LinuxPipeWireRoutingPlan::diagnose(
        disabled,
        LinuxPipeWireRoutingPlan::ToolState{},
        {});
    require(
        disabledDiagnostic.status == LinuxPipeWireRoutingPlan::DiagnosticStatus::Disabled,
        "disabled routing must diagnose as disabled regardless of missing tools");

    LinuxPipeWireRoutingPlan::Request enabledRequest;
    enabledRequest.enabled = true;
    enabledRequest.sessionKey = "diagnostic-session";
    const auto plan = LinuxPipeWireRoutingPlan::create(enabledRequest);

    LinuxPipeWireRoutingPlan::ToolState missingTool;
    missingTool.pipeWireSessionReachable = true;
    missingTool.pwLoopbackAvailable = false;
    require(
        LinuxPipeWireRoutingPlan::diagnose(plan, missingTool, {}).status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::MissingTool,
        "a missing pw-loopback executable must be classified");

    LinuxPipeWireRoutingPlan::ToolState unavailableSession;
    unavailableSession.pipeWireSessionReachable = false;
    unavailableSession.pwLoopbackAvailable = true;
    require(
        LinuxPipeWireRoutingPlan::diagnose(plan, unavailableSession, {}).status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::SessionUnavailable,
        "an unavailable PipeWire user session must be classified");

    LinuxPipeWireRoutingPlan::ToolState readyTools;
    readyTools.pipeWireSessionReachable = true;
    readyTools.pwLoopbackAvailable = true;
    require(
        LinuxPipeWireRoutingPlan::diagnose(plan, readyTools, {}).status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::Ready,
        "available tools without execution results must diagnose as ready");

    const auto token = plan.startProcesses.front().ownershipToken;
    const std::vector<LinuxPipeWireRoutingPlan::ProcessObservation> running{
        {token, LinuxPipeWireRoutingPlan::ProcessState::Running, {}, ""},
    };
    require(
        LinuxPipeWireRoutingPlan::diagnose(plan, readyTools, running).status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::Active,
        "a running owned process must diagnose as active");

    const std::vector<LinuxPipeWireRoutingPlan::ProcessObservation> failedStart{
        {token, LinuxPipeWireRoutingPlan::ProcessState::FailedToStart, 127, "not found"},
    };
    require(
        LinuxPipeWireRoutingPlan::diagnose(plan, readyTools, failedStart).status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::StartFailed,
        "a failed child start must be classified");

    const std::vector<LinuxPipeWireRoutingPlan::ProcessObservation> stopped{
        {token, LinuxPipeWireRoutingPlan::ProcessState::Stopped, 0, ""},
    };
    require(
        LinuxPipeWireRoutingPlan::diagnose(plan, readyTools, stopped).status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::Stopped,
        "successful owned cleanup must diagnose as stopped");

    const std::vector<LinuxPipeWireRoutingPlan::ProcessObservation> failedStop{
        {token, LinuxPipeWireRoutingPlan::ProcessState::FailedToStop, {}, "still running"},
    };
    require(
        LinuxPipeWireRoutingPlan::diagnose(plan, readyTools, failedStop).status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::StopFailed,
        "failed owned cleanup must be classified");

    const std::vector<LinuxPipeWireRoutingPlan::ProcessObservation> unexpected{
        {"not-owned-by-this-plan", LinuxPipeWireRoutingPlan::ProcessState::Running, {}, ""},
    };
    require(
        LinuxPipeWireRoutingPlan::diagnose(plan, readyTools, unexpected).status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::InvalidRuntimeState,
        "unowned observations must not be treated as Cuelet routing");

    const LinuxPipeWireRoutingPlan::ProcessStart futureMonitor{
        "cuelet-owned-future-monitor",
        {"pw-loopback"},
        false,
        true,
    };
    const LinuxPipeWireRoutingPlan::ProcessStop futureMonitorStop{
        "cuelet-owned-future-monitor",
        LinuxPipeWireRoutingPlan::StopMode::TerminateOwnedProcess,
        2000,
        true,
        false,
        true,
    };
    const LinuxPipeWireRoutingPlan::Plan multiProcessPlan{
        plan.enabled,
        plan.scope,
        plan.requiredExecutables,
        {plan.startProcesses.front(), futureMonitor},
        {plan.stopProcesses.front(), futureMonitorStop},
        plan.configurationWrites,
        plan.virtualSinkNode,
        plan.virtualSourceNode,
        plan.bridgesVirtualSinkMonitorToSource,
        plan.changesDefaultInput,
        plan.changesDefaultOutput,
        plan.routesLocalPlayback,
        plan.writesSystemConfiguration,
    };
    const std::vector<LinuxPipeWireRoutingPlan::ProcessObservation> partialFailure{
        {token, LinuxPipeWireRoutingPlan::ProcessState::Running, {}, ""},
        {
            futureMonitor.ownershipToken,
            LinuxPipeWireRoutingPlan::ProcessState::FailedToStart,
            1,
            "injected failure",
        },
    };
    require(
        LinuxPipeWireRoutingPlan::diagnose(multiProcessPlan, readyTools, partialFailure).status ==
            LinuxPipeWireRoutingPlan::DiagnosticStatus::PartiallyActive,
        "mixed process outcomes must be classified as a partial activation");
}

} // namespace

int main()
{
    try {
        testDisabledPlanHasNoSideEffects();
        testEnabledPlanIsScopedAndSymmetric();
        testHostileStringsRemainSingleEscapedArguments();
        testGenerationIsDeterministicAndSessionSpecific();
        testDiagnosticsClassifyInjectedRuntimeState();
    } catch (const std::exception& error) {
        std::cerr << "cuelet PipeWire routing tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "cuelet PipeWire routing tests passed\n";
    return EXIT_SUCCESS;
}
