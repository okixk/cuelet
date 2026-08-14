#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path


CANONICAL_SHA256 = "1f9c8a5ec9acda40808ba79d2fa0b42935c548b99f1ff5917fe9d2ea6ce63909"
SOURCE_VIEWBOX = b'viewBox="0 0 300 300"'
PADDED_VIEWBOX = b'viewBox="-18 -18 336 336"'
CANONICAL_CANVAS_CENTER = (150.0, 150.0)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_generator(generator: Path, source: Path, output: Path) -> None:
    subprocess.run(
        [sys.executable, str(generator), str(source), str(output)],
        check=True,
        text=True,
        capture_output=True,
    )


def main() -> int:
    if len(sys.argv) not in (3, 4):
        print(
            "usage: padded_icon_tests.py GENERATOR CANONICAL_SVG [BUILT_SVG]",
            file=sys.stderr,
        )
        return 2

    generator = Path(sys.argv[1]).resolve()
    canonical = Path(sys.argv[2]).resolve()
    original = canonical.read_bytes()
    original_hash = hashlib.sha256(original).hexdigest()
    require(original_hash == CANONICAL_SHA256, "canonical icon source changed")
    require(original.count(SOURCE_VIEWBOX) == 1, "canonical viewBox is not unique")

    with tempfile.TemporaryDirectory(prefix="cuelet-padded-icon-test-") as temp:
        generated = Path(temp) / "io.cuelet.Cuelet.svg"
        run_generator(generator, canonical, generated)
        result = generated.read_bytes()

    expected = original.replace(SOURCE_VIEWBOX, PADDED_VIEWBOX)
    require(result == expected, "generator changed data other than the root viewBox")
    require(canonical.read_bytes() == original, "generator modified the canonical source")
    if len(sys.argv) == 4:
        built = Path(sys.argv[3]).resolve().read_bytes()
        require(built == expected, "Meson-built icon differs from generator output")

    root = ET.fromstring(result)
    viewbox = [float(value) for value in root.attrib["viewBox"].split()]
    require(viewbox == [-18.0, -18.0, 336.0, 336.0],
            "generated viewBox is wrong")
    require(abs(viewbox[0] + viewbox[2] / 2 - CANONICAL_CANVAS_CENTER[0]) < 1e-12,
            "canonical canvas is not horizontally centered")
    require(abs(viewbox[1] + viewbox[3] / 2 - CANONICAL_CANVAS_CENTER[1]) < 1e-12,
            "canonical canvas is not vertically centered")
    require(abs(300.0 / viewbox[2] - 0.8928571428571429) < 1e-12,
            "generated optical scale is wrong")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
