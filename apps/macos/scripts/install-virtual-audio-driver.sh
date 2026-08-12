#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MACOS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE="${1:-${MACOS_DIR}/Driver/build/Release/CueletVirtualAudio.driver}"
DESTINATION_DIR="/Library/Audio/Plug-Ins/HAL"
DESTINATION="${DESTINATION_DIR}/CueletVirtualAudio.driver"
EXPECTED_BUNDLE_ID="ch.oki.cuelet.virtual-microphone.driver"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP="${DESTINATION_DIR}/CueletVirtualAudio.driver.backup-${TIMESTAMP}"
STAGE="${DESTINATION_DIR}/.CueletVirtualAudio.install-${$}"

echo "Source:      ${SOURCE}"
echo "Destination: ${DESTINATION}"
"${SCRIPT_DIR}/verify-virtual-audio-driver.sh" "${SOURCE}"

if [[ -e "${DESTINATION}" ]]; then
    INSTALLED_INFO="${DESTINATION}/Contents/Info.plist"
    INSTALLED_ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "${INSTALLED_INFO}" 2>/dev/null || true)"
    if [[ "${INSTALLED_ID}" != "${EXPECTED_BUNDLE_ID}" ]]; then
        echo "Refusing to replace a bundle not owned by Cuelet." >&2
        exit 1
    fi
    echo "Existing Cuelet bundle backup: ${BACKUP}"
else
    echo "Existing Cuelet bundle backup: not needed"
fi

read -r -p "Type INSTALL to copy this driver with administrator privileges: " CONFIRMATION
[[ "${CONFIRMATION}" == "INSTALL" ]] || {
    echo "Installation canceled."
    exit 2
}

cleanup_stage() {
    sudo rm -rf -- "${STAGE}" >/dev/null 2>&1 || true
}
trap cleanup_stage EXIT

sudo mkdir -p "${DESTINATION_DIR}"
sudo ditto "${SOURCE}" "${STAGE}"
sudo chown -R root:wheel "${STAGE}"
sudo find "${STAGE}" -type d -exec chmod 755 {} +
sudo find "${STAGE}" -type f -exec chmod 644 {} +
sudo chmod 755 "${STAGE}/Contents/MacOS/CueletVirtualAudio"
# Preserve a reliable app-side restart-required signal even when ditto keeps
# an older build timestamp from a bundle prepared before this boot.
sudo touch "${STAGE}/Contents/Info.plist"
sudo codesign --verify --deep --strict --verbose=4 "${STAGE}"

if [[ -e "${DESTINATION}" ]]; then
    sudo mv "${DESTINATION}" "${BACKUP}"
fi

if ! sudo mv "${STAGE}" "${DESTINATION}"; then
    if [[ -e "${BACKUP}" && ! -e "${DESTINATION}" ]]; then
        sudo mv "${BACKUP}" "${DESTINATION}"
    fi
    exit 1
fi

trap - EXIT
"${SCRIPT_DIR}/verify-virtual-audio-driver.sh" "${DESTINATION}"

echo "Installed ${DESTINATION}"
echo "A full Mac restart is required. coreaudiod was not killed or restarted."
