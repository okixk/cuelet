#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 9 ]]; then
    echo "Usage: test-release-package.sh PACKAGE APP_PACKAGE_VERSION DRIVER_PACKAGE_VERSION APP DRIVER APP_SCRIPTS DRIVER_SCRIPTS VALIDATION_ROOT" >&2
    exit 2
fi

PACKAGE_PATH="$1"
APP_PACKAGE_VERSION="$2"
DRIVER_PACKAGE_VERSION="$3"
STAGED_APP="$4"
STAGED_DRIVER="$5"
APP_SCRIPTS="$6"
DRIVER_SCRIPTS="$7"
VALIDATION_ROOT="$8"
# The ninth argument is reserved for forward-compatible test configuration.
TEST_CONFIGURATION="$9"

case "${TEST_CONFIGURATION}" in
    package-v2-local) PACKAGE_MODE="local" ;;
    package-v2-release) PACKAGE_MODE="release" ;;
    *)
        echo "Unknown package test configuration." >&2
        exit 2
        ;;
esac

APP_PACKAGE_ID="ch.oki.cuelet.pkg.application"
DRIVER_PACKAGE_ID="ch.oki.cuelet.pkg.virtual-audio"
PRODUCT_PACKAGE_ID="ch.oki.cuelet.installer"

WORK_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/cuelet-package-test.XXXXXX")"
cleanup() {
    if [[ -n "${WORK_ROOT:-}" && "${WORK_ROOT}" == "${TMPDIR:-/tmp}"/cuelet-package-test.* ]]; then
        /bin/rm -rf -- "${WORK_ROOT}"
    fi
}
trap cleanup EXIT

EXPANDED="${WORK_ROOT}/expanded"
pkgutil --expand "${PACKAGE_PATH}" "${EXPANDED}"

[[ -f "${EXPANDED}/Distribution" ]] || { echo "Distribution is missing." >&2; exit 1; }
[[ -e "${EXPANDED}/CueletApplication.pkg" ]] || { echo "App component is missing." >&2; exit 1; }
[[ -e "${EXPANDED}/CueletVirtualAudio.pkg" ]] || { echo "Driver component is missing." >&2; exit 1; }
LOCAL_RESOURCES="${VALIDATION_ROOT}/inventories/installer-resources-local"
PUBLIC_RESOURCES="${VALIDATION_ROOT}/inventories/installer-resources-release"
for page in Welcome.html ReadMe.html Conclusion.html; do
    [[ -s "${LOCAL_RESOURCES}/${page}" && -s "${PUBLIC_RESOURCES}/${page}" ]]
    grep -q 'LOCAL TEST PACKAGE — NOT FOR PUBLIC DISTRIBUTION' "${LOCAL_RESOURCES}/${page}"
done
if grep -Eiq 'LOCAL|TEST PACKAGE|NOT FOR PUBLIC DISTRIBUTION' "${PUBLIC_RESOURCES}"/*.html; then
    echo "Rendered public Installer resources contain local-test wording." >&2
    exit 1
fi
grep -q '<h1>Welcome to Cuelet</h1>' "${PUBLIC_RESOURCES}/Welcome.html"
grep -q 'This installer adds Cuelet and Cuelet Virtual Microphone to your Mac.' \
    "${PUBLIC_RESOURCES}/Welcome.html"
grep -q '<h1>Cuelet is installed</h1>' "${PUBLIC_RESOURCES}/Conclusion.html"
grep -q 'Cuelet is ready to use.' "${PUBLIC_RESOURCES}/Conclusion.html"
grep -q 'Restart your Mac when convenient to activate Cuelet Virtual Microphone.' \
    "${PUBLIC_RESOURCES}/Conclusion.html"
grep -q 'After restarting, open Cuelet and select Cuelet Virtual Microphone in Cuelet’s audio settings.' \
    "${PUBLIC_RESOURCES}/Conclusion.html"

if [[ "${PACKAGE_MODE}" == "local" ]]; then
    SELECTED_RESOURCES="${LOCAL_RESOURCES}"
else
    SELECTED_RESOURCES="${PUBLIC_RESOURCES}"
fi
for page in Welcome.html ReadMe.html Conclusion.html; do
    cmp -s "${SELECTED_RESOURCES}/${page}" "${EXPANDED}/Resources/${page}"
done
if [[ "${PACKAGE_MODE}" == "release" ]] &&
   grep -Eiq 'LOCAL|TEST PACKAGE|NOT FOR PUBLIC DISTRIBUTION' \
       "${EXPANDED}/Resources/Welcome.html" \
       "${EXPANDED}/Resources/ReadMe.html" \
       "${EXPANDED}/Resources/Conclusion.html"; then
    echo "Public Installer pages contain local-test wording." >&2
    exit 1
fi

grep -q "product id=\"${PRODUCT_PACKAGE_ID}\"" "${EXPANDED}/Distribution"
grep -q "pkg-ref id=\"${APP_PACKAGE_ID}\"" "${EXPANDED}/Distribution"
grep -q "pkg-ref id=\"${DRIVER_PACKAGE_ID}\"" "${EXPANDED}/Distribution"
if grep -Eq 'onConclusion(Script)?=|Require(Restart|Logout|Shutdown)' \
    "${EXPANDED}/Distribution" \
    "${EXPANDED}/CueletApplication.pkg/PackageInfo" \
    "${EXPANDED}/CueletVirtualAudio.pkg/PackageInfo"; then
    echo "The Installer still forces a restart, logout, or shutdown." >&2
    exit 1
fi
grep -q 'customize="never"' "${EXPANDED}/Distribution"
grep -q 'enable_localSystem="true"' "${EXPANDED}/Distribution"
grep -q 'enable_currentUserHome="false"' "${EXPANDED}/Distribution"

APP_COMPONENT="${EXPANDED}/CueletApplication.pkg"
DRIVER_COMPONENT="${EXPANDED}/CueletVirtualAudio.pkg"
APP_INFO="${STAGED_APP}/Contents/Info.plist"
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "${APP_INFO}")" == "Cuelet" ]]
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconName' "${APP_INFO}")" == "Cuelet" ]]
[[ -s "${STAGED_APP}/Contents/Resources/Assets.car" ]]
[[ -s "${STAGED_APP}/Contents/Resources/Cuelet.icns" ]]
[[ -s "${STAGED_APP}/Contents/Resources/LICENSE.txt" ]]
grep -q 'GNU AFFERO GENERAL PUBLIC LICENSE' "${STAGED_APP}/Contents/Resources/LICENSE.txt"
assetutil --info "${STAGED_APP}/Contents/Resources/Assets.car" \
    >"${VALIDATION_ROOT}/logs/packaged-app-asset-catalog.json"
grep -q '"Name" : "Cuelet"' "${VALIDATION_ROOT}/logs/packaged-app-asset-catalog.json"
(cd "${EXPANDED}" && find . -print | LC_ALL=C sort) \
    >"${VALIDATION_ROOT}/inventories/package-expanded-tree.txt"
cp "${APP_COMPONENT}/PackageInfo" "${VALIDATION_ROOT}/inventories/app-PackageInfo.xml"
cp "${DRIVER_COMPONENT}/PackageInfo" "${VALIDATION_ROOT}/inventories/driver-PackageInfo.xml"

APP_ID="$(xmllint --xpath 'string(/pkg-info/@identifier)' "${APP_COMPONENT}/PackageInfo")"
APP_VERSION="$(xmllint --xpath 'string(/pkg-info/@version)' "${APP_COMPONENT}/PackageInfo")"
DRIVER_ID="$(xmllint --xpath 'string(/pkg-info/@identifier)' "${DRIVER_COMPONENT}/PackageInfo")"
DRIVER_VERSION="$(xmllint --xpath 'string(/pkg-info/@version)' "${DRIVER_COMPONENT}/PackageInfo")"
[[ "${APP_ID}" == "${APP_PACKAGE_ID}" && "${APP_VERSION}" == "${APP_PACKAGE_VERSION}" ]]
[[ "${DRIVER_ID}" == "${DRIVER_PACKAGE_ID}" && "${DRIVER_VERSION}" == "${DRIVER_PACKAGE_VERSION}" ]]
grep -q '<upgrade-bundle>' "${APP_COMPONENT}/PackageInfo"
grep -q '<upgrade-bundle>' "${DRIVER_COMPONENT}/PackageInfo"
grep -q '<strict-identifier>' "${APP_COMPONENT}/PackageInfo"
grep -q '<strict-identifier>' "${DRIVER_COMPONENT}/PackageInfo"
grep -q 'relocatable="false"' "${APP_COMPONENT}/PackageInfo"
grep -q 'relocatable="false"' "${DRIVER_COMPONENT}/PackageInfo"

lsbom -s "${APP_COMPONENT}/Bom" >"${VALIDATION_ROOT}/inventories/app-payload.txt"
lsbom -s "${DRIVER_COMPONENT}/Bom" >"${VALIDATION_ROOT}/inventories/driver-payload.txt"
lsbom "${APP_COMPONENT}/Bom" >"${VALIDATION_ROOT}/inventories/app-modes-ownership.txt"
lsbom "${DRIVER_COMPONENT}/Bom" >"${VALIDATION_ROOT}/inventories/driver-modes-ownership.txt"
cp "${EXPANDED}/Distribution" "${VALIDATION_ROOT}/inventories/Distribution.xml"

while IFS= read -r path; do
    case "${path}" in
        .|./Applications|./Applications/Cuelet.app|./Applications/Cuelet.app/*) ;;
        *) echo "Unexpected path in app payload: ${path}" >&2; exit 1 ;;
    esac
done <"${VALIDATION_ROOT}/inventories/app-payload.txt"
while IFS= read -r path; do
    case "${path}" in
        .|./Library|./Library/Audio|./Library/Audio/Plug-Ins|./Library/Audio/Plug-Ins/HAL|\
        ./Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver|\
        ./Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver/*) ;;
        *) echo "Unexpected path in driver payload: ${path}" >&2; exit 1 ;;
    esac
done <"${VALIDATION_ROOT}/inventories/driver-payload.txt"

if grep -Eiq '(cuelet-driver-diagnostics|cuelet-hal-|cuelet-auhal-|\.wav$|\.jsonl$|\.DS_Store|/Tests?/|/Sources?/)' \
    "${VALIDATION_ROOT}/inventories/app-payload.txt" \
    "${VALIDATION_ROOT}/inventories/driver-payload.txt"; then
    echo "Development or validation content leaked into the package payload." >&2
    exit 1
fi

if awk '$2 ~ /^[0-9]+$/ && substr($2, length($2), 1) ~ /[2367]/ { bad=1 } END { exit !bad }' \
    "${VALIDATION_ROOT}/inventories/app-modes-ownership.txt" \
    "${VALIDATION_ROOT}/inventories/driver-modes-ownership.txt"; then
    echo "A packaged path is world-writable." >&2
    exit 1
fi
if awk '$3 != "0/0" { bad=1 } END { exit !bad }' \
    "${VALIDATION_ROOT}/inventories/app-modes-ownership.txt" \
    "${VALIDATION_ROOT}/inventories/driver-modes-ownership.txt"; then
    echo "Package payload ownership is not root:wheel." >&2
    exit 1
fi

if rg -a -n '/Users/[^/]+|~/projects/|/home/[^/]+' \
    "${STAGED_APP}" \
    "${STAGED_DRIVER}" \
    "${EXPANDED}/Distribution" \
    "${EXPANDED}/Resources" \
    "${APP_COMPONENT}/Scripts" \
    "${DRIVER_COMPONENT}/Scripts" \
    >"${VALIDATION_ROOT}/logs/developer-path-scan.txt"; then
    echo "A developer-specific path is embedded in a production payload." >&2
    exit 1
fi

run_preinstall() {
    local scripts="$1"
    local target_root="$2"
    "${scripts}/preinstall" ignored / "${target_root}"
}

make_case_root() {
    local label="$1"
    local relative_parent="$2"
    local root="${WORK_ROOT}/policy-${label}"
    mkdir -p "${root}${relative_parent}"
    printf '%s\n' "${root}"
}

# Clean install.
run_preinstall "${APP_SCRIPTS}" "$(make_case_root app-clean /Applications)"
run_preinstall "${DRIVER_SCRIPTS}" "$(make_case_root driver-clean /Library/Audio/Plug-Ins/HAL)"

# Same-version repair.
APP_SAME_ROOT="$(make_case_root app-same /Applications)"
ditto "${STAGED_APP}" "${APP_SAME_ROOT}/Applications/Cuelet.app"
run_preinstall "${APP_SCRIPTS}" "${APP_SAME_ROOT}"
DRIVER_SAME_ROOT="$(make_case_root driver-same /Library/Audio/Plug-Ins/HAL)"
ditto "${STAGED_DRIVER}" "${DRIVER_SAME_ROOT}/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver"
run_preinstall "${DRIVER_SCRIPTS}" "${DRIVER_SAME_ROOT}"
"${DRIVER_SCRIPTS}/postinstall" ignored / "${DRIVER_SAME_ROOT}"
if grep -Eiq 'coreaudiod|killall|launchctl|reboot|shutdown' "${DRIVER_SCRIPTS}/postinstall"; then
    echo "The driver post-install script attempts an unsafe activation or restart." >&2
    exit 1
fi

# Older Cuelet driver upgrade.
DRIVER_OLD_ROOT="$(make_case_root driver-old /Library/Audio/Plug-Ins/HAL)"
ditto "${STAGED_DRIVER}" "${DRIVER_OLD_ROOT}/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver"
/usr/libexec/PlistBuddy -c 'Set :CFBundleShortVersionString 0.1.8' \
    "${DRIVER_OLD_ROOT}/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleVersion 9' \
    "${DRIVER_OLD_ROOT}/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver/Contents/Info.plist"
run_preinstall "${DRIVER_SCRIPTS}" "${DRIVER_OLD_ROOT}"

# Newer Cuelet and foreign exact-path bundles must be rejected.
DRIVER_NEW_ROOT="$(make_case_root driver-new /Library/Audio/Plug-Ins/HAL)"
ditto "${STAGED_DRIVER}" "${DRIVER_NEW_ROOT}/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver"
/usr/libexec/PlistBuddy -c 'Set :CFBundleShortVersionString 99.0.0' \
    "${DRIVER_NEW_ROOT}/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver/Contents/Info.plist"
if run_preinstall "${DRIVER_SCRIPTS}" "${DRIVER_NEW_ROOT}" >/dev/null 2>&1; then
    echo "Newer driver downgrade was not rejected." >&2
    exit 1
fi

DRIVER_FOREIGN_ROOT="$(make_case_root driver-foreign /Library/Audio/Plug-Ins/HAL)"
ditto "${STAGED_DRIVER}" "${DRIVER_FOREIGN_ROOT}/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver"
/usr/libexec/PlistBuddy -c 'Set :CFBundleIdentifier com.example.foreign-driver' \
    "${DRIVER_FOREIGN_ROOT}/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver/Contents/Info.plist"
if run_preinstall "${DRIVER_SCRIPTS}" "${DRIVER_FOREIGN_ROOT}" >/dev/null 2>&1; then
    echo "Foreign exact-path driver was not rejected." >&2
    exit 1
fi

# A foreign bundle at another HAL path is outside package scope.
DRIVER_OTHER_ROOT="$(make_case_root driver-other /Library/Audio/Plug-Ins/HAL)"
mkdir -p "${DRIVER_OTHER_ROOT}/Library/Audio/Plug-Ins/HAL/OtherVendor.driver"
run_preinstall "${DRIVER_SCRIPTS}" "${DRIVER_OTHER_ROOT}"
[[ -d "${DRIVER_OTHER_ROOT}/Library/Audio/Plug-Ins/HAL/OtherVendor.driver" ]]

# Public mode must fail before packaging when signing identities are absent.
RELEASE_FAILURE_LOG="${VALIDATION_ROOT}/logs/release-mode-no-fallback.txt"
if env -u CUELET_DEVELOPER_ID_APPLICATION -u CUELET_DEVELOPER_ID_INSTALLER \
    "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-release-package.sh" \
    --release --skip-build \
    --output "${WORK_ROOT}/must-not-exist.pkg" \
    >"${RELEASE_FAILURE_LOG}" 2>&1; then
    echo "Release mode silently produced an unsigned package." >&2
    exit 1
fi
grep -q 'No unsigned or ad-hoc fallback was used' "${RELEASE_FAILURE_LOG}"
[[ ! -e "${WORK_ROOT}/must-not-exist.pkg" ]]

{
    echo "PASS: package identifiers and versions"
    echo "PASS: native app icon, license, and Installer resources"
    echo "PASS: exact app and driver destinations"
    echo "PASS: fixed, strict, atomic replacement metadata"
    echo "PASS: payload hygiene"
    echo "PASS: root:wheel ownership intent and non-world-writable modes"
    echo "PASS: clean install policy"
    echo "PASS: same-version repair policy"
    echo "PASS: older Cuelet driver upgrade policy"
    echo "PASS: newer Cuelet driver downgrade rejection"
    echo "PASS: foreign exact-path rejection"
    echo "PASS: unrelated HAL bundle preservation"
    echo "PASS: driver post-install verification harness"
    echo "PASS: local/public Installer copy remains distinct"
    echo "PASS: Installer completes without a forced restart"
    echo "PASS: full package remains a non-customizable system install"
    echo "PASS: release mode has no unsigned fallback"
} | tee "${VALIDATION_ROOT}/logs/package-tests.txt"
