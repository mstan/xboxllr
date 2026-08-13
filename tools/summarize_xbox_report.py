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


def difference(base: Any, *subtract: Any) -> int | float | None:
    if not isinstance(base, (int, float)):
        return None
    total = base
    for value in subtract:
        if not isinstance(value, (int, float)):
            return None
        total -= value
    return total


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
        color_reads_delta = int(entry.get("color_reads", 0)) - int(
            old.get("color_reads", 0)
        )
        depth_reads_delta = int(entry.get("depth_reads", 0)) - int(
            old.get("depth_reads", 0)
        )
        batches_delta = int(entry.get("batches", 0)) - int(old.get("batches", 0))
        if ns_delta <= 0 and frags_delta <= 0 and batches_delta <= 0:
            continue
        deltas.append(
            (ns_delta, entry, frags_delta, batches_delta, color_reads_delta, depth_reads_delta)
        )
    if not deltas:
        return "-"
    deltas.sort(reverse=True, key=lambda item: item[0])
    parts = []
    for (
        ns_delta,
        entry,
        frags_delta,
        batches_delta,
        color_reads_delta,
        depth_reads_delta,
    ) in deltas[:limit]:
        fp = str(entry.get("fp", ""))[:8]
        full = str(entry.get("full", ""))[:8]
        parts.append(
            f"{fp}/{full}:b{batches_delta}/f{frags_delta / 1_000_000:.2f}M/"
            f"cr{color_reads_delta / 1_000_000:.2f}M/"
            f"dr{depth_reads_delta / 1_000_000:.2f}M/"
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
        color_reads_delta = int(entry.get("color_reads", 0)) - int(
            old.get("color_reads", 0)
        )
        depth_reads_delta = int(entry.get("depth_reads", 0)) - int(
            old.get("depth_reads", 0)
        )
        batches_delta = int(entry.get("batches", 0)) - int(old.get("batches", 0))
        if ns_delta <= 0 and frags_delta <= 0 and batches_delta <= 0:
            continue
        deltas.append(
            (ns_delta, entry, frags_delta, batches_delta, color_reads_delta, depth_reads_delta)
        )
    if not deltas:
        return "-"
    deltas.sort(reverse=True, key=lambda item: item[0])
    parts = []
    for (
        ns_delta,
        entry,
        frags_delta,
        batches_delta,
        color_reads_delta,
        depth_reads_delta,
    ) in deltas[:limit]:
        parts.append(
            f"{entry.get('surface')}/sh{entry.get('shader')}/c{entry.get('combiner')}"
            f"/en{entry.get('enables')}:b{batches_delta}/"
            f"f{frags_delta / 1_000_000:.2f}M/"
            f"cr{color_reads_delta / 1_000_000:.2f}M/"
            f"dr{depth_reads_delta / 1_000_000:.2f}M/{ns_to_s(ns_delta)}"
        )
    return ",".join(parts)


def resolve_context_delta(
    current: Mapping[str, Any] | None,
    previous: Mapping[str, Any] | None,
    limit: int = 3,
) -> str:
    if not current:
        return "-"
    previous = previous or {}
    deltas: list[tuple[int, str, int]] = []
    for name, values in current.items():
        if not isinstance(values, Mapping):
            continue
        old = previous.get(name, {})
        if not isinstance(old, Mapping):
            old = {}
        bytes_delta = int(values.get("bytes", 0)) - int(old.get("bytes", 0))
        calls_delta = int(values.get("calls", 0)) - int(old.get("calls", 0))
        if bytes_delta <= 0 and calls_delta <= 0:
            continue
        deltas.append((bytes_delta, str(name), calls_delta))
    if not deltas:
        return "-"
    deltas.sort(reverse=True)
    parts = []
    for bytes_delta, name, calls_delta in deltas[:limit]:
        parts.append(f"{name}:n{calls_delta}/{mib(bytes_delta)}")
    return ",".join(parts)


def vsh_program_delta(
    current: list[Mapping[str, Any]] | None,
    previous: list[Mapping[str, Any]] | None,
    limit: int = 2,
) -> str:
    if not current:
        return "-"
    previous_by_hash = {
        str(entry.get("hash", "")): entry
        for entry in previous or []
    }
    deltas: list[tuple[int, Mapping[str, Any], int, int]] = []
    for entry in current:
        key = str(entry.get("hash", ""))
        old = previous_by_hash.get(key, {})
        vertices_delta = int(entry.get("vertices", 0)) - int(old.get("vertices", 0))
        steps_delta = int(entry.get("step_invocations", 0)) - int(
            old.get("step_invocations", 0)
        )
        batches_delta = int(entry.get("batches", 0)) - int(old.get("batches", 0))
        if vertices_delta <= 0 and steps_delta <= 0 and batches_delta <= 0:
            continue
        deltas.append((vertices_delta, entry, steps_delta, batches_delta))
    if not deltas:
        return "-"
    deltas.sort(reverse=True, key=lambda item: item[0])
    parts = []
    for vertices_delta, entry, steps_delta, batches_delta in deltas[:limit]:
        parts.append(
            f"{str(entry.get('hash', ''))[:8]}:b{batches_delta}/"
            f"v{vertices_delta / 1_000_000:.2f}M/"
            f"steps{steps_delta / 1_000_000:.2f}M/"
            f"maxspv{entry.get('max_steps_per_vertex', '-')}"
        )
    return ",".join(parts)


def vertex_reuse_delta(
    current: list[Mapping[str, Any]] | None,
    previous: list[Mapping[str, Any]] | None,
    limit: int = 2,
) -> str:
    if not current:
        return "-"
    previous_by_hash = {
        str(entry.get("hash", "")): entry
        for entry in previous or []
    }
    deltas: list[tuple[int, Mapping[str, Any], int, int]] = []
    for entry in current:
        key = str(entry.get("hash", ""))
        old = previous_by_hash.get(key, {})
        vertices_delta = int(entry.get("vertices", 0)) - int(old.get("vertices", 0))
        lru_delta = int(entry.get("lru24_hits", 0)) - int(old.get("lru24_hits", 0))
        dup_delta = int(entry.get("exact_duplicate_vertices", 0)) - int(
            old.get("exact_duplicate_vertices", 0)
        )
        if vertices_delta <= 0 and lru_delta <= 0 and dup_delta <= 0:
            continue
        deltas.append((vertices_delta, entry, lru_delta, dup_delta))
    if not deltas:
        return "-"
    deltas.sort(reverse=True, key=lambda item: item[0])
    parts = []
    for vertices_delta, entry, lru_delta, dup_delta in deltas[:limit]:
        hit_pct = 100.0 * lru_delta / vertices_delta if vertices_delta else 0.0
        dup_pct = 100.0 * dup_delta / vertices_delta if vertices_delta else 0.0
        parts.append(
            f"{str(entry.get('hash', ''))[:8]}:"
            f"v{vertices_delta / 1_000_000:.2f}M/"
            f"lru{lru_delta / 1_000_000:.2f}M({hit_pct:.1f}%)/"
            f"dup{dup_delta / 1_000_000:.2f}M({dup_pct:.1f}%)/"
            f"maxv{entry.get('max_vertices_per_batch', '-')}"
        )
    return ",".join(parts)


def summarize(path: pathlib.Path) -> None:
    report = json.loads(path.read_text())
    parsed = report.get("parsed", {})
    frames = parsed.get("frame_series", [])
    perf = parsed.get("perf_samples", [])
    renderer = parsed.get("renderer_samples", [])
    renderer_resolve_context = parsed.get("renderer_resolve_context_samples", [])
    resident = parsed.get("resident_samples", [])
    memory = parsed.get("host_memory_samples", [])
    unicorn_memory = parsed.get("unicorn_memory_samples", [])
    generic_perf = parsed.get("generic_perf_samples", [])
    generic_family = parsed.get("generic_family_samples", [])
    generic_split = parsed.get("generic_split_samples", [])
    generic_detail = parsed.get("generic_detail_samples", [])
    generic_admission = parsed.get("generic_admission_samples", [])
    generic_texture_detail = parsed.get("generic_texture_detail_samples", [])
    nv2a_split = parsed.get("nv2a_split_samples", [])
    vsh_programs = parsed.get("vsh_program_samples", [])
    vertex_reuse_samples = parsed.get("vertex_reuse_samples", [])
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
    previous_renderer_resolve_context = None
    previous_generic_perf = None
    previous_generic_family = None
    previous_generic_split = None
    previous_generic_detail = None
    previous_generic_admission = None
    previous_generic_texture_detail = None
    previous_nv2a_split = None
    previous_vsh_programs = None
    previous_vertex_reuse = None
    previous_unicorn_memory = None
    for index, frame in enumerate(frames):
        p = sample_for_deadline(perf, index)
        r = sample_for_deadline(renderer, index)
        rc = sample_for_deadline(renderer_resolve_context, index)
        rs = sample_for_deadline(resident, index)
        m = sample_for_deadline(memory, index)
        ucm = sample_for_deadline(unicorn_memory, index)
        gp = sample_for_deadline(generic_perf, index)
        gf = sample_for_deadline(generic_family, index)
        gs = sample_for_deadline(generic_split, index)
        gd = sample_for_deadline(generic_detail, index)
        ga = sample_for_deadline(generic_admission, index)
        gt = sample_for_deadline(generic_texture_detail, index)
        ns = sample_for_deadline(nv2a_split, index)
        vsh = sample_for_deadline(vsh_programs, index)
        vr = sample_for_deadline(vertex_reuse_samples, index)
        pd = delta_dict(p, previous_perf) if p else {}
        rd = delta_dict(r, previous_renderer) if r else {}
        rsd = delta_dict(rs, previous_resident) if rs else {}
        md = delta_dict(m, previous_memory) if m else {}
        ucmd = delta_dict(ucm, previous_unicorn_memory) if ucm else {}
        gsd = delta_dict(gs, previous_generic_split) if gs else {}
        gdd = delta_dict(gd, previous_generic_detail) if gd else {}
        gad = delta_dict(ga, previous_generic_admission) if ga else {}
        gtd = delta_dict(gt, previous_generic_texture_detail) if gt else {}
        nsd = delta_dict(ns, previous_nv2a_split) if ns else {}
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
        previous_generic_detail = gd or previous_generic_detail
        previous_generic_admission = ga or previous_generic_admission
        previous_generic_texture_detail = gt or previous_generic_texture_detail
        previous_nv2a_split = ns or previous_nv2a_split
        raster_witnessed_est = difference(
            pd.get("raster_ns"), pd.get("raster_generic_ns")
        )
        generic_other_est = difference(
            pd.get("raster_generic_ns"),
            gsd.get("texture_setup_ns"),
            gsd.get("software_raster_ns"),
            gsd.get("flush_ns"),
        )
        generic_detail_other_est = difference(
            generic_other_est,
            gdd.get("state_setup_ns"),
            gdd.get("accel_path_ns"),
            gdd.get("fallback_setup_ns"),
        )
        generic_admission_other_est = difference(
            gdd.get("accel_path_ns"),
            gad.get("resident_support_ns"),
            gad.get("resident_prepare_ns"),
            gad.get("resident_submit_ns"),
            gad.get("nonresident_support_ns"),
            gad.get("nonresident_snapshot_ns"),
            gad.get("nonresident_submit_ns"),
            gad.get("nonresident_writeback_ns"),
        )
        generic_fallback_other_est = difference(
            gdd.get("fallback_setup_ns"),
            gad.get("fallback_color_resolve_ns"),
            gad.get("fallback_depth_resolve_ns"),
        )
        generic_texture_other_est = difference(
            gsd.get("texture_setup_ns"),
            gtd.get("texture_resolve_ns"),
            gtd.get("texture_dma_read_ns"),
            gtd.get("texture_decode_ns"),
            gtd.get("texture_swizzle_lookup_ns"),
        )
        submit_self_est = difference(
            nsd.get("submit_vertex_ns"),
            nsd.get("vertex_transform_ns"),
            pd.get("raster_ns"),
        )
        dispatch_self_est = difference(
            nsd.get("dispatch_method_ns"), nsd.get("submit_vertex_ns")
        )
        generic_delta = generic_top_delta(gp, previous_generic_perf)
        family_delta = generic_family_delta(gf, previous_generic_family)
        vsh_delta = vsh_program_delta(vsh, previous_vsh_programs)
        vertex_reuse_summary = vertex_reuse_delta(vr, previous_vertex_reuse)
        resolve_delta = resolve_context_delta(rc, previous_renderer_resolve_context)
        previous_renderer_resolve_context = rc or previous_renderer_resolve_context
        previous_generic_perf = gp or previous_generic_perf
        previous_generic_family = gf or previous_generic_family
        previous_vsh_programs = vsh or previous_vsh_programs
        previous_vertex_reuse = vr or previous_vertex_reuse
        print(
            "   "
            f"{frame.get('deadline', '-') / 1_000_000_000:.1f}B "
            f"fps={frame.get('effective_fps', '-')} "
            f"wall={ns_to_s(frame.get('wall_ns'))} "
            f"cpu={ns_to_s(pd.get('cpu_ns'))} "
            f"push={ns_to_s(pd.get('nv2a_pushbuffer_ns'))} "
            f"drain={ns_to_s(pd.get('nv2a_pull_drain_ns'))} "
            f"rast={ns_to_s(pd.get('raster_ns'))} "
            f"rwit={ns_to_s(raster_witnessed_est)} "
            f"gen={ns_to_s(pd.get('raster_generic_ns'))} "
            f"gsetup={ns_to_s(gsd.get('texture_setup_ns'))} "
            f"gtres={ns_to_s(gtd.get('texture_resolve_ns'))} "
            f"gtrd={ns_to_s(gtd.get('texture_dma_read_ns'))} "
            f"gtdec={ns_to_s(gtd.get('texture_decode_ns'))} "
            f"gtswz={ns_to_s(gtd.get('texture_swizzle_lookup_ns'))} "
            f"gtother~={ns_to_s(generic_texture_other_est)} "
            f"gsoft={ns_to_s(gsd.get('software_raster_ns'))} "
            f"gflush={ns_to_s(gsd.get('flush_ns'))} "
            f"gother~={ns_to_s(generic_other_est)} "
            f"gstate={ns_to_s(gdd.get('state_setup_ns'))} "
            f"gaccel={ns_to_s(gdd.get('accel_path_ns'))} "
            f"gfbsetup={ns_to_s(gdd.get('fallback_setup_ns'))} "
            f"gother2~={ns_to_s(generic_detail_other_est)} "
            f"grsup={ns_to_s(gad.get('resident_support_ns'))} "
            f"grprep={ns_to_s(gad.get('resident_prepare_ns'))} "
            f"grsub={ns_to_s(gad.get('resident_submit_ns'))} "
            f"gnsup={ns_to_s(gad.get('nonresident_support_ns'))} "
            f"gnsnap={ns_to_s(gad.get('nonresident_snapshot_ns'))} "
            f"gnsub={ns_to_s(gad.get('nonresident_submit_ns'))} "
            f"gnwb={ns_to_s(gad.get('nonresident_writeback_ns'))} "
            f"gaother~={ns_to_s(generic_admission_other_est)} "
            f"gfcol={ns_to_s(gad.get('fallback_color_resolve_ns'))} "
            f"gfz={ns_to_s(gad.get('fallback_depth_resolve_ns'))} "
            f"gfother~={ns_to_s(generic_fallback_other_est)} "
            f"dispatch={ns_to_s(nsd.get('dispatch_method_ns'))} "
            f"dispself~={ns_to_s(dispatch_self_est)} "
            f"submit={ns_to_s(nsd.get('submit_vertex_ns'))} "
            f"subself~={ns_to_s(submit_self_est)} "
            f"xform={ns_to_s(nsd.get('vertex_transform_ns'))} "
            f"rtex={ns_to_s(rd.get('texture_stage_ns'))} "
            f"rvup={ns_to_s(rd.get('vertex_upload_ns'))} "
            f"rdraw={ns_to_s(rd.get('draw_ns'))} "
            f"resolv={ns_to_s(rd.get('resolve_ns'))} "
            f"ensure={ns_to_s(rd.get('resident_ensure_ns'))} "
            f"uploads={rsd.get('uploads', '-')} "
            f"submits={rsd.get('submits', '-')} "
            f"dirty={dirty_pct} "
            f"resolveMP={rd.get('resolve_pixels', 0) / 1_000_000:.1f} "
            f"ucode+={ucmd.get('code_hook_regions', '-')} "
            f"texMB={md.get('generic_texture_bytes', 0) / BYTES_PER_MIB:.1f} "
            f"vshTop={vsh_delta} "
            f"vReuse={vertex_reuse_summary} "
            f"resCtx={resolve_delta} "
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
