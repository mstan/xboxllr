"""Summarize guest-anchored capacity windows; these are not displayed-frame rates.

Example: py -3 tools/summarize_xbox_product_windows.py build/run*.report.json
  --window settled:14500000000:16000000000 --json build/windows.json
Uses frame-series interval walls, excludes the interval ENDING at the start,
and requires exact endpoints and uninterrupted checkpoints. Do not compute
frame latency percentiles from these aggregated instruction windows.
"""
from __future__ import annotations

import argparse
import glob
import json
import math
from pathlib import Path
import statistics

CPU_HZ = 733333333  # Machine::kCpuHz, including deterministic idle fast-forward.


def window_summary(report: dict, start: int, end: int) -> dict:
    if not report.get("ok"):
        raise ValueError("report validation failed")
    if not 0 <= start < end:
        raise ValueError("window must have 0 <= start < end")
    frames = report["parsed"]["frame_series"]
    by_deadline = {f["deadline"]: f for f in frames}
    if len(by_deadline) != len(frames) or not frames:
        raise ValueError("empty or duplicate frame-series deadlines")
    cadence = min(by_deadline)
    if cadence <= 0 or start % cadence or end % cadence:
        raise ValueError("window boundaries do not align to checkpoint cadence")
    deadlines = list(range(start + cadence, end + 1, cadence))
    if end not in by_deadline or (start and start not in by_deadline):
        raise ValueError("missing window endpoint")
    if any(d not in by_deadline for d in deadlines):
        raise ValueError("missing checkpoint inside window")
    selected = [by_deadline[d] for d in deadlines]
    if any(not math.isfinite(f["wall_ns"]) or f["wall_ns"] <= 0 for f in selected):
        raise ValueError("nonpositive or nonfinite interval wall time")
    previous_insns = by_deadline[start]["insns"] if start else 0
    begin_insns = previous_insns
    for frame in selected:
        if frame["insns"] <= previous_insns:
            raise ValueError("guest instruction clock is not increasing")
        previous_insns = frame["insns"]
    wall_seconds = sum(f["wall_ns"] for f in selected) / 1e9
    guest_seconds = (previous_insns - begin_insns) / CPU_HZ
    return {
        "start": start, "end": end, "intervals": len(selected),
        "wall_seconds": wall_seconds, "guest_seconds": guest_seconds,
        "guest_speed": guest_seconds / wall_seconds,
        "guest_equivalent_fps": 60 * guest_seconds / wall_seconds,
        "guest_vblank_rate": sum(f["vblank_delta"] for f in selected) / wall_seconds,
        "timeline": [[f["deadline"], f["insns"], f["endpoint2"], f["vblank_delta"]]
                     for f in selected],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+")
    parser.add_argument("--window", action="append", required=True, metavar="NAME:START:END")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    paths = sorted({Path(p) for pattern in args.reports for p in glob.glob(pattern)})
    if not paths:
        parser.error("no matching reports")
    windows = {}
    try:
        for item in args.window:
            name, start, end = item.split(":")
            if name in windows:
                raise ValueError("duplicate window name")
            windows[name] = (int(start), int(end))
        rows = []
        for path in paths:
            report = json.loads(path.read_text(encoding="utf-8"))
            rows.append({"report": str(path), "leg": report.get("provenance", {}).get("leg"),
                         "windows": {name: window_summary(report, *bounds)
                                     for name, bounds in windows.items()}})
    except (ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    groups = {}
    for row in rows:
        arm = (row["leg"] or {}).get("arm", "unknown")
        for name, window in row["windows"].items():
            groups.setdefault(f"{arm}/{name}", []).append(window)
    summaries = {}
    for name, group in groups.items():
        walls = [w["wall_seconds"] for w in group]
        summaries[name] = {"runs": len(group), "wall_min": min(walls),
                           "wall_median": statistics.median(walls),
                           "wall_max": max(walls),
                           "same_timeline": all(w["timeline"] == group[0]["timeline"] for w in group),
                           "guest_equivalent_fps_min": min(w["guest_equivalent_fps"] for w in group),
                           "guest_equivalent_fps_median": statistics.median(w["guest_equivalent_fps"] for w in group)}
    output = {"label": "guest clock capacity; not distinct displayed FPS or latency",
              "reports": rows, "groups": summaries}
    if args.json:
        args.json.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summaries, indent=2))


if __name__ == "__main__":
    main()
