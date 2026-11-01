"""Build a golden record from an existing xlle replay report (+ stdout log).

A golden record names its own identity, per docs/reference/testing/PERF-METHODOLOGY.md section 3
item 5: the chunk size, engine and build that produced it, together with every
per-profile output hash and the final frame SHA-256. That is what makes a
re-baseline auditable -- a bare `final_frame_dump_sha256` cannot attribute drift
and is itself sensitive to --machine-chunk-insns (beads-xbox.1.14.8).

The report is the primary input. Reports written before schema v2 have no
`parsed.profile_hashes`, so the stdout log is re-parsed to recover them; pass
--stdout to override the path recorded in the report.

Example:

    py tools/make_golden_record.py \
        build/codex-cachefix-noperf-v1-12b.report.json \
        --out build/golden-noperf-4096.json
"""

from __future__ import annotations

import argparse
import datetime
import json
import pathlib
import sys
from typing import Any

sys.path.insert(0, str(pathlib.Path(__file__.replace("\\", "/")).resolve().parent))
from measure_xbox_replay import (  # noqa: E402
    DEFAULT_ENGINE,
    chunk_insns_from_command,
    parse_stdout,
    perf_accounting_from_command,
)


GOLDEN_SCHEMA = "xboxlle.replay.golden.v1"

# Command-line flags whose argument is a guest medium the run reads.
INPUT_FLAGS = ("--boot", "--run2bl", "--hdd", "--eeprom", "--dvd")


def file_sha256(path: pathlib.Path) -> str | None:
    if not path.exists():
        return None
    import hashlib

    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 22), b""):
            digest.update(block)
    return digest.hexdigest()


def input_hashes(command: str) -> dict[str, Any]:
    """SHA-256 of every guest medium named on the command line.

    Paths are resolved relative to the repository root, which is where the
    replay harness runs (tools/run_stockdash_replay.ps1 pushes to it).
    """
    root = pathlib.Path(__file__.replace("\\", "/")).resolve().parent.parent
    tokens = command.replace("\\", "/").split()
    out: dict[str, Any] = {}
    for index, token in enumerate(tokens):
        if token in INPUT_FLAGS and index + 1 < len(tokens):
            raw = tokens[index + 1]
            path = pathlib.Path(raw)
            if not path.is_absolute():
                path = (root / raw).resolve()
            out[token.lstrip("-")] = {
                "path": str(path),
                "sha256": file_sha256(path),
                "bytes": path.stat().st_size if path.exists() else None,
            }
    return out


def resolve_stdout(
    report: dict[str, Any], override: pathlib.Path | None, report_path: pathlib.Path
) -> pathlib.Path | None:
    if override:
        return override
    recorded = report.get("inputs", {}).get("stdout")
    if recorded:
        candidate = pathlib.Path(recorded)
        if candidate.exists():
            return candidate
    # Reports are conventionally written next to their log as <name>.report.json
    # and <name>.stdout.log; fall back to that pairing when the recorded absolute
    # path came from another machine or another checkout.
    sibling = report_path.parent / (
        report_path.name.replace(".report.json", ".stdout.log")
    )
    return sibling if sibling.exists() else None


def build_golden(
    report_path: pathlib.Path,
    stdout_override: pathlib.Path | None,
    name: str | None,
    engine: str | None,
    note: str | None,
) -> dict[str, Any]:
    report = json.loads(report_path.read_text(encoding="utf-8"))
    parsed = report.get("parsed", {})
    provenance = report.get("provenance", {})
    command = report.get("inputs", {}).get("command")

    profile_hashes = parsed.get("profile_hashes")
    census_sha = parsed.get("generic_fingerprint_census_sha256")
    census_unique = parsed.get("generic_fingerprint_unique")
    telemetry_gated = parsed.get("telemetry_gated_profile_hashes")
    reparsed_from: str | None = None
    if not profile_hashes:
        stdout_path = resolve_stdout(report, stdout_override, report_path)
        if stdout_path is None:
            raise SystemExit(
                f"{report_path}: no parsed.profile_hashes and no readable stdout log; "
                "pass --stdout"
            )
        reparsed = parse_stdout(stdout_path)
        profile_hashes = reparsed["profile_hashes"]
        census_sha = reparsed["generic_fingerprint_census_sha256"]
        census_unique = reparsed["generic_fingerprint_unique"]
        telemetry_gated = reparsed["telemetry_gated_profile_hashes"]
        reparsed_from = str(stdout_path)

    chunk_insns = provenance.get("chunk_insns")
    if chunk_insns is None:
        chunk_insns = chunk_insns_from_command(command)
    perf_accounting = provenance.get("perf_accounting")
    if perf_accounting is None:
        perf_accounting = perf_accounting_from_command(command)

    retired = parsed.get("retired") or {}
    frames = parsed.get("frame_series") or []

    golden: dict[str, Any] = {
        "schema": GOLDEN_SCHEMA,
        "name": name or report.get("name"),
        "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(
            timespec="seconds"
        ),
        "source_report": str(report_path),
        "source_stdout": reparsed_from
        or report.get("inputs", {}).get("stdout"),
        # --- identity (PERF-METHODOLOGY section 3 item 5) --------------------
        "chunk_insns": chunk_insns,
        "engine": engine or provenance.get("engine") or DEFAULT_ENGINE,
        "exe_sha256": provenance.get("exe_sha256"),
        "git_head": provenance.get("git_head"),
        "perf_accounting": perf_accounting,
        "renderer": parsed.get("selected_renderer"),
        "max_insns": retired.get("insns"),
        "frame_series_count": len(frames),
        "command": command,
        # --- expectations ----------------------------------------------------
        "final_frame_sha256": report.get("artifacts", {}).get("frame_dump_sha256"),
        "final_scanout_fnv64": (parsed.get("scanout") or {}).get("rgba_fnv64"),
        "final_frame_series_hash": (
            frames[-1].get("hash") if frames else None
        ),
        "frame_series_hashes": [
            {"deadline": frame.get("deadline"), "hash": frame.get("hash")}
            for frame in frames
        ],
        "profile_hashes": profile_hashes,
        "telemetry_gated_profile_hashes": telemetry_gated,
        "generic_fingerprint_unique": census_unique,
        "generic_fingerprint_census_sha256": census_sha,
        "ohci_xid_endpoint2": parsed.get("ohci_xid_endpoint2"),
        # --- input identity (beads-xbox.1.14.11) -----------------------------
        # The guest media are inputs, not constants. The HDD image in particular
        # WAS being modified by the run itself until the copy-on-write attach
        # landed (docs/reference/testing/PERF-METHODOLOGY.md section 3b), so a golden that does not
        # name the image it was produced against cannot be checked.
        "input_sha256": input_hashes(command),
        "hdd_write_policy": ("writeback" if "--hdd-writeback" in command
                             else "copy-on-write"),
        "note": note,
    }
    return golden


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=pathlib.Path)
    parser.add_argument("--stdout", type=pathlib.Path, help="Override the stdout log path.")
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--name")
    parser.add_argument("--engine")
    parser.add_argument("--note", help="Free-text provenance note stored in the record.")
    args = parser.parse_args()

    golden = build_golden(args.report, args.stdout, args.name, args.engine, args.note)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    # newline="" stops Windows from translating json.dumps's "\n" into CRLF. goldens/ is
    # tracked (goldens/README.md), and a CRLF record makes `git diff --check` flag every
    # line as trailing whitespace.
    with args.out.open("w", encoding="utf-8", newline="") as handle:
        handle.write(json.dumps(golden, indent=2, sort_keys=True) + "\n")

    print(f"wrote {args.out}")
    print(f"  name={golden['name']}")
    print(
        f"  identity: chunk_insns={golden['chunk_insns']} engine={golden['engine']} "
        f"perf_accounting={golden['perf_accounting']}"
    )
    print(
        f"            exe_sha256={golden['exe_sha256']} git_head={golden['git_head']}"
    )
    print(f"  final_frame_sha256={golden['final_frame_sha256']}")
    print(
        f"  profile_hashes={len(golden['profile_hashes'])} "
        f"telemetry_gated={golden['telemetry_gated_profile_hashes']}"
    )
    print(
        f"  generic_fingerprint unique={golden['generic_fingerprint_unique']} "
        f"census_sha256={golden['generic_fingerprint_census_sha256']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
