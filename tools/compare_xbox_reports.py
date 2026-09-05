"""Compare xlle replay measurement reports: one-vs-one, or set-vs-set.

This is intentionally a post-processing gate. It does not launch xlle or change
emulator behavior; it turns expensive replay reports into a small accept/reject
summary focused on the dashboard phase.

Two modes:

  Single pair (legacy, unchanged CLI):

      py tools/compare_xbox_reports.py \
          build/resident-constcolor-v1-12b.report.json \
          build/candidate-12b.report.json \
          --require-same-final-sha \
          --min-final-fps-gain-pct 10

  Set vs set (the retention gate of docs/PERF-METHODOLOGY.md section 2 rule 7):

      py tools/compare_xbox_reports.py \
          --set-a build/leg-00-A-r1.report.json build/leg-02-A-r2.report.json \
          --set-b build/leg-01-B-r1.report.json build/leg-03-B-r2.report.json

  Set mode gates on the TOTAL 12 B wall -- the sum of every frame-series window
  wall: BOTH the median and the minimum must improve by at least
  --min-improvement-pct (default 5). The final-four wall is still computed and
  printed alongside as a tail-phase indicator, but it does not decide
  (PERF-METHODOLOGY section 2 rule 5, amended 2026-09-03: the final four windows
  are generic-combiner / GL-tail dominated and hid a 4-7 % witnessed-raster
  win). `final_fps` is never a gate metric.

Two structural corrections over the first version (beads-xbox.1.14.1):

  * Counter samples are keyed on the guest-anchored frame-series deadline, not
    on the list index. xlle emits one counter block per checkpoint plus one at
    shutdown, and some families (perf-resident, perf-texture-modulate) skip the
    shutdown block while others are absent entirely from a noperf log. Index
    arithmetic silently produced zeroes; this version raises AlignmentError.

  * The three "self time" subtractions are relabelled INVALID_ESTIMATE_*. The
    perf buckets are NOT a mutually exclusive partition
    (src/machine/perf_accounting.hpp:15-21: "none of these buckets may be
    subtracted to derive a pure-CPU number without interval-aware exclusive
    accounting"), so those differences have no defined meaning. Only
    raster_witnessed_est_ns is a legitimate subtraction, because
    g_ns_raster_generic is documented as a strict subset of g_ns_raster
    (perf_accounting.hpp, "raster_witnessed = g_ns_raster - g_ns_raster_generic").
"""

from __future__ import annotations

import argparse
import json
import pathlib
from collections.abc import Mapping, Sequence
from typing import Any


NS_PER_SEC = 1_000_000_000

# docs/PERF-METHODOLOGY.md section 2 rule 7.
DEFAULT_MIN_IMPROVEMENT_PCT = 5.0


class AlignmentError(RuntimeError):
    """A counter sample family could not be aligned to the frame-series windows."""


# Metric -> (parsed sample list, counter field). A metric is unavailable (None,
# printed as n/a) when its family is absent from the log, or when the field is
# absent from every sample of that family because the report predates it. It is
# never silently reported as 0.
SIMPLE_METRICS: dict[str, tuple[str, str]] = {
    "cpu_ns": ("perf_samples", "cpu_ns"),
    "nv2a_pull_drain_ns": ("perf_samples", "nv2a_pull_drain_ns"),
    "raster_ns": ("perf_samples", "raster_ns"),
    "raster_generic_ns": ("perf_samples", "raster_generic_ns"),
    "generic_texture_setup_ns": ("generic_split_samples", "texture_setup_ns"),
    "generic_software_raster_ns": ("generic_split_samples", "software_raster_ns"),
    "generic_flush_ns": ("generic_split_samples", "flush_ns"),
    "dispatch_method_ns": ("nv2a_split_samples", "dispatch_method_ns"),
    "submit_vertex_ns": ("nv2a_split_samples", "submit_vertex_ns"),
    "vertex_transform_ns": ("nv2a_split_samples", "vertex_transform_ns"),
    "resolve_ns": ("renderer_samples", "resolve_ns"),
    "resident_ensure_ns": ("renderer_samples", "resident_ensure_ns"),
    "uploads": ("resident_samples", "uploads"),
    "submits": ("resident_samples", "submits"),
    "code_hook_regions": ("unicorn_memory_samples", "code_hook_regions"),
}

# Derived metric -> (positive term, negative terms...). Each term names a simple
# metric; a derived metric is available only when all of its terms are.
DERIVED_METRICS: dict[str, tuple[str, ...]] = {
    # Legitimate: g_ns_raster_generic is a documented strict subset of
    # g_ns_raster (src/machine/perf_accounting.hpp).
    "raster_witnessed_est_ns": ("raster_ns", "raster_generic_ns"),
    # INVALID: the generic split buckets are not a partition of raster_generic.
    "INVALID_ESTIMATE_generic_other_ns": (
        "raster_generic_ns",
        "generic_texture_setup_ns",
        "generic_software_raster_ns",
        "generic_flush_ns",
    ),
    # INVALID: submit_vertex is not a strict subset of dispatch_method.
    "INVALID_ESTIMATE_dispatch_self_ns": ("dispatch_method_ns", "submit_vertex_ns"),
    # INVALID: raster also runs from drain_pull_queue(), outside submit_vertex,
    # so this difference can go negative and has no defined meaning.
    "INVALID_ESTIMATE_submit_self_ns": (
        "submit_vertex_ns",
        "vertex_transform_ns",
        "raster_ns",
    ),
}

SAMPLE_KINDS = tuple(sorted({kind for kind, _ in SIMPLE_METRICS.values()}))

REPORT_METRIC_ORDER = (
    "wall_ns",
    "cpu_ns",
    "nv2a_pull_drain_ns",
    "raster_ns",
    "raster_witnessed_est_ns",
    "raster_generic_ns",
    "generic_texture_setup_ns",
    "generic_software_raster_ns",
    "generic_flush_ns",
    "INVALID_ESTIMATE_generic_other_ns",
    "dispatch_method_ns",
    "INVALID_ESTIMATE_dispatch_self_ns",
    "submit_vertex_ns",
    "INVALID_ESTIMATE_submit_self_ns",
    "vertex_transform_ns",
    "resolve_ns",
    "resident_ensure_ns",
)


def load_report(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def frame_series(report: Mapping[str, Any]) -> list[Mapping[str, Any]]:
    frames = report.get("parsed", {}).get("frame_series", [])
    return [frame for frame in frames if isinstance(frame, Mapping)] if isinstance(
        frames, list
    ) else []


def frame_indices(report: Mapping[str, Any], focus_last: int) -> list[int]:
    frames = frame_series(report)
    if not frames:
        return []
    start = max(0, len(frames) - focus_last)
    return list(range(start, len(frames)))


def ns(value: float | None) -> str:
    if value is None:
        return "n/a"
    return f"{value / NS_PER_SEC:.3f}s"


def pct(delta: float | None, base: float | None) -> str:
    if delta is None or base is None or base == 0:
        return "n/a"
    return f"{100.0 * delta / base:+.2f}%"


def final_fps(report: Mapping[str, Any]) -> float | None:
    value = report.get("parsed", {}).get("final_frame", {}).get("effective_fps")
    return float(value) if isinstance(value, (int, float)) else None


def final_sha(report: Mapping[str, Any]) -> str | None:
    value = report.get("artifacts", {}).get("frame_dump_sha256")
    return str(value).lower() if value else None


def profile_hashes(report: Mapping[str, Any]) -> dict[str, str]:
    value = report.get("parsed", {}).get("profile_hashes")
    if not isinstance(value, Mapping):
        return {}
    return {str(k): str(v).lower() for k, v in value.items()}


def perf_accounting(report: Mapping[str, Any]) -> bool | None:
    value = report.get("provenance", {}).get("perf_accounting")
    if isinstance(value, bool):
        return value
    command = report.get("inputs", {}).get("command")
    if isinstance(command, str) and command:
        return "--perf-accounting" in command
    # A log with zero [perf] samples cannot have run with accounting on.
    samples = report.get("parsed", {}).get("perf_samples")
    if isinstance(samples, list):
        return bool(samples)
    return None


# --------------------------------------------------------------------------
# Guest-anchored sample alignment
# --------------------------------------------------------------------------


class SampleIndex:
    """Maps a frame-series deadline to the counter sample recorded at it."""

    def __init__(self, kind: str, samples: list[Any], mapping: dict[int, int], mode: str):
        self.kind = kind
        self.samples = samples
        self.mapping = mapping
        self.mode = mode
        self._has: dict[str, bool] = {}

    def has(self, key: str) -> bool:
        """True when at least one sample carries a numeric value for `key`.

        A key absent from every sample means the field did not exist when the
        log or report was produced -- that is 'unavailable', not a misalignment.
        A key present in some samples but missing from the one being read is a
        format change and raises from value().
        """
        cached = self._has.get(key)
        if cached is None:
            cached = any(
                isinstance(sample, Mapping)
                and isinstance(sample.get(key), (int, float))
                for sample in self.samples
            )
            self._has[key] = cached
        return cached

    def value(self, deadline: int, key: str) -> float:
        index = self.mapping.get(deadline)
        if index is None:
            raise AlignmentError(
                f"{self.kind}: no sample recorded at frame-series deadline {deadline} "
                f"(alignment mode {self.mode}, {len(self.samples)} samples, "
                f"{len(self.mapping)} keyed checkpoints)"
            )
        sample = self.samples[index]
        if not isinstance(sample, Mapping):
            raise AlignmentError(f"{self.kind}: sample at deadline {deadline} is not a mapping")
        current = sample.get(key)
        if not isinstance(current, (int, float)):
            raise AlignmentError(
                f"{self.kind}: sample at deadline {deadline} has no numeric '{key}' "
                f"(got {current!r}); the log format changed under this tool"
            )
        return float(current)

    def delta(self, deadline: int, previous_deadline: int | None, key: str) -> float:
        current = self.value(deadline, key)
        if previous_deadline is None:
            return current
        return current - self.value(previous_deadline, key)


def build_sample_index(
    report: Mapping[str, Any], kind: str, deadlines: Sequence[int]
) -> SampleIndex | None:
    """Return an index for `kind`, or None when the family is absent entirely.

    Raises AlignmentError when the family is present but cannot be mapped onto
    the frame-series checkpoints.
    """
    parsed = report.get("parsed", {})
    samples = parsed.get(kind)
    if not isinstance(samples, list) or not samples:
        return None

    tags = parsed.get("sample_checkpoints", {})
    tags = tags.get(kind) if isinstance(tags, Mapping) else None
    if isinstance(tags, list) and len(tags) == len(samples):
        mapping: dict[int, int] = {}
        for index, tag in enumerate(tags):
            if not isinstance(tag, Mapping):
                continue
            deadline = tag.get("deadline")
            if isinstance(deadline, int) and deadline not in mapping:
                mapping[deadline] = index
        if not mapping:
            raise AlignmentError(
                f"{kind}: {len(samples)} samples but none carry a checkpoint deadline"
            )
        return SampleIndex(kind, samples, mapping, "deadline-keyed")

    # Schema v1 report: no per-sample checkpoint tags. Fall back to positional
    # association, which is only defensible when there is at least one sample per
    # checkpoint and the extra samples (if any) trail the last checkpoint.
    if len(samples) < len(deadlines):
        raise AlignmentError(
            f"{kind}: {len(samples)} samples for {len(deadlines)} frame-series "
            "checkpoints and no sample_checkpoints tags (schema v1 report); "
            "re-run measure_xbox_replay.py to regenerate this report"
        )
    mapping = {deadline: index for index, deadline in enumerate(deadlines)}
    return SampleIndex(kind, samples, mapping, "positional-fallback")


def focus_sums(report: Mapping[str, Any], focus_last: int) -> dict[str, float | None]:
    """Sum per-window costs over the last `focus_last` frame-series windows.

    Values are None where the underlying counter family is absent from the log
    (for example every perf bucket in a noperf run), never 0.
    """
    frames = frame_series(report)
    if not frames:
        return {}
    deadlines = [int(frame["deadline"]) for frame in frames]

    indices: dict[str, SampleIndex | None] = {
        kind: build_sample_index(report, kind, deadlines) for kind in SAMPLE_KINDS
    }

    out: dict[str, float | None] = {"wall_ns": 0.0}
    for metric, (kind, key) in SIMPLE_METRICS.items():
        index = indices[kind]
        out[metric] = 0.0 if (index is not None and index.has(key)) else None
    for metric, terms in DERIVED_METRICS.items():
        out[metric] = 0.0 if all(out[term] is not None for term in terms) else None

    for window in frame_indices(report, focus_last):
        deadline = deadlines[window]
        previous = deadlines[window - 1] if window else None
        wall = frames[window].get("wall_ns")
        if not isinstance(wall, (int, float)):
            raise AlignmentError(
                f"frame_series: window at deadline {deadline} has no numeric wall_ns"
            )
        out["wall_ns"] += float(wall)

        window_values: dict[str, float] = {}
        for metric, (kind, key) in SIMPLE_METRICS.items():
            if out[metric] is None:
                continue
            index = indices[kind]
            assert index is not None
            value = index.delta(deadline, previous, key)
            window_values[metric] = value
            out[metric] += value

        for metric, terms in DERIVED_METRICS.items():
            if out[metric] is None:
                continue
            head, *tail = terms
            out[metric] += window_values[head] - sum(
                window_values[term] for term in tail
            )

    out["_alignment_modes"] = {  # type: ignore[assignment]
        kind: (index.mode if index else "absent") for kind, index in indices.items()
    }
    return out


def print_focus_delta(
    name: str, base: float | None, cand: float | None, is_time: bool = True
) -> None:
    if base is None or cand is None:
        left = "n/a" if base is None else (ns(base) if is_time else f"{base:.0f}")
        right = "n/a" if cand is None else (ns(cand) if is_time else f"{cand:.0f}")
        print(f"  {name}: {left} -> {right} (unavailable: counter family absent)")
        return
    delta = cand - base
    left = ns(base) if is_time else f"{base:.0f}"
    right = ns(cand) if is_time else f"{cand:.0f}"
    print(f"  {name}: {left} -> {right} ({pct(delta, base)})")


# --------------------------------------------------------------------------
# Profile hash comparison (beads-xbox.1.14.8)
# --------------------------------------------------------------------------

# See measure_xbox_replay.TELEMETRY_GATED_PROFILE_HASHES: these two accumulators
# only advance when collect_diagnostics is true (nv2a.cpp:1234 / 5948), i.e.
# under --perf-accounting, so they are incomparable across arms that differ in it.
TELEMETRY_GATED_PROFILE_HASHES = frozenset(
    {"generic_combiner", "transform_position_fnv64"}
)


def compare_profile_hashes(
    baseline: Mapping[str, Any], candidate: Mapping[str, Any]
) -> dict[str, Any]:
    base_hashes = profile_hashes(baseline)
    cand_hashes = profile_hashes(candidate)
    base_perf = perf_accounting(baseline)
    cand_perf = perf_accounting(candidate)
    telemetry_incomparable = (
        base_perf is not None and cand_perf is not None and bool(base_perf) != bool(cand_perf)
    )

    same: list[str] = []
    differ: list[dict[str, str]] = []
    skipped: list[dict[str, str]] = []
    only_base: list[str] = []
    only_cand: list[str] = []
    for key in sorted(set(base_hashes) | set(cand_hashes)):
        base_value = base_hashes.get(key)
        cand_value = cand_hashes.get(key)
        if base_value is None:
            only_cand.append(key)
            continue
        if cand_value is None:
            only_base.append(key)
            continue
        if key in TELEMETRY_GATED_PROFILE_HASHES and telemetry_incomparable:
            skipped.append(
                {
                    "key": key,
                    "baseline": base_value,
                    "candidate": cand_value,
                    "reason": (
                        f"telemetry-gated hash; perf_accounting baseline={base_perf} "
                        f"candidate={cand_perf}"
                    ),
                }
            )
            continue
        if base_value == cand_value:
            same.append(key)
        else:
            differ.append(
                {"key": key, "baseline": base_value, "candidate": cand_value}
            )
    return {
        "available": bool(base_hashes and cand_hashes),
        "baseline_perf_accounting": base_perf,
        "candidate_perf_accounting": cand_perf,
        "same": same,
        "differ": differ,
        "skipped": skipped,
        "only_in_baseline": only_base,
        "only_in_candidate": only_cand,
    }


def print_profile_hash_diff(diff: Mapping[str, Any]) -> None:
    if not diff["available"]:
        print(
            "profile hashes: unavailable (one or both reports predate schema v2; "
            "regenerate with measure_xbox_replay.py)"
        )
        return
    print(
        f"profile hashes: {len(diff['same'])} identical, "
        f"{len(diff['differ'])} differ, {len(diff['skipped'])} skipped"
    )
    for entry in diff["differ"]:
        print(f"  DIFFER {entry['key']}: {entry['baseline']} -> {entry['candidate']}")
    for entry in diff["skipped"]:
        print(
            f"  SKIP   {entry['key']}: {entry['baseline']} -> {entry['candidate']} "
            f"({entry['reason']})"
        )
    for key in diff["only_in_baseline"]:
        print(f"  ONLY-A {key}")
    for key in diff["only_in_candidate"]:
        print(f"  ONLY-B {key}")


# --------------------------------------------------------------------------
# Single-pair mode
# --------------------------------------------------------------------------


def run_pair(args: argparse.Namespace) -> int:
    baseline = load_report(args.baseline)
    candidate = load_report(args.candidate)

    base_fps = final_fps(baseline)
    cand_fps = final_fps(candidate)
    base_sha = final_sha(baseline)
    cand_sha = final_sha(candidate)

    print(f"baseline:  {args.baseline}")
    print(f"candidate: {args.candidate}")
    print(
        f"effective_fps (realtime-ratio x60, not presented fps): {base_fps} -> {cand_fps}"
    )
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

    hash_diff = compare_profile_hashes(baseline, candidate)
    print_profile_hash_diff(hash_diff)
    if args.require_same_profile_hashes and hash_diff["differ"]:
        failed.append(
            f"{len(hash_diff['differ'])} profile output hashes differ: "
            + ", ".join(entry["key"] for entry in hash_diff["differ"])
        )

    try:
        base_focus = focus_sums(baseline, args.focus_last)
        cand_focus = focus_sums(candidate, args.focus_last)
    except AlignmentError as error:
        print(f"ALIGNMENT ERROR: {error}")
        print("result: REJECT")
        return 3

    for label, focus in (("baseline", base_focus), ("candidate", cand_focus)):
        modes = focus.get("_alignment_modes") or {}
        fallback = sorted(k for k, v in modes.items() if v == "positional-fallback")
        if fallback:
            print(
                f"note: {label} is a schema v1 report; these families were aligned "
                f"positionally, not by deadline: {', '.join(fallback)}"
            )

    if base_focus and cand_focus:
        print(f"focus: final {args.focus_last} frame-series intervals")
        for key in REPORT_METRIC_ORDER:
            print_focus_delta(key, base_focus.get(key), cand_focus.get(key))
        for key in ("uploads", "submits", "code_hook_regions"):
            print_focus_delta(key, base_focus.get(key), cand_focus.get(key), False)

        base_wall = base_focus.get("wall_ns")
        cand_wall = cand_focus.get("wall_ns")
        if (
            args.max_focus_wall_regression_pct is not None
            and isinstance(base_wall, (int, float))
            and isinstance(cand_wall, (int, float))
            and base_wall
        ):
            regression = 100.0 * (cand_wall - base_wall) / base_wall
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
    print(
        "note: a single pair cannot clear the retention gate of "
        "docs/PERF-METHODOLOGY.md section 2 rule 3 (>= 3 interleaved legs per "
        "arm); use --set-a/--set-b."
    )
    return 0


# --------------------------------------------------------------------------
# Set-vs-set mode (the retention gate)
# --------------------------------------------------------------------------


def run_sets(args: argparse.Namespace) -> int:
    # Imported here: summarize_xbox_report_set imports from this module.
    import sys

    sys.path.insert(0, str(pathlib.Path(__file__.replace("\\", "/")).resolve().parent))
    from summarize_xbox_report_set import print_two_arm, two_arm_summary

    summary = two_arm_summary(
        args.set_a, args.set_b, args.focus_last, args.label_a, args.label_b
    )
    print_two_arm(summary)

    # Gate metric: TOTAL 12 B wall (summary["improvement"]). The final-four wall
    # is carried through as a reported indicator only.
    gate_metric = summary["gate_metric"]
    a_stats = summary["arm_a"][gate_metric]
    b_stats = summary["arm_b"][gate_metric]
    a_four = summary["arm_a"]["final_four_wall_ns"]
    b_four = summary["arm_b"]["final_four_wall_ns"]
    median_gain = summary["improvement"]["median_pct"]
    min_gain = summary["improvement"]["min_pct"]
    four_improvement = summary.get("final_four_improvement") or {}
    four_median_gain = four_improvement.get("median_pct")
    four_min_gain = four_improvement.get("min_pct")
    threshold = args.min_improvement_pct

    failed: list[str] = []
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

    if not summary["protocol"]["meets_minimum"]:
        failed.append(
            f"fewer than 3 legs per arm "
            f"({summary['protocol']['arm_a_legs']}/{summary['protocol']['arm_b_legs']}); "
            "docs/PERF-METHODOLOGY.md section 2 rule 3"
        )

    if args.require_quiescence and summary["contamination"]["any_contaminated"]:
        failed.append("at least one leg was contaminated by foreign processes")

    if args.require_same_final_sha:
        shas = set(summary["arm_a"]["final_shas"]) | set(summary["arm_b"]["final_shas"])
        if len(shas) != 1:
            failed.append(f"expected one final SHA across both arms, found {len(shas)}")

    # Hash comparison uses the first leg of each arm; every leg within an arm is
    # the same binary by construction.
    hash_diff: dict[str, Any] = {"available": False}
    if args.set_a and args.set_b:
        hash_diff = compare_profile_hashes(
            load_report(args.set_a[0]), load_report(args.set_b[0])
        )
        print_profile_hash_diff(hash_diff)
        if args.require_same_profile_hashes and hash_diff["differ"]:
            failed.append(
                f"{len(hash_diff['differ'])} profile output hashes differ: "
                + ", ".join(entry["key"] for entry in hash_diff["differ"])
            )

    result = "REJECT" if failed else "PASS"
    override_applied = False
    if failed and args.override:
        override_applied = True
        result = "PASS"

    output: dict[str, Any] = {
        "schema": "xboxlle.replay.set_gate.v2",
        "gate": {
            "metric": gate_metric,
            "rule": "median AND min must improve by >= threshold",
            "threshold_pct": threshold,
            "reference": (
                "docs/PERF-METHODOLOGY.md section 2 rule 5/7 (amended 2026-09-03)"
            ),
            "median_improvement_pct": median_gain,
            "min_improvement_pct": min_gain,
            "arm_a_total_wall_ns": a_stats,
            "arm_b_total_wall_ns": b_stats,
        },
        "indicator": {
            "metric": "final_four_wall_ns",
            "gated": False,
            "median_improvement_pct": four_median_gain,
            "min_improvement_pct": four_min_gain,
            "arm_a_final_four_wall_ns": a_four,
            "arm_b_final_four_wall_ns": b_four,
        },
        "profile_hashes": hash_diff,
        "summary": summary,
        "failures": failed,
        "result": result,
        "override": {
            "applied": override_applied,
            "reason": args.override_reason if override_applied else None,
        },
    }

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps(output, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(f"wrote {args.json}")

    print("gate: TOTAL 12 B wall (sum of all frame-series windows), median AND min, "
          f"threshold {threshold:.2f}% (PERF-METHODOLOGY section 2 rule 5/7)")
    print(f"  median improvement: {'n/a' if median_gain is None else f'{median_gain:+.2f}%'}")
    print(f"  min improvement:    {'n/a' if min_gain is None else f'{min_gain:+.2f}%'}")
    print("indicator (NOT gated): final-four wall")
    print(
        "  median improvement: "
        f"{'n/a' if four_median_gain is None else f'{four_median_gain:+.2f}%'}"
    )
    print(
        "  min improvement:    "
        f"{'n/a' if four_min_gain is None else f'{four_min_gain:+.2f}%'}"
    )

    if failed:
        for item in failed:
            print(f"  - {item}")
    if override_applied:
        print(f"result: PASS (OVERRIDE) reason: {args.override_reason}")
        print("  the failures above stand on the record; this is an override, not a pass")
        return 0
    if failed:
        print("result: REJECT")
        return 2
    print("result: PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", nargs="?", type=pathlib.Path)
    parser.add_argument("candidate", nargs="?", type=pathlib.Path)
    parser.add_argument(
        "--set-a",
        nargs="+",
        type=pathlib.Path,
        help="Arm A (baseline) reports for the set-vs-set retention gate.",
    )
    parser.add_argument(
        "--set-b",
        nargs="+",
        type=pathlib.Path,
        help="Arm B (candidate) reports for the set-vs-set retention gate.",
    )
    parser.add_argument("--label-a", default="A")
    parser.add_argument("--label-b", default="B")
    parser.add_argument(
        "--focus-last",
        type=int,
        default=4,
        help="Number of final frame-series intervals to sum for the dashboard-phase comparison.",
    )
    parser.add_argument("--require-same-final-sha", action="store_true")
    parser.add_argument(
        "--require-same-profile-hashes",
        action="store_true",
        help="Reject when any per-profile output hash differs (beads-xbox.1.14.8).",
    )
    parser.add_argument("--min-final-fps-gain-pct", type=float)
    parser.add_argument("--max-focus-wall-regression-pct", type=float)
    parser.add_argument(
        "--min-improvement-pct",
        type=float,
        default=DEFAULT_MIN_IMPROVEMENT_PCT,
        help=(
            "Set mode: required TOTAL 12 B wall improvement on BOTH median and min. "
            "The final-four wall is reported alongside but never gated."
        ),
    )
    parser.add_argument(
        "--require-quiescence",
        action="store_true",
        help="Set mode: reject if any leg recorded a foreign compiler/runner process.",
    )
    parser.add_argument(
        "--override",
        action="store_true",
        help="Set mode: accept despite gate failures. Requires --override-reason.",
    )
    parser.add_argument(
        "--override-reason",
        help="Free text written into the output JSON and printed with the override.",
    )
    parser.add_argument("--json", type=pathlib.Path, help="Write the gate output JSON here.")
    args = parser.parse_args()

    if (args.set_a is None) != (args.set_b is None):
        parser.error("--set-a and --set-b must be given together")
    if args.override and not args.override_reason:
        parser.error("--override requires --override-reason")
    if args.set_a is not None:
        if args.baseline or args.candidate:
            parser.error("positional reports cannot be combined with --set-a/--set-b")
        return run_sets(args)
    if not args.baseline or not args.candidate:
        parser.error("give baseline and candidate reports, or --set-a/--set-b")
    return run_pair(args)


if __name__ == "__main__":
    raise SystemExit(main())
