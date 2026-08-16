#!/usr/bin/env bash

set -euo pipefail
umask 022

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
linux_source="$(cd -- "$script_dir/.." && pwd)"
source_root="$(cd -- "$linux_source/../.." && pwd)"
version="$(tr -d '[:space:]' < "$source_root/VERSION")"

if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Invalid release version in $source_root/VERSION: $version" >&2
    exit 1
fi

architecture="$(uname -m)"
if [[ ! "$architecture" =~ ^[A-Za-z0-9._-]+$ ]]; then
    echo "Invalid release architecture: $architecture" >&2
    exit 1
fi
package_name="Cuelet-${version}-linux-${architecture}"
output_argument="${1:-$linux_source/dist}"
mkdir -p -- "$output_argument"
output_dir="$(cd -- "$output_argument" && pwd)"
artifact="$output_dir/$package_name.tar.gz"

work_dir="$(mktemp -d -t cuelet-linux-release.XXXXXX)"
cleanup() {
    rm -rf -- "$work_dir"
}
trap cleanup EXIT

build_dir="$work_dir/build"
package_root="$work_dir/$package_name"
mkdir -p -- "$package_root"

if [[ -z "${SOURCE_DATE_EPOCH:-}" ]]; then
    if git -C "$source_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        SOURCE_DATE_EPOCH="$(git -C "$source_root" show -s --format=%ct HEAD)"
    else
        echo "SOURCE_DATE_EPOCH is required outside a Git checkout." >&2
        exit 1
    fi
fi
if [[ ! "$SOURCE_DATE_EPOCH" =~ ^[0-9]+$ ]]; then
    echo "SOURCE_DATE_EPOCH must be a non-negative integer." >&2
    exit 1
fi
export SOURCE_DATE_EPOCH

meson setup "$build_dir" "$linux_source" \
    --buildtype=release \
    -Dwerror=true \
    -Dstrip=true \
    --prefix=/usr
meson compile -C "$build_dir"
meson test -C "$build_dir" --print-errorlogs
DESTDIR="$package_root" meson install -C "$build_dir"

expected_entries="/usr	d
/usr/bin	d
/usr/bin/cuelet	f
/usr/share	d
/usr/share/applications	d
/usr/share/applications/io.cuelet.Cuelet.desktop	f
/usr/share/doc	d
/usr/share/doc/cuelet	d
/usr/share/doc/cuelet/README.md	f
/usr/share/doc/cuelet/installed-files.txt	f
/usr/share/icons	d
/usr/share/icons/hicolor	d
/usr/share/icons/hicolor/scalable	d
/usr/share/icons/hicolor/scalable/apps	d
/usr/share/icons/hicolor/scalable/apps/io.cuelet.Cuelet.svg	f
/usr/share/licenses	d
/usr/share/licenses/cuelet	d
/usr/share/licenses/cuelet/LICENSE	f
/usr/share/metainfo	d
/usr/share/metainfo/io.cuelet.Cuelet.metainfo.xml	f"
actual_entries="$(find "$package_root" -mindepth 1 -printf '/%P\t%y\n' | LC_ALL=C sort)"
if [[ "$actual_entries" != "$expected_entries" ]]; then
    echo "Unexpected staged installation tree:" >&2
    diff -u <(printf '%s\n' "$expected_entries") <(printf '%s\n' "$actual_entries") >&2 || true
    exit 1
fi

if [[ "$("$package_root/usr/bin/cuelet" --version)" != "Cuelet $version" ]]; then
    echo "Installed executable version does not match VERSION." >&2
    exit 1
fi

desktop-file-validate \
    "$package_root/usr/share/applications/io.cuelet.Cuelet.desktop"
grep -Fxq 'Name=Cuelet' \
    "$package_root/usr/share/applications/io.cuelet.Cuelet.desktop"
grep -Fxq 'Exec=cuelet' \
    "$package_root/usr/share/applications/io.cuelet.Cuelet.desktop"
grep -Fxq 'Icon=io.cuelet.Cuelet' \
    "$package_root/usr/share/applications/io.cuelet.Cuelet.desktop"
xmllint --noout \
    "$package_root/usr/share/icons/hicolor/scalable/apps/io.cuelet.Cuelet.svg" \
    "$package_root/usr/share/metainfo/io.cuelet.Cuelet.metainfo.xml"
python3 \
    "$linux_source/tests/padded_icon_tests.py" \
    "$linux_source/scripts/generate-padded-icon.py" \
    "$linux_source/data/io.cuelet.Cuelet.svg" \
    "$package_root/usr/share/icons/hicolor/scalable/apps/io.cuelet.Cuelet.svg"
appstreamcli validate --no-net \
    "$package_root/usr/share/metainfo/io.cuelet.Cuelet.metainfo.xml"
metainfo_version="$(xmllint --xpath \
    'string(/component/releases/release[1]/@version)' \
    "$package_root/usr/share/metainfo/io.cuelet.Cuelet.metainfo.xml")"
if [[ "$metainfo_version" != "$version" ]]; then
    echo "AppStream release version does not match VERSION." >&2
    exit 1
fi

if readelf -S "$package_root/usr/bin/cuelet" \
    | grep -Eq '\.debug_|\.symtab'; then
    echo "Release executable contains debug or unstripped symbol sections." >&2
    exit 1
fi
if ldd "$package_root/usr/bin/cuelet" | grep -q 'not found'; then
    echo "Release executable has unresolved shared-library dependencies." >&2
    exit 1
fi
if grep -R -a -F -q -- "$source_root" "$package_root"; then
    echo "Staged installation contains the developer source path." >&2
    exit 1
fi

temporary_artifact="$work_dir/$package_name.tar.gz"
tar --create \
    --format=pax \
    --sort=name \
    --mtime="@$SOURCE_DATE_EPOCH" \
    --owner=0 \
    --group=0 \
    --numeric-owner \
    --pax-option=delete=atime,delete=ctime \
    --directory="$work_dir" \
    "$package_name" \
    | gzip -n -9 > "$temporary_artifact"

archived_icon="$work_dir/archived-io.cuelet.Cuelet.svg"
tar -xOf "$temporary_artifact" \
    "$package_name/usr/share/icons/hicolor/scalable/apps/io.cuelet.Cuelet.svg" \
    > "$archived_icon"
python3 \
    "$linux_source/tests/padded_icon_tests.py" \
    "$linux_source/scripts/generate-padded-icon.py" \
    "$linux_source/data/io.cuelet.Cuelet.svg" \
    "$archived_icon"
mv -f -- "$temporary_artifact" "$artifact"

sha256sum "$artifact"
printf 'Release artifact: %s\n' "$artifact"
