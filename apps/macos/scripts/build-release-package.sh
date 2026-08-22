#!/usr/bin/env bash
set -euo pipefail

APP_NAME="Cuelet"
APP_BUNDLE_ID="ch.oki.cuelet"
APP_PACKAGE_ID="ch.oki.cuelet.pkg.application"
DRIVER_PACKAGE_ID="ch.oki.cuelet.pkg.virtual-audio"
PRODUCT_PACKAGE_ID="ch.oki.cuelet.installer"
DRIVER_BUNDLE_ID="ch.oki.cuelet.virtual-microphone.driver"
DRIVER_BUNDLE_NAME="CueletVirtualAudio.driver"
DRIVER_EXECUTABLE="CueletVirtualAudio"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MACOS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPOSITORY_ROOT="$(cd "${MACOS_DIR}/../.." && pwd)"
INSTALLER_DIR="${MACOS_DIR}/Installer"
DIST_DIR="${CUELET_DIST_DIR:-${MACOS_DIR}/dist/macos}"
APP_SOURCE="${DIST_DIR}/${APP_NAME}.app"
DRIVER_BUILD_SOURCE="${MACOS_DIR}/Driver/build/Release/${DRIVER_BUNDLE_NAME}"

usage() {
    cat <<'USAGE'
Usage: build-release-package.sh (--local | --beta-unsigned | --release) [options]

Options:
  --skip-build       Package the existing verified Release artifacts.
  --output PATH      Override the output package path.
  --validation-root PATH
                     Store package inspection evidence under PATH.

Local mode creates an unsigned structural test package. Beta mode creates the
intentional public beta package with an unsigned outer product archive. Release mode requires
CUELET_DEVELOPER_ID_APPLICATION and CUELET_DEVELOPER_ID_INSTALLER and never
falls back to ad-hoc or unsigned signing.
USAGE
}

MODE=""
SKIP_BUILD=0
OUTPUT_PATH=""
VALIDATION_ROOT="${CUELET_INSTALLER_VALIDATION_ROOT:-}"

while (($# > 0)); do
    case "$1" in
        --local|--beta-unsigned|--release)
            if [[ -n "${MODE}" ]]; then
                echo "Choose exactly one packaging mode." >&2
                exit 2
            fi
            MODE="${1#--}"
            shift
            ;;
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        --output)
            [[ $# -ge 2 ]] || { usage >&2; exit 2; }
            OUTPUT_PATH="$2"
            shift 2
            ;;
        --validation-root)
            [[ $# -ge 2 ]] || { usage >&2; exit 2; }
            VALIDATION_ROOT="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ -z "${MODE}" ]]; then
    usage >&2
    exit 2
fi

if [[ "${MODE}" == "release" ]]; then
    if [[ -z "${CUELET_DEVELOPER_ID_APPLICATION:-}" ||
          -z "${CUELET_DEVELOPER_ID_INSTALLER:-}" ]]; then
        echo "Release packaging requires CUELET_DEVELOPER_ID_APPLICATION and CUELET_DEVELOPER_ID_INSTALLER." >&2
        echo "No unsigned or ad-hoc fallback was used." >&2
        exit 2
    fi
fi

for tool in pkgbuild productbuild pkgutil plutil codesign ditto lipo shasum assetutil tr; do
    command -v "${tool}" >/dev/null 2>&1 || {
        echo "Required macOS packaging tool is unavailable: ${tool}" >&2
        exit 1
    }
done

APPLE_SAMPLE_LICENSE_SOURCE="${MACOS_DIR}/Driver/APPLE_SAMPLE_LICENSE.txt"
[[ -s "${APPLE_SAMPLE_LICENSE_SOURCE}" ]] || {
    echo "Apple sample license is missing: ${APPLE_SAMPLE_LICENSE_SOURCE}" >&2
    exit 1
}

if [[ -z "${VALIDATION_ROOT}" ]]; then
    VALIDATION_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/cuelet-installer-evidence.XXXXXX")"
fi
if [[ "${VALIDATION_ROOT}" != /* ]]; then
    echo "Validation root must be an absolute path." >&2
    exit 2
fi
mkdir -p \
    "${VALIDATION_ROOT}/logs" \
    "${VALIDATION_ROOT}/inventories" \
    "${VALIDATION_ROOT}/packages"

if ((SKIP_BUILD == 0)); then
    CUELET_DRIVER_DIAGNOSTICS=0 "${SCRIPT_DIR}/build-macos.sh"
fi

[[ -d "${APP_SOURCE}" ]] || {
    echo "Release app is missing: ${APP_SOURCE}" >&2
    exit 1
}
[[ -d "${DRIVER_BUILD_SOURCE}" ]] || {
    echo "Production driver artifact is missing: ${DRIVER_BUILD_SOURCE}" >&2
    exit 1
}

ROOT_VERSION="$(tr -d '[:space:]' < "${REPOSITORY_ROOT}/VERSION")"
ROOT_BUILD="$(tr -d '[:space:]' < "${MACOS_DIR}/APP_BUILD")"
APP_INFO="${APP_SOURCE}/Contents/Info.plist"
APP_VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "${APP_INFO}")"
APP_BUILD="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "${APP_INFO}")"
APP_ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "${APP_INFO}")"
APP_ICON_FILE="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "${APP_INFO}")"
APP_ICON_NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconName' "${APP_INFO}")"

if [[ "${APP_ID}" != "${APP_BUNDLE_ID}" ||
      "${APP_VERSION}" != "${ROOT_VERSION}" ||
      "${APP_BUILD}" != "${ROOT_BUILD}" ||
      "${APP_ICON_FILE}" != "${APP_NAME}" ||
      "${APP_ICON_NAME}" != "${APP_NAME}" ]]; then
    echo "Cuelet app identity, version, build, or icon metadata is inconsistent." >&2
    exit 1
fi
[[ -s "${APP_SOURCE}/Contents/Resources/Assets.car" &&
   -s "${APP_SOURCE}/Contents/Resources/${APP_NAME}.icns" ]] || {
    echo "Cuelet app is missing its compiled native icon resources." >&2
    exit 1
}
[[ -s "${APP_SOURCE}/Contents/Resources/LICENSE.txt" ]] &&
    cmp -s "${REPOSITORY_ROOT}/LICENSE" "${APP_SOURCE}/Contents/Resources/LICENSE.txt" || {
    echo "Cuelet app is missing the exact repository license text." >&2
    exit 1
}
[[ -s "${APP_SOURCE}/Contents/Resources/APPLE_SAMPLE_LICENSE.txt" ]] &&
    cmp -s "${APPLE_SAMPLE_LICENSE_SOURCE}" "${APP_SOURCE}/Contents/Resources/APPLE_SAMPLE_LICENSE.txt" || {
    echo "Cuelet app is missing the exact tracked Apple sample notice." >&2
    exit 1
}
assetutil --info "${APP_SOURCE}/Contents/Resources/Assets.car" \
    >"${VALIDATION_ROOT}/logs/app-asset-catalog.json"
grep -q '"Name" : "Cuelet"' "${VALIDATION_ROOT}/logs/app-asset-catalog.json" || {
    echo "Cuelet is missing from the compiled app asset catalog." >&2
    exit 1
}

DRIVER_INFO="${DRIVER_BUILD_SOURCE}/Contents/Info.plist"
DRIVER_VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "${DRIVER_INFO}")"
DRIVER_BUILD="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "${DRIVER_INFO}")"
DRIVER_ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "${DRIVER_INFO}")"
if [[ "${DRIVER_ID}" != "${DRIVER_BUNDLE_ID}" ]]; then
    echo "Production driver bundle identity is invalid." >&2
    exit 1
fi

EMBEDDED_DRIVER="${APP_SOURCE}/Contents/Resources/Driver/${DRIVER_BUNDLE_NAME}"
[[ -d "${EMBEDDED_DRIVER}" ]] || {
    echo "Release app does not contain the production driver." >&2
    exit 1
}
[[ -s "${EMBEDDED_DRIVER}/Contents/Resources/APPLE_SAMPLE_LICENSE.txt" ]] &&
    cmp -s "${APPLE_SAMPLE_LICENSE_SOURCE}" "${EMBEDDED_DRIVER}/Contents/Resources/APPLE_SAMPLE_LICENSE.txt" || {
    echo "The embedded driver is missing the exact tracked Apple sample notice." >&2
    exit 1
}
EMBEDDED_DRIVER_VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "${EMBEDDED_DRIVER}/Contents/Info.plist")"
EMBEDDED_DRIVER_BUILD="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "${EMBEDDED_DRIVER}/Contents/Info.plist")"
if [[ "${EMBEDDED_DRIVER_VERSION}" != "${DRIVER_VERSION}" ||
      "${EMBEDDED_DRIVER_BUILD}" != "${DRIVER_BUILD}" ]]; then
    echo "The app's embedded driver does not match the Release driver version." >&2
    exit 1
fi

SOURCE_DRIVER_HASH="$(shasum -a 256 "${DRIVER_BUILD_SOURCE}/Contents/MacOS/${DRIVER_EXECUTABLE}" | awk '{print $1}')"
EMBEDDED_DRIVER_HASH="$(shasum -a 256 "${EMBEDDED_DRIVER}/Contents/MacOS/${DRIVER_EXECUTABLE}" | awk '{print $1}')"
if [[ "${SOURCE_DRIVER_HASH}" != "${EMBEDDED_DRIVER_HASH}" ]]; then
    echo "The embedded driver binary is not byte-identical to the Release driver binary." >&2
    exit 1
fi

CUELET_EXPECT_DRIVER_DIAGNOSTICS=0 \
    "${SCRIPT_DIR}/verify-virtual-audio-driver.sh" "${DRIVER_BUILD_SOURCE}" \
    >"${VALIDATION_ROOT}/logs/production-driver-verifier.log" 2>&1

APP_ARCHS="$(lipo -archs "${APP_SOURCE}/Contents/MacOS/${APP_NAME}")"
DRIVER_ARCHS="$(lipo -archs "${DRIVER_BUILD_SOURCE}/Contents/MacOS/${DRIVER_EXECUTABLE}")"
if [[ " ${APP_ARCHS} " != *" arm64 "* || " ${DRIVER_ARCHS} " != *" arm64 "* ]]; then
    echo "Cuelet packaging currently requires arm64 Release artifacts." >&2
    exit 1
fi

PACKAGE_VERSION="${APP_VERSION}.${APP_BUILD}"
DRIVER_PACKAGE_VERSION="${DRIVER_VERSION}.${DRIVER_BUILD}"
if [[ -z "${OUTPUT_PATH}" ]]; then
    if [[ "${MODE}" == "local" ]]; then
        OUTPUT_PATH="${DIST_DIR}/Cuelet-${APP_VERSION}-local.pkg"
    elif [[ "${MODE}" == "beta-unsigned" ]]; then
        OUTPUT_PATH="${DIST_DIR}/Cuelet-${APP_VERSION}-beta.1-macos-arm64-unsigned.pkg"
    else
        OUTPUT_PATH="${DIST_DIR}/Cuelet-${APP_VERSION}.pkg"
    fi
fi
if [[ "${OUTPUT_PATH}" != /* ]]; then
    OUTPUT_PATH="${PWD}/${OUTPUT_PATH}"
fi
mkdir -p "$(dirname "${OUTPUT_PATH}")"

WORK_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/cuelet-package-build.XXXXXX")"
cleanup() {
    if [[ -n "${WORK_ROOT:-}" && "${WORK_ROOT}" == "${TMPDIR:-/tmp}"/cuelet-package-build.* ]]; then
        /bin/rm -rf -- "${WORK_ROOT}"
    fi
}
trap cleanup EXIT

APP_ROOT="${WORK_ROOT}/app-root"
DRIVER_ROOT="${WORK_ROOT}/driver-root"
PACKAGES_DIR="${WORK_ROOT}/components"
APP_SCRIPTS="${WORK_ROOT}/app-scripts"
DRIVER_SCRIPTS="${WORK_ROOT}/driver-scripts"
PRODUCT_RESOURCES="${WORK_ROOT}/resources"
mkdir -p \
    "${APP_ROOT}/Applications" \
    "${DRIVER_ROOT}/Library/Audio/Plug-Ins/HAL" \
    "${PACKAGES_DIR}" \
    "${APP_SCRIPTS}" \
    "${DRIVER_SCRIPTS}" \
    "${PRODUCT_RESOURCES}"

ditto "${APP_SOURCE}" "${APP_ROOT}/Applications/${APP_NAME}.app"
STAGED_APP="${APP_ROOT}/Applications/${APP_NAME}.app"
STAGED_EMBEDDED_DRIVER="${STAGED_APP}/Contents/Resources/Driver/${DRIVER_BUNDLE_NAME}"

if [[ "${MODE}" == "release" ]]; then
    APP_SIGN_ARGS=(--force --options runtime --timestamp --sign "${CUELET_DEVELOPER_ID_APPLICATION}")
    if [[ -n "${CUELET_SIGNING_KEYCHAIN:-}" ]]; then
        APP_SIGN_ARGS+=(--keychain "${CUELET_SIGNING_KEYCHAIN}")
    fi
    codesign "${APP_SIGN_ARGS[@]}" "${STAGED_EMBEDDED_DRIVER}"
    codesign "${APP_SIGN_ARGS[@]}" "${STAGED_APP}"
    ditto "${STAGED_EMBEDDED_DRIVER}" \
        "${DRIVER_ROOT}/Library/Audio/Plug-Ins/HAL/${DRIVER_BUNDLE_NAME}"
else
    ditto "${DRIVER_BUILD_SOURCE}" \
        "${DRIVER_ROOT}/Library/Audio/Plug-Ins/HAL/${DRIVER_BUNDLE_NAME}"
fi
STAGED_DRIVER="${DRIVER_ROOT}/Library/Audio/Plug-Ins/HAL/${DRIVER_BUNDLE_NAME}"

if [[ "${MODE}" == "beta-unsigned" ]]; then
    # Keep the payload locally ad-hoc signed; only the outer product archive is
    # unsigned. These are the same signing commands used by local builds.
    codesign --force --sign - "${STAGED_EMBEDDED_DRIVER}" >/dev/null
    codesign --force --deep --options runtime --sign - "${STAGED_APP}" >/dev/null
    codesign --force --sign - "${STAGED_DRIVER}" >/dev/null
fi

if [[ "${MODE}" == "beta-unsigned" ]]; then
    codesign --verify --deep --strict "${STAGED_APP}"
    codesign --verify --deep --strict "${STAGED_EMBEDDED_DRIVER}"
    codesign --verify --deep --strict "${STAGED_DRIVER}"
else
    codesign --verify --deep --strict "${STAGED_APP}"
    codesign --verify --deep --strict "${STAGED_DRIVER}"
fi

STAGED_EMBEDDED_HASH="$(shasum -a 256 "${STAGED_EMBEDDED_DRIVER}/Contents/MacOS/${DRIVER_EXECUTABLE}" | awk '{print $1}')"
STAGED_DRIVER_HASH="$(shasum -a 256 "${STAGED_DRIVER}/Contents/MacOS/${DRIVER_EXECUTABLE}" | awk '{print $1}')"
if [[ "${STAGED_EMBEDDED_HASH}" != "${STAGED_DRIVER_HASH}" ]]; then
    echo "The packaged standalone driver differs from Cuelet.app's embedded driver binary." >&2
    exit 1
fi

if find "${APP_ROOT}" "${DRIVER_ROOT}" -perm -0002 -print -quit | grep -q .; then
    echo "A package payload file is world-writable." >&2
    exit 1
fi

pkgbuild --analyze --root "${APP_ROOT}" "${WORK_ROOT}/app-components.plist"
set_component_value() {
    local plist="$1"
    local key="$2"
    local type="$3"
    local value="$4"
    /usr/libexec/PlistBuddy -c "Set :0:${key} ${value}" "${plist}" 2>/dev/null ||
        /usr/libexec/PlistBuddy -c "Add :0:${key} ${type} ${value}" "${plist}"
}
set_component_value "${WORK_ROOT}/app-components.plist" BundleIsRelocatable bool false
set_component_value "${WORK_ROOT}/app-components.plist" BundleIsVersionChecked bool true
set_component_value "${WORK_ROOT}/app-components.plist" BundleHasStrictIdentifier bool true
set_component_value "${WORK_ROOT}/app-components.plist" BundleOverwriteAction string upgrade
/usr/libexec/PlistBuddy -c 'Delete :0:ChildBundles' "${WORK_ROOT}/app-components.plist" 2>/dev/null || true

pkgbuild --analyze --root "${DRIVER_ROOT}" "${WORK_ROOT}/driver-components.plist"
set_component_value "${WORK_ROOT}/driver-components.plist" BundleIsRelocatable bool false
set_component_value "${WORK_ROOT}/driver-components.plist" BundleIsVersionChecked bool true
set_component_value "${WORK_ROOT}/driver-components.plist" BundleHasStrictIdentifier bool true
set_component_value "${WORK_ROOT}/driver-components.plist" BundleOverwriteAction string upgrade

cp "${INSTALLER_DIR}/Scripts/preinstall" "${APP_SCRIPTS}/preinstall"
cp "${INSTALLER_DIR}/Scripts/preinstall" "${DRIVER_SCRIPTS}/preinstall"
cp "${INSTALLER_DIR}/Scripts/driver-postinstall" "${DRIVER_SCRIPTS}/postinstall"
chmod 755 "${APP_SCRIPTS}/preinstall" "${DRIVER_SCRIPTS}/preinstall" "${DRIVER_SCRIPTS}/postinstall"

cat >"${APP_SCRIPTS}/package-metadata" <<EOF
CUELET_PAYLOAD_LABEL='Cuelet application'
CUELET_TARGET_RELATIVE_PATH='/Applications/Cuelet.app'
CUELET_EXPECTED_BUNDLE_ID='${APP_BUNDLE_ID}'
CUELET_EXPECTED_EXECUTABLE='${APP_NAME}'
CUELET_CANDIDATE_VERSION='${APP_VERSION}'
CUELET_CANDIDATE_BUILD='${APP_BUILD}'
CUELET_ALLOW_UNSIGNED='0'
EOF
cat >"${DRIVER_SCRIPTS}/package-metadata" <<EOF
CUELET_PAYLOAD_LABEL='Cuelet audio driver'
CUELET_TARGET_RELATIVE_PATH='/Library/Audio/Plug-Ins/HAL/${DRIVER_BUNDLE_NAME}'
CUELET_EXPECTED_BUNDLE_ID='${DRIVER_BUNDLE_ID}'
CUELET_EXPECTED_EXECUTABLE='${DRIVER_EXECUTABLE}'
CUELET_CANDIDATE_VERSION='${DRIVER_VERSION}'
CUELET_CANDIDATE_BUILD='${DRIVER_BUILD}'
CUELET_ALLOW_UNSIGNED='0'
EOF
chmod 644 "${APP_SCRIPTS}/package-metadata" "${DRIVER_SCRIPTS}/package-metadata"

pkgbuild \
    --root "${APP_ROOT}" \
    --component-plist "${WORK_ROOT}/app-components.plist" \
    --scripts "${APP_SCRIPTS}" \
    --identifier "${APP_PACKAGE_ID}" \
    --version "${PACKAGE_VERSION}" \
    --install-location / \
    --ownership recommended \
    "${PACKAGES_DIR}/CueletApplication.pkg"

pkgbuild \
    --root "${DRIVER_ROOT}" \
    --component-plist "${WORK_ROOT}/driver-components.plist" \
    --scripts "${DRIVER_SCRIPTS}" \
    --identifier "${DRIVER_PACKAGE_ID}" \
    --version "${DRIVER_PACKAGE_VERSION}" \
    --install-location / \
    --ownership recommended \
    "${PACKAGES_DIR}/CueletVirtualAudio.pkg"

LOCAL_RESOURCES="${WORK_ROOT}/resources-local"
PUBLIC_RESOURCES="${WORK_ROOT}/resources-release"
"${SCRIPT_DIR}/render-installer-resources.sh" \
    local "${INSTALLER_DIR}/Resources" "${LOCAL_RESOURCES}"
"${SCRIPT_DIR}/render-installer-resources.sh" \
    release "${INSTALLER_DIR}/Resources" "${PUBLIC_RESOURCES}"
BETAVERSION_RESOURCES="${WORK_ROOT}/resources-beta-unsigned"
"${SCRIPT_DIR}/render-installer-resources.sh" \
    beta-unsigned "${INSTALLER_DIR}/Resources" "${BETAVERSION_RESOURCES}"
ditto "${LOCAL_RESOURCES}" "${VALIDATION_ROOT}/inventories/installer-resources-local"
ditto "${PUBLIC_RESOURCES}" "${VALIDATION_ROOT}/inventories/installer-resources-release"
ditto "${BETAVERSION_RESOURCES}" "${VALIDATION_ROOT}/inventories/installer-resources-beta-unsigned"
if [[ "${MODE}" == "local" ]]; then
    ditto "${LOCAL_RESOURCES}" "${PRODUCT_RESOURCES}"
elif [[ "${MODE}" == "beta-unsigned" ]]; then
    ditto "${BETAVERSION_RESOURCES}" "${PRODUCT_RESOURCES}"
else
    ditto "${PUBLIC_RESOURCES}" "${PRODUCT_RESOURCES}"
fi
cp "${REPOSITORY_ROOT}/LICENSE" "${PRODUCT_RESOURCES}/License.txt"
sed \
    -e "s|@PACKAGE_VERSION@|${PACKAGE_VERSION}|g" \
    -e "s|@DRIVER_PACKAGE_VERSION@|${DRIVER_PACKAGE_VERSION}|g" \
    "${INSTALLER_DIR}/Distribution.xml.in" \
    >"${WORK_ROOT}/Distribution.xml"

/bin/rm -f -- "${OUTPUT_PATH}"
PRODUCTBUILD_ARGS=(
    --distribution "${WORK_ROOT}/Distribution.xml"
    --resources "${PRODUCT_RESOURCES}"
    --package-path "${PACKAGES_DIR}"
)
if [[ "${MODE}" == "release" ]]; then
    PRODUCTBUILD_ARGS+=(--sign "${CUELET_DEVELOPER_ID_INSTALLER}")
    if [[ -n "${CUELET_SIGNING_KEYCHAIN:-}" ]]; then
        PRODUCTBUILD_ARGS+=(--keychain "${CUELET_SIGNING_KEYCHAIN}")
    fi
fi
productbuild "${PRODUCTBUILD_ARGS[@]}" "${OUTPUT_PATH}"

"${SCRIPT_DIR}/test-release-package.sh" \
    "${OUTPUT_PATH}" \
    "${PACKAGE_VERSION}" \
    "${DRIVER_PACKAGE_VERSION}" \
    "${STAGED_APP}" \
    "${STAGED_DRIVER}" \
    "${APP_SCRIPTS}" \
    "${DRIVER_SCRIPTS}" \
    "${VALIDATION_ROOT}" \
    "package-v2-${MODE}"

PACKAGE_HASH="$(shasum -a 256 "${OUTPUT_PATH}" | awk '{print $1}')"
PACKAGE_SIZE="$(stat -f '%z' "${OUTPUT_PATH}")"
APP_EXECUTABLE_HASH="$(shasum -a 256 "${STAGED_APP}/Contents/MacOS/${APP_NAME}" | awk '{print $1}')"
{
    echo "mode=${MODE}"
    echo "product_identifier=${PRODUCT_PACKAGE_ID}"
    echo "app_package_identifier=${APP_PACKAGE_ID}"
    echo "driver_package_identifier=${DRIVER_PACKAGE_ID}"
    echo "app_version=${APP_VERSION}"
    echo "app_build=${APP_BUILD}"
    echo "driver_version=${DRIVER_VERSION}"
    echo "driver_build=${DRIVER_BUILD}"
    echo "app_architectures=${APP_ARCHS}"
    echo "driver_architectures=${DRIVER_ARCHS}"
    echo "app_executable_sha256=${APP_EXECUTABLE_HASH}"
    echo "release_driver_binary_sha256=${SOURCE_DRIVER_HASH}"
    echo "embedded_driver_binary_sha256=${EMBEDDED_DRIVER_HASH}"
    echo "driver_binary_sha256=${STAGED_DRIVER_HASH}"
    echo "package_path=${OUTPUT_PATH}"
    echo "package_size_bytes=${PACKAGE_SIZE}"
    echo "package_sha256=${PACKAGE_HASH}"
    if [[ "${MODE}" == "local" ]]; then
        echo "signature=unsigned local test package"
        echo "distributable=no"
    elif [[ "${MODE}" == "beta-unsigned" ]]; then
        echo "signature=unsigned outer package; ad-hoc app and driver payloads"
        echo "distributable=public beta; production signing and notarization pending"
    else
        echo "signature=Developer ID Installer"
        echo "distributable=pending notarization and stapling"
    fi
} | tee "${VALIDATION_ROOT}/logs/package-summary.txt"

pkgutil --check-signature "${OUTPUT_PATH}" \
    >"${VALIDATION_ROOT}/logs/package-signature.txt" 2>&1 || true
ditto "${OUTPUT_PATH}" "${VALIDATION_ROOT}/packages/$(basename "${OUTPUT_PATH}")"

echo "Built ${OUTPUT_PATH}"
if [[ "${MODE}" == "local" ]]; then
    echo "LOCAL TEST PACKAGE — NOT FOR PUBLIC DISTRIBUTION"
elif [[ "${MODE}" == "beta-unsigned" ]]; then
    echo "CUELET 0.x BETA — UNSIGNED PUBLIC BETA"
fi
echo "SHA-256: ${PACKAGE_HASH}"
echo "Evidence: ${VALIDATION_ROOT}"
