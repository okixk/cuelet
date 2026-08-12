#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MACOS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEFAULT_BUNDLE="${MACOS_DIR}/Driver/build/Release/CueletVirtualAudio.driver"
BUNDLE="${1:-${DEFAULT_BUNDLE}}"
EXPECTED_DESTINATION="/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver"
EXPECTED_BUNDLE_ID="ch.oki.cuelet.virtual-microphone.driver"
EXPECTED_EXECUTABLE="CueletVirtualAudio"
EXPECTED_DEVICE_UID="ch.oki.cuelet.virtual-microphone"
EXPECTED_MODEL_UID="ch.oki.cuelet.virtual-microphone.model"
EXPECTED_NAME="Cuelet Virtual Microphone"
EXPECTED_VERSION="0.1.11"
EXPECTED_BUILD="12"
EXPECTED_DIAGNOSTICS="${CUELET_EXPECT_DRIVER_DIAGNOSTICS:-0}"

fail() {
    echo "verify-virtual-audio-driver: $*" >&2
    exit 1
}

case "${EXPECTED_DIAGNOSTICS}" in
    0|1) ;;
    *) fail "CUELET_EXPECT_DRIVER_DIAGNOSTICS must be 0 or 1" ;;
esac

[[ -d "${BUNDLE}" ]] || fail "bundle not found: ${BUNDLE}"
BUNDLE="$(cd "$(dirname "${BUNDLE}")" && pwd)/$(basename "${BUNDLE}")"
INFO_PLIST="${BUNDLE}/Contents/Info.plist"
EXECUTABLE="${BUNDLE}/Contents/MacOS/${EXPECTED_EXECUTABLE}"
[[ -f "${INFO_PLIST}" ]] || fail "Info.plist is missing"
[[ -x "${EXECUTABLE}" ]] || fail "driver executable is missing or not executable"

plutil -lint "${INFO_PLIST}"
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "${INFO_PLIST}")" == "${EXPECTED_BUNDLE_ID}" ]] || fail "unexpected bundle identifier"
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "${INFO_PLIST}")" == "${EXPECTED_EXECUTABLE}" ]] || fail "unexpected executable name"
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "${INFO_PLIST}")" == "${EXPECTED_VERSION}" ]] || fail "unexpected driver version"
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "${INFO_PLIST}")" == "${EXPECTED_BUILD}" ]] || fail "unexpected driver build"
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFPlugInFactories:7506BE23-CB2E-4C1F-87B2-2E3FA31198E8' "${INFO_PLIST}")" == "CueletVirtualAudio_Create" ]] || fail "factory metadata is missing"

file "${EXECUTABLE}"
lipo -info "${EXECUTABLE}"
[[ "$(lipo -archs "${EXECUTABLE}")" == "arm64" ]] || fail "the first release must contain only arm64"
codesign --verify --deep --strict --verbose=4 "${BUNDLE}"
codesign -dvvv "${BUNDLE}" 2>&1

nm -gj "${EXECUTABLE}" | grep -qx '_CueletVirtualAudio_Create' || fail "factory symbol is not exported"
for value in "${EXPECTED_DEVICE_UID}" "${EXPECTED_MODEL_UID}" "${EXPECTED_NAME}" "Cuelet"; do
    strings "${EXECUTABLE}" | grep -Fqx "${value}" || fail "missing stable identity: ${value}"
done

DEPENDENCIES="$(otool -L "${EXECUTABLE}" | tail -n +2)"
echo "${DEPENDENCIES}"
if grep -Eq '/Users/|/private/tmp/|/var/folders/' <<<"${DEPENDENCIES}"; then
    fail "unexpected development-path dependency"
fi

if [[ -e "${EXPECTED_DESTINATION}" ]]; then
    INSTALLED_INFO="${EXPECTED_DESTINATION}/Contents/Info.plist"
    [[ -f "${INSTALLED_INFO}" ]] || fail "the expected destination is occupied by an invalid bundle"
    INSTALLED_ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "${INSTALLED_INFO}" 2>/dev/null || true)"
    [[ "${INSTALLED_ID}" == "${EXPECTED_BUNDLE_ID}" ]] || fail "installation would overwrite a non-Cuelet bundle"
fi

make -C "${MACOS_DIR}/Driver" test
make -C "${MACOS_DIR}/Driver" bundle-smoke CONFIGURATION=Release \
    DIAGNOSTICS="${EXPECTED_DIAGNOSTICS}" \
    SMOKE_DRIVER_EXECUTABLE="${EXECUTABLE}"

echo "Bundle structure: PASS"
echo "Stable identifiers: PASS"
echo "Destination ownership guard: PASS"
echo "Cuelet virtual audio driver verification: PASS"
