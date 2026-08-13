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
    r"(?:nv2a_pull_drain_ns=(?P<nv2a_pull_drain_ns>\d+) "
    r"\(n=(?P<nv2a_pull_drain_n>\d+)\) )?"
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

UNICORN_MEMORY_RE = re.compile(
    r"^\[unicorn-memory\] "
    r"code-hook-regions=(?P<code_hook_regions>\d+) "
    r"count-hook-regions=(?P<count_hook_regions>\d+)"
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
    r"(?: dirty-clip-pixels=(?P<dirty_clip_pixels>\d+) "
    r"dirty-surface-pixels=(?P<dirty_surface_pixels>\d+) "
    r"resolve-pixels=(?P<resolve_pixels>\d+))?"
)

PERF_RENDERER_RESIDENT_DETAIL_RE = re.compile(
    r"^\[perf-renderer-resident-detail\] "
    r"context-ns=(?P<resident_context_ns>\d+) "
    r"\(n=(?P<resident_context_n>\d+)\) "
    r"vertex-build-ns=(?P<resident_vertex_build_ns>\d+) "
    r"\(n=(?P<resident_vertex_build_n>\d+)\) "
    r"state-setup-ns=(?P<resident_state_setup_ns>\d+) "
    r"\(n=(?P<resident_state_setup_n>\d+)\) "
    r"uniforms-ns=(?P<resident_uniforms_ns>\d+) "
    r"\(n=(?P<resident_uniforms_n>\d+)\) "
    r"attribs-ns=(?P<resident_attribs_ns>\d+) "
    r"\(n=(?P<resident_attribs_n>\d+)\) "
    r"cleanup-ns=(?P<resident_cleanup_ns>\d+) "
    r"\(n=(?P<resident_cleanup_n>\d+)\) "
    r"error-check-ns=(?P<resident_error_check_ns>\d+) "
    r"\(n=(?P<resident_error_check_n>\d+)\)"
)

PERF_RENDERER_RESOLVE_CONTEXT_RE = re.compile(
    r"^\[perf-renderer-resolve-context\] top=(?P<top>.*)$"
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

PERF_GENERIC_SPLIT_RE = re.compile(
    r"^\[perf-generic-split\] "
    r"texture-setup-ns=(?P<texture_setup_ns>\d+) "
    r"\(n=(?P<texture_setup_n>\d+)\) "
    r"software-raster-ns=(?P<software_raster_ns>\d+) "
    r"\(n=(?P<software_raster_n>\d+)\) "
    r"flush-ns=(?P<flush_ns>\d+) "
    r"\(n=(?P<flush_n>\d+)\)"
)

PERF_GENERIC_DETAIL_RE = re.compile(
    r"^\[perf-generic-detail\] "
    r"state-setup-ns=(?P<state_setup_ns>\d+) "
    r"\(n=(?P<state_setup_n>\d+)\) "
    r"accel-path-ns=(?P<accel_path_ns>\d+) "
    r"\(n=(?P<accel_path_n>\d+)\) "
    r"fallback-setup-ns=(?P<fallback_setup_ns>\d+) "
    r"\(n=(?P<fallback_setup_n>\d+)\)"
)

PERF_GENERIC_ADMISSION_RE = re.compile(
    r"^\[perf-generic-admission\] "
    r"resident-support-ns=(?P<resident_support_ns>\d+) "
    r"\(n=(?P<resident_support_n>\d+)\) "
    r"resident-prepare-ns=(?P<resident_prepare_ns>\d+) "
    r"\(n=(?P<resident_prepare_n>\d+)\) "
    r"resident-submit-ns=(?P<resident_submit_ns>\d+) "
    r"\(n=(?P<resident_submit_n>\d+)\) "
    r"nonresident-support-ns=(?P<nonresident_support_ns>\d+) "
    r"\(n=(?P<nonresident_support_n>\d+)\) "
    r"nonresident-snapshot-ns=(?P<nonresident_snapshot_ns>\d+) "
    r"\(n=(?P<nonresident_snapshot_n>\d+)\) "
    r"nonresident-submit-ns=(?P<nonresident_submit_ns>\d+) "
    r"\(n=(?P<nonresident_submit_n>\d+)\) "
    r"nonresident-writeback-ns=(?P<nonresident_writeback_ns>\d+) "
    r"\(n=(?P<nonresident_writeback_n>\d+)\) "
    r"fallback-color-resolve-ns=(?P<fallback_color_resolve_ns>\d+) "
    r"\(n=(?P<fallback_color_resolve_n>\d+)\) "
    r"fallback-depth-resolve-ns=(?P<fallback_depth_resolve_ns>\d+) "
    r"\(n=(?P<fallback_depth_resolve_n>\d+)\)"
)

PERF_GENERIC_TEXTURE_DETAIL_RE = re.compile(
    r"^\[perf-generic-texture-detail\] "
    r"resolve-ns=(?P<texture_resolve_ns>\d+) "
    r"\(n=(?P<texture_resolve_n>\d+)\) "
    r"dma-read-ns=(?P<texture_dma_read_ns>\d+) "
    r"\(n=(?P<texture_dma_read_n>\d+)\) "
    r"decode-ns=(?P<texture_decode_ns>\d+) "
    r"\(n=(?P<texture_decode_n>\d+)\) "
    r"swizzle-lookup-ns=(?P<texture_swizzle_lookup_ns>\d+) "
    r"\(n=(?P<texture_swizzle_lookup_n>\d+)\)"
)

PERF_NV2A_SPLIT_RE = re.compile(
    r"^\[perf-nv2a-split\] "
    r"dispatch-method-ns=(?P<dispatch_method_ns>\d+) "
    r"\(n=(?P<dispatch_method_n>\d+)\) "
    r"submit-vertex-ns=(?P<submit_vertex_ns>\d+) "
    r"\(n=(?P<submit_vertex_n>\d+)\) "
    r"vertex-transform-ns=(?P<vertex_transform_ns>\d+) "
    r"\(n=(?P<vertex_transform_n>\d+)\)"
)

PERF_VSH_PROGRAM_RE = re.compile(r"^\[perf-vsh-program\] top=(?P<top>.*)$")

PERF_VERTEX_REUSE_RE = re.compile(r"^\[perf-vertex-reuse\] top=(?P<top>.*)$")

PERF_WITNESSED_PROFILE_RE = re.compile(r"^\[perf-witnessed-profile\] top=(?P<top>.*)$")

PERF_GENERIC_RE = re.compile(
    r"^\[perf-generic\] fingerprints=(?P<fingerprints>\d+) "
    r"top(?P<top_n>\d+)=(?P<top>.*)$"
)

PERF_GENERIC_FAMILY_RE = re.compile(
    r"^\[perf-generic-family\] families=(?P<families>\d+) "
    r"top(?P<top_n>\d+)=(?P<top>.*)$"
)

GENERIC_RE = re.compile(
    r"^\[2bl\] NV2A generic-combiner: "
    r"attempted/accelerated/fallback/software/exec/tri/frags/depth-pass/"
    r"writes/(?:color-reads/depth-reads/)?tex-samples="
    r"(?P<attempted>\d+)/(?P<accelerated>\d+)/(?P<fallback>\d+)/"
    r"(?P<software>\d+)/(?P<exec>\d+)/(?P<tri>\d+)/(?P<frags>\d+)/"
    r"(?P<depth_pass>\d+)/(?P<writes>\d+)/"
    r"(?:(?P<color_reads>\d+)/(?P<depth_reads>\d+)/)?"
    r"(?P<tex_samples>\d+) "
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


def parse_witnessed_profiles(text: str) -> list[dict[str, Any]]:
    if text == "none":
        return []
    profiles: list[dict[str, Any]] = []
    for item in text.split():
        match = re.match(r"^(?P<name>[^:]+):ns(?P<ns>\d+)/n(?P<calls>\d+)$", item)
        if not match:
            continue
        profiles.append(
            {
                "name": match.group("name"),
                "ns": int(match.group("ns")),
                "calls": int(match.group("calls")),
            }
        )
    return profiles


def parse_renderer_resolve_contexts(text: str) -> dict[str, dict[str, int]]:
    if text == "none":
        return {}
    contexts: dict[str, dict[str, int]] = {}
    for item in text.split():
        match = re.match(r"^(?P<name>[^:]+):n(?P<calls>\d+)/bytes(?P<bytes>\d+)$", item)
        if not match:
            continue
        contexts[match.group("name")] = {
            "calls": int(match.group("calls")),
            "bytes": int(match.group("bytes")),
        }
    return contexts


def parse_vsh_programs(text: str) -> list[dict[str, Any]]:
    if text == "none":
        return []
    programs: list[dict[str, Any]] = []
    for item in text.split():
        fields = item.split("/")
        if len(fields) < 5:
            programs.append({"raw": item})
            continue
        hash_text, sep, batches_text = fields[0].partition(":")
        if not sep:
            programs.append({"raw": item})
            continue
        try:
            program: dict[str, Any] = {
                "hash": hash_text.lower(),
                "batches": int(batches_text.removeprefix("b")),
                "vertices": int(fields[1].removeprefix("v")),
                "step_invocations": int(fields[2].removeprefix("steps")),
                "max_steps_per_vertex": int(fields[3].removeprefix("maxspv")),
                "context_writing_batches": int(fields[4].removeprefix("ctx")),
                "opcodes": {},
            }
            for opcode_field in fields[5:]:
                for opcode_name in (
                    "nop",
                    "mov",
                    "mul",
                    "add",
                    "mad",
                    "dp3",
                    "dph",
                    "dp4",
                    "dst",
                    "min",
                    "max",
                    "slt",
                    "sge",
                    "arl",
                    "rcp",
                    "rcc",
                    "rsq",
                    "exp",
                    "log",
                    "lit",
                ):
                    if opcode_field.startswith(opcode_name):
                        program["opcodes"][opcode_name] = int(
                            opcode_field[len(opcode_name) :]
                        )
                        break
            programs.append(program)
        except ValueError:
            programs.append({"raw": item})
    return programs


def parse_vertex_reuse_programs(text: str) -> list[dict[str, Any]]:
    if text == "none":
        return []
    programs: list[dict[str, Any]] = []
    for item in text.split():
        fields = item.split("/")
        if len(fields) < 5:
            programs.append({"raw": item})
            continue
        hash_text, sep, batches_text = fields[0].partition(":")
        if not sep:
            programs.append({"raw": item})
            continue
        try:
            programs.append(
                {
                    "hash": hash_text.lower(),
                    "batches": int(batches_text.removeprefix("b")),
                    "vertices": int(fields[1].removeprefix("v")),
                    "lru24_hits": int(fields[2].removeprefix("lru24")),
                    "exact_duplicate_vertices": int(fields[3].removeprefix("dup")),
                    "max_vertices_per_batch": int(fields[4].removeprefix("maxv")),
                }
            )
        except ValueError:
            programs.append({"raw": item})
    return programs


GENERIC_PERF_ITEM_RE = re.compile(
    r"^(?P<fp>[0-9a-f]+):"
    r"b(?P<batches>\d+)/f(?P<frags>\d+)/w(?P<writes>\d+)/"
    r"(?:cr(?P<color_reads>\d+)/dr(?P<depth_reads>\d+)/)?"
    r"ns(?P<ns>\d+)/bb(?P<bbox_pixels>\d+)/"
    r"maxf(?P<max_frags>\d+)/maxbb(?P<max_bbox_pixels>\d+):"
    r"prim(?P<primitive>\d+)/v(?P<min_vertices>\d+)-(?P<max_vertices>\d+)/"
    r"en(?P<enables>[0-9a-f]+)/sfmt(?P<surface_format>[0-9a-f]+)/"
    r"pitch(?P<pitch>[0-9a-f]+)/sh(?P<shader>[0-9a-f]+)/"
    r"comb(?P<combiner>[0-9a-f]+)/surf(?P<surface>[0-9a-f]+)/"
    r"full(?P<full>[0-9a-f]+)(?P<detail>.*)$"
)

GENERIC_FAMILY_ITEM_RE = re.compile(
    r"^surf(?P<surface>[0-9a-f]+)/sfmt(?P<surface_format>[0-9a-f]+)/"
    r"pitch(?P<pitch>[0-9a-f]+)/prim(?P<primitive>\d+)/"
    r"en(?P<enables>[0-9a-f]+)/sh(?P<shader>[0-9a-f]+)/"
    r"comb(?P<combiner>[0-9a-f]+)/"
    r"tf(?P<texture_format0>[0-9a-f]+),(?P<texture_format1>[0-9a-f]+),"
    r"(?P<texture_format2>[0-9a-f]+),(?P<texture_format3>[0-9a-f]+):"
    r"fp(?P<fingerprints>\d+)/b(?P<batches>\d+)/f(?P<frags>\d+)/"
    r"w(?P<writes>\d+)/"
    r"(?:cr(?P<color_reads>\d+)/dr(?P<depth_reads>\d+)/)?"
    r"ns(?P<ns>\d+)/bb(?P<bbox_pixels>\d+)/"
    r"(?:setup(?P<texture_setup_ns>\d+)/soft(?P<software_raster_ns>\d+)/"
    r"flush(?P<flush_ns>\d+)/)?"
    r"v(?P<min_vertices>\d+)-(?P<max_vertices>\d+)(?P<detail>.*)$"
)


def parse_generic_perf(match: re.Match[str]) -> dict[str, Any]:
    entries: list[dict[str, Any]] = []
    top_text = match.group("top").strip()
    if top_text:
        for item in top_text.split():
            item_match = GENERIC_PERF_ITEM_RE.match(item)
            if not item_match:
                entries.append({"raw": item})
                continue
            groups = item_match.groupdict()
            parsed = {
                "fp": groups["fp"],
                "full": groups["full"],
                "batches": int(groups["batches"]),
                "frags": int(groups["frags"]),
                "writes": int(groups["writes"]),
                "color_reads": int(groups["color_reads"] or 0),
                "depth_reads": int(groups["depth_reads"] or 0),
                "ns": int(groups["ns"]),
                "bbox_pixels": int(groups["bbox_pixels"]),
                "max_frags": int(groups["max_frags"]),
                "max_bbox_pixels": int(groups["max_bbox_pixels"]),
                "primitive": int(groups["primitive"]),
                "min_vertices": int(groups["min_vertices"]),
                "max_vertices": int(groups["max_vertices"]),
                "enables": groups["enables"],
                "surface_format": groups["surface_format"],
                "pitch": groups["pitch"],
                "shader": groups["shader"],
                "combiner": groups["combiner"],
                "surface": groups["surface"],
                "detail": groups["detail"].lstrip(":"),
            }
            entries.append(parsed)
    return {
        "fingerprints": int(match.group("fingerprints")),
        "top_n": int(match.group("top_n")),
        "top": entries,
    }


def parse_generic_family_perf(match: re.Match[str]) -> dict[str, Any]:
    entries: list[dict[str, Any]] = []
    top_text = match.group("top").strip()
    if top_text:
        for item in top_text.split():
            item_match = GENERIC_FAMILY_ITEM_RE.match(item)
            if not item_match:
                entries.append({"raw": item})
                continue
            groups = item_match.groupdict()
            entries.append(
                {
                    "surface": groups["surface"],
                    "surface_format": groups["surface_format"],
                    "pitch": groups["pitch"],
                    "primitive": int(groups["primitive"]),
                    "enables": groups["enables"],
                    "shader": groups["shader"],
                    "combiner": groups["combiner"],
                    "texture_format": [
                        groups["texture_format0"],
                        groups["texture_format1"],
                        groups["texture_format2"],
                        groups["texture_format3"],
                    ],
                    "fingerprints": int(groups["fingerprints"]),
                    "batches": int(groups["batches"]),
                    "frags": int(groups["frags"]),
                    "writes": int(groups["writes"]),
                    "color_reads": int(groups["color_reads"] or 0),
                    "depth_reads": int(groups["depth_reads"] or 0),
                    "ns": int(groups["ns"]),
                    "bbox_pixels": int(groups["bbox_pixels"]),
                    "texture_setup_ns": int(groups["texture_setup_ns"] or 0),
                    "software_raster_ns": int(groups["software_raster_ns"] or 0),
                    "flush_ns": int(groups["flush_ns"] or 0),
                    "min_vertices": int(groups["min_vertices"]),
                    "max_vertices": int(groups["max_vertices"]),
                    "detail": groups["detail"].lstrip("/"),
                }
            )
    return {
        "families": int(match.group("families")),
        "top_n": int(match.group("top_n")),
        "top": entries,
    }


def parse_stdout(path: pathlib.Path) -> dict[str, Any]:
    frames: list[dict[str, Any]] = []
    perf_samples: list[dict[str, Any]] = []
    resident_samples: list[dict[str, Any]] = []
    texture_modulate_samples: list[dict[str, Any]] = []
    host_memory_samples: list[dict[str, Any]] = []
    unicorn_memory_samples: list[dict[str, Any]] = []
    renderer_samples: list[dict[str, Any]] = []
    renderer_resident_detail_samples: list[dict[str, Any]] = []
    renderer_resolve_context_samples: list[dict[str, dict[str, int]]] = []
    perf_memory_samples: list[dict[str, Any]] = []
    generic_split_samples: list[dict[str, Any]] = []
    generic_detail_samples: list[dict[str, Any]] = []
    generic_admission_samples: list[dict[str, Any]] = []
    generic_texture_detail_samples: list[dict[str, Any]] = []
    nv2a_split_samples: list[dict[str, Any]] = []
    vsh_program_samples: list[list[dict[str, Any]]] = []
    vertex_reuse_samples: list[list[dict[str, Any]]] = []
    witnessed_profile_samples: list[list[dict[str, Any]]] = []
    generic_perf_samples: list[dict[str, Any]] = []
    generic_family_samples: list[dict[str, Any]] = []
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
        elif match := UNICORN_MEMORY_RE.match(line):
            unicorn_memory_samples.append(parse_int_fields(match))
        elif match := PERF_RENDERER_RE.match(line):
            renderer_samples.append(parse_int_fields(match))
        elif match := PERF_RENDERER_RESIDENT_DETAIL_RE.match(line):
            renderer_resident_detail_samples.append(parse_int_fields(match))
        elif match := PERF_RENDERER_RESOLVE_CONTEXT_RE.match(line):
            renderer_resolve_context_samples.append(
                parse_renderer_resolve_contexts(match.group("top"))
            )
        elif match := PERF_MEMORY_RE.match(line):
            perf_memory_samples.append(parse_int_fields(match))
        elif match := PERF_GENERIC_SPLIT_RE.match(line):
            generic_split_samples.append(parse_int_fields(match))
        elif match := PERF_GENERIC_DETAIL_RE.match(line):
            generic_detail_samples.append(parse_int_fields(match))
        elif match := PERF_GENERIC_ADMISSION_RE.match(line):
            generic_admission_samples.append(parse_int_fields(match))
        elif match := PERF_GENERIC_TEXTURE_DETAIL_RE.match(line):
            generic_texture_detail_samples.append(parse_int_fields(match))
        elif match := PERF_NV2A_SPLIT_RE.match(line):
            nv2a_split_samples.append(parse_int_fields(match))
        elif match := PERF_VSH_PROGRAM_RE.match(line):
            vsh_program_samples.append(parse_vsh_programs(match.group("top")))
        elif match := PERF_VERTEX_REUSE_RE.match(line):
            vertex_reuse_samples.append(
                parse_vertex_reuse_programs(match.group("top"))
            )
        elif match := PERF_WITNESSED_PROFILE_RE.match(line):
            witnessed_profile_samples.append(parse_witnessed_profiles(match.group("top")))
        elif match := PERF_GENERIC_RE.match(line):
            generic_perf_samples.append(parse_generic_perf(match))
        elif match := PERF_GENERIC_FAMILY_RE.match(line):
            generic_family_samples.append(parse_generic_family_perf(match))
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
        "unicorn_memory_samples": unicorn_memory_samples,
        "final_unicorn_memory": (
            unicorn_memory_samples[-1] if unicorn_memory_samples else None
        ),
        "renderer_samples": renderer_samples,
        "final_renderer": renderer_samples[-1] if renderer_samples else None,
        "renderer_resident_detail_samples": renderer_resident_detail_samples,
        "final_renderer_resident_detail": (
            renderer_resident_detail_samples[-1]
            if renderer_resident_detail_samples
            else None
        ),
        "renderer_resolve_context_samples": renderer_resolve_context_samples,
        "final_renderer_resolve_context": (
            renderer_resolve_context_samples[-1]
            if renderer_resolve_context_samples else None
        ),
        "perf_memory_samples": perf_memory_samples,
        "final_perf_memory": perf_memory_samples[-1] if perf_memory_samples else None,
        "generic_split_samples": generic_split_samples,
        "final_generic_split": (
            generic_split_samples[-1] if generic_split_samples else None
        ),
        "generic_detail_samples": generic_detail_samples,
        "final_generic_detail": (
            generic_detail_samples[-1] if generic_detail_samples else None
        ),
        "generic_admission_samples": generic_admission_samples,
        "final_generic_admission": (
            generic_admission_samples[-1] if generic_admission_samples else None
        ),
        "generic_texture_detail_samples": generic_texture_detail_samples,
        "final_generic_texture_detail": (
            generic_texture_detail_samples[-1]
            if generic_texture_detail_samples
            else None
        ),
        "nv2a_split_samples": nv2a_split_samples,
        "final_nv2a_split": nv2a_split_samples[-1] if nv2a_split_samples else None,
        "vsh_program_samples": vsh_program_samples,
        "final_vsh_programs": vsh_program_samples[-1] if vsh_program_samples else None,
        "vertex_reuse_samples": vertex_reuse_samples,
        "final_vertex_reuse": (
            vertex_reuse_samples[-1] if vertex_reuse_samples else None
        ),
        "witnessed_profile_samples": witnessed_profile_samples,
        "final_witnessed_profiles": (
            witnessed_profile_samples[-1] if witnessed_profile_samples else None
        ),
        "generic_perf_samples": generic_perf_samples,
        "final_generic_perf": generic_perf_samples[-1] if generic_perf_samples else None,
        "generic_family_samples": generic_family_samples,
        "final_generic_family": (
            generic_family_samples[-1] if generic_family_samples else None
        ),
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
