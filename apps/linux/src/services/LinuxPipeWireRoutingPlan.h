#pragma once

#include <optional>
#include <string>
#include <vector>

// Describes a temporary PipeWire graph without executing it. Callers must
// launch ProcessStart::argv directly (never through a shell), retain the child
// process handle under ownershipToken, and apply only the matching stop action.
class LinuxPipeWireRoutingPlan {
public:
    enum class Scope {
        UserSession,
    };

    enum class StopMode {
        TerminateOwnedProcess,
    };

    enum class ProcessState {
        Running,
        FailedToStart,
        ExitedUnexpectedly,
        Stopped,
        FailedToStop,
    };

    enum class DiagnosticStatus {
        Disabled,
        Ready,
        Active,
        MissingTool,
        SessionUnavailable,
        StartFailed,
        PartiallyActive,
        RuntimeFailed,
        Stopped,
        StopFailed,
        InvalidRuntimeState,
    };

    struct Request {
        bool enabled = false;
        std::string sessionKey;
        std::string sinkDescription = "Cuelet Virtual Microphone Input";
        std::string sourceDescription = "Cuelet Virtual Microphone";
    };

    struct ProcessStart {
        std::string ownershipToken;
        std::vector<std::string> argv;
        bool usesShell = false;
        bool longRunning = true;
    };

    struct ProcessStop {
        std::string ownershipToken;
        StopMode mode = StopMode::TerminateOwnedProcess;
        unsigned int gracePeriodMilliseconds = 2000;
        bool requiresTrackedChildHandle = true;
        bool allowNameBasedFallback = false;
        bool forceAfterGracePeriod = true;
    };

    struct ConfigurationWrite {
        std::string path;
        std::string contents;
        bool systemWide = false;
    };

    struct Plan {
        bool enabled = false;
        Scope scope = Scope::UserSession;
        std::vector<std::string> requiredExecutables;
        std::vector<ProcessStart> startProcesses;
        std::vector<ProcessStop> stopProcesses;
        std::vector<ConfigurationWrite> configurationWrites;
        std::string virtualSinkNode;
        std::string virtualSourceNode;
        bool bridgesVirtualSinkMonitorToSource = false;
        bool changesDefaultInput = false;
        bool changesDefaultOutput = false;
        bool routesLocalPlayback = false;
        bool writesSystemConfiguration = false;
    };

    struct ToolState {
        bool pipeWireSessionReachable = false;
        bool pwLoopbackAvailable = false;
    };

    struct ProcessObservation {
        std::string ownershipToken;
        ProcessState state = ProcessState::FailedToStart;
        std::optional<int> exitCode;
        std::string error;
    };

    struct Diagnostic {
        DiagnosticStatus status = DiagnosticStatus::Disabled;
        std::string summary;
        std::vector<std::string> details;
    };

    static Plan create(const Request& request);

    // Tool availability and process observations are supplied by the caller so
    // diagnostics remain deterministic and never probe or mutate the live graph.
    static Diagnostic diagnose(
        const Plan& plan,
        const ToolState& tools,
        const std::vector<ProcessObservation>& observations);
};
