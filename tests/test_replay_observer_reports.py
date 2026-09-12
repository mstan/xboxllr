import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from typing import Any


REPO = pathlib.Path(__file__).resolve().parents[1]
TOOLS = REPO / "tools"
sys.path.insert(0, str(TOOLS))

from summarize_xbox_report_set import quiescence_of


def observer(**overrides: Any) -> dict[str, Any]:
    value: dict[str, Any] = {
        "schema": "xboxlle.process-observer.v1",
        "mode": "sampled",
        "started": True,
        "completed": True,
        "healthy": True,
        "covered_leg": True,
        "degraded": False,
        "errors": [],
        "samples": 2,
        "interval_ms": 50,
        "max_gap_ms": 50,
        "allowed_gap_ms": 100,
    }
    value.update(overrides)
    return value


def report(quiescence: dict[str, Any] | None, wall_ns: int) -> dict[str, Any]:
    provenance: dict[str, Any] = {}
    if quiescence is not None:
        provenance["host_quiescence"] = quiescence
    return {
        "name": "leg",
        "ok": True,
        "artifacts": {
            "frame_dump_sha256": "a" * 64,
            "stderr_empty": True,
        },
        "parsed": {
            "final_frame": {"effective_fps": 1.0},
            "frame_series": [
                {"deadline": 1_000_000_000, "wall_ns": wall_ns},
                {"deadline": 2_000_000_000, "wall_ns": wall_ns},
            ],
        },
        "provenance": provenance,
    }


class ReplayObserverReportTest(unittest.TestCase):
    def test_quiescence_proof_cases(self):
        cases = [
            ("old_absent_record", None, ["host_quiescence_missing"]),
            (
                "missing_monitor",
                {"contaminated": False},
                ["observer_missing"],
            ),
            (
                "failed_monitor",
                {
                    "contaminated": False,
                    "observer": observer(completed=False, healthy=False, errors=["boom"]),
                },
                [
                    "observer_completed_not_true",
                    "observer_healthy_not_true",
                    "observer_errors_nonempty",
                ],
            ),
            (
                "degraded_monitor",
                {"contaminated": False, "observer": observer(degraded=True)},
                ["observer_degraded_not_false"],
            ),
            (
                "uncovered_monitor",
                {"contaminated": False, "observer": observer(covered_leg=False)},
                ["observer_covered_leg_not_true"],
            ),
            (
                "malformed_monitor_values",
                {
                    "contaminated": False,
                    "observer": observer(
                        samples=1,
                        interval_ms=0,
                        allowed_gap_ms=float("inf"),
                        max_gap_ms=-1,
                    ),
                },
                [
                    "observer_samples_lt_2",
                    "observer_interval_ms_not_positive_int",
                    "observer_allowed_gap_ms_not_positive_finite",
                    "observer_max_gap_ms_not_nonnegative_finite",
                ],
            ),
            (
                "contaminated_valid_proof",
                {"contaminated": True, "observer": observer()},
                ["contaminated_not_explicitly_false"],
            ),
        ]
        for name, quiescence, expected_reasons in cases:
            with self.subTest(name=name):
                proof = quiescence_of(report(quiescence, 100))
                self.assertFalse(proof["timing_usable"])
                for reason in expected_reasons:
                    self.assertIn(reason, proof["timing_unusable_reasons"])

        proof = quiescence_of(
            report({"contaminated": False, "observer": observer()}, 100)
        )
        self.assertTrue(proof["timing_usable"])
        self.assertEqual(proof["timing_unusable_reasons"], [])

    def write_set(
        self, directory: pathlib.Path, first_quiescence: dict[str, Any] | None
    ) -> tuple[list[pathlib.Path], list[pathlib.Path]]:
        clean = {"contaminated": False, "observer": observer()}
        paths_a: list[pathlib.Path] = []
        paths_b: list[pathlib.Path] = []
        for index in range(3):
            q_a = first_quiescence if index == 0 else clean
            path_a = directory / f"a{index}.report.json"
            path_b = directory / f"b{index}.report.json"
            path_a.write_text(json.dumps(report(q_a, 100 + index)), encoding="utf-8")
            path_b.write_text(json.dumps(report(clean, 90 + index)), encoding="utf-8")
            paths_a.append(path_a)
            paths_b.append(path_b)
        return paths_a, paths_b

    def run_tool(
        self, tool: str, paths_a: list[pathlib.Path], paths_b: list[pathlib.Path]
    ) -> subprocess.CompletedProcess[str]:
        if tool == "summarize_xbox_report_set.py":
            args = [
                sys.executable,
                str(TOOLS / tool),
                "--arm-a",
                *map(str, paths_a),
                "--arm-b",
                *map(str, paths_b),
                "--require-quiescence",
                "--min-improvement-pct",
                "0",
            ]
        else:
            args = [
                sys.executable,
                str(TOOLS / tool),
                "--set-a",
                *map(str, paths_a),
                "--set-b",
                *map(str, paths_b),
                "--require-quiescence",
                "--min-improvement-pct",
                "0",
            ]
        return subprocess.run(
            args,
            cwd=REPO,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )

    def test_both_cli_gates_require_current_observer_proof(self):
        invalid = {"contaminated": False}
        with tempfile.TemporaryDirectory() as temp:
            paths_a, paths_b = self.write_set(pathlib.Path(temp), invalid)
            for tool in ("summarize_xbox_report_set.py", "compare_xbox_reports.py"):
                with self.subTest(tool=tool):
                    result = self.run_tool(tool, paths_a, paths_b)
                    self.assertEqual(result.returncode, 2, result.stdout)
                    self.assertIn("host quiescence unusable for timing", result.stdout)
                    self.assertIn("observer_missing", result.stdout)
                    self.assertIn(str(paths_a[0]), result.stdout)

    def test_both_cli_gates_accept_clean_current_observer_proof(self):
        clean = {"contaminated": False, "observer": observer()}
        with tempfile.TemporaryDirectory() as temp:
            paths_a, paths_b = self.write_set(pathlib.Path(temp), clean)
            for tool in ("summarize_xbox_report_set.py", "compare_xbox_reports.py"):
                with self.subTest(tool=tool):
                    result = self.run_tool(tool, paths_a, paths_b)
                    self.assertEqual(result.returncode, 0, result.stdout)

    def test_cli_gate_rejects_contaminated_valid_observer_proof(self):
        contaminated = {"contaminated": True, "observer": observer()}
        with tempfile.TemporaryDirectory() as temp:
            paths_a, paths_b = self.write_set(pathlib.Path(temp), contaminated)
            result = self.run_tool("compare_xbox_reports.py", paths_a, paths_b)
            self.assertEqual(result.returncode, 2, result.stdout)
            self.assertIn("at least one leg was contaminated", result.stdout)
            self.assertIn("contaminated_not_explicitly_false", result.stdout)

    def test_single_modes_enforce_requested_quiescence(self):
        for valid in (False, True):
            with self.subTest(valid=valid), tempfile.TemporaryDirectory() as temp:
                value = {"contaminated": False}
                if valid:
                    value["observer"] = observer()
                paths_a, paths_b = self.write_set(pathlib.Path(temp), value)
                for tool, paths in (
                    ("summarize_xbox_report_set.py", paths_a),
                    ("compare_xbox_reports.py", [paths_a[0], paths_b[0]]),
                ):
                    result = subprocess.run(
                        [sys.executable, str(TOOLS / tool), *map(str, paths),
                         "--require-quiescence"], cwd=REPO, text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
                    self.assertEqual(result.returncode, 0 if valid else 2, result.stdout)
                    if not valid:
                        self.assertIn("observer_missing", result.stdout)


if __name__ == "__main__":
    unittest.main()
