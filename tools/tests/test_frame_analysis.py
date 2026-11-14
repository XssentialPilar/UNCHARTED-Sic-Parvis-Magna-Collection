import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "analyze_frame_times", Path(__file__).resolve().parents[1] / "analyze_frame_times.py")
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


class FrameAnalysisTests(unittest.TestCase):
    def test_stall_is_preserved_even_if_legacy_column_hides_it(self):
        # A one-second recording with ten normal 10 ms intervals and a real 3 s stall.
        times = [1000000 + i * 10000 for i in range(11)] + [4100000]
        rows = [{"frame_index": i, "timestamp_us": t, "frame_time_ms": 16.666}
                for i, t in enumerate(times)]
        result = analysis.analyze(rows)
        self.assertEqual(result["intervals"], 11)
        self.assertEqual(result["max_ms"], 3000)
        self.assertEqual(result["p50_ms"], 10)
        self.assertEqual(result["p99_ms"], 3000)
        self.assertAlmostEqual(result["average_fps"], 11 / 3.1)
        self.assertAlmostEqual(result["low_1_percent_fps"], 1 / 3)
        self.assertIsNone(result["gpu_execution_ms"])

    def test_missing_frames_are_reported_without_inventing_intervals(self):
        rows = [{"frame_index": i, "timestamp_us": t}
                for i, t in [(1, 10000), (2, 20000), (5, 100000), (6, 110000)]]
        result = analysis.analyze(rows)
        self.assertEqual(result["intervals"], 2)
        self.assertEqual(result["missing_samples"], 2)
        self.assertEqual(result["mean_ms"], 10)
        self.assertIsNone(result["jitter_p99_ms"])

    def test_invalid_clock_does_not_create_negative_fps(self):
        rows = [{"frame_index": i, "timestamp_us": t}
                for i, t in [(1, 10000), (2, 5000), (3, 15000)]]
        result = analysis.analyze(rows)
        self.assertEqual(result["invalid_transitions"], 1)
        self.assertEqual(result["average_fps"], 100)


if __name__ == "__main__":
    unittest.main()
