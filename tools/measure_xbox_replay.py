"""Summarize an xlle replay log into a stable JSON measurement report.

This intentionally starts as a parser, not a launcher.  Long stock-dashboard
replays are expensive, so the first goal is to make each existing stdout/stderr
pair auditable: exact git/exe provenance, frame-series FPS/hash checkpoints,
final perf buckets, final scanout hash, final image SHA-256, and selected NV2A
counter lines.

Example:

    py tools/measure_xbox_replay.py \
        --name tex-fastpath-v2-10_5b \
        --stdout build/tex-fastpath-v2-10_5b.stdout.log \
        --stderr build/tex-fastpath-v2-10_5b.stderr.log \
        --frame-dump build/tex-fastpath-v2-10_5b-final.ppm \
        --exe build-release/xlle.exe \
        --expect-final-sha B453... \
        --report build/tex-fastpath-v2-10_5b.report.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import subprocess
import sys
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[1]


FRAME_RE = re.compile(
    r"^\[host\] frame-series deadline=(?P<deadline>\d+) "
    r"insns=(?P<insns>\d+) endpoint2=(?P<endpoint2>\d+) "
    r"vblank-delta=(?P<vblank_delta>\d+) wall-ns=(?P<wall_ns>\d+) "
    r"effective-fps=(?P<effective_fps>[0-9.]+) hash=(?P<hash>[0-9a-f]+)"
)

PERF_RE = re.compile(
    r"^\[perf\] "
    r"cpu_ns=(?P<cpu_ns>\d+) \(n=(?P<cpu_n>\d+)\) "
    r"nv2a_pushbuffer_ns=(?P<nv2a_pushbuffer_ns>\d+) "
    r"\(n=(?P<nv2a_pushbuffer_n>\d+)\) "
    r"raster_ns=(?P<raster_ns>\d+) \(n=(?P<raster_n>\d+)\) "
    r"raster_witnessed_ns=(?P<raster_witnessed_ns>\d+) "
    r"raster_generic_ns=(?P<raster_generic_ns>\d+) "
    r"\(n=(?P<raster_generic_n>\d+)\) "
    r"host_poll_devices_ns=(?P<host_poll_devices_ns>\d+) "
    r"\(n=(?P<host_poll_devices_n>\d+)\)"
)

PERF_RESIDENT_RE = re.compile(
    r"^\[perf-resident\] "
    r"candidates=(?P<candidates>\d+) "
    r"supported=(?P<supported>\d+) "
    r"uploads=(?P<uploads>\d+) "
    r"reuses=(?P<reuses>\d+) "
    r"target-switch-resolves=(?P<target_switch_resolves>\d+) "
    r"submits=(?P<submits>\d+) "
    r"rejects=(?P<rejects>.*)$"
)

PERF_TEXTURE_MODULATE_RE = re.compile(
    r"^\[perf-texture-modulate\] "
    r"observed=(?P<observed>\d+) "
    r"exact=(?P<exact>\d+) "
    r"rejects=(?P<rejects>.*)$"
)

HOST_MEMORY_RE = re.compile(
    r"^\[host-memory\] "
    r"working-set=(?P<working_set>\d+) "
    r"private-commit=(?P<private_commit>\d+) "
    r"heap-count=(?P<heap_count>\d+) "
    r"heap-allocated=(?P<heap_allocated>\d+) "
    r"heap-committed=(?P<heap_committed>\d+) "
    r"heap-reserved=(?P<heap_reserved>\d+)"
)

PERF_RENDERER_RE = re.compile(
    r"^\[perf-renderer\] "
    r"resident-ensure-ns=(?P<resident_ensure_ns>\d+) "
    r"\(n=(?P<resident_ensure_n>\d+)\) "
    r"texture-stage-ns=(?P<texture_stage_ns>\d+) "
    r"\(n=(?P<texture_stage_n>\d+)\) "
    r"vertex-upload-ns=(?P<vertex_upload_ns>\d+) "
    r"\(n=(?P<vertex_upload_n>\d+)\) "
    r"draw-ns=(?P<draw_ns>\d+) "
    r"\(n=(?P<draw_n>\d+)\) "
    r"resolve-ns=(?P<resolve_ns>\d+) "
    r"\(n=(?P<resolve_n>\d+)\)"
)

PERF_MEMORY_RE = re.compile(
    r"^\[perf-memory\] "
    r"paged-surface-creations=(?P<paged_surface_creations>\d+) "
    r"(?:(?:direct-surfaces=(?P<direct_surfaces>\d+) "
    r"direct-surface-bytes=(?P<direct_surface_bytes>\d+) )?)"
    r"pages-loaded=(?P<pages_loaded>\d+) "
    r"page-payload-bytes-cumulative=(?P<page_payload_bytes_cumulative>\d+) "
    r"page-payload-bytes-live=(?P<page_payload_bytes_live>\d+) "
    r"page-payload-bytes-peak=(?P<page_payload_bytes_peak>\d+) "
    r"logical-surface-bytes-peak=(?P<logical_surface_bytes_peak>\d+) "
    r"generic-texture-allocations=(?P<generic_texture_allocations>\d+) "
    r"generic-texture-bytes=(?P<generic_texture_bytes>\d+) "
    r"generic-texture-batch-peak=(?P<generic_texture_batch_peak>\d+) "
    r"pfifo-queue-pushes=(?P<pfifo_queue_pushes>\d+) "
    r"pfifo-queue-entries-live=(?P<pfifo_queue_entries_live>\d+) "
    r"pfifo-queue-entries-peak=(?P<pfifo_queue_entries_peak>\d+)"
)

GENERIC_RE = re.compile(
    r"^\[2bl\] NV2A generic-combiner: "
    r"attempted/accelerated/fallback/software/exec/tri/frags/depth-pass/"
    r"writes/tex-samples="
    r"(?P<attempted>\d+)/(?P<accelerated>\d+)/(?P<fallback>\d+)/"
    r"(?P<software>\d+)/(?P<exec>\d+)/(?P<tri>\d+)/(?P<frags>\d+)/"
    r"(?P<depth_pass>\d+)/(?P<writes>\d+)/(?P<tex_samples>\d+) "
    r"last=(?P<last>[0-9a-fA-F]+) hash=(?P<hash>[0-9a-fA-F]+)"
)

SCANOUT_RE = re.compile(
    r"^\[2bl\] NV2A scanout: start=(?P<start>[0-9a-fA-F]+) "
    r"pitch=(?P<pitch>\d+) size=(?P<width>\d+)x(?P<height>\d+) "
    r"bpp=(?P<bpp>\d+) writes/nonzero=(?P<writes>\d+)/(?P<nonzero>\d+) "
    r"last-nonzero=(?P<last_nonzero>[0-9a-fA-F]+) "
    r"rgba-fnv64=(?P<rgba_fnv64>[0-9a-fA-F]+)"
)

RETIRED_RE = re.compile(
    r"^\[2bl\] retired (?P<insns>\d+) instructions; "
    r"final eip=(?P<eip>0x[0-9a-fA-F]+)"
)

OHCI_RE = re.compile(
    r"^\[2bl\] OHCI0: .*xid-endpoint2=(?P<xid_endpoint2>\d+)"
)


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_log_text(path: pathlib.Path) -> str:
    data = path.read_bytes()
    if data.startswith(b"\xff\xfe") or data.startswith(b"\xfe\xff"):
        return data.decode("utf-16", errors="replace")
    if data.startswith(b"\xef\xbb\xbf"):
        return data.decode("utf-8-sig", errors="replace")
    return data.decode("utf-8", errors="replace")


def git_text(args: list[str]) -> str | None:
    try:
        result = subprocess.run(
            ["git", "-C", str(ROOT), *args],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return None
    return result.stdout.strip()


def parse_int_fields(match: re.Match[str]) -> dict[str, Any]:
    parsed: dict[str, Any] = {}
    for key, value in match.groupdict().items():
        if value is None:
            parsed[key] = 0
            continue
        if key.endswith("hash") or key in {
            "hash",
            "last",
            "start",
            "last_nonzero",
            "rgba_fnv64",
            "eip",
            "rejects",
        }:
            parsed[key] = value.lower()
        elif key == "effective_fps":
            parsed[key] = float(value)
        else:
            parsed[key] = int(value)
    return parsed


def parse_rejects(text: str) -> dict[str, int]:
    if text == "none":
        return {}
    rejects: dict[str, int] = {}
    for item in text.split(","):
        if not item:
            continue
        name, sep, count = item.partition(":")
        if not sep:
            rejects[name] = 0
            continue
        try:
            rejects[name] = int(count)
        except ValueError:
            rejects[name] = 0
    return rejects


def parse_counter_sample(match: re.Match[str]) -> dict[str, Any]:
    parsed = parse_int_fields(match)
    if "rejects" in parsed:
        parsed["rejects"] = parse_rejects(match.group("rejects"))
    return parsed


def parse_stdout(path: pathlib.Path) -> dict[str, Any]:
    frames: list[dict[str, Any]] = []
    perf_samples: list[dict[str, Any]] = []
    resident_samples: list[dict[str, Any]] = []
    texture_modulate_samples: list[dict[str, Any]] = []
    host_memory_samples: list[dict[str, Any]] = []
    renderer_samples: list[dict[str, Any]] = []
    perf_memory_samples: list[dict[str, Any]] = []
    generic: dict[str, Any] | None = None
    scanout: dict[str, Any] | None = None
    retired: dict[str, Any] | None = None
    ohci_endpoint2: int | None = None
    selected_renderer: str | None = None
    memory_limit: str | None = None
    generic_fingerprints: str | None = None

    for raw_line in read_log_text(path).splitlines():
        line = raw_line.rstrip("\n")
        if line.startswith("[nv2a-renderer] "):
            selected_renderer = line[len("[nv2a-renderer] ") :]
        elif line.startswith("[host] hard process-memory limit armed: "):
            memory_limit = line[len("[host] ") :]
        elif match := FRAME_RE.match(line):
            frames.append(parse_int_fields(match))
        elif match := PERF_RE.match(line):
            perf_samples.append(parse_int_fields(match))
        elif match := PERF_RESIDENT_RE.match(line):
            resident_samples.append(parse_counter_sample(match))
        elif match := PERF_TEXTURE_MODULATE_RE.match(line):
            texture_modulate_samples.append(parse_counter_sample(match))
        elif match := HOST_MEMORY_RE.match(line):
            host_memory_samples.append(parse_int_fields(match))
        elif match := PERF_RENDERER_RE.match(line):
            renderer_samples.append(parse_int_fields(match))
        elif match := PERF_MEMORY_RE.match(line):
            perf_memory_samples.append(parse_int_fields(match))
        elif match := GENERIC_RE.match(line):
            generic = parse_int_fields(match)
        elif match := SCANOUT_RE.match(line):
            scanout = parse_int_fields(match)
        elif match := RETIRED_RE.match(line):
            retired = parse_int_fields(match)
        elif match := OHCI_RE.match(line):
            ohci_endpoint2 = int(match.group("xid_endpoint2"))
        elif line.startswith("[2bl] NV2A generic-combiner fingerprints: "):
            generic_fingerprints = line

    final_perf = perf_samples[-1] if perf_samples else None
    final_frame = frames[-1] if frames else None
    return {
        "selected_renderer": selected_renderer,
        "memory_limit": memory_limit,
        "frame_series": frames,
        "final_frame": final_frame,
        "perf_samples": perf_samples,
        "final_perf": final_perf,
        "resident_samples": resident_samples,
        "final_resident": resident_samples[-1] if resident_samples else None,
        "texture_modulate_samples": texture_modulate_samples,
        "final_texture_modulate": (
            texture_modulate_samples[-1] if texture_modulate_samples else None
        ),
        "host_memory_samples": host_memory_samples,
        "final_host_memory": host_memory_samples[-1] if host_memory_samples else None,
        "renderer_samples": renderer_samples,
        "final_renderer": renderer_samples[-1] if renderer_samples else None,
        "perf_memory_samples": perf_memory_samples,
        "final_perf_memory": perf_memory_samples[-1] if perf_memory_samples else None,
        "generic_combiner": generic,
        "generic_fingerprints": generic_fingerprints,
        "scanout": scanout,
        "retired": retired,
        "ohci_xid_endpoint2": ohci_endpoint2,
    }


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    stdout_path = args.stdout.resolve()
    stderr_path = args.stderr.resolve() if args.stderr else None
    frame_dump = args.frame_dump.resolve() if args.frame_dump else None
    exe = args.exe.resolve() if args.exe else None

    parsed = parse_stdout(stdout_path)
    stderr_text = ""
    if stderr_path and stderr_path.exists():
        stderr_text = read_log_text(stderr_path)

    report: dict[str, Any] = {
        "schema": "xboxlle.replay.measurement.v1",
        "name": args.name,
        "inputs": {
            "stdout": str(stdout_path),
            "stderr": str(stderr_path) if stderr_path else None,
            "frame_dump": str(frame_dump) if frame_dump else None,
            "exe": str(exe) if exe else None,
            "command": args.command,
        },
        "provenance": {
            "git_head": git_text(["rev-parse", "HEAD"]),
            "git_status_short": git_text(["status", "--short"]),
            "platform": platform.platform(),
            "python": sys.version,
            "exe_sha256": sha256_file(exe) if exe and exe.exists() else None,
        },
        "artifacts": {
            "stdout_sha256": sha256_file(stdout_path),
            "stderr_sha256": (
                sha256_file(stderr_path) if stderr_path and stderr_path.exists() else None
            ),
            "frame_dump_sha256": (
                sha256_file(frame_dump) if frame_dump and frame_dump.exists() else None
            ),
            "stderr_empty": stderr_text == "",
        },
        "parsed": parsed,
        "checks": [],
    }

    checks: list[dict[str, Any]] = report["checks"]

    def add_check(name: str, ok: bool, detail: str) -> None:
        checks.append({"name": name, "ok": ok, "detail": detail})

    if args.expect_final_sha:
        actual = report["artifacts"]["frame_dump_sha256"]
        expect = args.expect_final_sha.lower()
        add_check(
            "final_frame_dump_sha256",
            actual == expect,
            f"actual={actual} expected={expect}",
        )
    if args.expect_scanout_hash:
        actual = None
        if parsed["scanout"]:
            actual = parsed["scanout"]["rgba_fnv64"]
        expect = args.expect_scanout_hash.lower()
        add_check("scanout_fnv64", actual == expect, f"actual={actual} expected={expect}")
    if args.min_final_fps is not None:
        actual = None
        if parsed["final_frame"]:
            actual = parsed["final_frame"]["effective_fps"]
        add_check(
            "min_final_fps",
            actual is not None and actual >= args.min_final_fps,
            f"actual={actual} minimum={args.min_final_fps}",
        )
    if args.require_empty_stderr:
        add_check(
            "stderr_empty",
            report["artifacts"]["stderr_empty"],
            f"empty={report['artifacts']['stderr_empty']}",
        )

    report["ok"] = all(check["ok"] for check in checks)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--stdout", type=pathlib.Path, required=True)
    parser.add_argument("--stderr", type=pathlib.Path)
    parser.add_argument("--frame-dump", type=pathlib.Path)
    parser.add_argument("--exe", type=pathlib.Path)
    parser.add_argument("--command", help="Exact replay command, if known")
    parser.add_argument("--report", type=pathlib.Path, required=True)
    parser.add_argument("--expect-final-sha")
    parser.add_argument("--expect-scanout-hash")
    parser.add_argument("--min-final-fps", type=float)
    parser.add_argument("--require-empty-stderr", action="store_true")
    args = parser.parse_args()

    report = build_report(args)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    tmp = args.report.with_suffix(args.report.suffix + ".partial")
    tmp.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(tmp, args.report)

    print(
        f"wrote {args.report} ok={report['ok']} "
        f"final_fps={report['parsed']['final_frame']['effective_fps'] if report['parsed']['final_frame'] else 'n/a'}"
    )
    for check in report["checks"]:
        print(f"check {check['name']}: {'ok' if check['ok'] else 'FAIL'} {check['detail']}")
    return 0 if report["ok"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
