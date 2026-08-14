#!/usr/bin/env bash
set -euo pipefail

APP_NAME="Cuelet"
BUNDLE_IDENTIFIER="ch.oki.cuelet"
CONFIGURATION="release"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPOSITORY_ROOT="$(cd "${PACKAGE_DIR}/../.." && pwd)"
APP_VERSION="$(tr -d '[:space:]' < "${REPOSITORY_ROOT}/VERSION")"
APP_BUILD="$(tr -d '[:space:]' < "${PACKAGE_DIR}/APP_BUILD")"
DIST_DIR="${CUELET_DIST_DIR:-${PACKAGE_DIR}/dist/macos}"
APP_DIR="${DIST_DIR}/${APP_NAME}.app"
CONTENTS_DIR="${APP_DIR}/Contents"
MACOS_DIR="${CONTENTS_DIR}/MacOS"
RESOURCES_DIR="${CONTENTS_DIR}/Resources"
ICON_NAME="${APP_NAME}"
ICON_SOURCE="${PACKAGE_DIR}/Cuelet/Resources/${ICON_NAME}.icon"
TEMP_ROOT="${TMPDIR:-/tmp}"
SCRATCH_PATH="${CUELET_SWIFTPM_SCRATCH_PATH:-${TEMP_ROOT%/}/cuelet-swift-build}"
ICON_INFO_PLIST="${SCRATCH_PATH}/${ICON_NAME}-Icon-Info.plist"

cd "${PACKAGE_DIR}"

export CLANG_MODULE_CACHE_PATH="${CLANG_MODULE_CACHE_PATH:-${TEMP_ROOT%/}/cuelet-module-cache}"
mkdir -p "${CLANG_MODULE_CACHE_PATH}" "${SCRATCH_PATH}"

[[ -d "${ICON_SOURCE}" ]] || {
    echo "Cuelet Icon Composer asset is missing: ${ICON_SOURCE}" >&2
    exit 1
}
[[ -f "${REPOSITORY_ROOT}/LICENSE" ]] || {
    echo "Repository license is missing: ${REPOSITORY_ROOT}/LICENSE" >&2
    exit 1
}
ACTOOL="$(xcrun --find actool)"

swift build \
    -c "${CONFIGURATION}" \
    --product "${APP_NAME}" \
    --disable-sandbox \
    --scratch-path "${SCRATCH_PATH}"

BUILD_DIR="$(swift build \
    -c "${CONFIGURATION}" \
    --disable-sandbox \
    --scratch-path "${SCRATCH_PATH}" \
    --show-bin-path)"
EXECUTABLE_PATH="${BUILD_DIR}/${APP_NAME}"

rm -rf "${APP_DIR}"
mkdir -p "${MACOS_DIR}" "${RESOURCES_DIR}"

cp "${EXECUTABLE_PATH}" "${MACOS_DIR}/${APP_NAME}"
chmod +x "${MACOS_DIR}/${APP_NAME}"
codesign --remove-signature "${MACOS_DIR}/${APP_NAME}" >/dev/null 2>&1 || true
strip -S "${MACOS_DIR}/${APP_NAME}"

"${ACTOOL}" \
    "${ICON_SOURCE}" \
    --compile "${RESOURCES_DIR}" \
    --platform macosx \
    --minimum-deployment-target 14.0 \
    --app-icon "${ICON_NAME}" \
    --output-partial-info-plist "${ICON_INFO_PLIST}" \
    --warnings \
    --notices

[[ -s "${RESOURCES_DIR}/Assets.car" && -s "${RESOURCES_DIR}/${ICON_NAME}.icns" ]] || {
    echo "The native Cuelet icon did not compile into Assets.car and ${ICON_NAME}.icns." >&2
    exit 1
}
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconName' "${ICON_INFO_PLIST}")" == "${ICON_NAME}" &&
   "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "${ICON_INFO_PLIST}")" == "${ICON_NAME}" ]] || {
    echo "The compiled Cuelet icon metadata is inconsistent." >&2
    exit 1
}

while IFS= read -r resource_bundle; do
    ditto "${resource_bundle}" "${RESOURCES_DIR}/$(basename "${resource_bundle}")"
done < <(find "${BUILD_DIR}" -maxdepth 1 -name "*.bundle" -type d ! -name "*Tests*")

install -m 0644 "${REPOSITORY_ROOT}/LICENSE" "${RESOURCES_DIR}/LICENSE.txt"
cmp -s "${REPOSITORY_ROOT}/LICENSE" "${RESOURCES_DIR}/LICENSE.txt" || {
    echo "The packaged Cuelet license does not match the repository LICENSE." >&2
    exit 1
}

if [[ -n "${CUELET_VIRTUAL_AUDIO_DRIVER_BUNDLE:-}" ]]; then
    DRIVER_SOURCE="${CUELET_VIRTUAL_AUDIO_DRIVER_BUNDLE}"
else
    CUELET_DRIVER_DIAGNOSTICS=0 \
        "${SCRIPT_DIR}/build-virtual-audio-driver.sh" Release
    DRIVER_SOURCE="${PACKAGE_DIR}/Driver/build/Release/CueletVirtualAudio.driver"
fi
if [[ -d "${DRIVER_SOURCE}" ]]; then
    mkdir -p "${RESOURCES_DIR}/Driver"
    ditto "${DRIVER_SOURCE}" "${RESOURCES_DIR}/Driver/CueletVirtualAudio.driver"
fi

cat > "${CONTENTS_DIR}/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleDisplayName</key>
    <string>${APP_NAME}</string>
    <key>CFBundleExecutable</key>
    <string>${APP_NAME}</string>
    <key>CFBundleIdentifier</key>
    <string>${BUNDLE_IDENTIFIER}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleIconFile</key>
    <string>${ICON_NAME}</string>
    <key>CFBundleIconName</key>
    <string>${ICON_NAME}</string>
    <key>CFBundleName</key>
    <string>${APP_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${APP_VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${APP_BUILD}</string>
    <key>LSMinimumSystemVersion</key>
    <string>14.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSMicrophoneUsageDescription</key>
    <string>Cuelet uses microphone access for input-level monitoring and optional microphone routing.</string>
    <key>NSSupportsAutomaticTermination</key>
    <false/>
    <key>NSSupportsSuddenTermination</key>
    <false/>
</dict>
</plist>
PLIST

printf 'APPL????' > "${CONTENTS_DIR}/PkgInfo"

if command -v codesign >/dev/null 2>&1; then
    codesign --force --deep --options runtime --sign - "${APP_DIR}" >/dev/null
fi

echo "Built ${APP_DIR}"
