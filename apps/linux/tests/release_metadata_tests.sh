#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
linux_source="$(cd -- "$script_dir/.." && pwd)"
source_root="$(cd -- "$linux_source/../.." && pwd)"
application_id="io.cuelet.Cuelet"
desktop_file="$linux_source/data/$application_id.desktop"
canonical_icon_file="$linux_source/data/$application_id.svg"
metainfo_file="$linux_source/data/$application_id.metainfo.xml"
about_source="$linux_source/src/CueletAboutDialog.cpp"
about_window_source="$linux_source/src/CueletWindowAbout.cpp"
window_source="$linux_source/src/CueletWindow.cpp"
main_source="$linux_source/src/main.cpp"
style_file="$linux_source/resources/style.css"
epoch_policy="$linux_source/scripts/resolve-linux-release-epoch.sh"
package_script="$linux_source/scripts/package-linux-release.sh"

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

grep -Fq 'g_menu_append(appSection, "About Cuelet", "app.about");' \
    "$window_source" \
    || fail "primary menu does not expose the app.about action"
grep -Fq 'g_simple_action_new("about", nullptr);' "$main_source" \
    || fail "application does not register the about action"
grep -Fq 'adw_about_dialog_set_version(about, CUELET_VERSION);' "$about_source" \
    || fail "About dialog version does not use the generated version"
grep -Fq 'adw_about_dialog_set_license_type(about, GTK_LICENSE_AGPL_3_0_ONLY);' \
    "$about_source" \
    || fail "About dialog license does not match AGPL-3.0-only"
grep -Fq 'if (!aboutDialog_) {' "$about_window_source" \
    || fail "About dialog presentation is not singleton-scoped"
grep -Fq 'label.cuelet-about-heading {' "$style_file" \
    || fail "About heading CSS is not scoped to its dedicated label class"

grep -Fq "<id>$application_id</id>" "$metainfo_file" \
    || fail "AppStream ID does not match the application ID"
grep -Fq "<launchable type=\"desktop-id\">$application_id.desktop</launchable>" \
    "$metainfo_file" \
    || fail "AppStream launchable does not match the desktop ID"

expected_icon_hash='1f9c8a5ec9acda40808ba79d2fa0b42935c548b99f1ff5917fe9d2ea6ce63909'
actual_icon_hash="$(sha256sum "$canonical_icon_file" | cut -d' ' -f1)"
[[ "$actual_icon_hash" == "$expected_icon_hash" ]] \
    || fail "canonical icon differs from the approved release artwork"

grep -Fq "'data/$application_id.desktop'" "$linux_source/meson.build" \
    || fail "Meson does not install the canonical desktop file"
grep -Fq "'data/$application_id.svg'" "$linux_source/meson.build" \
    || fail "Meson does not generate the Linux icon from the canonical source"
grep -Fq "files('scripts/generate-padded-icon.py')" "$linux_source/meson.build" \
    || fail "Meson does not use the padded icon generator"
grep -Fq "'icons' / 'hicolor' / 'scalable' / 'apps'" "$linux_source/meson.build" \
    || fail "Meson icon install directory is not the hicolor apps directory"

[[ -x "$epoch_policy" ]] \
    || fail "Linux release epoch resolver is missing or not executable"
grep -Fq '"$script_dir/resolve-linux-release-epoch.sh" "$source_root"' \
    "$package_script" \
    || fail "Linux package script does not use the stable release epoch resolver"

epoch_test_root="$(mktemp -d -t cuelet-release-epoch-test.XXXXXX)"
cleanup_epoch_test() {
    if [[ -d "$epoch_test_root" ]]; then
        find "$epoch_test_root" -depth -delete
    fi
}
trap cleanup_epoch_test EXIT

epoch_test_repo="$epoch_test_root/repository"
mkdir -p \
    "$epoch_test_repo/apps/linux/data" \
    "$epoch_test_repo/apps/linux/resources" \
    "$epoch_test_repo/apps/linux/scripts" \
    "$epoch_test_repo/apps/linux/src" \
    "$epoch_test_repo/core/cuelet-core/include" \
    "$epoch_test_repo/core/cuelet-core/src" \
    "$epoch_test_repo/docs" \
    "$epoch_test_repo/payload"
cp "$epoch_policy" "$epoch_test_repo/apps/linux/scripts/resolve-linux-release-epoch.sh"
cp "$linux_source/scripts/package-linux-release.sh" \
    "$epoch_test_repo/apps/linux/scripts/package-linux-release.sh"
printf '0.1.0\n' >"$epoch_test_repo/VERSION"
printf 'fixture license\n' >"$epoch_test_repo/LICENSE"
printf 'project fixture\n' >"$epoch_test_repo/apps/linux/meson.build"
printf 'option fixture\n' >"$epoch_test_repo/apps/linux/meson_options.txt"
printf 'fixture\n' >"$epoch_test_repo/apps/linux/data/INSTALL.md"
printf 'fixture\n' >"$epoch_test_repo/apps/linux/resources/style.css"
printf 'fixture\n' >"$epoch_test_repo/apps/linux/scripts/generate-padded-icon.py"
printf 'fixture\n' >"$epoch_test_repo/apps/linux/src/main.cpp"
printf 'developer-only fixture\n' \
    >"$epoch_test_repo/apps/linux/src/CueletWindowVisualCapture.cpp"
printf 'fixture\n' >"$epoch_test_repo/core/cuelet-core/include/fixture.h"
printf 'fixture\n' >"$epoch_test_repo/core/cuelet-core/src/fixture.cpp"
printf 'stable payload\n' >"$epoch_test_repo/payload/value.txt"

git -C "$epoch_test_repo" init -q
git -C "$epoch_test_repo" add \
    VERSION LICENSE apps core
GIT_AUTHOR_DATE='@1700000000 +0000' \
GIT_COMMITTER_DATE='@1700000000 +0000' \
    git -C "$epoch_test_repo" \
        -c user.name='Cuelet Tests' \
        -c user.email='tests@invalid.example' \
        commit -q -m 'fixture: package inputs'

resolve_default_epoch() {
    env -u SOURCE_DATE_EPOCH \
        "$epoch_test_repo/apps/linux/scripts/resolve-linux-release-epoch.sh" \
        "$epoch_test_repo"
}

write_fixture_archive() {
    local epoch="$1"
    local output="$2"
    tar --create \
        --format=pax \
        --sort=name \
        --mtime="@$epoch" \
        --owner=0 \
        --group=0 \
        --numeric-owner \
        --pax-option=delete=atime,delete=ctime \
        --directory="$epoch_test_repo" \
        payload \
        | gzip -n -9 >"$output"
}

initial_epoch="$(resolve_default_epoch)"
[[ "$initial_epoch" == '1700000000' ]] \
    || fail "default release epoch does not follow the newest package-input commit"
write_fixture_archive "$initial_epoch" "$epoch_test_root/before.tar.gz"

printf 'unrelated documentation\n' >"$epoch_test_repo/docs/unrelated.md"
git -C "$epoch_test_repo" add docs/unrelated.md
GIT_AUTHOR_DATE='@1700001000 +0000' \
GIT_COMMITTER_DATE='@1700001000 +0000' \
    git -C "$epoch_test_repo" \
        -c user.name='Cuelet Tests' \
        -c user.email='tests@invalid.example' \
        commit -q -m 'docs: unrelated change'

unrelated_epoch="$(resolve_default_epoch)"
[[ "$unrelated_epoch" == "$initial_epoch" ]] \
    || fail "unrelated documentation changed the default release epoch"
write_fixture_archive "$unrelated_epoch" "$epoch_test_root/after.tar.gz"
cmp -s "$epoch_test_root/before.tar.gz" "$epoch_test_root/after.tar.gz" \
    || fail "equivalent package inputs did not produce identical fixture archives"

printf 'changed developer-only visual capture\n' \
    >"$epoch_test_repo/apps/linux/src/CueletWindowVisualCapture.cpp"
git -C "$epoch_test_repo" add \
    apps/linux/src/CueletWindowVisualCapture.cpp
GIT_AUTHOR_DATE='@1700002000 +0000' \
GIT_COMMITTER_DATE='@1700002000 +0000' \
    git -C "$epoch_test_repo" \
        -c user.name='Cuelet Tests' \
        -c user.email='tests@invalid.example' \
        commit -q -m 'test: change unpackaged developer visual capture'

developer_only_epoch="$(resolve_default_epoch)"
[[ "$developer_only_epoch" == "$initial_epoch" ]] \
    || fail "unpackaged developer-only source changed the default release epoch"

printf 'changed installed documentation\n' \
    >"$epoch_test_repo/apps/linux/data/INSTALL.md"
git -C "$epoch_test_repo" add apps/linux/data/INSTALL.md
GIT_AUTHOR_DATE='@1700003000 +0000' \
GIT_COMMITTER_DATE='@1700003000 +0000' \
    git -C "$epoch_test_repo" \
        -c user.name='Cuelet Tests' \
        -c user.email='tests@invalid.example' \
        commit -q -m 'docs: change installed package documentation'

input_epoch="$(resolve_default_epoch)"
[[ "$input_epoch" == '1700003000' ]] \
    || fail "a package-input change did not update the default release epoch"

override_epoch="$(
    SOURCE_DATE_EPOCH=1700004000 \
        "$epoch_test_repo/apps/linux/scripts/resolve-linux-release-epoch.sh" \
        "$epoch_test_repo"
)"
[[ "$override_epoch" == '1700004000' ]] \
    || fail "externally supplied SOURCE_DATE_EPOCH was not respected"

non_git_source="$epoch_test_root/non-git-source"
mkdir -p "$non_git_source"
outside_git_epoch="$(
    SOURCE_DATE_EPOCH=1700005000 \
        "$epoch_test_repo/apps/linux/scripts/resolve-linux-release-epoch.sh" \
        "$non_git_source"
)"
[[ "$outside_git_epoch" == '1700005000' ]] \
    || fail "explicit SOURCE_DATE_EPOCH did not support a non-Git source tree"
if env -u SOURCE_DATE_EPOCH \
    "$epoch_test_repo/apps/linux/scripts/resolve-linux-release-epoch.sh" \
    "$non_git_source" >/dev/null 2>&1; then
    fail "a non-Git source tree did not require explicit SOURCE_DATE_EPOCH"
fi
if SOURCE_DATE_EPOCH=invalid \
    "$epoch_test_repo/apps/linux/scripts/resolve-linux-release-epoch.sh" \
    "$epoch_test_repo" >/dev/null 2>&1; then
    fail "an invalid SOURCE_DATE_EPOCH was accepted"
fi
