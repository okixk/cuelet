#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
linux_source="$(cd -- "$script_dir/.." && pwd)"
source_root="$(cd -- "$linux_source/../.." && pwd)"
application_id="io.cuelet.Cuelet"
desktop_file="$linux_source/data/$application_id.desktop"
icon_file="$linux_source/data/$application_id.svg"
metainfo_file="$linux_source/data/$application_id.metainfo.xml"

fail() {
    printf 'release metadata check failed: %s\n' "$1" >&2
    exit 1
}

obsolete_prototype_paths=(
    CMakeLists.txt
    src/app
    src/audio
    src/core
    src/main.cpp
    src/platform
    src/storage
    src/ui
    tests/CMakeLists.txt
    tests/test_legacy_settings_importer.cpp
    tests/test_library_scanner.cpp
    tests/test_metadata_store.cpp
    tests/test_path_handling.cpp
    tests/test_smoke.cpp
    tests/test_sound_filter.cpp
    resources/app_icon.icns
    resources/app_icon.svg
    resources/icons
)
for obsolete_path in "${obsolete_prototype_paths[@]}"; do
    if [[ -e "$source_root/$obsolete_path" ]]; then
        fail "obsolete root Qt prototype path remains: $obsolete_path"
    fi
done

[[ "$(basename -- "$desktop_file")" == "$application_id.desktop" ]] \
    || fail "desktop filename does not match the application ID"
grep -Fxq 'Name=Cuelet' "$desktop_file" \
    || fail "desktop Name is not Cuelet"
grep -Fxq 'Exec=cuelet' "$desktop_file" \
    || fail "desktop Exec is not the installed cuelet command"
grep -Fxq "Icon=$application_id" "$desktop_file" \
    || fail "desktop Icon does not match the application ID"
if grep -Fq 'StartupWMClass=' "$desktop_file"; then
    fail "desktop file contains an unnecessary X11 StartupWMClass workaround"
fi

grep -Fq "constexpr const char* applicationId = \"$application_id\";" \
    "$linux_source/src/main.cpp" \
    || fail "GtkApplication ID does not match the desktop ID"
if grep -r -I -E -q \
    'g_set_prgname|gdk_set_program_class|gtk_window_set_(default_)?icon_name' \
    "$linux_source/src"; then
    fail "Linux source overrides GTK4 application identity or icon naming"
fi

grep -Fq "<id>$application_id</id>" "$metainfo_file" \
    || fail "AppStream ID does not match the application ID"
grep -Fq "<launchable type=\"desktop-id\">$application_id.desktop</launchable>" \
    "$metainfo_file" \
    || fail "AppStream launchable does not match the desktop ID"

expected_icon_hash='1f9c8a5ec9acda40808ba79d2fa0b42935c548b99f1ff5917fe9d2ea6ce63909'
actual_icon_hash="$(sha256sum "$icon_file" | cut -d' ' -f1)"
[[ "$actual_icon_hash" == "$expected_icon_hash" ]] \
    || fail "Linux icon differs from the approved release artwork"

grep -Fq "'data/$application_id.desktop'" "$linux_source/meson.build" \
    || fail "Meson does not install the canonical desktop file"
grep -Fq "'data/$application_id.svg'" "$linux_source/meson.build" \
    || fail "Meson does not install the canonical icon"
grep -Fq "'icons' / 'hicolor' / 'scalable' / 'apps'" "$linux_source/meson.build" \
    || fail "Meson icon install directory is not the hicolor apps directory"
