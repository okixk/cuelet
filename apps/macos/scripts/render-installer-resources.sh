#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: render-installer-resources.sh (local | release) TEMPLATE_DIR OUTPUT_DIR" >&2
    exit 2
fi

MODE="$1"
TEMPLATE_DIR="$2"
OUTPUT_DIR="$3"

case "${MODE}" in
    local)
        LOCAL_TEST_NOTICE='<p class="test-notice"><strong>LOCAL TEST PACKAGE — NOT FOR PUBLIC DISTRIBUTION</strong></p>'
        ;;
    release)
        LOCAL_TEST_NOTICE=''
        ;;
    *)
        echo "Installer resource mode must be local or release." >&2
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
        -e "s|@LOCAL_TEST_NOTICE@|${LOCAL_TEST_NOTICE}|g" \
        "${template_path}" \
        >"${output_path}"
done

if grep -ERq '@LOCAL_TEST_NOTICE@' "${OUTPUT_DIR}"; then
    echo "An Installer resource placeholder was not rendered." >&2
    exit 1
fi

if [[ "${MODE}" == "local" ]]; then
    for page in Welcome.html ReadMe.html Conclusion.html; do
        grep -q 'LOCAL TEST PACKAGE — NOT FOR PUBLIC DISTRIBUTION' "${OUTPUT_DIR}/${page}"
    done
else
    if grep -Eiq 'LOCAL|TEST PACKAGE|NOT FOR PUBLIC DISTRIBUTION' "${OUTPUT_DIR}"/*.html; then
        echo "Public Installer resources contain local-test wording." >&2
        exit 1
    fi
fi
