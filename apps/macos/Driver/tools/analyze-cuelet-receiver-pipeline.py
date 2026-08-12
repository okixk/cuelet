#!/usr/bin/env python3
"""Correlate bounded AUHAL, queue/WAV, and Cuelet driver telemetry."""

from __future__ import annotations

import argparse
import collections
import json
import struct
from pathlib import Path

FNV_SEED = 1469598103934665603
FNV_PRIME = 1099511628211


def checksum(samples: bytes) -> str:
    value = FNV_SEED
    for offset in range(0, len(samples), 8):
        packed = int.from_bytes(samples[offset : offset + 8], "little")
        value ^= packed
        value = (value * FNV_PRIME) & ((1 << 64) - 1)
    return f"{value:016x}"


def read_json_lines(path: Path) -> list[dict]:
    records = []
    with path.open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError as error:
                raise ValueError(f"{path}:{line_number}: {error}") from error
    return records


def read_float_wav(path: Path) -> bytes:
    content = path.read_bytes()
    if len(content) < 12 or content[:4] != b"RIFF" or content[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a RIFF/WAVE file")
    offset = 12
    format_code = None
    channels = None
    bits = None
    data = None
    while offset + 8 <= len(content):
        name = content[offset : offset + 4]
        size = struct.unpack_from("<I", content, offset + 4)[0]
        start = offset + 8
        end = start + size
        if end > len(content):
            raise ValueError(f"{path}: truncated {name!r} chunk")
        if name == b"fmt " and size >= 16:
            format_code, channels, _, _, _, bits = struct.unpack_from(
                "<HHIIHH", content, start
            )
        elif name == b"data":
            data = content[start:end]
        offset = end + (size & 1)
    if format_code != 3 or channels != 2 or bits != 32 or data is None:
        raise ValueError(
            f"{path}: expected stereo IEEE Float32, got "
            f"format={format_code} channels={channels} bits={bits}"
        )
    return data


def driver_read_events(path: Path | None) -> list[dict]:
    if path is None:
        return []
    events: dict[int, dict] = {}
    for record in read_json_lines(path):
        for event in record.get("criticalEvents", []):
            if event.get("kind") == 2:
                events[int(event["sequence"])] = event
    return [events[key] for key in sorted(events)]


def infer_source_offset(callbacks: list[dict], reads: list[dict]) -> int | None:
    by_payload: dict[tuple[str, int], list[dict]] = collections.defaultdict(list)
    for event in reads:
        by_payload[(event.get("checksum"), int(event.get("frames", 0)))].append(event)
    offsets: collections.Counter[int] = collections.Counter()
    for callback in callbacks:
        key = (callback.get("renderChecksum"), int(callback.get("actualFrames", 0)))
        if callback.get("renderNonzeroFrames", 0) == 0:
            continue
        for event in by_payload.get(key, []):
            offsets[int(event["start"]) - round(float(callback["sampleTime"]))] += 1
    return offsets.most_common(1)[0][0] if offsets else None


def analyze(receiver_path: Path, wav_path: Path, driver_path: Path | None, client: int | None) -> dict:
    receiver_records = read_json_lines(receiver_path)
    callbacks = [item for item in receiver_records if item.get("type") == "receiver_callback"]
    lifecycle = [item for item in receiver_records if item.get("type") == "receiver_lifecycle"]
    wav = read_float_wav(wav_path)
    reads = driver_read_events(driver_path)
    if client is not None:
        reads = [event for event in reads if int(event.get("client", -1)) == client]

    render_copy_mismatches = []
    copy_event_wav_mismatches = []
    disk_wav_mismatches = []
    wav_offset_mismatches = []
    mixed_zero_callbacks = []
    cumulative_frames = 0
    for event in callbacks:
        sequence = int(event["sequence"])
        actual = int(event.get("actualFrames", 0))
        wav_frames = int(event.get("wavFrames", 0))
        if event.get("renderChecksum") != event.get("copiedChecksum"):
            render_copy_mismatches.append(sequence)
        if actual and event.get("copiedChecksum") != event.get("wavChecksum"):
            copy_event_wav_mismatches.append(sequence)
        if int(event.get("wavStartFrame", -1)) != cumulative_frames:
            wav_offset_mismatches.append(sequence)
        start = int(event.get("wavStartFrame", 0)) * 8
        end = start + wav_frames * 8
        disk_checksum = checksum(wav[start:end])
        if disk_checksum != event.get("wavChecksum"):
            disk_wav_mismatches.append(sequence)
        cumulative_frames += wav_frames
        zero_frames = int(event.get("renderZeroFrames", 0))
        if 0 < zero_frames < actual:
            mixed_zero_callbacks.append(
                {
                    "sequence": sequence,
                    "sampleTime": event.get("sampleTime"),
                    "wavStartFrame": event.get("wavStartFrame"),
                    "zeroFrames": zero_frames,
                    "longestZeroRunStart": event.get("renderLongestZeroRunStart"),
                    "longestZeroRunFrames": event.get("renderLongestZeroRunFrames"),
                    "renderChecksum": event.get("renderChecksum"),
                }
            )

    source_offset = infer_source_offset(callbacks, reads)
    reads_by_range = {
        (int(event["start"]), int(event["frames"])): event for event in reads
    }
    matched = 0
    driver_receiver_mismatches = []
    driver_nonzero_receiver_zero = []
    if source_offset is not None:
        for callback in callbacks:
            frames = int(callback.get("actualFrames", 0))
            key = (round(float(callback["sampleTime"])) + source_offset, frames)
            read = reads_by_range.get(key)
            if read is None:
                continue
            matched += 1
            if read.get("checksum") != callback.get("renderChecksum"):
                mismatch = {
                    "receiverSequence": callback["sequence"],
                    "sampleTime": callback["sampleTime"],
                    "sourceStart": key[0],
                    "frames": frames,
                    "driverSequence": read["sequence"],
                    "driverResult": read.get("result"),
                    "driverValidFrames": read.get("validFrames"),
                    "driverChecksum": read.get("checksum"),
                    "receiverChecksum": callback.get("renderChecksum"),
                    "receiverZeroFrames": callback.get("renderZeroFrames"),
                }
                driver_receiver_mismatches.append(mismatch)
                if (
                    int(read.get("validFrames", 0)) == frames
                    and int(callback.get("renderZeroFrames", 0)) > 0
                    and read.get("checksum") != checksum(bytes(frames * 8))
                ):
                    driver_nonzero_receiver_zero.append(mismatch)

    return {
        "receiverEvents": str(receiver_path),
        "wav": str(wav_path),
        "driverWatch": str(driver_path) if driver_path else None,
        "client": client,
        "lifecycle": lifecycle,
        "callbacks": len(callbacks),
        "frames": cumulative_frames,
        "wavFileFrames": len(wav) // 8,
        "renderErrors": sum(int(item.get("renderStatus", 0) != 0) for item in callbacks),
        "invalidRenderBuffers": sum(not bool(item.get("renderBuffersValid", 0)) for item in callbacks),
        "sentinelFrames": sum(int(item.get("renderSentinelFrames", 0)) for item in callbacks),
        "shortWavWrites": sum(bool(item.get("wavWriteShort", 0)) for item in callbacks),
        "allZeroCallbacks": sum(bool(item.get("renderAllZero", 0)) for item in callbacks),
        "mixedZeroCallbacks": mixed_zero_callbacks,
        "renderCopyMismatchSequences": render_copy_mismatches,
        "copyEventWavMismatchSequences": copy_event_wav_mismatches,
        "diskWavMismatchSequences": disk_wav_mismatches,
        "wavOffsetMismatchSequences": wav_offset_mismatches,
        "driverReadEvents": len(reads),
        "inferredSourceOffset": source_offset,
        "driverReceiverMatchedCallbacks": matched,
        "driverReceiverMismatches": driver_receiver_mismatches,
        "driverNonzeroReceiverZeroEvents": driver_nonzero_receiver_zero,
        "pipelineIntegrity": not any(
            (
                render_copy_mismatches,
                copy_event_wav_mismatches,
                disk_wav_mismatches,
                wav_offset_mismatches,
            )
        )
        and cumulative_frames == len(wav) // 8,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--receiver-events", type=Path, required=True)
    parser.add_argument("--wav", type=Path, required=True)
    parser.add_argument("--driver-watch", type=Path)
    parser.add_argument("--client", type=int)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    result = analyze(
        arguments.receiver_events,
        arguments.wav,
        arguments.driver_watch,
        arguments.client,
    )
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if arguments.output:
        arguments.output.write_text(encoded, encoding="utf-8")
    else:
        print(encoded, end="")
    return 0 if result["pipelineIntegrity"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
