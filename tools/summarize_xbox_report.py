"""Print compact interval deltas from an xlle measurement report.

The replay parser stores most telemetry as cumulative samples.  This helper
turns those into per-frame-series interval costs so optimization work can target
the phase that actually matters instead of cumulative boot totals.

Example:

    py tools/summarize_xbox_report.py build/edge-step-v1-12b.report.json
"""

from __future__ import annotations

import argparse
import json
import pathlib
from collections.abc import Mapping
from typing import Any


NS_PER_SEC = 1_000_000_000
BYTES_PER_MIB = 1024 * 1024


def ns_to_s(value: int | float | None) -> str:
    if value is None:
        return "-"
    return f"{float(value) / NS_PER_SEC:.3f}s"


def mib(value: int | float | None) -> str:
    if value is None:
        return "-"
    return f"{float(value) / BYTES_PER_MIB:.1f}MiB"


def delta_dict(current: Mapping[str, Any], previous: Mapping[str, Any] | None) -> dict[str, Any]:
    if previous is None:
        return dict(current)
    out: dict[str, Any] = {}
    for key, value in current.items():
        old = previous.get(key)
        if isinstance(value, (int, float)) and isinstance(old, (int, float)):
            out[key] = value - old
        elif isinstance(value, Mapping) and isinstance(old, Mapping):
            out[key] = delta_dict(value, old)
        else:
            out[key] = value
    return out


def sample_for_deadline(samples: list[dict[str, Any]], index: int) -> dict[str, Any] | None:
    if index < len(samples):
        return samples[index]
    return None


def generic_entry_key(entry: Mapping[str, Any]) -> tuple[str, str]:
    return (str(entry.get("fp", "")), str(entry.get("full", "")))


def generic_family_key(entry: Mapping[str, Any]) -> tuple[Any, ...]:
    return (
        entry.get("surface"),
        entry.get("surface_format"),
        entry.get("pitch"),
        entry.get("primitive"),
        entry.get("enables"),
        entry.get("shader"),
        entry.get("combiner"),
        tuple(entry.get("texture_format", [])),
    )


def generic_top_delta(
    current: Mapping[str, Any] | None,
    previous: Mapping[str, Any] | None,
    limit: int = 3,
) -> str:
    if not current:
        return "-"
    previous_by_key = {
        generic_entry_key(entry): entry
        for entry in previous.get("top", [])  # type: ignore[union-attr]
    } if previous else {}
    deltas: list[tuple[int, Mapping[str, Any], int, int]] = []
    for entry in current.get("top", []):
        if "fp" not in entry:
            continue
        old = previous_by_key.get(generic_entry_key(entry), {})
        ns_delta = int(entry.get("ns", 0)) - int(old.get("ns", 0))
        frags_delta = int(entry.get("frags", 0)) - int(old.get("frags", 0))
        batches_delta = int(entry.get("batches", 0)) - int(old.get("batches", 0))
        if ns_delta <= 0 and frags_delta <= 0 and batches_delta <= 0:
            continue
        deltas.append((ns_delta, entry, frags_delta, batches_delta))
    if not deltas:
        return "-"
    deltas.sort(reverse=True, key=lambda item: item[0])
    parts = []
    for ns_delta, entry, frags_delta, batches_delta in deltas[:limit]:
        fp = str(entry.get("fp", ""))[:8]
        full = str(entry.get("full", ""))[:8]
        parts.append(
            f"{fp}/{full}:b{batches_delta}/f{frags_delta / 1_000_000:.2f}M/"
            f"{ns_to_s(ns_delta)}"
        )
    return ",".join(parts)


def generic_family_delta(
    current: Mapping[str, Any] | None,
    previous: Mapping[str, Any] | None,
    limit: int = 3,
) -> str:
    if not current:
        return "-"
    previous_by_key = {
        generic_family_key(entry): entry
        for entry in previous.get("top", [])  # type: ignore[union-attr]
    } if previous else {}
    deltas: list[tuple[int, Mapping[str, Any], int, int]] = []
    for entry in current.get("top", []):
        if "surface" not in entry:
            continue
        old = previous_by_key.get(generic_family_key(entry), {})
        ns_delta = int(entry.get("ns", 0)) - int(old.get("ns", 0))
        frags_delta = int(entry.get("frags", 0)) - int(old.get("frags", 0))
        batches_delta = int(entry.get("batches", 0)) - int(old.get("batches", 0))
        if ns_delta <= 0 and frags_delta <= 0 and batches_delta <= 0:
            continue
        deltas.append((ns_delta, entry, frags_delta, batches_delta))
    if not deltas:
        return "-"
    deltas.sort(reverse=True, key=lambda item: item[0])
    parts = []
    for ns_delta, entry, frags_delta, batches_delta in deltas[:limit]:
        parts.append(
            f"{entry.get('surface')}/sh{entry.get('shader')}/c{entry.get('combiner')}"
            f"/en{entry.get('enables')}:b{batches_delta}/"
            f"f{frags_delta / 1_000_000:.2f}M/{ns_to_s(ns_delta)}"
        )
    return ",".join(parts)


def summarize(path: pathlib.Path) -> None:
    report = json.loads(path.read_text())
    parsed = report.get("parsed", {})
    frames = parsed.get("frame_series", [])
    perf = parsed.get("perf_samples", [])
    renderer = parsed.get("renderer_samples", [])
    resident = parsed.get("resident_samples", [])
    memory = parsed.get("host_memory_samples", [])
    unicorn_memory = parsed.get("unicorn_memory_samples", [])
    generic_perf = parsed.get("generic_perf_samples", [])
    generic_family = parsed.get("generic_family_samples", [])
    generic_split = parsed.get("generic_split_samples", [])
    final_frame = parsed.get("final_frame", {})
    final_mem = parsed.get("final_host_memory") or {}
    final_unicorn = parsed.get("final_unicorn_memory") or {}

    print(path)
    print(
        "  final "
        f"ok={report.get('ok')} "
        f"fps={final_frame.get('effective_fps', '-')} "
        f"frame={final_frame.get('hash', '-')} "
        f"private={mib(final_mem.get('private_commit'))} "
        f"ws={mib(final_mem.get('working_set'))} "
        f"ucode={final_unicorn.get('code_hook_regions', '-')}"
    )
    if not frames:
        print("  no frame-series samples")
        return

    print("  interval deltas:")
    previous_perf = previous_renderer = previous_resident = previous_memory = None
    previous_generic_perf = None
    previous_generic_family = None
    previous_generic_split = None
    previous_unicorn_memory = None
    for index, frame in enumerate(frames):
        p = sample_for_deadline(perf, index)
        r = sample_for_deadline(renderer, index)
        rs = sample_for_deadline(resident, index)
        m = sample_for_deadline(memory, index)
        ucm = sample_for_deadline(unicorn_memory, index)
        gp = sample_for_deadline(generic_perf, index)
        gf = sample_for_deadline(generic_family, index)
        gs = sample_for_deadline(generic_split, index)
        pd = delta_dict(p, previous_perf) if p else {}
        rd = delta_dict(r, previous_renderer) if r else {}
        rsd = delta_dict(rs, previous_resident) if rs else {}
        md = delta_dict(m, previous_memory) if m else {}
        ucmd = delta_dict(ucm, previous_unicorn_memory) if ucm else {}
        gsd = delta_dict(gs, previous_generic_split) if gs else {}
        dirty_clip = rd.get("dirty_clip_pixels", 0)
        dirty_surface = rd.get("dirty_surface_pixels", 0)
        dirty_pct = (
            f"{(100.0 * dirty_clip / dirty_surface):.1f}%"
            if dirty_surface
            else "-"
        )
        previous_perf = p or previous_perf
        previous_renderer = r or previous_renderer
        previous_resident = rs or previous_resident
        previous_memory = m or previous_memory
        previous_unicorn_memory = ucm or previous_unicorn_memory
        previous_generic_split = gs or previous_generic_split
        generic_delta = generic_top_delta(gp, previous_generic_perf)
        family_delta = generic_family_delta(gf, previous_generic_family)
        previous_generic_perf = gp or previous_generic_perf
        previous_generic_family = gf or previous_generic_family
        print(
            "   "
            f"{frame.get('deadline', '-') / 1_000_000_000:.1f}B "
            f"fps={frame.get('effective_fps', '-')} "
            f"wall={ns_to_s(frame.get('wall_ns'))} "
            f"cpu={ns_to_s(pd.get('cpu_ns'))} "
            f"push={ns_to_s(pd.get('nv2a_pushbuffer_ns'))} "
            f"drain={ns_to_s(pd.get('nv2a_pull_drain_ns'))} "
            f"rast={ns_to_s(pd.get('raster_ns'))} "
            f"gen={ns_to_s(pd.get('raster_generic_ns'))} "
            f"gsetup={ns_to_s(gsd.get('texture_setup_ns'))} "
            f"gsoft={ns_to_s(gsd.get('software_raster_ns'))} "
            f"gflush={ns_to_s(gsd.get('flush_ns'))} "
            f"resolv={ns_to_s(rd.get('resolve_ns'))} "
            f"ensure={ns_to_s(rd.get('resident_ensure_ns'))} "
            f"uploads={rsd.get('uploads', '-')} "
            f"submits={rsd.get('submits', '-')} "
            f"dirty={dirty_pct} "
            f"resolveMP={rd.get('resolve_pixels', 0) / 1_000_000:.1f} "
            f"ucode+={ucmd.get('code_hook_regions', '-')} "
            f"texMB={md.get('generic_texture_bytes', 0) / BYTES_PER_MIB:.1f} "
            f"genTop={generic_delta} "
            f"famTop={family_delta}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=pathlib.Path)
    args = parser.parse_args()
    for index, path in enumerate(args.reports):
        if index:
            print()
        summarize(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
