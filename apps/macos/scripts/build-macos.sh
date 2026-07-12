#!/usr/bin/env bash
set -euo pipefail

APP_NAME="Cuelet"
BUNDLE_IDENTIFIER="ch.oki.cuelet"
CONFIGURATION="release"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DIST_DIR="${CUELET_DIST_DIR:-${PACKAGE_DIR}/dist/macos}"
APP_DIR="${DIST_DIR}/${APP_NAME}.app"
CONTENTS_DIR="${APP_DIR}/Contents"
MACOS_DIR="${CONTENTS_DIR}/MacOS"
RESOURCES_DIR="${CONTENTS_DIR}/Resources"
SCRATCH_PATH="${CUELET_SWIFTPM_SCRATCH_PATH:-/private/tmp/cuelet-swift-build}"

cd "${PACKAGE_DIR}"

export CLANG_MODULE_CACHE_PATH="${CLANG_MODULE_CACHE_PATH:-/private/tmp/cuelet-module-cache}"
mkdir -p "${CLANG_MODULE_CACHE_PATH}" "${SCRATCH_PATH}"

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

find "${BUILD_DIR}" -maxdepth 1 -name "*.bundle" -type d -exec cp -R {} "${RESOURCES_DIR}/" \;

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
    <key>CFBundleName</key>
    <string>${APP_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>0.1.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>LSMinimumSystemVersion</key>
    <string>14.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSMicrophoneUsageDescription</key>
    <string>Cuelet uses microphone access only for local input monitoring and future audio routing controls.</string>
    <key>NSSupportsAutomaticTermination</key>
    <false/>
    <key>NSSupportsSuddenTermination</key>
    <false/>
</dict>
</plist>
PLIST

printf 'APPL????' > "${CONTENTS_DIR}/PkgInfo"

if command -v codesign >/dev/null 2>&1; then
    codesign --force --deep --sign - "${APP_DIR}" >/dev/null
fi

echo "Built ${APP_DIR}"
