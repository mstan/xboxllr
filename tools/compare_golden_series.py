#!/usr/bin/env python3
"""Side-by-side of a replay report against a golden record, per PERF-METHODOLOGY section 3.

Prints, for one or more candidate reports:
  * the 24 per-checkpoint scanout hashes against the golden's, with the first differing
    deadline called out (that is where the two executions parted, in guest time);
  * the 22 per-profile output hashes (the two telemetry-gated families are labelled);
  * the final frame SHA-256 and scanout FNV;
  * guest-side counters (retired, IRQ0 pulses, delivered interrupts, XID endpoint-2
    reports, idle fraction) with their deltas.

Usage:
  py -3 tools/compare_golden_series.py --golden build/golden-....json \
      --report build/a.report.json [--report build/b.report.json ...]
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def load(path: str) -> dict[str, Any]:
    return json.loads(Path(path).read_text(encoding="utf-8"))


def series_of(report: dict[str, Any]) -> dict[int, str]:
    out: dict[int, str] = {}
    for row in report["parsed"].get("frame_series") or []:
        out[int(row["deadline"])] = str(row.get("hash", ""))
    return out


def insns_of(report: dict[str, Any]) -> dict[int, int]:
    out: dict[int, int] = {}
    for row in report["parsed"].get("frame_series") or []:
        out[int(row["deadline"])] = int(row.get("insns", 0))
    return out


def census(report: dict[str, Any]) -> dict[str, Any]:
    c = report["parsed"].get("idle_census") or {}
    return c


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--golden", required=True)
    ap.add_argument("--report", action="append", required=True)
    args = ap.parse_args()

    golden = load(args.golden)
    gseries = {int(x["deadline"]): x["hash"] for x in golden["frame_series_hashes"]}
    gprofiles = golden["profile_hashes"]
    gated = set(golden.get("telemetry_gated_profile_hashes") or [])

    reports = [(p, load(p)) for p in args.report]

    print(f"golden: {golden['name']}  engine={golden['engine']} "
          f"chunk={golden['chunk_insns']} exe={golden['exe_sha256'][:12]}")
    print(f"        final_frame_sha256={golden['final_frame_sha256'][:16]}")
    print()

    # ---- 24 scanout checkpoints -------------------------------------------
    print("=== frame-series scanout hashes (guest-anchored checkpoints) ===")
    header = f"{'deadline':>13} {'golden':>17}"
    for path, _ in reports:
        header += f" {Path(path).stem[:22]:>23}"
    print(header)
    first_diff: dict[str, int | None] = {p: None for p, _ in reports}
    n_diff: dict[str, int] = {p: 0 for p, _ in reports}
    for deadline in sorted(gseries):
        row = f"{deadline:>13} {gseries[deadline]:>17}"
        for path, rep in reports:
            s = series_of(rep)
            h = s.get(deadline, "-missing-")
            same = h == gseries[deadline]
            if not same:
                n_diff[path] += 1
                if first_diff[path] is None:
                    first_diff[path] = deadline
            row += f" {h:>17}{'  ' if same else ' *'}"
        print(row)
    print()
    for path, _ in reports:
        fd = first_diff[path]
        print(f"{Path(path).stem}: {n_diff[path]}/{len(gseries)} checkpoints differ"
              + (f"; first at deadline {fd}" if fd is not None else "; identical"))
    print()

    # ---- per-profile output hashes ----------------------------------------
    print("=== per-profile output hashes ===")
    for name in sorted(gprofiles):
        row = f"{name:>44} {gprofiles[name]:>17}"
        for path, rep in reports:
            h = (rep["parsed"].get("profile_hashes") or {}).get(name, "-missing-")
            row += f" {h:>17}{'  ' if h == gprofiles[name] else ' *'}"
        if name in gated:
            row += "  [telemetry-gated]"
        print(row)
    print()

    # ---- final frame -------------------------------------------------------
    print("=== final frame ===")
    print(f"{'golden final_frame_sha256':>44} {golden['final_frame_sha256']}")
    for path, rep in reports:
        art = rep.get("artifacts") or {}
        sha = art.get("final_frame_dump_sha256") or rep["parsed"].get("final_frame", {}).get(
            "sha256", "-")
        print(f"{Path(path).stem:>44} {sha}")
    print()

    # ---- guest-side counters ----------------------------------------------
    print("=== guest-side counters ===")
    keys = [
        ("retired", lambda r: r["parsed"].get("retired")),
        ("irq0_pulses", lambda r: census(r).get("irq0")),
        ("injected", lambda r: census(r).get("injected")),
        ("pending_boundaries", lambda r: census(r).get("pending_boundaries")),
        ("slices", lambda r: census(r).get("slices")),
        ("idle_insn_pct", lambda r: census(r).get("idle_insn_pct")),
        ("early_stops", lambda r: census(r).get("early_stops")),
        ("budget_slack", lambda r: census(r).get("budget_slack")),
        ("tb_entries", lambda r: census(r).get("tb_entries")),
        ("tb_insns", lambda r: census(r).get("tb_insns")),
        ("tb_icount_misses", lambda r: census(r).get("tb_icount_misses")),
        ("tb_icount_entries", lambda r: census(r).get("tb_icount_entries")),
        ("tb_bootstrap_blocks", lambda r: census(r).get("tb_bootstrap_blocks")),
        ("guest_tsc_reads", lambda r: census(r).get("guest_tsc_reads")),
        ("guest_tsc_deterministic", lambda r: census(r).get("guest_tsc_deterministic")),
        ("ohci_xid_endpoint2", lambda r: r["parsed"].get("ohci_xid_endpoint2")),
        ("presented_flips", lambda r: (r["parsed"].get("flip") or {}).get("flips")),
    ]
    head = f"{'counter':>26}"
    for path, _ in reports:
        head += f" {Path(path).stem[:22]:>23}"
    print(head)
    for name, fn in keys:
        row = f"{name:>26}"
        for path, rep in reports:
            try:
                v = fn(rep)
            except Exception:
                v = None
            row += f" {str(v):>23}"
        print(row)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
