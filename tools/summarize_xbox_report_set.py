"""Summarize variance across xlle replay measurement reports, one or two arms.

This is a post-processing helper for performance gates. It does not launch xlle;
it turns repeated replay reports into a noise-floor summary so small code-shape
deltas are not accepted or rejected from one run
(docs/PERF-METHODOLOGY.md section 2, beads-xbox.1.14.1).

Two modes:

  Single set (legacy, unchanged):

      py tools/summarize_xbox_report_set.py \
          build/baseline-a.report.json \
          build/baseline-b.report.json \
          --require-same-final-sha

  Two arms (interleaved ABBA protocol, minimum 3 legs per arm):

      py tools/summarize_xbox_report_set.py \
          --arm-a build/leg-00-A-r1.report.json build/leg-02-A-r2.report.json \
          --arm-b build/leg-01-B-r1.report.json build/leg-03-B-r2.report.json \
          --json build/abba-summary.json

The two-arm mode reports, per arm: min / median / mean / stdev / CV for the
total 12 B wall (the gate metric, section 2 rule 5 as amended 2026-09-03: the
sum of every frame-series window wall), for the final-four wall (retained as a
tail-phase indicator only) and for every frame-series window wall; the
median-to-minimum spread (a contamination detector, rule 4); and a
host-quiescence contamination summary. Across arms it reports the paired
per-round deltas A_i vs B_i and the fraction of rounds favouring B, decided on
the total wall.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import statistics
import sys
from collections.abc import Callable, Mapping, Sequence
from typing import Any

sys.path.insert(0, str(pathlib.Path(__file__.replace("\\", "/")).resolve().parent))
from compare_xbox_reports import (
    AlignmentError,
    final_fps,
    final_sha,
    focus_sums,
    frame_indices,
)


NS_PER_SEC = 1_000_000_000

# Gate metric (docs/PERF-METHODOLOGY.md section 2 rule 5, amended 2026-09-03):
# the TOTAL 12 B wall, i.e. the sum of every frame-series window wall. The
# final-four wall below is retained as a reported tail-phase indicator only,
# because the final four windows are generic-combiner / GL-tail dominated and
# hid a 4-7 % witnessed-raster win.
GATE_METRIC = "total_wall_ns"
INDICATOR_METRIC = "final_four_wall_ns"

# Number of trailing frame-series windows summed for the indicator.
FOCUS_LAST_DEFAULT = 4


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


# --------------------------------------------------------------------------
# Statistics
# --------------------------------------------------------------------------


def describe(values: Sequence[float]) -> dict[str, Any]:
    """min / median / mean / stdev / CV plus the median-to-min spread.

    The median-to-minimum spread is the contamination detector of
    PERF-METHODOLOGY section 2 rule 4: a clean host has the median close to the
    minimum, because contamination can only ever make a leg slower.
    """
    numbers = [float(v) for v in values]
    if not numbers:
        return {
            "n": 0,
            "min": None,
            "median": None,
            "mean": None,
            "stdev": None,
            "cv_pct": None,
            "max": None,
            "median_to_min_spread_pct": None,
        }
    lo = min(numbers)
    med = statistics.median(numbers)
    avg = statistics.fmean(numbers)
    sigma = statistics.stdev(numbers) if len(numbers) > 1 else 0.0
    return {
        "n": len(numbers),
        "min": lo,
        "median": med,
        "mean": avg,
        "stdev": sigma,
        "cv_pct": (100.0 * sigma / avg) if avg else None,
        "max": max(numbers),
        "median_to_min_spread_pct": (100.0 * (med - lo) / lo) if lo else None,
    }


def quiescence_of(report: Mapping[str, Any]) -> dict[str, Any]:
    value = report.get("provenance", {}).get("host_quiescence")
    if not isinstance(value, Mapping):
        return {"recorded": False, "contaminated": None, "foreign_processes": []}
    foreign = value.get("foreign_processes")
    # ConvertTo-Json in Windows PowerShell 5.1 collapses a one-element array to a
    # scalar, so accept both shapes.
    if isinstance(foreign, list):
        processes = [str(item) for item in foreign]
    elif foreign is None:
        processes = []
    else:
        processes = [str(foreign)]
    return {
        "recorded": True,
        "contaminated": value.get("contaminated"),
        "foreign_processes": processes,
    }


def leg_identity(report: Mapping[str, Any]) -> dict[str, Any]:
    provenance = report.get("provenance", {})
    leg = provenance.get("leg") or {}
    return {
        "name": report.get("name"),
        "arm": leg.get("arm"),
        "leg_index": leg.get("index"),
        "repetition": leg.get("repetition"),
        "session": leg.get("session"),
        "chunk_insns": provenance.get("chunk_insns"),
        "engine": provenance.get("engine"),
        "perf_accounting": provenance.get("perf_accounting"),
        "exe_sha256": provenance.get("exe_sha256"),
        "git_head": provenance.get("git_head"),
    }


def window_walls(report: Mapping[str, Any]) -> list[dict[str, Any]]:
    frames = report.get("parsed", {}).get("frame_series", [])
    out: list[dict[str, Any]] = []
    if not isinstance(frames, list):
        return out
    for frame in frames:
        if not isinstance(frame, Mapping):
            continue
        out.append(
            {
                "deadline": frame.get("deadline"),
                "wall_ns": frame.get("wall_ns"),
                "effective_fps": frame.get("effective_fps"),
            }
        )
    return out


def final_four_wall(report: Mapping[str, Any], focus_last: int) -> float | None:
    frames = report.get("parsed", {}).get("frame_series", [])
    if not isinstance(frames, list) or not frames:
        return None
    total = 0.0
    for index in frame_indices(report, focus_last):
        frame = frames[index]
        if isinstance(frame, Mapping) and isinstance(frame.get("wall_ns"), (int, float)):
            total += float(frame["wall_ns"])
    return total


def total_wall(report: Mapping[str, Any]) -> float | None:
    """Sum of EVERY frame-series window wall -- the gate metric (rule 5).

    Returns None when the report carries no frame series at all, so an
    unusable leg is never silently reported as 0.0.
    """
    frames = report.get("parsed", {}).get("frame_series", [])
    if not isinstance(frames, list) or not frames:
        return None
    total = 0.0
    seen = False
    for frame in frames:
        if isinstance(frame, Mapping) and isinstance(frame.get("wall_ns"), (int, float)):
            total += float(frame["wall_ns"])
            seen = True
    return total if seen else None


def presented_flips_per_second(report: Mapping[str, Any]) -> float | None:
    presented = report.get("parsed", {}).get("presented_frames")
    if not isinstance(presented, Mapping):
        return None
    value = presented.get("flips_per_second")
    return float(value) if isinstance(value, (int, float)) else None


def arm_summary(
    label: str,
    paths: Sequence[pathlib.Path],
    reports: Sequence[Mapping[str, Any]],
    focus_last: int,
) -> dict[str, Any]:
    legs: list[dict[str, Any]] = []
    final_four: list[float] = []
    totals: list[float] = []
    fps_values: list[float] = []
    flips_values: list[float] = []
    per_window: dict[int, list[float]] = {}
    alignment_notes: list[str] = []

    for path, report in zip(paths, reports):
        wall = final_four_wall(report, focus_last)
        total = total_wall(report)
        identity = leg_identity(report)
        quiescence = quiescence_of(report)
        try:
            focus = focus_sums(report, focus_last)
            focus_error = None
        except AlignmentError as error:
            focus = {}
            focus_error = str(error)
            alignment_notes.append(f"{path}: {error}")
        modes = focus.get("_alignment_modes") or {}
        fallback = sorted(key for key, mode in modes.items() if mode == "positional-fallback")
        if fallback:
            alignment_notes.append(
                f"{path}: schema v1 report; positional fallback for {', '.join(fallback)}"
            )
        legs.append(
            {
                "path": str(path),
                "identity": identity,
                "ok": report.get("ok"),
                "final_sha": final_sha(report),
                "stderr_empty": stderr_empty(report),
                "total_wall_ns": total,
                "final_four_wall_ns": wall,
                "effective_fps": final_fps(report),
                "presented_flips_per_second": presented_flips_per_second(report),
                "quiescence": quiescence,
                "focus_alignment_error": focus_error,
                "focus_cpu_ns": focus.get("cpu_ns"),
            }
        )
        if wall is not None:
            final_four.append(wall)
        if total is not None:
            totals.append(total)
        fps = final_fps(report)
        if fps is not None:
            fps_values.append(float(fps))
        flips = presented_flips_per_second(report)
        if flips is not None:
            flips_values.append(flips)
        for entry in window_walls(report):
            deadline = entry.get("deadline")
            wall_ns = entry.get("wall_ns")
            if isinstance(deadline, int) and isinstance(wall_ns, (int, float)):
                per_window.setdefault(deadline, []).append(float(wall_ns))

    contaminated = [
        leg["path"] for leg in legs if leg["quiescence"].get("contaminated") is True
    ]
    unrecorded = [
        leg["path"] for leg in legs if not leg["quiescence"].get("recorded")
    ]

    return {
        "label": label,
        "legs": legs,
        "total_wall_ns": describe(totals),
        "final_four_wall_ns": describe(final_four),
        "effective_fps": describe(fps_values),
        "host_present": {
            key: describe([
                float((report.get("parsed", {}).get("host_present") or {})[key])
                for report in reports
                if isinstance((report.get("parsed", {}).get("host_present") or {}).get(key),
                              (int, float))
            ])
            for key in ("presented_fps", "guest_speed", "capture_us_avg",
                        "capture_to_present_us_avg", "upload_us_avg", "swap_us_avg",
                        "capture_to_present_us_max_total", "present_fail")
        },
        "presented_flips_per_second": describe(flips_values),
        "window_wall_ns": {
            str(deadline): describe(values)
            for deadline, values in sorted(per_window.items())
        },
        "contamination": {
            "legs": len(legs),
            "contaminated_legs": contaminated,
            "contaminated_count": len(contaminated),
            "quiescence_unrecorded_legs": unrecorded,
            "quiescence_unrecorded_count": len(unrecorded),
        },
        "final_shas": sorted({sha for sha in (leg["final_sha"] for leg in legs) if sha}),
        "alignment_notes": alignment_notes,
    }


def paired_deltas(
    arm_a: Mapping[str, Any], arm_b: Mapping[str, Any]
) -> dict[str, Any]:
    """Pair leg i of arm A with leg i of arm B (A1 B1 A2 B2 ... round order).

    `favours_b` and the round tally are decided on the GATE metric (total 12 B
    wall). The final-four columns travel alongside as the reported indicator.
    """
    legs_a = arm_a["legs"]
    legs_b = arm_b["legs"]
    rounds: list[dict[str, Any]] = []
    favouring_b = 0
    comparable = 0
    for index in range(min(len(legs_a), len(legs_b))):
        a_wall = legs_a[index][GATE_METRIC]
        b_wall = legs_b[index][GATE_METRIC]
        a_four = legs_a[index][INDICATOR_METRIC]
        b_four = legs_b[index][INDICATOR_METRIC]
        entry: dict[str, Any] = {
            "round": index + 1,
            "a_path": legs_a[index]["path"],
            "b_path": legs_b[index]["path"],
            "a_total_wall_ns": a_wall,
            "b_total_wall_ns": b_wall,
            "a_final_four_wall_ns": a_four,
            "b_final_four_wall_ns": b_four,
            "delta_ns": None,
            "delta_pct": None,
            "final_four_delta_ns": None,
            "final_four_delta_pct": None,
            "favours_b": None,
            "contaminated": bool(
                legs_a[index]["quiescence"].get("contaminated")
                or legs_b[index]["quiescence"].get("contaminated")
            ),
        }
        if isinstance(a_wall, (int, float)) and isinstance(b_wall, (int, float)):
            delta = float(b_wall) - float(a_wall)
            entry["delta_ns"] = delta
            entry["delta_pct"] = (100.0 * delta / a_wall) if a_wall else None
            entry["favours_b"] = delta < 0.0
            comparable += 1
            if delta < 0.0:
                favouring_b += 1
        if isinstance(a_four, (int, float)) and isinstance(b_four, (int, float)):
            four_delta = float(b_four) - float(a_four)
            entry["final_four_delta_ns"] = four_delta
            entry["final_four_delta_pct"] = (
                (100.0 * four_delta / a_four) if a_four else None
            )
        rounds.append(entry)
    return {
        "metric": GATE_METRIC,
        "rounds": rounds,
        "paired_rounds": comparable,
        "rounds_favouring_b": favouring_b,
        "fraction_favouring_b": (favouring_b / comparable) if comparable else None,
        "unpaired_legs": abs(len(legs_a) - len(legs_b)),
    }


def improvement_pct(base: float | None, cand: float | None) -> float | None:
    """Positive = candidate is faster (less wall time) than baseline."""
    if not isinstance(base, (int, float)) or not isinstance(cand, (int, float)):
        return None
    if not base:
        return None
    return 100.0 * (float(base) - float(cand)) / float(base)


def two_arm_summary(
    paths_a: Sequence[pathlib.Path],
    paths_b: Sequence[pathlib.Path],
    focus_last: int,
    label_a: str,
    label_b: str,
) -> dict[str, Any]:
    reports_a = [load_report(path) for path in paths_a]
    reports_b = [load_report(path) for path in paths_b]
    arm_a = arm_summary(label_a, paths_a, reports_a, focus_last)
    arm_b = arm_summary(label_b, paths_b, reports_b, focus_last)
    pairs = paired_deltas(arm_a, arm_b)

    a_stats = arm_a[GATE_METRIC]
    b_stats = arm_b[GATE_METRIC]
    a_four = arm_a[INDICATOR_METRIC]
    b_four = arm_b[INDICATOR_METRIC]
    return {
        "schema": "xboxlle.replay.two_arm_summary.v2",
        "focus_last": focus_last,
        "gate_metric": GATE_METRIC,
        "indicator_metric": INDICATOR_METRIC,
        "gate_reference": (
            "docs/PERF-METHODOLOGY.md section 2 rule 5 (amended 2026-09-03): "
            "total 12 B wall, median AND min"
        ),
        "arm_a": arm_a,
        "arm_b": arm_b,
        "paired": pairs,
        # `improvement` is the GATE metric (total wall). Consumers that gate
        # read this block; `final_four_improvement` is reported, never gated.
        "improvement": {
            "metric": GATE_METRIC,
            "median_pct": improvement_pct(a_stats["median"], b_stats["median"]),
            "min_pct": improvement_pct(a_stats["min"], b_stats["min"]),
            "mean_pct": improvement_pct(a_stats["mean"], b_stats["mean"]),
        },
        "final_four_improvement": {
            "metric": INDICATOR_METRIC,
            "median_pct": improvement_pct(a_four["median"], b_four["median"]),
            "min_pct": improvement_pct(a_four["min"], b_four["min"]),
            "mean_pct": improvement_pct(a_four["mean"], b_four["mean"]),
        },
        "contamination": {
            "arm_a": arm_a["contamination"],
            "arm_b": arm_b["contamination"],
            "any_contaminated": bool(
                arm_a["contamination"]["contaminated_count"]
                or arm_b["contamination"]["contaminated_count"]
            ),
        },
        "protocol": {
            "min_legs_per_arm": 3,
            "arm_a_legs": len(paths_a),
            "arm_b_legs": len(paths_b),
            "meets_minimum": len(paths_a) >= 3 and len(paths_b) >= 3,
            "reference": "docs/PERF-METHODOLOGY.md section 2 rule 3",
        },
    }


# --------------------------------------------------------------------------
# Human tables
# --------------------------------------------------------------------------


def fmt_stat(value: float | None, *, seconds: bool) -> str:
    if value is None:
        return "-"
    return fmt_seconds(value) if seconds else f"{value:.3f}"


def fmt_pct(value: float | None) -> str:
    return "-" if value is None else f"{value:+.2f}%"


def fmt_ratio_pct(value: float | None) -> str:
    return "-" if value is None else f"{value:.2f}%"


def print_stat_row(label: str, stats: Mapping[str, Any], *, seconds: bool) -> None:
    cv = fmt_ratio_pct(stats["cv_pct"])
    spread = fmt_ratio_pct(stats["median_to_min_spread_pct"])
    print(
        f"  {label:<22} n={stats['n']:<3} "
        f"min={fmt_stat(stats['min'], seconds=seconds):>10} "
        f"med={fmt_stat(stats['median'], seconds=seconds):>10} "
        f"mean={fmt_stat(stats['mean'], seconds=seconds):>10} "
        f"sd={fmt_stat(stats['stdev'], seconds=seconds):>10} "
        f"cv={cv:>8} med-min={spread:>8}"
    )


def print_arm(arm: Mapping[str, Any]) -> None:
    print(f"arm {arm['label']}: {len(arm['legs'])} legs")
    for leg in arm["legs"]:
        identity = leg["identity"]
        leg_index = "-" if identity["leg_index"] is None else str(identity["leg_index"])
        flips = leg["presented_flips_per_second"]
        flips_text = "-" if flips is None else f"{flips:.3f}"
        print(
            f"    leg={leg_index:>3} "
            f"rep={identity['repetition']} "
            f"ok={leg['ok']} "
            f"total={fmt_stat(leg['total_wall_ns'], seconds=True)} "
            f"wall4={fmt_stat(leg['final_four_wall_ns'], seconds=True)} "
            f"fps={leg['effective_fps']} "
            f"flips/s={flips_text} "
            f"contaminated={leg['quiescence']['contaminated']} "
            f"sha={(leg['final_sha'] or '-')[:8]}"
        )
        if leg["focus_alignment_error"]:
            print(f"        ALIGNMENT: {leg['focus_alignment_error']}")
    print_stat_row("total_wall (GATE)", arm["total_wall_ns"], seconds=True)
    print_stat_row("final_four_wall (ind)", arm["final_four_wall_ns"], seconds=True)
    print_stat_row("effective_fps", arm["effective_fps"], seconds=False)
    for key, stats in arm.get("host_present", {}).items():
        if stats.get("n"):
            print_stat_row("host_" + key, stats, seconds=False)
    print_stat_row(
        "guest_flip_writes_per_s", arm["presented_flips_per_second"], seconds=False
    )
    print("  per-window wall:")
    for deadline, stats in arm["window_wall_ns"].items():
        label = f"{int(deadline) / 1_000_000_000:.1f}B"
        print_stat_row(label, stats, seconds=True)
    contamination = arm["contamination"]
    print(
        f"  contamination: contaminated={contamination['contaminated_count']}/"
        f"{contamination['legs']} "
        f"quiescence_unrecorded={contamination['quiescence_unrecorded_count']}"
    )
    if len(arm["final_shas"]) > 1:
        print(f"  WARNING: {len(arm['final_shas'])} distinct final SHAs within the arm")
    for note in arm["alignment_notes"]:
        print(f"  ALIGNMENT NOTE: {note}")


def print_two_arm(summary: Mapping[str, Any]) -> None:
    print(
        f"two-arm summary (gate metric {summary['gate_metric']} = total 12 B wall; "
        f"indicator {summary.get('indicator_metric', INDICATOR_METRIC)}, "
        f"focus_last={summary['focus_last']})"
    )
    protocol = summary["protocol"]
    if not protocol["meets_minimum"]:
        print(
            f"  PROTOCOL WARNING: {protocol['arm_a_legs']}/{protocol['arm_b_legs']} legs; "
            f"{protocol['reference']} requires >= {protocol['min_legs_per_arm']} per arm"
        )
    print_arm(summary["arm_a"])
    print_arm(summary["arm_b"])
    print(
        "paired rounds (A_i vs B_i, negative delta = B faster; "
        "total is the gate, wall4 the indicator):"
    )
    print(
        f"  {'round':<6}{'A total':>12}{'B total':>12}{'delta':>12}{'delta%':>10}"
        f"{'A wall4':>12}{'B wall4':>12}{'w4 delta%':>11}  contaminated"
    )
    for entry in summary["paired"]["rounds"]:
        print(
            f"  {entry['round']:<6}"
            f"{fmt_stat(entry.get('a_total_wall_ns'), seconds=True):>12}"
            f"{fmt_stat(entry.get('b_total_wall_ns'), seconds=True):>12}"
            f"{fmt_stat(entry['delta_ns'], seconds=True):>12}"
            f"{fmt_pct(entry['delta_pct']):>10}"
            f"{fmt_stat(entry['a_final_four_wall_ns'], seconds=True):>12}"
            f"{fmt_stat(entry['b_final_four_wall_ns'], seconds=True):>12}"
            f"{fmt_pct(entry.get('final_four_delta_pct')):>11}"
            f"  {entry['contaminated']}"
        )
    paired = summary["paired"]
    fraction = paired["fraction_favouring_b"]
    fraction_text = "-" if fraction is None else f"{100.0 * fraction:.0f}%"
    print(
        f"  rounds favouring B: {paired['rounds_favouring_b']}/{paired['paired_rounds']}"
        f" ({fraction_text})"
    )
    improvement = summary["improvement"]
    print("B vs A improvement on TOTAL 12 B wall [GATE] (positive = B faster):")
    print(f"  median={fmt_pct(improvement['median_pct'])}")
    print(f"  min   ={fmt_pct(improvement['min_pct'])}")
    print(f"  mean  ={fmt_pct(improvement['mean_pct'])}")
    four = summary.get("final_four_improvement")
    if four:
        print(
            "B vs A improvement on final-four wall [INDICATOR ONLY, not gated] "
            "(positive = B faster):"
        )
        print(f"  median={fmt_pct(four['median_pct'])}")
        print(f"  min   ={fmt_pct(four['min_pct'])}")
        print(f"  mean  ={fmt_pct(four['mean_pct'])}")
    if summary["contamination"]["any_contaminated"]:
        print("  CONTAMINATION: at least one leg ran with foreign processes present")


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------


def run_single_set(args: argparse.Namespace) -> int:
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
    print_metric(
        "guest_flip_writes_per_second", collect(loaded, presented_flips_per_second)
    )
    print_metric("total_wall_ns", collect(loaded, total_wall), seconds=True)
    print_metric(
        "final_four_wall_ns",
        collect(loaded, lambda report: final_four_wall(report, args.focus_last)),
        seconds=True,
    )

    focus: list[dict[str, Any]] = []
    for path, report in zip(args.reports, loaded):
        try:
            focus.append(focus_sums(report, args.focus_last))
        except AlignmentError as error:
            print(f"  ALIGNMENT ERROR {path}: {error}")
            failed.append(f"{path}: {error}")
            focus.append({})

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
        "INVALID_ESTIMATE_generic_other_ns",
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


def run_two_arm(args: argparse.Namespace) -> int:
    summary = two_arm_summary(
        args.arm_a, args.arm_b, args.focus_last, args.label_a, args.label_b
    )
    print_two_arm(summary)

    failed: list[str] = []
    if args.require_same_final_sha:
        shas = set(summary["arm_a"]["final_shas"]) | set(summary["arm_b"]["final_shas"])
        if len(shas) != 1:
            failed.append(f"expected one final SHA across both arms, found {len(shas)}")
    if args.require_empty_stderr:
        for arm_key in ("arm_a", "arm_b"):
            for leg in summary[arm_key]["legs"]:
                if leg["stderr_empty"] is not True:
                    failed.append(f"{leg['path']}: stderr not empty")
    if args.require_quiescence and summary["contamination"]["any_contaminated"]:
        failed.append("at least one leg was contaminated by foreign processes")

    # Perf gate (opt-in here; compare_xbox_reports.py applies it unconditionally
    # in set mode). Always on the TOTAL 12 B wall, median AND min, per
    # PERF-METHODOLOGY section 2 rule 5/7 as amended 2026-09-03.
    if args.min_improvement_pct is not None:
        threshold = args.min_improvement_pct
        median_gain = summary["improvement"]["median_pct"]
        min_gain = summary["improvement"]["min_pct"]
        print(
            f"gate: {summary['gate_metric']} (total 12 B wall), median AND min, "
            f"threshold {threshold:.2f}% (PERF-METHODOLOGY section 2 rule 5/7)"
        )
        print(f"  median improvement: {fmt_pct(median_gain)}")
        print(f"  min improvement:    {fmt_pct(min_gain)}")
        if median_gain is None or min_gain is None:
            failed.append("total 12 B wall unavailable for one or both arms")
        else:
            if median_gain < threshold:
                failed.append(
                    f"total 12 B wall median improvement {median_gain:+.2f}% below "
                    f"threshold {threshold:.2f}%"
                )
            if min_gain < threshold:
                failed.append(
                    f"total 12 B wall min improvement {min_gain:+.2f}% below "
                    f"threshold {threshold:.2f}%"
                )
        summary["gate"] = {
            "metric": summary["gate_metric"],
            "rule": "median AND min must improve by >= threshold",
            "threshold_pct": threshold,
            "median_improvement_pct": median_gain,
            "min_improvement_pct": min_gain,
            "final_four_indicator": summary.get("final_four_improvement"),
        }

    summary["result"] = "REJECT" if failed else "PASS"
    summary["failures"] = failed

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(f"wrote {args.json}")

    if failed:
        print("result: REJECT")
        for item in failed:
            print(f"  - {item}")
        return 2
    print("result: PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="*", type=pathlib.Path)
    parser.add_argument("--arm-a", nargs="+", type=pathlib.Path, default=None)
    parser.add_argument("--arm-b", nargs="+", type=pathlib.Path, default=None)
    parser.add_argument("--label-a", default="A")
    parser.add_argument("--label-b", default="B")
    parser.add_argument(
        "--focus-last",
        type=int,
        default=FOCUS_LAST_DEFAULT,
        help="Number of final frame-series intervals to summarize.",
    )
    parser.add_argument("--require-same-final-sha", action="store_true")
    parser.add_argument("--require-empty-stderr", action="store_true")
    parser.add_argument(
        "--require-quiescence",
        action="store_true",
        help="Two-arm mode: reject if any leg recorded a foreign compiler/runner.",
    )
    parser.add_argument(
        "--min-improvement-pct",
        type=float,
        default=None,
        help=(
            "Two-arm mode: apply the retention gate here too. Required improvement "
            "on the TOTAL 12 B wall, on BOTH median and min. Omitted = report only."
        ),
    )
    parser.add_argument(
        "--json", type=pathlib.Path, help="Two-arm mode: write the summary JSON here."
    )
    args = parser.parse_args()

    if (args.arm_a is None) != (args.arm_b is None):
        parser.error("--arm-a and --arm-b must be given together")
    if args.arm_a is not None:
        if args.reports:
            parser.error("positional reports cannot be combined with --arm-a/--arm-b")
        return run_two_arm(args)
    if not args.reports:
        parser.error("give report paths, or --arm-a/--arm-b")
    return run_single_set(args)


if __name__ == "__main__":
    raise SystemExit(main())
