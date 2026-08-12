#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import math
import struct
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYZER_PATH = ROOT / "tools" / "analyze-cuelet-capture.py"
SPEC = importlib.util.spec_from_file_location("cuelet_capture_analyzer", ANALYZER_PATH)
assert SPEC is not None and SPEC.loader is not None
ANALYZER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYZER)


def write_float_wav(path: Path, samples: list[tuple[float, float]], rate: int) -> None:
    payload = b"".join(struct.pack("<ff", left, right) for left, right in samples)
    format_payload = struct.pack("<HHIIHH", 3, 2, rate, rate * 8, 8, 32)
    riff_size = 4 + 8 + len(format_payload) + 8 + len(payload)
    path.write_bytes(
        b"RIFF"
        + struct.pack("<I", riff_size)
        + b"WAVEfmt "
        + struct.pack("<I", len(format_payload))
        + format_payload
        + b"data"
        + struct.pack("<I", len(payload))
        + payload
    )


class CaptureAnalyzerTests(unittest.TestCase):
    def test_frequency_holes_phase_and_marker_ordering(self) -> None:
        rate = 48000
        total = 96000
        leading = 4096
        trailing = 4096
        hole_start = 46000
        hole_end = hole_start + 512
        samples: list[tuple[float, float]] = []
        for frame in range(total):
            if (
                frame < leading
                or frame >= total - trailing
                or hole_start <= frame < hole_end
            ):
                samples.append((0.0, 0.0))
            else:
                samples.append(
                    (
                        0.25 * math.sin(2.0 * math.pi * 997.0 * frame / rate),
                        0.25 * math.sin(2.0 * math.pi * 1499.0 * frame / rate),
                    )
                )

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.wav"
            write_float_wav(path, samples, rate)
            result = ANALYZER.analyze(path)

        self.assertTrue(result["frequencyValidationPassed"])
        self.assertAlmostEqual(result["dominantFrequencyLeft"], 997.0, delta=1.0)
        self.assertAlmostEqual(result["dominantFrequencyRight"], 1499.0, delta=1.0)
        self.assertTrue(result["peakValidationPassed"])
        self.assertEqual(result["driverCreatedHoleCandidateCount"], 1)
        self.assertEqual(result["driverCreatedHoleCandidateFrames"], 512)
        self.assertEqual(result["activeZeroRunCount"], 1)
        self.assertEqual(result["activeZeroRunFrameCount"], 512)
        self.assertEqual(result["phaseContinuity"]["analyzedHoleBoundaries"], 1)
        self.assertTrue(result["phaseContinuity"]["continuousAcrossAnalyzedHoles"])
        self.assertEqual(result["phaseContinuity"]["discontinuityCount"], 0)
        self.assertTrue(result["markerOrdering"]["ordered"])
        self.assertEqual(result["leadingZeroFrames"], leading)
        self.assertEqual(result["trailingZeroFrames"], trailing)


if __name__ == "__main__":
    unittest.main()
