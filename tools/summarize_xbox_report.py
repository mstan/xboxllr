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


def summarize(path: pathlib.Path) -> None:
    report = json.loads(path.read_text())
    parsed = report.get("parsed", {})
    frames = parsed.get("frame_series", [])
    perf = parsed.get("perf_samples", [])
    renderer = parsed.get("renderer_samples", [])
    resident = parsed.get("resident_samples", [])
    memory = parsed.get("host_memory_samples", [])
    final_frame = parsed.get("final_frame", {})
    final_mem = parsed.get("final_host_memory", {})

    print(path)
    print(
        "  final "
        f"ok={report.get('ok')} "
        f"fps={final_frame.get('effective_fps', '-')} "
        f"frame={final_frame.get('hash', '-')} "
        f"private={mib(final_mem.get('private_commit'))} "
        f"ws={mib(final_mem.get('working_set'))}"
    )
    if not frames:
        print("  no frame-series samples")
        return

    print("  interval deltas:")
    previous_perf = previous_renderer = previous_resident = previous_memory = None
    for index, frame in enumerate(frames):
        p = sample_for_deadline(perf, index)
        r = sample_for_deadline(renderer, index)
        rs = sample_for_deadline(resident, index)
        m = sample_for_deadline(memory, index)
        pd = delta_dict(p, previous_perf) if p else {}
        rd = delta_dict(r, previous_renderer) if r else {}
        rsd = delta_dict(rs, previous_resident) if rs else {}
        md = delta_dict(m, previous_memory) if m else {}
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
        print(
            "   "
            f"{frame.get('deadline', '-') / 1_000_000_000:.1f}B "
            f"fps={frame.get('effective_fps', '-')} "
            f"wall={ns_to_s(frame.get('wall_ns'))} "
            f"cpu={ns_to_s(pd.get('cpu_ns'))} "
            f"push={ns_to_s(pd.get('nv2a_pushbuffer_ns'))} "
            f"rast={ns_to_s(pd.get('raster_ns'))} "
            f"gen={ns_to_s(pd.get('raster_generic_ns'))} "
            f"resolv={ns_to_s(rd.get('resolve_ns'))} "
            f"ensure={ns_to_s(rd.get('resident_ensure_ns'))} "
            f"uploads={rsd.get('uploads', '-')} "
            f"submits={rsd.get('submits', '-')} "
            f"dirty={dirty_pct} "
            f"resolveMP={rd.get('resolve_pixels', 0) / 1_000_000:.1f} "
            f"texMB={md.get('generic_texture_bytes', 0) / BYTES_PER_MIB:.1f}"
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
