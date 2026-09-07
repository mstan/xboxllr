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


if __name__ == "__main__":
    unittest.main()
