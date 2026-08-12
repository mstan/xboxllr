"""Compare two xlle replay measurement reports.

This is intentionally a post-processing gate.  It does not launch xlle or
change emulator behavior; it turns two expensive replay reports into a small
accept/reject summary focused on the dashboard phase.

Example:

    py tools/compare_xbox_reports.py \
        build/resident-constcolor-v1-12b.report.json \
        build/candidate-12b.report.json \
        --require-same-final-sha \
        --min-final-fps-gain-pct 10
"""

from __future__ import annotations

import argparse
import json
import pathlib
from collections.abc import Mapping
from typing import Any


NS_PER_SEC = 1_000_000_000


def load_report(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def sample_delta(
    samples: list[Mapping[str, Any]],
    index: int,
    previous_index: int | None,
    key: str,
) -> float:
    if index < 0 or index >= len(samples):
        return 0.0
    current = samples[index].get(key)
    if not isinstance(current, (int, float)):
        return 0.0
    if previous_index is None or previous_index < 0 or previous_index >= len(samples):
        return float(current)
    previous = samples[previous_index].get(key)
    if not isinstance(previous, (int, float)):
        return float(current)
    return float(current - previous)


def frame_indices(report: Mapping[str, Any], focus_last: int) -> list[int]:
    frames = report.get("parsed", {}).get("frame_series", [])
    if not isinstance(frames, list) or not frames:
        return []
    start = max(0, len(frames) - focus_last)
    return list(range(start, len(frames)))


def ns(value: float) -> str:
    return f"{value / NS_PER_SEC:.3f}s"


def pct(delta: float, base: float) -> str:
    if base == 0:
        return "n/a"
    return f"{100.0 * delta / base:+.2f}%"


def final_fps(report: Mapping[str, Any]) -> float | None:
    value = report.get("parsed", {}).get("final_frame", {}).get("effective_fps")
    return float(value) if isinstance(value, (int, float)) else None


def final_sha(report: Mapping[str, Any]) -> str | None:
    value = report.get("artifacts", {}).get("frame_dump_sha256")
    return str(value).lower() if value else None


def focus_sums(report: Mapping[str, Any], focus_last: int) -> dict[str, float]:
    parsed = report.get("parsed", {})
    frames = parsed.get("frame_series", [])
    perf = parsed.get("perf_samples", [])
    renderer = parsed.get("renderer_samples", [])
    resident = parsed.get("resident_samples", [])
    unicorn_memory = parsed.get("unicorn_memory_samples", [])
    if not all(isinstance(x, list) for x in (frames, perf, renderer, resident)):
        return {}
    out = {
        "wall_ns": 0.0,
        "cpu_ns": 0.0,
        "nv2a_pull_drain_ns": 0.0,
        "raster_generic_ns": 0.0,
        "resolve_ns": 0.0,
        "resident_ensure_ns": 0.0,
        "uploads": 0.0,
        "submits": 0.0,
        "code_hook_regions": 0.0,
    }
    for index in frame_indices(report, focus_last):
        previous = index - 1 if index else None
        frame = frames[index]
        if isinstance(frame, Mapping):
            value = frame.get("wall_ns")
            if isinstance(value, (int, float)):
                out["wall_ns"] += float(value)
        out["cpu_ns"] += sample_delta(perf, index, previous, "cpu_ns")
        out["nv2a_pull_drain_ns"] += sample_delta(
            perf, index, previous, "nv2a_pull_drain_ns"
        )
        out["raster_generic_ns"] += sample_delta(perf, index, previous, "raster_generic_ns")
        out["resolve_ns"] += sample_delta(renderer, index, previous, "resolve_ns")
        out["resident_ensure_ns"] += sample_delta(renderer, index, previous, "resident_ensure_ns")
        out["uploads"] += sample_delta(resident, index, previous, "uploads")
        out["submits"] += sample_delta(resident, index, previous, "submits")
        if isinstance(unicorn_memory, list):
            out["code_hook_regions"] += sample_delta(
                unicorn_memory, index, previous, "code_hook_regions"
            )
    return out


def print_focus_delta(name: str, base: float, cand: float, is_time: bool = True) -> None:
    delta = cand - base
    left = ns(base) if is_time else f"{base:.0f}"
    right = ns(cand) if is_time else f"{cand:.0f}"
    print(f"  {name}: {left} -> {right} ({pct(delta, base)})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=pathlib.Path)
    parser.add_argument("candidate", type=pathlib.Path)
    parser.add_argument(
        "--focus-last",
        type=int,
        default=4,
        help="Number of final frame-series intervals to sum for the dashboard-phase comparison.",
    )
    parser.add_argument("--require-same-final-sha", action="store_true")
    parser.add_argument("--min-final-fps-gain-pct", type=float)
    parser.add_argument("--max-focus-wall-regression-pct", type=float)
    args = parser.parse_args()

    baseline = load_report(args.baseline)
    candidate = load_report(args.candidate)

    base_fps = final_fps(baseline)
    cand_fps = final_fps(candidate)
    base_sha = final_sha(baseline)
    cand_sha = final_sha(candidate)

    print(f"baseline:  {args.baseline}")
    print(f"candidate: {args.candidate}")
    print(f"final fps: {base_fps} -> {cand_fps}")
    print(f"final sha: {base_sha} -> {cand_sha}")

    failed: list[str] = []
    if args.require_same_final_sha and base_sha != cand_sha:
        failed.append("final SHA changed")

    if (
        args.min_final_fps_gain_pct is not None
        and base_fps is not None
        and cand_fps is not None
    ):
        gain = 100.0 * (cand_fps - base_fps) / base_fps if base_fps else 0.0
        if gain < args.min_final_fps_gain_pct:
            failed.append(
                f"final FPS gain {gain:.2f}% below minimum {args.min_final_fps_gain_pct:.2f}%"
            )

    base_focus = focus_sums(baseline, args.focus_last)
    cand_focus = focus_sums(candidate, args.focus_last)
    if base_focus and cand_focus:
        print(f"focus: final {args.focus_last} frame-series intervals")
        for key in (
            "wall_ns",
            "cpu_ns",
            "nv2a_pull_drain_ns",
            "raster_generic_ns",
            "resolve_ns",
            "resident_ensure_ns",
        ):
            print_focus_delta(key, base_focus[key], cand_focus[key])
        print_focus_delta("uploads", base_focus["uploads"], cand_focus["uploads"], False)
        print_focus_delta("submits", base_focus["submits"], cand_focus["submits"], False)
        print_focus_delta(
            "code_hook_regions",
            base_focus["code_hook_regions"],
            cand_focus["code_hook_regions"],
            False,
        )

        if args.max_focus_wall_regression_pct is not None and base_focus["wall_ns"]:
            regression = 100.0 * (cand_focus["wall_ns"] - base_focus["wall_ns"]) / base_focus["wall_ns"]
            if regression > args.max_focus_wall_regression_pct:
                failed.append(
                    f"focus wall regression {regression:.2f}% exceeds maximum "
                    f"{args.max_focus_wall_regression_pct:.2f}%"
                )

    if failed:
        print("result: REJECT")
        for item in failed:
            print(f"  - {item}")
        return 2

    print("result: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
