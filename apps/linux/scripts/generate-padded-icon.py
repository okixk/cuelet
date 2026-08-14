#!/usr/bin/env python3

from __future__ import annotations

import sys
from pathlib import Path


SOURCE_VIEWBOX = b'viewBox="0 0 300 300"'
PADDED_VIEWBOX = b'viewBox="-18 -18 336 336"'


def generate(source: Path, output: Path) -> None:
    if source.resolve() == output.resolve():
        raise ValueError("source and output paths must differ")

    source_data = source.read_bytes()
    if source_data.count(SOURCE_VIEWBOX) != 1 or source_data.count(b"viewBox=") != 1:
        raise ValueError("canonical SVG must contain exactly one 0 0 300 300 viewBox")

    output.write_bytes(source_data.replace(SOURCE_VIEWBOX, PADDED_VIEWBOX))


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: generate-padded-icon.py SOURCE_SVG OUTPUT_SVG", file=sys.stderr)
        return 2

    try:
        generate(Path(sys.argv[1]), Path(sys.argv[2]))
    except (OSError, ValueError) as error:
        print(f"generate-padded-icon.py: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
