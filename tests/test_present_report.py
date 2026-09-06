"""Presentation reports must not turn guest flip writes into host presents."""
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from measure_xbox_replay import parse_stdout
from summarize_xbox_report_set import arm_summary


class PresentReportTest(unittest.TestCase):
    def parse(self, log):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "log.txt"
            path.write_text(log, encoding="utf-8")
            return parse_stdout(path)

    def test_headless_does_not_invent_host_presents(self):
        parsed = self.parse("")
        self.assertIsNone(parsed["host_present"])
        self.assertEqual(parsed["host_present_samples"], [])

    def test_intervals_do_not_replace_cumulative_and_set_uses_all_legs(self):
        parsed = self.parse(
            "[host] present scope=interval presented=10 presented_fps=10.00 guest_speed=0.250\n"
            "[host] present scope=cumulative wall_ms=2000.0 presented=30 "
            "presented_fps=15.00 guest_speed=0.500 capture_us_avg=7.0 present_fail=0\n"
        )
        self.assertEqual(len(parsed["host_present_samples"]), 2)
        self.assertEqual(parsed["host_present"]["presented"], 30)
        self.assertEqual(parsed["host_present"]["presented_fps"], 15.0)
        self.assertEqual(parsed["host_present"]["guest_speed"], 0.5)
        reports = [{"parsed": parsed} for _ in range(3)]
        summary = arm_summary("A", [pathlib.Path(str(i)) for i in range(3)], reports, 4)
        self.assertEqual(summary["host_present"]["presented_fps"]["median"], 15.0)
        self.assertEqual(summary["host_present"]["capture_us_avg"]["min"], 7.0)


if __name__ == "__main__":
    unittest.main()
