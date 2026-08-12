import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path


TOOL = Path(__file__).parents[1] / "tools" / "analyze-cuelet-receiver-pipeline.py"
SPEC = importlib.util.spec_from_file_location("receiver_pipeline", TOOL)
PIPELINE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(PIPELINE)


def write_wav(path: Path, samples: bytes, frames: int) -> None:
    data_size = len(samples)
    header = (
        b"RIFF"
        + struct.pack("<I", 36 + data_size)
        + b"WAVEfmt "
        + struct.pack("<IHHIIHH", 16, 3, 2, 48000, 48000 * 8, 8, 32)
        + b"data"
        + struct.pack("<I", data_size)
    )
    path.write_bytes(header + samples)
    assert frames * 8 == data_size


class ReceiverPipelineAnalysisTests(unittest.TestCase):
    def test_correlates_render_copy_disk_and_driver(self):
        samples = struct.pack("<ffff", 0.0, 0.0, 0.25, -0.25)
        payload_checksum = PIPELINE.checksum(samples)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            receiver = root / "receiver.jsonl"
            wav = root / "capture.wav"
            driver = root / "driver.jsonl"
            receiver.write_text(
                json.dumps(
                    {
                        "type": "receiver_callback",
                        "sequence": 0,
                        "sampleTime": 1000,
                        "actualFrames": 2,
                        "renderStatus": 0,
                        "renderBuffersValid": 1,
                        "renderSentinelFrames": 0,
                        "renderChecksum": payload_checksum,
                        "copiedChecksum": payload_checksum,
                        "wavChecksum": payload_checksum,
                        "wavStartFrame": 0,
                        "wavFrames": 2,
                        "wavWriteShort": 0,
                        "renderZeroFrames": 1,
                        "renderNonzeroFrames": 1,
                        "renderLongestZeroRunStart": 0,
                        "renderLongestZeroRunFrames": 1,
                    }
                )
                + "\n",
                encoding="utf-8",
            )
            write_wav(wav, samples, 2)
            driver.write_text(
                json.dumps(
                    {
                        "criticalEvents": [
                            {
                                "sequence": 12,
                                "kind": 2,
                                "client": 7,
                                "start": 1128,
                                "frames": 2,
                                "validFrames": 2,
                                "result": "READ_OK",
                                "checksum": payload_checksum,
                            }
                        ]
                    }
                )
                + "\n",
                encoding="utf-8",
            )

            result = PIPELINE.analyze(receiver, wav, driver, 7)

            self.assertTrue(result["pipelineIntegrity"])
            self.assertEqual(result["inferredSourceOffset"], 128)
            self.assertEqual(result["driverReceiverMatchedCallbacks"], 1)
            self.assertEqual(result["driverReceiverMismatches"], [])
            self.assertEqual(result["diskWavMismatchSequences"], [])

    def test_detects_saved_wav_corruption(self):
        samples = struct.pack("<ff", 0.25, -0.25)
        payload_checksum = PIPELINE.checksum(samples)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            receiver = root / "receiver.jsonl"
            wav = root / "capture.wav"
            receiver.write_text(
                json.dumps(
                    {
                        "type": "receiver_callback",
                        "sequence": 3,
                        "sampleTime": 0,
                        "actualFrames": 1,
                        "renderStatus": 0,
                        "renderBuffersValid": 1,
                        "renderSentinelFrames": 0,
                        "renderChecksum": payload_checksum,
                        "copiedChecksum": payload_checksum,
                        "wavChecksum": payload_checksum,
                        "wavStartFrame": 0,
                        "wavFrames": 1,
                        "wavWriteShort": 0,
                        "renderZeroFrames": 0,
                        "renderNonzeroFrames": 1,
                    }
                )
                + "\n",
                encoding="utf-8",
            )
            write_wav(wav, bytes(8), 1)

            result = PIPELINE.analyze(receiver, wav, None, None)

            self.assertFalse(result["pipelineIntegrity"])
            self.assertEqual(result["diskWavMismatchSequences"], [3])


if __name__ == "__main__":
    unittest.main()
