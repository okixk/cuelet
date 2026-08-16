#!/usr/bin/env bash
set -euo pipefail

REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MACOS_APP="${1:-${REPOSITORY_ROOT}/apps/macos/dist/macos/Cuelet.app}"
APP_VERSION="$(tr -d '[:space:]' < "${REPOSITORY_ROOT}/VERSION")"

fail() {
    echo "release hygiene: $*" >&2
    exit 1
}

PRODUCT_SOURCES=(
    "${REPOSITORY_ROOT}/apps/macos/Cuelet"
    "${REPOSITORY_ROOT}/apps/linux/src"
    "${REPOSITORY_ROOT}/apps/windows/Cuelet.Core.Win32"
    "${REPOSITORY_ROOT}/apps/windows/Cuelet.WinUI"
)

if rg -n \
    'Show Demo Library|Demo Mode|--demo|loadDemoLibrary|PreviewLibrary|showsDemoLibrary' \
    "${PRODUCT_SOURCES[@]}"; then
    fail "demo behavior remains in a production source tree"
fi

if rg -n '/Users/[^/]+|/home/[^/]+|~/projects/cuelet' "${PRODUCT_SOURCES[@]}"; then
    fail "a production source tree contains a developer-specific path"
fi

if ! rg -q "value: false" "${REPOSITORY_ROOT}/apps/linux/meson_options.txt"; then
    fail "Linux developer tools are not disabled by default"
fi

if ! rg -q \
    "CueletIncludeDevelopmentVirtualAudioDriver.*false" \
    "${REPOSITORY_ROOT}/apps/windows/Cuelet.WinUI/Cuelet.WinUI.vcxproj"; then
    fail "Windows development driver packaging is not disabled by default"
fi

if ! rg -q "version: '${APP_VERSION}'" "${REPOSITORY_ROOT}/apps/linux/meson.build"; then
    fail "Linux application version does not match VERSION (${APP_VERSION})"
fi

if ! rg -q "Version=\"${APP_VERSION}\.0\"" \
    "${REPOSITORY_ROOT}/apps/windows/Cuelet.WinUI/Package.appxmanifest"; then
    fail "Windows package version does not match VERSION (${APP_VERSION})"
fi

if ! grep -Fq \
    "VALUE \"ProductVersion\", \"${APP_VERSION}\\0\"" \
    "${REPOSITORY_ROOT}/apps/windows/Cuelet.WinUI/Cuelet.rc"; then
    fail "Windows executable version does not match VERSION (${APP_VERSION})"
fi

if ! rg -q 'applicationVersionFromCurrentModule\(\)' \
    "${REPOSITORY_ROOT}/apps/windows/Cuelet.WinUI/MainWindow.xaml.cpp"; then
    fail "Windows About dialog does not use the executable release version"
fi

if [[ -d "${MACOS_APP}" ]]; then
    bundle_version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
        "${MACOS_APP}/Contents/Info.plist" 2>/dev/null || true)"
    [[ "${bundle_version}" == "${APP_VERSION}" ]] || \
        fail "macOS bundle version does not match VERSION (${APP_VERSION})"

    forbidden_bundle_entry="$({
        find "${MACOS_APP}" -type f \( \
            -name '*.swift' -o -name '*.c' -o -name '*.h' -o \
            -name '*.jsonl' -o -name '*.wav' -o -name '*.aiff' -o \
            -name '*Tests*' -o -name 'cuelet-driver-diagnostics' -o \
            -name 'cuelet-driver-property-probe' \
        \) -print
        find "${MACOS_APP}" -type d -name '*.xcassets' -print
    } | head -n 1)"
    [[ -z "${forbidden_bundle_entry}" ]] || \
        fail "unexpected production-bundle entry: ${forbidden_bundle_entry}"

    macos_executable="${MACOS_APP}/Contents/MacOS/Cuelet"
    if [[ -f "${macos_executable}" ]] && strings -a "${macos_executable}" | rg -q \
        'Show Demo Library|Demo Mode|--demo|/Users/[^/]+|/home/[^/]+|~/projects/cuelet'; then
        fail "the macOS Release executable contains demo markers or developer paths"
    fi
fi

echo "release hygiene: PASS"
