#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: render-installer-resources.sh (local | beta-unsigned | release) TEMPLATE_DIR OUTPUT_DIR" >&2
    exit 2
fi

MODE="$1"
TEMPLATE_DIR="$2"
OUTPUT_DIR="$3"

case "${MODE}" in
    local)
        INSTALLER_NOTICE='<p class="installer-notice"><strong>LOCAL TEST PACKAGE — NOT FOR PUBLIC DISTRIBUTION</strong></p>'
        ;;
    beta-unsigned)
        INSTALLER_NOTICE='<p class="installer-notice"><strong>Cuelet 0.x Beta</strong><br>Beta builds are currently distributed without production code signing.<br>macOS may show security warnings during installation.<br>Signed and notarized builds are planned for a later release once Cuelet has received more real-world testing.</p>'
        ;;
    release)
        INSTALLER_NOTICE=''
        ;;
    *)
        echo "Installer resource mode must be local, beta-unsigned, or release." >&2
        exit 2
        ;;
esac

[[ -d "${TEMPLATE_DIR}" ]] || {
    echo "Installer resource templates are missing: ${TEMPLATE_DIR}" >&2
    exit 1
}
[[ ! -e "${OUTPUT_DIR}" ]] || {
    echo "Installer resource output already exists: ${OUTPUT_DIR}" >&2
    exit 1
}

mkdir -p "${OUTPUT_DIR}"

for template_path in "${TEMPLATE_DIR}"/*.html; do
    output_path="${OUTPUT_DIR}/$(basename "${template_path}")"
    sed \
        -e "s|@INSTALLER_NOTICE@|${INSTALLER_NOTICE}|g" \
        "${template_path}" \
        >"${output_path}"
done

if grep -ERq '@INSTALLER_NOTICE@' "${OUTPUT_DIR}"; then
    echo "An Installer resource placeholder was not rendered." >&2
    exit 1
fi

if [[ "${MODE}" == "local" ]]; then
    for page in Welcome.html ReadMe.html Conclusion.html; do
        grep -q 'LOCAL TEST PACKAGE — NOT FOR PUBLIC DISTRIBUTION' "${OUTPUT_DIR}/${page}"
    done
elif [[ "${MODE}" == "beta-unsigned" ]]; then
    for page in Welcome.html ReadMe.html Conclusion.html; do
        grep -q 'Cuelet 0.x Beta' "${OUTPUT_DIR}/${page}"
        grep -q 'without production code signing' "${OUTPUT_DIR}/${page}"
        if grep -Eiq 'LOCAL TEST PACKAGE|NOT FOR PUBLIC DISTRIBUTION' "${OUTPUT_DIR}/${page}"; then
            echo "Beta Installer resources contain local-test wording." >&2
            exit 1
        fi
    done
else
    if grep -Eiq 'LOCAL|TEST PACKAGE|NOT FOR PUBLIC DISTRIBUTION' "${OUTPUT_DIR}"/*.html; then
        echo "Public Installer resources contain local-test wording." >&2
        exit 1
    fi
fi
