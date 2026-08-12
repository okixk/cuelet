#!/usr/bin/env bash
set -euo pipefail

DESTINATION="/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver"
EXPECTED_BUNDLE_ID="ch.oki.cuelet.virtual-microphone.driver"
EXPECTED_EXECUTABLE="CueletVirtualAudio"
LOG="/tmp/cuelet-virtual-audio-uninstall-$(date +%Y%m%d-%H%M%S).log"

record() {
    printf '%s\n' "$*" | tee -a "${LOG}"
}

record "Cuelet virtual audio uninstall diagnostic"
record "Target: ${DESTINATION}"

if [[ ! -e "${DESTINATION}" ]]; then
    record "Result: Cuelet driver is not installed. No change made."
    exit 0
fi

INFO_PLIST="${DESTINATION}/Contents/Info.plist"
[[ -f "${INFO_PLIST}" ]] || {
    record "Refused: target has no Info.plist."
    exit 1
}
BUNDLE_ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "${INFO_PLIST}" 2>/dev/null || true)"
EXECUTABLE="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "${INFO_PLIST}" 2>/dev/null || true)"
if [[ "${BUNDLE_ID}" != "${EXPECTED_BUNDLE_ID}" || "${EXECUTABLE}" != "${EXPECTED_EXECUTABLE}" ]]; then
    record "Refused: target identity is not Cuelet's exact driver identity."
    exit 1
fi

record "Verified bundle identifier: ${BUNDLE_ID}"
read -r -p "Type UNINSTALL to remove only ${DESTINATION}: " CONFIRMATION
[[ "${CONFIRMATION}" == "UNINSTALL" ]] || {
    record "Result: uninstall canceled."
    exit 2
}

sudo rm -rf -- "${DESTINATION}"
if [[ -e "${DESTINATION}" ]]; then
    record "Result: removal failed."
    exit 1
fi

record "Result: removed the exact Cuelet driver bundle."
record "Restart required. coreaudiod was not killed or restarted."
record "Diagnostic log: ${LOG}"
