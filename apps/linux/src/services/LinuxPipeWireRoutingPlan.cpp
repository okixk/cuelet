#include "services/LinuxPipeWireRoutingPlan.h"

#include <cstdint>
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>

namespace {

std::string stableIdentifier(const std::string& value)
{
    // FNV-1a is used only to create compact, deterministic graph names. It is
    // not a security boundary and does not expose caller-controlled syntax.
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (const auto character : value) {
        hash ^= static_cast<unsigned char>(character);
        hash *= UINT64_C(1099511628211);
    }

    std::ostringstream result;
    result << std::hex << std::setfill('0') << std::setw(16) << hash;
    return result.str();
}

std::string jsonString(const std::string& value)
{
    static constexpr char hexadecimal[] = "0123456789abcdef";

    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (byte) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (byte < 0x20) {
                escaped += "\\u00";
                escaped.push_back(hexadecimal[(byte >> 4U) & 0x0fU]);
                escaped.push_back(hexadecimal[byte & 0x0fU]);
            } else {
                escaped.push_back(character);
            }
            break;
        }
    }
    escaped.push_back('"');
    return escaped;
}

std::string nodeProperties(
    const std::string& mediaClass,
    const std::string& nodeName,
    const std::string& description)
{
    return std::string("{") +
        "\"media.class\":" + jsonString(mediaClass) +
        ",\"node.name\":" + jsonString(nodeName) +
        ",\"node.description\":" + jsonString(description) +
        ",\"node.virtual\":true" +
        ",\"node.autoconnect\":false" +
        ",\"object.linger\":false" +
        ",\"priority.driver\":0" +
        ",\"priority.session\":0" +
        ",\"audio.channels\":2" +
        ",\"audio.position\":[\"FL\",\"FR\"]" +
        "}";
}

LinuxPipeWireRoutingPlan::Diagnostic diagnostic(
    LinuxPipeWireRoutingPlan::DiagnosticStatus status,
    std::string summary,
    std::vector<std::string> details = {})
{
    return LinuxPipeWireRoutingPlan::Diagnostic{
        status,
        std::move(summary),
        std::move(details),
    };
}

} // namespace

LinuxPipeWireRoutingPlan::Plan LinuxPipeWireRoutingPlan::create(const Request& request)
{
    Plan plan;
    if (!request.enabled) {
        return plan;
    }

    const auto identifier = stableIdentifier(request.sessionKey);
    const auto ownershipToken = "cuelet-pipewire-session-" + identifier;
    const auto virtualSinkNode = "cuelet_virtual_sink_" + identifier;
    const auto virtualSourceNode = "cuelet_virtual_source_" + identifier;
    const auto sinkDescription = request.sinkDescription.empty()
        ? std::string("Cuelet Virtual Microphone Input")
        : request.sinkDescription;
    const auto sourceDescription = request.sourceDescription.empty()
        ? std::string("Cuelet Virtual Microphone")
        : request.sourceDescription;

    const ProcessStart start{
        ownershipToken,
        {
            "pw-loopback",
            "--name=" + ownershipToken,
            "--group=" + ownershipToken,
            "--channels=2",
            "--channel-map=[ FL, FR ]",
            "--capture-props=" +
                nodeProperties("Audio/Sink", virtualSinkNode, sinkDescription),
            "--playback-props=" +
                nodeProperties("Audio/Source", virtualSourceNode, sourceDescription),
        },
        false,
        true,
    };
    const ProcessStop stop{
        ownershipToken,
        StopMode::TerminateOwnedProcess,
        2000,
        true,
        false,
        true,
    };

    return Plan{
        true,
        Scope::UserSession,
        {"pw-loopback"},
        {start},
        {stop},
        {},
        virtualSinkNode,
        virtualSourceNode,
        true,
        false,
        false,
        false,
        false,
    };
}

LinuxPipeWireRoutingPlan::Diagnostic LinuxPipeWireRoutingPlan::diagnose(
    const Plan& plan,
    const ToolState& tools,
    const std::vector<ProcessObservation>& observations)
{
    if (!plan.enabled) {
        return diagnostic(DiagnosticStatus::Disabled, "Virtual microphone routing is disabled.");
    }
    if (!tools.pwLoopbackAvailable) {
        return diagnostic(
            DiagnosticStatus::MissingTool,
            "PipeWire routing is unavailable because pw-loopback is missing.",
            {"Install a package that provides pw-loopback, then refresh diagnostics."});
    }
    if (!tools.pipeWireSessionReachable) {
        return diagnostic(
            DiagnosticStatus::SessionUnavailable,
            "The PipeWire user session is not reachable.",
            {"Cuelet did not make any routing or default-device changes."});
    }
    if (observations.empty()) {
        return diagnostic(
            DiagnosticStatus::Ready,
            "PipeWire is available and the temporary routing plan is ready.");
    }

    std::set<std::string> plannedTokens;
    for (const auto& process : plan.startProcesses) {
        plannedTokens.insert(process.ownershipToken);
    }

    std::set<std::string> observedTokens;
    std::size_t runningCount = 0;
    std::size_t failedStartCount = 0;
    std::size_t unexpectedExitCount = 0;
    std::size_t stoppedCount = 0;
    std::size_t failedStopCount = 0;
    std::vector<std::string> details;

    for (const auto& observation : observations) {
        if (plannedTokens.count(observation.ownershipToken) == 0 ||
            !observedTokens.insert(observation.ownershipToken).second) {
            return diagnostic(
                DiagnosticStatus::InvalidRuntimeState,
                "Routing diagnostics contained an unowned or duplicate process.",
                {"No untracked process will be stopped by Cuelet."});
        }

        switch (observation.state) {
        case ProcessState::Running:
            ++runningCount;
            break;
        case ProcessState::FailedToStart:
            ++failedStartCount;
            break;
        case ProcessState::ExitedUnexpectedly:
            ++unexpectedExitCount;
            break;
        case ProcessState::Stopped:
            ++stoppedCount;
            break;
        case ProcessState::FailedToStop:
            ++failedStopCount;
            break;
        }

        if (!observation.error.empty()) {
            details.push_back(observation.ownershipToken + ": " + observation.error);
        } else if (observation.exitCode.has_value() && *observation.exitCode != 0) {
            details.push_back(
                observation.ownershipToken + " exited with status " +
                std::to_string(*observation.exitCode) + ".");
        }
    }

    const auto plannedCount = plan.startProcesses.size();
    if (failedStopCount > 0) {
        return diagnostic(
            DiagnosticStatus::StopFailed,
            "Cuelet could not confirm cleanup of every owned routing process.",
            std::move(details));
    }
    if (runningCount == plannedCount && observedTokens.size() == plannedCount) {
        return diagnostic(
            DiagnosticStatus::Active,
            "The Cuelet virtual sink-to-source route is active.",
            std::move(details));
    }
    if (runningCount > 0) {
        return diagnostic(
            DiagnosticStatus::PartiallyActive,
            "Only part of the Cuelet-owned routing plan is active.",
            std::move(details));
    }
    if (failedStartCount > 0 &&
        failedStartCount + stoppedCount == plannedCount &&
        observedTokens.size() == plannedCount) {
        return diagnostic(
            DiagnosticStatus::StartFailed,
            "The Cuelet-owned PipeWire route could not be started and was rolled back.",
            std::move(details));
    }
    if (unexpectedExitCount > 0 &&
        unexpectedExitCount + stoppedCount == plannedCount &&
        observedTokens.size() == plannedCount) {
        return diagnostic(
            DiagnosticStatus::RuntimeFailed,
            "The Cuelet-owned PipeWire route exited unexpectedly and was cleaned up.",
            std::move(details));
    }
    if (stoppedCount == plannedCount && observedTokens.size() == plannedCount) {
        return diagnostic(
            DiagnosticStatus::Stopped,
            "The Cuelet-owned PipeWire route was stopped and cleaned up.",
            std::move(details));
    }
    if (failedStartCount > 0 || unexpectedExitCount > 0) {
        return diagnostic(
            DiagnosticStatus::PartiallyActive,
            "The Cuelet-owned routing plan completed only partially.",
            std::move(details));
    }

    return diagnostic(
        DiagnosticStatus::InvalidRuntimeState,
        "Routing diagnostics did not describe a complete Cuelet-owned state.",
        std::move(details));
}
