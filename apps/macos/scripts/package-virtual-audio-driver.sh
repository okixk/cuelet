#!/usr/bin/env bash
set -euo pipefail

CONFIGURATION="${1:-${CUELET_DRIVER_CONFIGURATION:-Release}}"
case "${CONFIGURATION}" in
    Debug|Release) ;;
    *)
        echo "Configuration must be Debug or Release." >&2
        exit 64
        ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MACOS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE="${MACOS_DIR}/Driver/build/${CONFIGURATION}/CueletVirtualAudio.driver"
DESTINATION_DIR="${MACOS_DIR}/Driver/dist/${CONFIGURATION}"
DESTINATION="${DESTINATION_DIR}/CueletVirtualAudio.driver"

if [[ ! -d "${SOURCE}" ]]; then
    "${SCRIPT_DIR}/build-virtual-audio-driver.sh" "${CONFIGURATION}"
fi

rm -rf "${DESTINATION}"
mkdir -p "${DESTINATION_DIR}"
ditto "${SOURCE}" "${DESTINATION}"
codesign --force --sign - "${DESTINATION}" >/dev/null

echo "Packaged Cuelet virtual audio driver: ${DESTINATION}"
