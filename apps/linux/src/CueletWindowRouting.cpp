#include "CueletWindow.h"

#include <unistd.h>

#include <string>

bool CueletWindow::enableVirtualMicrophone()
{
    if (pipeWireRouting_.isActive()) {
        return true;
    }
    if (!audio_.playingPaths().empty()) {
        showError("Stop playback before enabling the virtual microphone.");
        return false;
    }
    if (!LinuxAudioService::outputBackendAvailable(
            LinuxAudioService::OutputBackend::PipeWire)) {
        showError(
            "Cuelet cannot route audio because the GStreamer PipeWire output "
            "plugin is unavailable.");
        return false;
    }

    LinuxPipeWireRoutingPlan::Request request;
    request.enabled = true;
    request.sessionKey =
        "io.cuelet.Cuelet-" + std::to_string(static_cast<long long>(getpid()));
    const auto plan = LinuxPipeWireRoutingPlan::create(request);
    if (!pipeWireRouting_.start(plan)) {
        showError(virtualMicrophoneStatus());
        return false;
    }

    const auto previousOutput = audio_.outputSelection();
    LinuxAudioService::OutputSelection virtualOutput;
    virtualOutput.backend = LinuxAudioService::OutputBackend::PipeWire;
    virtualOutput.deviceId = pipeWireRouting_.virtualSinkNode();
    if (!audio_.setOutputSelection(virtualOutput)) {
        pipeWireRouting_.stop();
        return false;
    }

    outputBeforeVirtualMicrophone_ = previousOutput;
    showToast(
        "Temporary virtual microphone enabled. Select “Cuelet Virtual "
        "Microphone” in the receiving application.");
    return true;
}

bool CueletWindow::disableVirtualMicrophone()
{
    audio_.stopAll();
    const auto restoredOutput = outputBeforeVirtualMicrophone_.value_or(
        LinuxAudioService::OutputSelection{});
    if (!audio_.setOutputSelection(restoredOutput)) {
        audio_.setOutputSelection({});
    }
    outputBeforeVirtualMicrophone_.reset();

    const bool stopped = pipeWireRouting_.stop();
    if (!stopped) {
        showError(virtualMicrophoneStatus());
        return false;
    }
    showToast("Temporary virtual microphone stopped and removed.");
    refreshNowPlaying();
    return true;
}

bool CueletWindow::virtualMicrophoneActive()
{
    return pipeWireRouting_.isActive();
}

bool CueletWindow::virtualMicrophoneNeedsCleanup() const
{
    return outputBeforeVirtualMicrophone_.has_value()
        || pipeWireRouting_.ownedProcessCount() > 0;
}

std::string CueletWindow::virtualMicrophoneStatus()
{
    const auto& diagnostic = pipeWireRouting_.diagnostic();
    std::string status = diagnostic.summary;
    if (!diagnostic.details.empty()) {
        status += " " + diagnostic.details.front();
    }
    return status;
}

std::string CueletWindow::virtualMicrophoneEndpoint() const
{
    const auto endpoint = pipeWireRouting_.virtualSourceNode();
    return endpoint.empty()
        ? "No temporary endpoint is active."
        : "Session node: " + endpoint;
}
