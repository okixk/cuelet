#!/usr/bin/env bash

set -euo pipefail

source_root="${1:-}"
if [[ -z "$source_root" || ! -d "$source_root" ]]; then
    echo "Usage: resolve-linux-release-epoch.sh SOURCE_ROOT" >&2
    exit 2
fi

if [[ -n "${SOURCE_DATE_EPOCH:-}" ]]; then
    if [[ ! "$SOURCE_DATE_EPOCH" =~ ^[0-9]+$ ]]; then
        echo "SOURCE_DATE_EPOCH must be a non-negative integer." >&2
        exit 1
    fi
    printf '%s\n' "$SOURCE_DATE_EPOCH"
    exit 0
fi

if ! git -C "$source_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "SOURCE_DATE_EPOCH is required outside a Git checkout." >&2
    exit 1
fi

# Keep this list limited to tracked inputs that can change the installed Linux
# payload or the deterministic archive construction. Tests and developer docs
# validate the release but do not become part of its bytes.
release_input_paths=(
    VERSION
    LICENSE
    apps/linux/meson.build
    apps/linux/meson_options.txt
    apps/linux/data
    apps/linux/resources
    apps/linux/src
    apps/linux/scripts/generate-padded-icon.py
    apps/linux/scripts/package-linux-release.sh
    apps/linux/scripts/resolve-linux-release-epoch.sh
    core/cuelet-core/include
    core/cuelet-core/src
)

resolved_epoch="$(
    git -C "$source_root" log -1 --format=%ct -- "${release_input_paths[@]}"
)"
if [[ ! "$resolved_epoch" =~ ^[0-9]+$ ]]; then
    echo "Could not derive SOURCE_DATE_EPOCH from Linux release-package inputs." >&2
    exit 1
fi

printf '%s\n' "$resolved_epoch"
