#!/usr/bin/env bash
set -euo pipefail

CONFIGURATION="${1:-${CUELET_DRIVER_CONFIGURATION:-Debug}}"
case "${CONFIGURATION}" in
    Debug|Release) ;;
    *)
        echo "Configuration must be Debug or Release." >&2
        exit 64
        ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MACOS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DRIVER_DIR="${MACOS_DIR}/Driver"

if [[ -n "${CUELET_DRIVER_DIAGNOSTICS+x}" ]]; then
    DIAGNOSTICS="${CUELET_DRIVER_DIAGNOSTICS}"
elif [[ "${CONFIGURATION}" == "Debug" ]]; then
    DIAGNOSTICS=1
else
    DIAGNOSTICS=0
fi
case "${DIAGNOSTICS}" in
    0|1) ;;
    *)
        echo "CUELET_DRIVER_DIAGNOSTICS must be 0 or 1." >&2
        exit 64
        ;;
esac

TARGETS=(clean bundle)
if [[ "${DIAGNOSTICS}" == "1" ]]; then
    TARGETS+=(tools)
fi

make -C "${DRIVER_DIR}" \
    CONFIGURATION="${CONFIGURATION}" \
    DIAGNOSTICS="${DIAGNOSTICS}" \
    "${TARGETS[@]}"

BUNDLE="${DRIVER_DIR}/build/${CONFIGURATION}/CueletVirtualAudio.driver"
echo "Cuelet virtual audio driver: ${BUNDLE} (diagnostics=${DIAGNOSTICS})"
