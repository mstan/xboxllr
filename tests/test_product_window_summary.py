import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from summarize_xbox_product_windows import window_summary


class ProductWindows(unittest.TestCase):
    def setUp(self):
        self.report = {"ok": True, "parsed": {"frame_series": [
            {"deadline": d, "insns": d + 1, "wall_ns": wall, "endpoint2": d // 10,
             "vblank_delta": 2} for d, wall in [(100, 9e9), (200, 2e9), (300, 3e9)]
        ]}}

    def test_excludes_interval_ending_at_start(self):
        result = window_summary(self.report, 100, 300)
        self.assertEqual(result["wall_seconds"], 5)
        self.assertEqual(result["intervals"], 2)
        self.assertAlmostEqual(result["guest_vblank_rate"], 0.8)
        self.assertAlmostEqual(result["guest_seconds"], 200 / 733333333)

    def test_missing_endpoint_or_interior_is_not_silently_shortened(self):
        for removed in range(3):
            report = copy.deepcopy(self.report)
            del report["parsed"]["frame_series"][removed]
            with self.assertRaises(ValueError):
                window_summary(report, 100, 300)

    def test_invalid_walls_and_unaligned_bounds(self):
        for wall in (0, -1, float("nan"), float("inf")):
            report = copy.deepcopy(self.report)
            report["parsed"]["frame_series"][1]["wall_ns"] = wall
            with self.assertRaises(ValueError):
                window_summary(report, 100, 300)
        with self.assertRaises(ValueError):
            window_summary(self.report, 101, 300)

    def test_failed_report_and_duplicate_clock(self):
        self.report["ok"] = False
        with self.assertRaises(ValueError):
            window_summary(self.report, 100, 300)
        self.report["ok"] = True
        self.report["parsed"]["frame_series"][2]["insns"] = 201
        with self.assertRaises(ValueError):
            window_summary(self.report, 100, 300)


if __name__ == "__main__":
    unittest.main()
