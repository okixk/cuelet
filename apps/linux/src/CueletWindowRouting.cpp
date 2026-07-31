#include "CueletWindow.h"

#include <glib.h>
#include <unistd.h>

#include <string>
#include <utility>

namespace {

std::uint64_t monotonicMilliseconds()
{
    return static_cast<std::uint64_t>(g_get_monotonic_time() / 1000);
}

cuelet_linux::VirtualMicrophoneRoutingMode serviceMode(
    const std::string& persisted)
{
    if (persisted == "virtualMicrophoneOnly") {
        return cuelet_linux::VirtualMicrophoneRoutingMode::VirtualMicrophoneOnly;
    }
    if (persisted == "speakersAndVirtualMicrophone") {
        return cuelet_linux::VirtualMicrophoneRoutingMode::SpeakersAndVirtualMicrophone;
    }
    return cuelet_linux::VirtualMicrophoneRoutingMode::SpeakersOnly;
}

LinuxAudioService::PlaybackRoutingMode playbackMode(
    cuelet_linux::VirtualMicrophoneRoutingMode mode)
{
    switch (mode) {
    case cuelet_linux::VirtualMicrophoneRoutingMode::VirtualMicrophoneOnly:
        return LinuxAudioService::PlaybackRoutingMode::VirtualMicrophoneOnly;
    case cuelet_linux::VirtualMicrophoneRoutingMode::SpeakersAndVirtualMicrophone:
        return LinuxAudioService::PlaybackRoutingMode::SpeakersAndVirtualMicrophone;
    case cuelet_linux::VirtualMicrophoneRoutingMode::SpeakersOnly:
        return LinuxAudioService::PlaybackRoutingMode::SpeakersOnly;
    }
    return LinuxAudioService::PlaybackRoutingMode::SpeakersOnly;
}

} // namespace

bool CueletWindow::enableVirtualMicrophone()
{
    if (settings_.virtualMicrophoneMode == "speakersOnly") {
        settings_.virtualMicrophoneMode = "virtualMicrophoneOnly";
    }
    if (!applyVirtualMicrophoneSettings()) {
        settings_.virtualMicrophoneMode = "speakersOnly";
        saveSettings();
        return false;
    }
    saveSettings();
    showToast(
        "Cuelet Virtual Microphone is active. Select it in the receiving application.");
    return true;
}

bool CueletWindow::disableVirtualMicrophone()
{
    settings_.virtualMicrophoneMode = "speakersOnly";
    audio_.stopAll();
    const bool audioReset = audio_.setRoutingConfiguration({});
    bool graphStopped = true;
    if (virtualMicrophoneService_) {
        cuelet_linux::VirtualMicrophoneConfiguration configuration;
        graphStopped = virtualMicrophoneService_->apply(
            configuration, monotonicMilliseconds());
    }
    saveSettings();
    refreshNowPlaying();
    if (!audioReset || !graphStopped) {
        showError(virtualMicrophoneStatus());
        return false;
    }
    showToast("Cuelet Virtual Microphone stopped and removed.");
    return true;
}

bool CueletWindow::applyVirtualMicrophoneSettings(bool reportFailure)
{
    if (!virtualMicrophoneService_) {
        return false;
    }
    const auto mode = serviceMode(settings_.virtualMicrophoneMode);
    if (!audio_.playingPaths().empty() &&
        playbackMode(mode) != audio_.routingConfiguration().mode) {
        if (reportFailure) {
            showError("Stop playback before changing virtual microphone routing.");
        }
        return false;
    }

    cuelet_linux::VirtualMicrophoneConfiguration configuration;
    configuration.mode = mode;
    configuration.mixPhysicalMicrophone = settings_.mixesPhysicalMicrophone;
    configuration.physicalMicrophoneId = settings_.physicalMicrophoneDevice;
    configuration.soundboardLevel = settings_.virtualMicrophoneLevel;
    configuration.physicalMicrophoneLevel = settings_.physicalMicrophoneLevel;
    if (!virtualMicrophoneService_->apply(
            configuration, monotonicMilliseconds())) {
        if (reportFailure) {
            showError(virtualMicrophoneStatus());
        }
        return false;
    }

    LinuxAudioService::RoutingConfiguration playback;
    playback.mode = playbackMode(mode);
    playback.virtualMicrophoneLevel = settings_.virtualMicrophoneLevel;
    if (mode != cuelet_linux::VirtualMicrophoneRoutingMode::SpeakersOnly) {
        playback.virtualSinkNode =
            virtualMicrophoneBackend_->virtualSinkNode();
    }
    if (!audio_.setRoutingConfiguration(std::move(playback))) {
        cuelet_linux::VirtualMicrophoneConfiguration disabled;
        virtualMicrophoneService_->apply(disabled, monotonicMilliseconds());
        return false;
    }
    return true;
}

void CueletWindow::pollVirtualMicrophone()
{
    if (!virtualMicrophoneService_) {
        return;
    }
    virtualMicrophoneService_->poll(monotonicMilliseconds());
    const auto& status = virtualMicrophoneService_->status();
    if (preferencesDialog_) {
        auto* statusRow = static_cast<GtkWidget*>(g_object_get_data(
            G_OBJECT(preferencesDialog_), "cuelet-vmic-status-row"));
        auto* endpointRow = static_cast<GtkWidget*>(g_object_get_data(
            G_OBJECT(preferencesDialog_), "cuelet-vmic-endpoint-row"));
        if (ADW_IS_ACTION_ROW(statusRow)) {
            adw_action_row_set_subtitle(
                ADW_ACTION_ROW(statusRow), status.message.c_str());
        }
        if (ADW_IS_ACTION_ROW(endpointRow)) {
            const auto endpoint = virtualMicrophoneEndpoint();
            adw_action_row_set_subtitle(
                ADW_ACTION_ROW(endpointRow), endpoint.c_str());
        }
    }
    if (status.state == cuelet_linux::VirtualMicrophoneState::Failed ||
        status.state == cuelet_linux::VirtualMicrophoneState::PipeWireUnavailable) {
        audio_.stopAll();
        return;
    }
    const auto desiredMode = playbackMode(serviceMode(settings_.virtualMicrophoneMode));
    if (status.virtualSourceVisible &&
        desiredMode != LinuxAudioService::PlaybackRoutingMode::SpeakersOnly &&
        audio_.routingConfiguration().mode != desiredMode &&
        audio_.playingPaths().empty()) {
        LinuxAudioService::RoutingConfiguration playback;
        playback.mode = desiredMode;
        playback.virtualSinkNode = virtualMicrophoneBackend_->virtualSinkNode();
        playback.virtualMicrophoneLevel = settings_.virtualMicrophoneLevel;
        audio_.setRoutingConfiguration(std::move(playback));
    }
}

bool CueletWindow::virtualMicrophoneActive()
{
    if (!virtualMicrophoneService_) {
        return false;
    }
    const auto state = virtualMicrophoneService_->status().state;
    return state == cuelet_linux::VirtualMicrophoneState::Starting ||
        state == cuelet_linux::VirtualMicrophoneState::Ready ||
        state == cuelet_linux::VirtualMicrophoneState::DegradedMicrophoneUnavailable ||
        state == cuelet_linux::VirtualMicrophoneState::Reconnecting;
}

bool CueletWindow::virtualMicrophoneNeedsCleanup() const
{
    return virtualMicrophoneService_ &&
        virtualMicrophoneService_->status().state !=
            cuelet_linux::VirtualMicrophoneState::Off;
}

std::string CueletWindow::virtualMicrophoneStatus()
{
    return virtualMicrophoneService_
        ? virtualMicrophoneService_->status().message
        : "Off";
}

std::string CueletWindow::virtualMicrophoneEndpoint() const
{
    if (!virtualMicrophoneBackend_ || !virtualMicrophoneService_ ||
        !virtualMicrophoneService_->status().virtualSourceVisible) {
        return "The virtual source is not currently visible in PipeWire.";
    }
    return "Cuelet Virtual Microphone (" +
        virtualMicrophoneBackend_->virtualSourceNode() + ")";
}

std::vector<cuelet_linux::PhysicalMicrophoneInfo>
CueletWindow::physicalMicrophones()
{
    return virtualMicrophoneService_
        ? virtualMicrophoneService_->physicalMicrophones()
        : std::vector<cuelet_linux::PhysicalMicrophoneInfo>{};
}
