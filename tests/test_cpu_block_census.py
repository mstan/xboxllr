import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("census", Path(__file__).resolve().parents[1] /
                                              "tools/summarize_cpu_blocks.py")
census = importlib.util.module_from_spec(spec)
spec.loader.exec_module(census)
hygiene_spec = importlib.util.spec_from_file_location("hygiene", Path(__file__).resolve().parents[1] /
                                                     "tools/check_repository_hygiene.py")
hygiene = importlib.util.module_from_spec(hygiene_spec)
hygiene_spec.loader.exec_module(hygiene)


class CensusTests(unittest.TestCase):
    sample = ("# schema=xboxlle.block-census.v1 total_insns=101 fallback_insns=81 "
              "idle_skipped=10 native_insns=10 overflow=0\n"
              "pc,size,entries,charged_insns,unretired_insns\n"
              "0x1000,5,40,80,0\n0xf00,5,1,4,3\n")

    def read(self, text):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "census.csv"
            path.write_text(text, encoding="utf-8")
            return census.load_census(path)

    def test_fault_prefix_and_partition(self):
        report = self.read(self.sample)
        self.assertEqual([r["retired_insns"] for r in report["blocks"]], [80, 1])
        self.assertEqual(report["counters"]["total_insns"], 101)

    def test_missing_or_overflowed_counts_reject(self):
        for text in (self.sample.replace("overflow=0", "overflow=1"),
                     self.sample.replace("fallback_insns=81", "fallback_insns=82"),
                     self.sample.replace("total_insns=101", "total_insns=102")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                self.read(text)

    def test_invalid_or_duplicate_rows_reject(self):
        for text in (self.sample.replace("4,3", "4,5"),
                     self.sample + "0x1000,5,0,0,0\n",
                     self.sample.replace("0xf00,5", "0xffffffff,5")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                self.read(text)

    def test_repository_hygiene_marks_census_csv_private(self):
        findings = list(hygiene.content_findings(self.sample.encode("utf-8")))
        self.assertIn(("private block census metadata", 1), findings)

    def paired(self):
        before = self.sample.replace("overflow=0", "overflow=0 run_id=" + "a" * 32)
        after = before.replace("total_insns=101", "total_insns=123").replace(
            "fallback_insns=81", "fallback_insns=97").replace("idle_skipped=10", "idle_skipped=11").replace(
            "native_insns=10", "native_insns=15").replace("40,80,0", "45,90,0").replace(
            "1,4,3", "2,9,4") + "0x2000,2,1,2,0\n"
        return self.read(before), self.read(after)

    def test_interval_separates_boot_and_new_blocks_with_fault_correction(self):
        before, after = self.paired()
        result = census.interval_census(before, after)
        self.assertEqual(result["start_clock"], 101)
        self.assertEqual(result["end_clock"], 123)
        self.assertEqual(result["counters"], dict(total_insns=22, fallback_insns=16,
                                                idle_skipped=1, native_insns=5, overflow=0))
        self.assertEqual([r["retired_insns"] for r in result["blocks"]], [10, 4, 2])
        self.assertTrue(result["private_guest_input"])

    def test_interval_rejects_other_run_missing_identity_and_reverse_time(self):
        before, after = self.paired()
        with self.assertRaises(ValueError):
            census.interval_census(after, before)
        with self.assertRaises(ValueError):
            census.interval_census(before, before)
        for identity in (None, "b" * 32):
            with self.subTest(identity=identity), self.assertRaises(ValueError):
                census.interval_census(before, {**after, "run_id": identity})

    def test_interval_rejects_lost_or_decreasing_block_counters(self):
        before, after = self.paired()
        with self.assertRaisesRegex(ValueError, "lost"):
            census.interval_census(before, {**after, "blocks": after["blocks"][1:]})
        for field in ("entries", "charged_insns", "unretired_insns", "retired_insns"):
            changed = [{**r} for r in after["blocks"]]
            # The 0xf00 block has nonzero initial values for every field.
            next(r for r in changed if r["pc"] == 0xf00)[field] = 0
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "decreased"):
                census.interval_census(before, {**after, "blocks": changed})

    def test_bad_run_identity_rejected_and_legacy_cumulative_still_readable(self):
        self.assertIsNone(self.read(self.sample)["run_id"])
        with self.assertRaisesRegex(ValueError, "identity"):
            self.read(self.sample.replace("overflow=0", "overflow=0 run_id=bad"))


if __name__ == "__main__":
    unittest.main()
