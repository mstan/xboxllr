"""Summarize variance across multiple xlle replay measurement reports.

This is a post-processing helper for performance gates. It does not launch xlle;
it turns repeated replay reports into a noise-floor summary so small code-shape
deltas are not accepted or rejected from one run.

Example:

    py tools/summarize_xbox_report_set.py \
        build/baseline-a.report.json \
        build/baseline-b.report.json \
        build/baseline-c.report.json \
        --require-same-final-sha
"""

from __future__ import annotations

import argparse
import json
import pathlib
import statistics
import sys
from collections.abc import Callable, Mapping
from typing import Any

sys.path.insert(0, str(pathlib.Path(__file__.replace("\\", "/")).resolve().parent))
from compare_xbox_reports import final_fps, final_sha, focus_sums


NS_PER_SEC = 1_000_000_000


def load_report(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def mean(values: list[float]) -> float:
    return statistics.fmean(values) if values else 0.0


def stdev(values: list[float]) -> float:
    return statistics.stdev(values) if len(values) > 1 else 0.0


def pct(value: float, base: float) -> str:
    if base == 0.0:
        return "n/a"
    return f"{100.0 * value / base:.2f}%"


def fmt_seconds(value: float) -> str:
    return f"{value / NS_PER_SEC:.3f}s"


def fmt_number(value: float) -> str:
    return f"{value:.3f}"


def collect(
    reports: list[Mapping[str, Any]],
    getter: Callable[[Mapping[str, Any]], float | None],
) -> list[float]:
    values: list[float] = []
    for report in reports:
        value = getter(report)
        if value is not None:
            values.append(float(value))
    return values


def print_metric(name: str, values: list[float], *, seconds: bool = False) -> None:
    if not values:
        print(f"  {name}: -")
        return
    avg = mean(values)
    sigma = stdev(values)
    lo = min(values)
    hi = max(values)
    fmt = fmt_seconds if seconds else fmt_number
    print(
        f"  {name}: n={len(values)} mean={fmt(avg)} stdev={fmt(sigma)} "
        f"cv={pct(sigma, avg)} min={fmt(lo)} max={fmt(hi)}"
    )


def stderr_empty(report: Mapping[str, Any]) -> bool | None:
    value = report.get("artifacts", {}).get("stderr_empty")
    return bool(value) if isinstance(value, bool) else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=pathlib.Path)
    parser.add_argument(
        "--focus-last",
        type=int,
        default=4,
        help="Number of final frame-series intervals to summarize.",
    )
    parser.add_argument("--require-same-final-sha", action="store_true")
    parser.add_argument("--require-empty-stderr", action="store_true")
    args = parser.parse_args()

    loaded = [load_report(path) for path in args.reports]
    failed: list[str] = []

    shas = [final_sha(report) for report in loaded]
    unique_shas = sorted({sha for sha in shas if sha})
    if args.require_same_final_sha and len(unique_shas) != 1:
        failed.append(f"expected one final SHA, found {len(unique_shas)}")

    stderr_values = [stderr_empty(report) for report in loaded]
    if args.require_empty_stderr and not all(value is True for value in stderr_values):
        failed.append("one or more reports have non-empty or missing stderr_empty")

    print("reports:")
    for path, report in zip(args.reports, loaded):
        ok = report.get("ok")
        fps = final_fps(report)
        sha = final_sha(report)
        print(
            f"  {path}: ok={ok} fps={fps if fps is not None else '-'} "
            f"sha={sha if sha else '-'} stderr_empty={stderr_empty(report)}"
        )

    print("summary:")
    print(f"  final_sha_count={len(unique_shas)}")
    if unique_shas:
        print(f"  final_sha={unique_shas[0] if len(unique_shas) == 1 else 'mixed'}")
    print_metric("final_fps", collect(loaded, final_fps))

    focus = [focus_sums(report, args.focus_last) for report in loaded]

    def focus_value(key: str) -> Callable[[Mapping[str, Any]], float | None]:
        index_by_id = {id(report): index for index, report in enumerate(loaded)}

        def getter(report: Mapping[str, Any]) -> float | None:
            values = focus[index_by_id[id(report)]]
            value = values.get(key)
            return float(value) if isinstance(value, (int, float)) else None

        return getter

    for key in (
        "wall_ns",
        "cpu_ns",
        "raster_generic_ns",
        "generic_software_raster_ns",
        "generic_other_est_ns",
        "vertex_transform_ns",
        "resolve_ns",
        "resident_ensure_ns",
    ):
        print_metric(key, collect(loaded, focus_value(key)), seconds=key.endswith("_ns"))

    if failed:
        print("result: REJECT")
        for item in failed:
            print(f"  - {item}")
        return 2
    print("result: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
