#!/usr/bin/env python3
"""Compare a Cuelet capture WAV with its known source using only stdlib."""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import struct


SAMPLE_RATE = 48_000
CHANNELS = 2


def read_wav(path: pathlib.Path) -> tuple[int, list[tuple[float, float]]]:
    payload = path.read_bytes()
    if payload[:4] != b"RIFF" or payload[8:12] != b"WAVE":
        raise ValueError(f"{path} is not a RIFF WAVE file")
    offset = 12
    fmt = None
    data = None
    while offset + 8 <= len(payload):
        tag = payload[offset : offset + 4]
        size = struct.unpack_from("<I", payload, offset + 4)[0]
        body = offset + 8
        if tag == b"fmt ":
            fmt = payload[body : body + size]
        elif tag == b"data":
            data = payload[body : body + size]
        offset = body + size + (size & 1)
    if fmt is None or data is None or len(fmt) < 16:
        raise ValueError(f"{path} lacks a complete fmt/data chunk")
    tag, channels, rate, _, block, bits = struct.unpack_from("<HHIIHH", fmt)
    if channels not in (1, 2):
        raise ValueError(f"{path} has unsupported channel count {channels}")
    if tag == 3 and bits == 32:
        values = struct.unpack("<" + "f" * (len(data) // 4), data)
    elif tag == 1 and bits == 16:
        integers = struct.unpack("<" + "h" * (len(data) // 2), data)
        values = tuple(value / 32768.0 for value in integers)
    else:
        raise ValueError(f"{path} has unsupported format tag={tag}, bits={bits}")
    frames: list[tuple[float, float]] = []
    if channels == 1:
        frames = [(value, value) for value in values]
    else:
        frames = list(zip(values[0::2], values[1::2]))
    if block != channels * (bits // 8):
        raise ValueError(f"{path} has inconsistent block alignment")
    return rate, frames


def magnitude(frame: tuple[float, float]) -> float:
    return 0.5 * (abs(frame[0]) + abs(frame[1]))


def first_active(frames: list[tuple[float, float]], threshold: float) -> int:
    window = max(1, SAMPLE_RATE // 200)
    for start in range(0, len(frames) - window + 1, window):
        rms = math.sqrt(
            sum(magnitude(frame) ** 2 for frame in frames[start : start + window])
            / window
        )
        if rms >= threshold:
            return start
    return len(frames)


def last_active(frames: list[tuple[float, float]], threshold: float) -> int:
    window = max(1, SAMPLE_RATE // 200)
    for start in range(len(frames) - window, -1, -window):
        rms = math.sqrt(
            sum(magnitude(frame) ** 2 for frame in frames[start : start + window])
            / window
        )
        if rms >= threshold:
            return start + window
    return 0


def mono(frame: tuple[float, float]) -> float:
    return 0.5 * (frame[0] + frame[1])


def db(value: float) -> float:
    return 20.0 * math.log10(max(abs(value), 1e-15))


def rms_envelope(
    frames: list[tuple[float, float]], quantum: int
) -> list[float]:
    result: list[float] = []
    for start in range(0, len(frames) - quantum + 1, quantum):
        power = sum(
            left * left + right * right
            for left, right in frames[start : start + quantum]
        )
        result.append(math.sqrt(power / (2 * quantum)))
    return result


def envelope_active_range(
    envelope: list[float], threshold: float
) -> tuple[int, int]:
    start = next(
        (index for index, value in enumerate(envelope) if value >= threshold),
        len(envelope),
    )
    end = next(
        (
            index + 1
            for index, value in reversed(list(enumerate(envelope)))
            if value >= threshold
        ),
        0,
    )
    return start, end


def lowpass_rms(
    frames: list[tuple[float, float]], cutoff_hz: float
) -> float:
    alpha = 1.0 - math.exp(-2.0 * math.pi * cutoff_hz / SAMPLE_RATE)
    left_state = right_state = 0.0
    power = 0.0
    for left, right in frames:
        left_state += alpha * (left - left_state)
        right_state += alpha * (right - right_state)
        power += left_state * left_state + right_state * right_state
    return math.sqrt(power / max(2 * len(frames), 1))


def analyze_media_path(
    reference: list[tuple[float, float]],
    capture: list[tuple[float, float]],
    expected_preroll_ms: float,
) -> dict[str, object]:
    quantum = SAMPLE_RATE // 100  # 10 ms, phase-insensitive envelope.
    reference_envelope = rms_envelope(reference, quantum)
    capture_envelope = rms_envelope(capture, quantum)
    reference_start, reference_end = envelope_active_range(
        reference_envelope, 0.001
    )
    active_quanta = reference_end - reference_start
    if active_quanta < 100:
        raise ValueError("media reference has less than one active second")

    reference_active = reference_envelope[reference_start:reference_end]
    reference_power = sum(value * value for value in reference_active)
    best_score = -1.0
    capture_start = 0
    for candidate in range(
        0, len(capture_envelope) - active_quanta + 1
    ):
        observed = capture_envelope[
            candidate : candidate + active_quanta
        ]
        dot = sum(
            expected * actual
            for expected, actual in zip(reference_active, observed)
        )
        observed_power = sum(value * value for value in observed)
        score = (
            dot / math.sqrt(reference_power * observed_power)
            if reference_power > 1e-20 and observed_power > 1e-20
            else 0.0
        )
        if score > best_score:
            best_score = score
            capture_start = candidate

    reference_frame_start = reference_start * quantum
    capture_frame_start = capture_start * quantum
    frame_count = min(
        (reference_end - reference_start) * quantum,
        len(reference) - reference_frame_start,
        len(capture) - capture_frame_start,
    )
    reference_aligned = reference[
        reference_frame_start : reference_frame_start + frame_count
    ]
    capture_aligned = capture[
        capture_frame_start : capture_frame_start + frame_count
    ]
    reference_rms = math.sqrt(
        sum(left * left + right * right for left, right in reference_aligned)
        / max(2 * frame_count, 1)
    )
    capture_rms = math.sqrt(
        sum(left * left + right * right for left, right in capture_aligned)
        / max(2 * frame_count, 1)
    )
    fullband_gain = capture_rms / max(reference_rms, 1e-15)
    reference_bass_rms = lowpass_rms(reference_aligned, 200.0)
    capture_bass_rms = lowpass_rms(capture_aligned, 200.0)
    bass_gain = capture_bass_rms / max(reference_bass_rms, 1e-15)
    bass_response_db = db(bass_gain / max(fullband_gain, 1e-15))

    def channel_balance(frames: list[tuple[float, float]]) -> float:
        left_power = sum(left * left for left, _ in frames)
        right_power = sum(right * right for _, right in frames)
        return db(
            math.sqrt(left_power / max(right_power, 1e-30))
        )

    reference_balance = channel_balance(reference_aligned)
    capture_balance = channel_balance(capture_aligned)
    peak = max(
        max(abs(left), abs(right)) for left, right in capture_aligned
    )
    clipped = sum(
        abs(sample) >= 0.999969
        for frame in capture_aligned
        for sample in frame
    )
    observed_envelope = capture_envelope[
        capture_start : capture_start + active_quanta
    ]
    silent_quanta = sum(
        expected >= 0.005 and observed <= 1e-7
        for expected, observed in zip(
            reference_active, observed_envelope
        )
    )
    click_candidates = sum(
        abs(mono(current) - mono(previous)) > 0.25
        for previous, current in zip(
            capture_aligned, capture_aligned[1:]
        )
    )
    capture_active_start, capture_active_end = envelope_active_range(
        capture_envelope, 0.001 * max(fullband_gain, 1e-6)
    )
    reference_duration_ms = (
        1000.0 * active_quanta * quantum / SAMPLE_RATE
    )
    capture_duration_ms = (
        1000.0
        * (capture_active_end - capture_active_start)
        * quantum
        / SAMPLE_RATE
    )
    duration_drift_ms = capture_duration_ms - reference_duration_ms
    onset_ms = (
        1000.0
        * (
            capture_frame_start - reference_frame_start
        )
        / SAMPLE_RATE
        - expected_preroll_ms
    )
    channel_balance_error_db = capture_balance - reference_balance
    passed = (
        best_score >= 0.95
        and abs(bass_response_db) <= 1.0
        and abs(channel_balance_error_db) <= 0.5
        and clipped == 0
        and silent_quanta == 0
        and click_candidates == 0
        and abs(duration_drift_ms) <= 150.0
        and abs(onset_ms) <= 250.0
        and peak >= 0.001
    )
    return {
        "passed": passed,
        "analysisMode": "media-envelope",
        "alignedFrames": frame_count,
        "envelopeCorrelation": best_score,
        "fullbandGain": fullband_gain,
        "fullbandGainDb": db(fullband_gain),
        "bassGain": bass_gain,
        "bassResponseDb": bass_response_db,
        "referenceChannelBalanceDb": reference_balance,
        "captureChannelBalanceDb": capture_balance,
        "channelBalanceErrorDb": channel_balance_error_db,
        "peak": peak,
        "rms": capture_rms,
        "clippedSamples": clipped,
        "zeroFilled10msQuanta": silent_quanta,
        "clickCandidates": click_candidates,
        "onsetMs": onset_ms,
        "durationDriftMs": duration_drift_ms,
    }


def best_alignment(
    reference: list[tuple[float, float]],
    capture: list[tuple[float, float]],
) -> tuple[int, int]:
    reference_onset = first_active(reference, 0.01)
    if reference_onset == len(reference):
        raise ValueError("could not detect source/capture onset")

    # Real-application captures intentionally include pre-roll. Searching
    # only around the first active capture packet can lock onto stale FIFO
    # data, graph-start transients, or an enabled physical microphone.
    # First search the whole recording at 5 ms resolution, then refine the
    # best candidate to two-frame resolution.
    window = min(SAMPLE_RATE * 2, len(reference) - reference_onset)
    if window < SAMPLE_RATE // 2:
        raise ValueError("reference has too little active audio to align")

    def correlation(candidate: int, stride: int) -> float:
        dot = reference_power = capture_power = 0.0
        for index in range(0, window, stride):
            expected = mono(reference[reference_onset + index])
            observed = mono(capture[candidate + index])
            dot += expected * observed
            reference_power += expected * expected
            capture_power += observed * observed
        denominator = math.sqrt(reference_power * capture_power)
        return dot / denominator if denominator > 1e-15 else -1.0

    coarse_step = SAMPLE_RATE // 200
    best_score = -2.0
    best_capture = 0
    for candidate in range(
        0,
        len(capture) - window + 1,
        coarse_step,
    ):
        score = correlation(candidate, 64)
        if score > best_score:
            best_score = score
            best_capture = candidate

    fine_radius = coarse_step
    for candidate in range(
        max(0, best_capture - fine_radius),
        min(len(capture) - window + 1, best_capture + fine_radius + 1),
        2,
    ):
        score = correlation(candidate, 8)
        if score > best_score:
            best_score = score
            best_capture = candidate
    return reference_onset, best_capture


def analyze(
    reference: list[tuple[float, float]],
    capture: list[tuple[float, float]],
) -> dict[str, object]:
    reference_start, capture_start = best_alignment(reference, capture)
    reference_end = last_active(reference, 0.01)
    reference_active_frames = reference_end - reference_start
    tail_radius = SAMPLE_RATE // 2
    tail_begin = max(
        capture_start,
        capture_start + reference_active_frames - tail_radius,
    )
    tail_end = min(
        len(capture),
        capture_start + reference_active_frames + tail_radius,
    )
    capture_end = tail_begin + last_active(
        capture[tail_begin:tail_end], 0.01
    )
    frames = min(reference_end - reference_start, len(capture) - capture_start)
    trim = min(SAMPLE_RATE // 50, frames // 8)
    reference_start += trim
    capture_start += trim
    frames -= 2 * trim
    if frames < SAMPLE_RATE:
        raise ValueError("less than one aligned second was captured")

    reference_mono = [mono(reference[reference_start + index]) for index in range(frames)]
    capture_mono = [mono(capture[capture_start + index]) for index in range(frames)]
    reference_power = sum(value * value for value in reference_mono)
    capture_power = sum(value * value for value in capture_mono)
    dot = sum(
        expected * observed
        for expected, observed in zip(reference_mono, capture_mono)
    )
    scale = dot / reference_power if reference_power > 1e-15 else 0.0
    residual = [
        observed - scale * expected
        for expected, observed in zip(reference_mono, capture_mono)
    ]
    residual_power = sum(value * value for value in residual)
    correlation = (
        dot / math.sqrt(reference_power * capture_power)
        if reference_power > 1e-15 and capture_power > 1e-15
        else 0.0
    )
    signal_rms = abs(scale) * math.sqrt(reference_power / frames)
    residual_rms = math.sqrt(residual_power / frames)
    snr = (
        20.0 * math.log10(signal_rms / max(residual_rms, 1e-15))
        if signal_rms > 0.0
        else -300.0
    )
    aligned_capture = capture[capture_start : capture_start + frames]
    peak = max(max(abs(left), abs(right)) for left, right in aligned_capture)
    rms = math.sqrt(
        sum(left * left + right * right for left, right in aligned_capture)
        / (2 * frames)
    )
    clipped = sum(
        abs(sample) >= 0.999969
        for frame in aligned_capture
        for sample in frame
    )
    frozen_runs = 0
    equal_run = 0
    for previous, current in zip(aligned_capture, aligned_capture[1:]):
        if current == previous and magnitude(current) > 1e-5:
            equal_run += 1
            if equal_run + 1 == SAMPLE_RATE // 1000:
                frozen_runs += 1
        else:
            equal_run = 0
    zero_quanta = 0
    quantum = SAMPLE_RATE // 1000
    for start in range(0, frames - quantum + 1, quantum):
        expected_peak = max(
            max(abs(left), abs(right))
            for left, right in reference[
                reference_start + start : reference_start + start + quantum
            ]
        )
        observed_peak = max(
            max(abs(left), abs(right))
            for left, right in aligned_capture[start : start + quantum]
        )
        if expected_peak >= 0.02 and observed_peak <= 1e-7:
            zero_quanta += 1
    click_threshold = max(0.10, 8.0 * residual_rms)
    clicks = sum(
        abs(current - previous) > click_threshold
        for previous, current in zip(residual, residual[1:])
    )
    onset_ms = 1000.0 * (capture_start - trim - (reference_start - trim)) / SAMPLE_RATE
    duration_drift_ms = (
        1000.0
        * (
            (capture_end - (capture_start - trim))
            - (reference_end - (reference_start - trim))
        )
        / SAMPLE_RATE
    )
    passed = (
        correlation >= 0.99
        and snr >= 20.0
        and clipped == 0
        and frozen_runs == 0
        and zero_quanta == 0
        and clicks == 0
        and abs(duration_drift_ms) <= 30.0
    )
    return {
        "passed": passed,
        "alignedFrames": frames,
        "fittedGain": scale,
        "referenceCorrelation": correlation,
        "snrDb": snr,
        "peak": peak,
        "rms": rms,
        "clippedSamples": clipped,
        "duplicateFrameRuns": frozen_runs,
        "zeroFilledQuanta": zero_quanta,
        "clickCandidates": clicks,
        "onsetMs": onset_ms,
        "durationDriftMs": duration_drift_ms,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--reference", required=True, type=pathlib.Path)
    parser.add_argument("--capture", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--media-player", action="store_true")
    parser.add_argument("--expected-preroll-ms", type=float, default=0.0)
    args = parser.parse_args()
    reference_rate, reference = read_wav(args.reference)
    capture_rate, capture = read_wav(args.capture)
    if reference_rate != SAMPLE_RATE or capture_rate != SAMPLE_RATE:
        raise ValueError(
            f"comparison expects 48 kHz WAVs, got {reference_rate}/{capture_rate}"
        )
    result = (
        analyze_media_path(
            reference, capture, args.expected_preroll_ms
        )
        if args.media_player
        else analyze(reference, capture)
    )
    result["reference"] = str(args.reference.resolve())
    result["capture"] = str(args.capture.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
