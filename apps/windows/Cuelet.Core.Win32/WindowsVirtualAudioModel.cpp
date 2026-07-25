#include "WindowsVirtualAudioModel.h"

#include <algorithm>
#include <cwctype>

namespace cuelet::windows {
namespace {

std::wstring lowercase(std::wstring_view value)
{
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return result;
}

} // namespace

VirtualAudioDriverStatus driverStatus(
    const VirtualAudioVerification& verification,
    bool allowDevelopmentTestPackage) noexcept
{
    if (!verification.packageInstalled) return VirtualAudioDriverStatus::NotInstalled;
    if (verification.restartRequired) return VirtualAudioDriverStatus::RestartRequired;
    if (verification.updateAvailable) return VirtualAudioDriverStatus::UpdateAvailable;
    if ((!verification.signatureTrusted && !allowDevelopmentTestPackage) ||
        !verification.renderEndpointPresent ||
        !verification.captureEndpointPresent ||
        !verification.endpointPairValid) {
        return VirtualAudioDriverStatus::RepairRequired;
    }
    return VirtualAudioDriverStatus::Connected;
}

std::wstring driverStatusLabel(VirtualAudioDriverStatus status)
{
    switch (status) {
    case VirtualAudioDriverStatus::NotInstalled: return L"Not installed";
    case VirtualAudioDriverStatus::Installing: return L"Installing\u2026";
    case VirtualAudioDriverStatus::Installed: return L"Installed";
    case VirtualAudioDriverStatus::Connected: return L"Connected";
    case VirtualAudioDriverStatus::RepairRequired: return L"Repair required";
    case VirtualAudioDriverStatus::RestartRequired: return L"Restart required";
    case VirtualAudioDriverStatus::UpdateAvailable: return L"Update available";
    case VirtualAudioDriverStatus::InstallationFailed: return L"Installation failed";
    }
    return L"Installation failed";
}

bool isCompleteCueletEndpointPair(
    const VirtualAudioVerification& verification,
    bool allowDevelopmentTestPackage) noexcept
{
    return verification.packageInstalled &&
           (verification.signatureTrusted || allowDevelopmentTestPackage) &&
           verification.renderEndpointPresent &&
           verification.captureEndpointPresent &&
           verification.endpointPairValid;
}

bool mayRemoveDriverPackage(std::wstring_view provider,
                            std::wstring_view hardwareId) noexcept
{
    return lowercase(provider) == L"cuelet" &&
           lowercase(hardwareId) == L"root\\cueletvirtualaudio";
}

VirtualAudioInstallerWorkflowResult runVirtualAudioInstallerWorkflow(
    VirtualAudioInstallerAction action,
    IVirtualAudioInstallerProcessBackend& process,
    IVirtualAudioDriverVerificationBackend& driver)
{
    const auto invocation = process.invoke(action);
    if (invocation.outcome == InstallerLaunchOutcome::UacCanceled) {
        return {driverStatus(driver.verify()), false, true};
    }
    if (invocation.outcome != InstallerLaunchOutcome::Completed ||
        invocation.exitCode != 0) {
        return {VirtualAudioDriverStatus::InstallationFailed, false, false};
    }

    auto verification = driver.verify();
    verification.restartRequired =
        verification.restartRequired || invocation.restartRequired;
    if (action == VirtualAudioInstallerAction::Uninstall) {
        return verification.packageInstalled
            ? VirtualAudioInstallerWorkflowResult{
                  VirtualAudioDriverStatus::InstallationFailed, false, false}
            : VirtualAudioInstallerWorkflowResult{
                  VirtualAudioDriverStatus::NotInstalled, true, false};
    }
    if (verification.restartRequired) {
        return {VirtualAudioDriverStatus::RestartRequired, true, false};
    }
    if (isCompleteCueletEndpointPair(verification)) {
        return {VirtualAudioDriverStatus::Connected, true, false};
    }
    if (verification.packageInstalled) {
        return {VirtualAudioDriverStatus::RepairRequired, false, false};
    }
    return {VirtualAudioDriverStatus::InstallationFailed, false, false};
}

} // namespace cuelet::windows
