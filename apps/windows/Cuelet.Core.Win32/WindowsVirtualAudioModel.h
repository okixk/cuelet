#pragma once

#include <string>
#include <string_view>

namespace cuelet::windows {

enum class VirtualAudioDriverStatus {
    NotInstalled,
    Installing,
    Installed,
    Connected,
    RepairRequired,
    RestartRequired,
    UpdateAvailable,
    InstallationFailed,
};

struct VirtualAudioVerification {
    bool packageInstalled = false;
    bool signatureTrusted = false;
    bool renderEndpointPresent = false;
    bool captureEndpointPresent = false;
    bool endpointPairValid = false;
    bool restartRequired = false;
    bool updateAvailable = false;
};

VirtualAudioDriverStatus driverStatus(
    const VirtualAudioVerification& verification,
    bool allowDevelopmentTestPackage = false) noexcept;
std::wstring driverStatusLabel(VirtualAudioDriverStatus status);
bool isCompleteCueletEndpointPair(
    const VirtualAudioVerification& verification,
    bool allowDevelopmentTestPackage = false) noexcept;
bool mayRemoveDriverPackage(std::wstring_view provider,
                            std::wstring_view hardwareId) noexcept;

enum class VirtualAudioInstallerAction {
    Install,
    Repair,
    Uninstall,
};

enum class InstallerLaunchOutcome {
    Completed,
    UacCanceled,
    LaunchFailed,
};

struct InstallerInvocation {
    InstallerLaunchOutcome outcome = InstallerLaunchOutcome::LaunchFailed;
    int exitCode = -1;
    bool restartRequired = false;
};

class IVirtualAudioInstallerProcessBackend {
public:
    virtual ~IVirtualAudioInstallerProcessBackend() = default;
    virtual InstallerInvocation invoke(VirtualAudioInstallerAction action) = 0;
};

class IVirtualAudioDriverVerificationBackend {
public:
    virtual ~IVirtualAudioDriverVerificationBackend() = default;
    virtual VirtualAudioVerification verify() = 0;
};

struct VirtualAudioInstallerWorkflowResult {
    VirtualAudioDriverStatus status = VirtualAudioDriverStatus::InstallationFailed;
    bool succeeded = false;
    bool uacCanceled = false;
};

VirtualAudioInstallerWorkflowResult runVirtualAudioInstallerWorkflow(
    VirtualAudioInstallerAction action,
    IVirtualAudioInstallerProcessBackend& process,
    IVirtualAudioDriverVerificationBackend& driver);

} // namespace cuelet::windows
