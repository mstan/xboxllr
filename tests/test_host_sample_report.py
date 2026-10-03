from pathlib import Path
from types import SimpleNamespace
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import symbolize_host_profile as profile

PROBE = sys.argv.pop(1) if len(sys.argv) > 1 and sys.argv[1].endswith(".exe") else None


class HostSampleReport(unittest.TestCase):
    def parse(self, text):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "profile.log"
            path.write_text(text, encoding="utf-8")
            return profile.parse_sampler_report(path)

    def render(self, window):
        lines = []
        result = profile.report_window(18, window, None, None,
                                       SimpleNamespace(top=1, collapse_anon=False), lines)
        return result, "\n".join(lines)

    def test_new_bounds_denominator_and_json(self):
        text = """[host-sampler] samples=10 windows=32 window_insns=1500000000 window_layout=2
[host-sampler] module id=1 base=0x100000 size=0x3000 name=xlle.exe
[host-sampler] window=18 insns=27000000000-28500000000 samples=10 range=bounded sampled_min=27000000000 sampled_max=28000000000
[host-sampler] leaf w=18 m=1 off=0x1000 n=4
[host-sampler] leaf w=18 m=1 off=0x2000 n=6
"""
        _, _, _, windows = self.parse(text)
        result, rendered = self.render(windows[18])
        self.assertEqual(result["instruction_range"], {"begin": 27000000000, "end_exclusive": 28500000000})
        self.assertEqual(result["sampled_progress"], {"minimum": 27000000000, "maximum": 28000000000})
        self.assertEqual(result["accounted"], result["samples"])
        self.assertTrue(result["complete"])
        self.assertEqual([r["pct"] for r in result["ranked"]], [60, 40])
        self.assertIn("[27000000000,28500000000)", rendered)
        self.assertIn("sampled progress (inclusive): 27000000000-28000000000", rendered)

    def test_tail_and_maximum_progress(self):
        bounds, observed, source = profile.parse_window_range(
            {"insns": "46500000000-unbounded", "range": "tail",
             "sampled_min": "46500000000", "sampled_max": str(2**64-1)}, 31,
            {"windows": "32", "window_layout": "2"})
        self.assertIsNone(bounds["end_exclusive"])
        self.assertEqual(observed["maximum"], 2**64-1)
        self.assertEqual(source, "recorded")

    def test_logged_interval_is_not_recomputed_from_index(self):
        _, _, _, windows = self.parse("""[host-sampler] samples=1 windows=32 window_insns=500000000 window_layout=2
[host-sampler] window=1 insns=500000000-1000000000 samples=1 range=bounded sampled_min=700000000 sampled_max=700000000
[host-sampler] leaf w=1 m=0 off=0x1000 n=1
""")
        result, rendered = self.render(windows[1])
        self.assertEqual(result["instruction_range"]["begin"], 500000000)
        self.assertIn("[500000000,1000000000)", rendered)

    def test_legacy_tail_is_not_a_finite_phase(self):
        for index, expected in [(6, 10500000000), (7, None)]:
            fields = {"insns": f"{index*1500000000}-{(index+1)*1500000000}"}
            bounds, observed, source = profile.parse_window_range(fields, index, {"windows": "8"})
            self.assertEqual(bounds["end_exclusive"], expected)
            self.assertIsNone(observed)
            if index == 7:
                self.assertEqual(source, "legacy-clamped-tail")
        self.assertEqual(profile.parse_window_range({}, 5, {}), (None, None, "unavailable"))

    def test_invalid_new_range_is_rejected(self):
        valid = dict(insns="0-1500000000", range="bounded", sampled_min="0", sampled_max="10")
        for patch in [dict(sampled_min="11"), dict(sampled_max="1500000000"),
                      dict(sampled_max=str(2**64)), dict(sampled_min="-1"),
                      dict(insns="0-unbounded"), dict(insns="2-1"),
                      dict(range="tail"), dict(sampled_max="")]:
            with self.subTest(patch=patch), self.assertRaises(ValueError):
                profile.parse_window_range(valid | patch, 0, {"window_layout": "2"})

    def test_truncated_leaf_table_keeps_true_denominator(self):
        _, _, _, windows = self.parse("""[host-sampler] samples=100 windows=8
[host-sampler] window=7 insns=10500000000-12000000000 samples=100
[host-sampler] top window=7 rank=0 count=25 pct=25.0 off=0x1000 module=[no-module]
""")
        result, rendered = self.render(windows[7])
        self.assertFalse(result["complete"])
        self.assertEqual(result["coverage_pct"], 25)
        self.assertEqual(result["ranked"][0]["pct"], 25)
        self.assertIn("unbounded", rendered)
        self.assertIn("legacy-clamped-tail", rendered)

    @unittest.skipUnless(PROBE, "requires the Windows sampling probe")
    def test_real_sampler_complete_tables_and_bounds(self):
        env = os.environ.copy()
        env["XLLE_HOST_SAMPLER_FULL_TABLE"] = "1"
        run = subprocess.run([PROBE], capture_output=True, text=True, timeout=30, env=env)
        self.assertEqual(run.returncode, 0, run.stderr)
        meta, binary, _, windows = self.parse(run.stdout)
        self.assertEqual(int(meta["window_layout"]), 2)
        self.assertEqual(int(meta["windows"]), 32)
        self.assertGreater(int(meta["window_storage_bytes"]), 0)
        self.assertLess(int(meta["window_storage_bytes"]), 64*1024*1024)
        self.assertEqual(binary["sha256"], profile.sha256_file(PROBE))
        for field in ("overflow", "suspend_failures", "context_failures", "open_failed"):
            self.assertEqual(int(meta[field]), 0)
        self.assertEqual(set(windows), {0, 7, 18, 31})
        self.assertEqual(sum(w["samples"] for w in windows.values()), int(meta["samples"]))
        for index, window in windows.items():
            self.assertEqual(sum(window["leaves"].values()), window["samples"])
            self.assertEqual(window["leaves_source"], "full")
        self.assertEqual(windows[18]["sampled_progress"], {"minimum": 27000000000, "maximum": 27000000000})
        self.assertEqual(windows[31]["sampled_progress"], {"minimum": 46500000000, "maximum": 2**64-1})
        self.assertIsNone(windows[31]["instruction_range"]["end_exclusive"])


if __name__ == "__main__":
    unittest.main()
