#!/usr/bin/env python3
"""Analyze Cuelet stereo Float32 captures without third-party modules."""

from __future__ import annotations

import argparse
import cmath
import hashlib
import json
import math
import struct
from pathlib import Path


CUELET_LEFT_FREQUENCY = 997.0
CUELET_RIGHT_FREQUENCY = 1499.0
CUELET_PEAK = 0.25


def read_chunks(path: Path) -> tuple[dict[str, int], int, int]:
    with path.open("rb") as source:
        header = source.read(12)
        if len(header) != 12 or header[:4] != b"RIFF" or header[8:] != b"WAVE":
            raise ValueError("not a RIFF/WAVE file")
        format_info: dict[str, int] | None = None
        data_offset = 0
        data_size = 0
        while True:
            chunk_header = source.read(8)
            if not chunk_header:
                break
            if len(chunk_header) != 8:
                raise ValueError("truncated WAV chunk header")
            chunk_id, chunk_size = struct.unpack("<4sI", chunk_header)
            chunk_offset = source.tell()
            if chunk_id == b"fmt ":
                payload = source.read(chunk_size)
                if len(payload) < 16:
                    raise ValueError("truncated WAV format chunk")
                audio_format, channels, sample_rate, _, block_align, bits = (
                    struct.unpack("<HHIIHH", payload[:16])
                )
                format_info = {
                    "audioFormat": audio_format,
                    "channels": channels,
                    "sampleRate": sample_rate,
                    "blockAlign": block_align,
                    "bitsPerSample": bits,
                }
            elif chunk_id == b"data":
                data_offset = chunk_offset
                data_size = chunk_size
                source.seek(chunk_size, 1)
            else:
                source.seek(chunk_size, 1)
            if chunk_size & 1:
                source.seek(1, 1)
        if format_info is None or data_offset == 0:
            raise ValueError("WAV is missing format or data chunk")
        return format_info, data_offset, data_size


def _load_samples(
    path: Path, offset: int, frames: int
) -> tuple[list[float], list[float], str]:
    left: list[float] = []
    right: list[float] = []
    digest = hashlib.sha256()
    with path.open("rb") as source:
        source.seek(offset)
        remaining = frames
        while remaining:
            count = min(remaining, 16384)
            payload = source.read(count * 8)
            if len(payload) != count * 8:
                raise ValueError("truncated WAV audio payload")
            digest.update(payload)
            for left_sample, right_sample in struct.iter_unpack("<ff", payload):
                left.append(left_sample)
                right.append(right_sample)
            remaining -= count
    return left, right, digest.hexdigest()


def _zero_runs(left: list[float], right: list[float]) -> list[tuple[int, int]]:
    runs: list[tuple[int, int]] = []
    start: int | None = None
    for index, (left_sample, right_sample) in enumerate(zip(left, right)):
        is_zero = left_sample == 0.0 and right_sample == 0.0
        if is_zero and start is None:
            start = index
        elif not is_zero and start is not None:
            runs.append((start, index))
            start = None
    if start is not None:
        runs.append((start, len(left)))
    return runs


def _fft(values: list[complex]) -> None:
    count = len(values)
    target = 0
    for index in range(1, count):
        bit = count >> 1
        while target & bit:
            target ^= bit
            bit >>= 1
        target ^= bit
        if index < target:
            values[index], values[target] = values[target], values[index]
    length = 2
    while length <= count:
        step = cmath.exp(-2j * math.pi / length)
        half = length // 2
        for base in range(0, count, length):
            rotation = 1.0 + 0.0j
            for offset in range(half):
                even = values[base + offset]
                odd = values[base + offset + half] * rotation
                values[base + offset] = even + odd
                values[base + offset + half] = even - odd
                rotation *= step
        length *= 2


def _dominant_frequency(
    samples: list[float], sample_rate: int, active_start: int, active_end: int
) -> float | None:
    available = active_end - active_start
    if available < 256 or sample_rate <= 0:
        return None
    transform_size = 1 << min(18, available.bit_length() - 1)
    transform_size = max(256, transform_size)
    start = active_start + max(0, (available - transform_size) // 2)
    denominator = transform_size - 1
    values = [
        complex(
            samples[start + index]
            * (0.5 - 0.5 * math.cos(2.0 * math.pi * index / denominator)),
            0.0,
        )
        for index in range(transform_size)
    ]
    _fft(values)
    first_bin = max(1, int(20.0 * transform_size / sample_rate))
    final_bin = min(
        transform_size // 2 - 1,
        int(5000.0 * transform_size / sample_rate),
    )
    peak_bin = max(
        range(first_bin, final_bin + 1), key=lambda index: abs(values[index])
    )
    magnitudes = [
        max(abs(values[peak_bin + offset]), 1e-30) for offset in (-1, 0, 1)
    ]
    alpha, beta, gamma = (math.log(value) for value in magnitudes)
    divisor = alpha - 2.0 * beta + gamma
    correction = 0.0 if divisor == 0.0 else 0.5 * (alpha - gamma) / divisor
    correction = max(-0.5, min(0.5, correction))
    return (peak_bin + correction) * sample_rate / transform_size


def _phase_fit(
    samples: list[float], start: int, end: int, frequency: float, sample_rate: int
) -> tuple[float, float] | None:
    if end - start < 16:
        return None
    omega = 2.0 * math.pi * frequency / sample_rate
    sine_square = 0.0
    cosine_square = 0.0
    cross = 0.0
    sample_sine = 0.0
    sample_cosine = 0.0
    for frame in range(start, end):
        sine = math.sin(omega * frame)
        cosine = math.cos(omega * frame)
        value = samples[frame]
        sine_square += sine * sine
        cosine_square += cosine * cosine
        cross += sine * cosine
        sample_sine += value * sine
        sample_cosine += value * cosine
    determinant = sine_square * cosine_square - cross * cross
    if abs(determinant) < 1e-12:
        return None
    sine_coefficient = (
        sample_sine * cosine_square - sample_cosine * cross
    ) / determinant
    cosine_coefficient = (
        sample_cosine * sine_square - sample_sine * cross
    ) / determinant
    return (
        math.atan2(cosine_coefficient, sine_coefficient),
        math.hypot(sine_coefficient, cosine_coefficient),
    )


def _wrapped_phase_error(before: float, after: float) -> float:
    return math.atan2(math.sin(after - before), math.cos(after - before))


def _phase_discontinuities(
    left: list[float],
    right: list[float],
    runs: list[tuple[int, int]],
    sample_rate: int,
    minimum_hole_frames: int,
    left_frequency: float,
    right_frequency: float,
) -> tuple[list[dict[str, object]], int]:
    discontinuities: list[dict[str, object]] = []
    analyzed = 0
    phase_window = min(1024, max(128, sample_rate // 50))
    for start, end in runs:
        if end - start < minimum_hole_frames:
            continue
        if start < phase_window or end + phase_window > len(left):
            continue
        channel_errors: dict[str, float] = {}
        channel_amplitudes: dict[str, list[float]] = {}
        for name, samples, frequency in (
            ("left", left, left_frequency),
            ("right", right, right_frequency),
        ):
            before = _phase_fit(
                samples, start - phase_window, start, frequency, sample_rate
            )
            after = _phase_fit(
                samples, end, end + phase_window, frequency, sample_rate
            )
            if before is None or after is None:
                continue
            channel_errors[name] = _wrapped_phase_error(before[0], after[0])
            channel_amplitudes[name] = [before[1], after[1]]
        if not channel_errors:
            continue
        analyzed += 1
        if any(abs(error) > 0.1 for error in channel_errors.values()):
            discontinuities.append(
                {
                    "startFrame": start,
                    "endFrame": end,
                    "zeroFrames": end - start,
                    "phaseErrorRadians": channel_errors,
                    "boundaryAmplitudes": channel_amplitudes,
                }
            )
    return discontinuities, analyzed


def analyze(
    path: Path,
    *,
    expected_left_frequency: float = CUELET_LEFT_FREQUENCY,
    expected_right_frequency: float = CUELET_RIGHT_FREQUENCY,
    expected_peak: float = CUELET_PEAK,
    minimum_hole_frames: int = 2,
) -> dict[str, object]:
    info, offset, byte_count = read_chunks(path)
    if info != {
        "audioFormat": 3,
        "channels": 2,
        "sampleRate": info["sampleRate"],
        "blockAlign": 8,
        "bitsPerSample": 32,
    }:
        raise ValueError(f"expected stereo interleaved IEEE Float32, got {info}")

    frames = byte_count // 8
    left, right, digest = _load_samples(path, offset, frames)
    peaks = [0.0, 0.0]
    sums = [0.0, 0.0]
    dc_sums = [0.0, 0.0]
    non_finite = 0
    for left_sample, right_sample in zip(left, right):
        if not math.isfinite(left_sample) or not math.isfinite(right_sample):
            non_finite += 1
            continue
        peaks[0] = max(peaks[0], abs(left_sample))
        peaks[1] = max(peaks[1], abs(right_sample))
        sums[0] += left_sample * left_sample
        sums[1] += right_sample * right_sample
        dc_sums[0] += left_sample
        dc_sums[1] += right_sample

    runs = _zero_runs(left, right)
    zero_frames = sum(end - start for start, end in runs)
    nonzero_indices = [
        index
        for index, (left_sample, right_sample) in enumerate(zip(left, right))
        if left_sample != 0.0 or right_sample != 0.0
    ]
    active_start = nonzero_indices[0] if nonzero_indices else 0
    active_end = nonzero_indices[-1] + 1 if nonzero_indices else 0
    run_descriptions: list[dict[str, object]] = []
    active_runs: list[dict[str, object]] = []
    for start, end in runs:
        if end <= active_start:
            classification = "intentional_leading_silence"
        elif start >= active_end:
            classification = "intentional_trailing_silence"
        elif end - start >= minimum_hole_frames:
            classification = "driver_hole_candidate"
        else:
            classification = "producer_zero_crossing"
        description = {
            "startFrame": start,
            "endFrame": end,
            "frames": end - start,
            "classification": classification,
        }
        run_descriptions.append(description)
        if start > active_start and end < active_end:
            active_runs.append(description)

    hole_candidates = [
        run for run in active_runs if run["classification"] == "driver_hole_candidate"
    ]
    phase_discontinuities, analyzed_boundaries = _phase_discontinuities(
        left,
        right,
        [(int(run["startFrame"]), int(run["endFrame"])) for run in active_runs],
        info["sampleRate"],
        minimum_hole_frames,
        expected_left_frequency,
        expected_right_frequency,
    )
    dominant_left = _dominant_frequency(
        left, info["sampleRate"], active_start, active_end
    )
    dominant_right = _dominant_frequency(
        right, info["sampleRate"], active_start, active_end
    )
    denominator = frames if frames else 1
    rate = info["sampleRate"]
    frequency_tolerance = 1.0
    peak_tolerance = 1e-5
    marker_ordering = {
        "ordered": bool(nonzero_indices) and 0 <= active_start < active_end <= frames,
        "markers": [
            {"name": "leadingSilenceEnd", "frame": active_start},
            {"name": "activeSignalStart", "frame": active_start},
            {"name": "activeSignalEnd", "frame": active_end},
            {"name": "trailingSilenceStart", "frame": active_end},
        ],
    }
    return {
        "path": str(path),
        "sampleRate": rate,
        "channels": 2,
        "frames": frames,
        "durationSeconds": frames / rate if rate else 0.0,
        "peakLeft": peaks[0],
        "peakRight": peaks[1],
        "rmsLeft": math.sqrt(sums[0] / denominator),
        "rmsRight": math.sqrt(sums[1] / denominator),
        "dcLeft": dc_sums[0] / denominator,
        "dcRight": dc_sums[1] / denominator,
        "zeroFrames": zero_frames,
        "nonzeroFrames": frames - zero_frames,
        "nonFiniteFrames": non_finite,
        "allZero": frames == zero_frames,
        "payloadSHA256": digest,
        "activeSignalStartFrame": active_start,
        "activeSignalEndFrame": active_end,
        "leadingZeroFrames": active_start if nonzero_indices else frames,
        "trailingZeroFrames": frames - active_end if nonzero_indices else 0,
        "zeroRuns": run_descriptions,
        "activeZeroRuns": active_runs,
        "activeZeroRunCount": len(active_runs),
        "activeZeroRunFrameCount": sum(int(run["frames"]) for run in active_runs),
        "driverCreatedHoleCandidateCount": len(hole_candidates),
        "driverCreatedHoleCandidateFrames": sum(
            int(run["frames"]) for run in hole_candidates
        ),
        "dominantFrequencyLeft": dominant_left,
        "dominantFrequencyRight": dominant_right,
        "expectedFrequencyLeft": expected_left_frequency,
        "expectedFrequencyRight": expected_right_frequency,
        "frequencyValidationPassed": dominant_left is not None
        and dominant_right is not None
        and abs(dominant_left - expected_left_frequency) <= frequency_tolerance
        and abs(dominant_right - expected_right_frequency) <= frequency_tolerance,
        "expectedPeak": expected_peak,
        "peakValidationPassed": abs(peaks[0] - expected_peak) <= peak_tolerance
        and abs(peaks[1] - expected_peak) <= peak_tolerance,
        "phaseContinuity": {
            "analyzedHoleBoundaries": analyzed_boundaries,
            "discontinuityCount": len(phase_discontinuities),
            "continuousAcrossAnalyzedHoles": not phase_discontinuities,
            "discontinuities": phase_discontinuities,
        },
        "markerOrdering": marker_ordering,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("wav", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--expected-left-frequency", type=float, default=CUELET_LEFT_FREQUENCY
    )
    parser.add_argument(
        "--expected-right-frequency", type=float, default=CUELET_RIGHT_FREQUENCY
    )
    parser.add_argument("--expected-peak", type=float, default=CUELET_PEAK)
    parser.add_argument("--minimum-hole-frames", type=int, default=2)
    arguments = parser.parse_args()
    if arguments.minimum_hole_frames < 1:
        parser.error("--minimum-hole-frames must be positive")
    result = analyze(
        arguments.wav,
        expected_left_frequency=arguments.expected_left_frequency,
        expected_right_frequency=arguments.expected_right_frequency,
        expected_peak=arguments.expected_peak,
        minimum_hole_frames=arguments.minimum_hole_frames,
    )
    encoded = json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n"
    if arguments.output:
        arguments.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
