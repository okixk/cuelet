#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DRIVER_DIR="$(cd "${SCRIPT_DIR}/../Driver" && pwd)"

make -C "${DRIVER_DIR}" test
CUELET_DRIVER_STRESS_ITERATIONS="${CUELET_DRIVER_STRESS_ITERATIONS:-100000}" \
    make -C "${DRIVER_DIR}" stress-test

if [[ "${CUELET_DRIVER_RUN_SANITIZERS:-1}" == "1" ]]; then
    make -C "${DRIVER_DIR}" asan-test
    make -C "${DRIVER_DIR}" tsan-test
fi
