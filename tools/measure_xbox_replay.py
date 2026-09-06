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

# beads-xbox.1.14.6: GPU-side timing. renderer_draw_ns / renderer_resolve_ns are
# CPU issue time; these are GL_TIME_ELAPSED nanoseconds plus the fence split of a
# readback (drain = GPU reaching the fence, readpixels = transfer + driver,
# cpu_copy = the flip into guest bytes). bytes_read vs dirty_bytes is the
# full-surface readback against the tracked dirty union -- the headroom a
# scissored readback would have. gpu_timing_available=0 means the driver lacked
# an entry point, so zeros there mean "not measured", NOT "the GPU was idle".
#
# The renderer has no per-checkpoint report hook (main.cpp formats the perf::
# buckets itself), so xlle emits exactly one of these at shutdown, scope=
# cumulative. The parser keeps a list anyway so per-checkpoint lines need no
# change here if one is ever added.
PERF_GL_GPU_RE = re.compile(
    r"^\[perf-gl-gpu\] "
    r"scope=(?P<scope>\S+) "
    r"gpu_timing_available=(?P<gpu_timing_available>\d+) "
    r"gpu_draw_ns=(?P<gpu_draw_ns>\d+) "
    r"gpu_draw_n=(?P<gpu_draw_n>\d+) "
    r"gpu_upload_ns=(?P<gpu_upload_ns>\d+) "
    r"gpu_upload_n=(?P<gpu_upload_n>\d+) "
    r"gpu_resolve_ns=(?P<gpu_resolve_ns>\d+) "
    r"gpu_resolve_n=(?P<gpu_resolve_n>\d+) "
    r"drain_ns=(?P<drain_ns>\d+) "
    r"readpixels_ns=(?P<readpixels_ns>\d+) "
    r"cpu_copy_ns=(?P<cpu_copy_ns>\d+) "
    r"bytes_read=(?P<bytes_read>\d+) "
    r"dirty_bytes=(?P<dirty_bytes>\d+) "
    r"nested_skipped=(?P<nested_skipped>\d+) "
    r"queries_dropped=(?P<queries_dropped>\d+)"
    # beads-xbox.1.14.6: resolve_bracket_ok is a print-time self-check on the
    # GL_TIME_ELAPSED bracket around glReadPixels, added 2026-09-04. OPTIONAL on
    # purpose: reports captured before that build carry the line without it, and
    # the parser must keep reading them rather than dropping the whole family.
    # parse_gl_gpu() maps a missing group to None, which lands in the JSON as
    # null -- distinguishable from a printed 0, which means the check FAILED.
    r"(?: resolve_bracket_ok=(?P<resolve_bracket_ok>\d+))?$"
)

# [perf-excl] (beads-xbox.1.14.3) is a FLAT key=value line whose field list grows
# every time a bucket is added -- perf_excl.hpp emits one `<bucket>_ns=` /
# `<bucket>_n=` pair per entry of kBucketNames, walked over kBucketCount. A rigid
# field-by-field regex like the ones above would therefore have to be edited in
# lockstep with the header and would silently stop matching the whole line the
# first time it was not, dropping the family rather than one field. Match the
# prefix only and split the rest generically.
PERF_EXCL_RE = re.compile(r"^\[perf-excl\] (?P<fields>\S+=\S+(?: \S+=\S+)*)$")

# [host] idle-census (beads-xbox.1.14.4) is mostly flat key=value, plus two
# comma-separated association lists introduced by `<name>-insns:`.
IDLE_CENSUS_RE = re.compile(r"^\[host\] idle-census (?P<fields>.+)$")

# src/app/main.cpp print_seam_census -- the stage (d) NV2A seam census
# (beads-xbox.1.15.4, DESIGN-NV2A-SEAM.md measurement M5). Two line families,
# each emitted once per frame-series checkpoint with scope=interval and once
# at shutdown with scope=cumulative:
#   [host] seam-census  scope=... deadline=... jobs_latched=... ...
#   [host] seam-anchor  scope=... deadline=... anchor=<name> hits=... ...
# Absent from any pre-stage-(d) log, so both are parsed additively: a log
# without them yields empty lists and no key changes meaning.
SEAM_CENSUS_RE = re.compile(r"^\[host\] seam-census (?P<fields>.+)$")
SEAM_ANCHOR_RE = re.compile(r"^\[host\] seam-anchor (?P<fields>.+)$")

# src/app/main.cpp print_seam_census -- the stage (e) THREADED draw consumer
# census (beads-xbox.1.15.5, DESIGN-NV2A-SEAM.md section 5(e)). Same two scopes
# as the seam census above:
#   [host] draw-queue scope=... deadline=... threads=... jobs_queued=... ...
#   [host] draw-depth scope=... deadline=... bucket=<name> count=...
# jobs_queued is the WITNESS that threading actually ran: a byte-lock that passes
# with jobs_queued=0 proved nothing. Parsed additively like the two above.
DRAW_QUEUE_RE = re.compile(r"^\[host\] draw-queue (?P<fields>.+)$")
DRAW_DEPTH_RE = re.compile(r"^\[host\] draw-depth (?P<fields>.+)$")
# Stage (e2), beads-xbox.1.15.9: the destination-span partition. Its own line
# family so DRAW_QUEUE_RE keeps its exact field set across the stage (e) and
# stage (e2) reports.
DRAW_SPAN_RE = re.compile(r"^\[host\] draw-span (?P<fields>.+)$")
# Off-thread raster time, printed BESIDE the [perf-excl] partition and
# deliberately NOT inside it (beads-xbox.1.15.9 loose end 1): [perf-excl]'s
# denominator is emulation-thread wall, so folding helper time in would make the
# partition sum past wall. Parsed as its own family for the same reason.
PERF_WORKER_RE = re.compile(r"^\[perf-worker\] (?P<fields>.+)$")


def parse_seam_fields(fields: str) -> dict[str, Any]:
    """`k=v` tokens -> dict, ints where possible, floats next, else strings."""
    parsed: dict[str, Any] = {}
    for token in fields.split():
        key, sep, value = token.partition("=")
        if not sep:
            continue
        key = key.replace("-", "_")
        try:
            parsed[key] = int(value)
            continue
        except ValueError:
            pass
        try:
            parsed[key] = float(value)
        except ValueError:
            parsed[key] = value
    return parsed
IDLE_CENSUS_LIST_KEYS = ("threads-insns", "boundary-pages-insns")

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

# beads-xbox.1.14.8 -- per-profile output hashes and fingerprint/census hashes.
#
# Every witnessed Profile prints one end-of-run line of the shape
#   [2bl] NV2A <label>: <counters> last=<addr> hash=<16 hex>
# (src/app/main.cpp:3133..3520; the accumulators live in src/machine/nv2a.cpp,
# e.g. raster_output_hash_ at nv2a.cpp:4599, radial_color_output_hash_ at 5699).
# Matching the *shape* rather than an enumerated list means a Profile added later
# is picked up without touching this parser.
PROFILE_HASH_RE = re.compile(
    r"^\[2bl\] NV2A (?P<label>[^:]+): (?P<body>.*?)\bhash=(?P<hash>[0-9a-fA-F]{16})\b"
)

# src/app/main.cpp:3570-3572 -- transform position FNV-64.
TRANSFORM_POSITION_RE = re.compile(
    r"^\[2bl\] NV2A transform: .*\bposition-fnv64=(?P<hash>[0-9a-fA-F]{16})\b"
)

# src/app/main.cpp:3902 -- scanout RGBA FNV-64 (also captured by SCANOUT_RE).
SCANOUT_FNV_RE = re.compile(
    r"^\[2bl\] NV2A scanout: .*\brgba-fnv64=(?P<hash>[0-9a-fA-F]{16})\b"
)

# src/app/main.cpp:3413 -- generic-combiner fingerprint census.
# Only populated when perf::g_enabled (nv2a.cpp:5947), i.e. --perf-accounting.
FINGERPRINT_CENSUS_RE = re.compile(
    r"^\[2bl\] NV2A generic-combiner fingerprints: unique=(?P<unique>\d+)(?P<top>.*)$"
)

# src/app/main.cpp:3892 -- guest-visible flip counters (presented frames).
FLIP_RE = re.compile(
    r"^\[2bl\] NV2A flip: write-increments=(?P<write_increments>\d+) "
    r"stalls=(?P<stalls>\d+) read-increments=(?P<read_increments>\d+) "
    r"notifications=(?P<notifications>\d+) surface=(?P<surface>[0-9a-fA-F]+) "
    r"waiting=(?P<waiting>\d+)"
)

# Hashes whose accumulators are gated on `collect_diagnostics`
# (= detailed_telemetry_ || perf::g_enabled) and therefore only advance when
# --perf-accounting is on:
#   * transform_position_fnv64 -- nv2a.cpp:1234 sets collect_diagnostics,
#     nv2a.cpp:1458 gates record_transformed_vertex_stats which folds the hash
#     at nv2a.cpp:1451.
#   * generic_combiner -- nv2a.cpp:5948 sets collect_diagnostics,
#     nv2a.cpp:9730-9733 gates the hash fold.
# In a noperf run both stay at the FNV-1a offset basis 14650fb0739d0383, so they
# are NOT comparable across arms with different --perf-accounting settings.
TELEMETRY_GATED_PROFILE_HASHES = frozenset(
    {"generic_combiner", "transform_position_fnv64"}
)

# FNV-1a 64-bit offset basis: the value an output hash keeps when its Profile was
# never exercised (or its accumulator was gated off).
FNV1A64_OFFSET_BASIS = "14650fb0739d0383"


def profile_hash_key(label: str) -> str:
    """Stable snake_case key for an NV2A profile label.

    'tex=f fan' -> 'tex_f_fan', 'immediate-quad menu I8+palette 64x128' ->
    'immediate_quad_menu_i8_palette_64x128'.
    """
    slug = re.sub(r"[^0-9a-z]+", "_", label.strip().lower())
    return slug.strip("_")


def parse_fingerprint_census(text: str) -> dict[str, int]:
    census: dict[str, int] = {}
    for item in text.split():
        fingerprint, sep, count = item.partition(":")
        if not sep:
            continue
        try:
            census[fingerprint.lower()] = int(count)
        except ValueError:
            continue
    return census


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


def parse_gl_gpu(match: re.Match[str]) -> dict[str, Any]:
    """[perf-gl-gpu] fields: `scope` is a label, everything else a counter.

    parse_int_fields would int() the scope and raise, so this family gets its own
    two-line parser rather than a special case bolted into the shared one.
    """
    fields: dict[str, Any] = {}
    for key, value in match.groupdict().items():
        if key == "scope":
            fields[key] = value
        elif value is None:
            # An optional field absent from an older report line. None, not 0:
            # resolve_bracket_ok=0 means the bracket check FAILED, and a missing
            # field must never be readable as a failure that was measured.
            fields[key] = None
        else:
            fields[key] = int(value)
    return fields


def _coerce_scalar(key: str, value: str) -> Any:
    """int where possible, float for the _pct/_per_tick fields, else the string.

    Kept deliberately permissive: these two families carry a mix of counters,
    ratios, a `scope` label and hex ranges (`idle_range=0x8001e3ce-0x8001e3ff`),
    and a new field must land in the JSON as-is rather than raise.
    """
    if key in ("scope", "idle_range"):
        return value
    try:
        return int(value)
    except ValueError:
        pass
    try:
        return float(value)
    except ValueError:
        return value


def parse_perf_excl(fields: str) -> dict[str, Any]:
    """[perf-excl] -> flat dict, one entry per key=value token.

    Bucket-count agnostic by construction (see PERF_EXCL_RE). `unaccounted_ns` is
    signed and must survive as a negative int, which int() handles; the partition
    is only trustworthy when `unaccounted_pct` is small, so that field is the one
    a consumer should gate on.
    """
    parsed: dict[str, Any] = {}
    for token in fields.split():
        key, _, value = token.partition("=")
        parsed[key] = _coerce_scalar(key, value)
    return parsed


def parse_idle_census(fields: str) -> dict[str, Any]:
    """[host] idle-census -> flat dict plus the two association lists.

    `threads-insns:` and `boundary-pages-insns:` are comma-separated
    `0xADDR=COUNT` pairs. They become `threads_insns` / `boundary_pages_insns`
    dicts keyed by the lowercased address string, so the retired-instruction
    attribution beads-xbox.1.14.4 needs is queryable from the report JSON instead
    of only from the log.
    """
    parsed: dict[str, Any] = {}
    for token in fields.split():
        if ":" in token and token.split(":", 1)[0] in IDLE_CENSUS_LIST_KEYS:
            name, _, body = token.partition(":")
            entries: dict[str, int] = {}
            for item in body.split(","):
                if not item:
                    continue
                addr, _, count = item.partition("=")
                try:
                    entries[addr.lower()] = int(count)
                except ValueError:
                    continue
            parsed[name.replace("-", "_")] = entries
            continue
        key, sep, value = token.partition("=")
        if not sep:
            continue
        parsed[key.replace("-", "_")] = _coerce_scalar(key, value)
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
    gl_gpu_samples: list[dict[str, Any]] = []
    perf_excl_samples: list[dict[str, Any]] = []
    idle_census_samples: list[dict[str, Any]] = []
    seam_census_samples: list[dict[str, Any]] = []
    seam_anchor_samples: list[dict[str, Any]] = []
    draw_queue_samples: list[dict[str, Any]] = []
    draw_depth_samples: list[dict[str, Any]] = []
    draw_span_samples: list[dict[str, Any]] = []
    perf_worker_samples: list[dict[str, Any]] = []
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
    profile_hashes: dict[str, str] = {}
    profile_hash_labels: dict[str, str] = {}
    fingerprint_census: dict[str, int] | None = None
    fingerprint_unique: int | None = None
    flip: dict[str, Any] | None = None
    host_present_samples: list[dict[str, Any]] = []

    # beads-xbox.1.14.1 item 3(c): counter samples are emitted as a block right
    # after each `[host] frame-series` line, plus one extra block at shutdown.
    # Positional indexing therefore silently mis-associates whenever a sample
    # family is missing a checkpoint (perf-resident/perf-texture-modulate emit 24
    # blocks where [perf] emits 25) or absent entirely (a noperf run has zero).
    # Record the owning checkpoint for every sample so consumers can key on the
    # guest-anchored deadline instead of the list index.
    checkpoints: dict[str, list[dict[str, Any]]] = {}
    checkpoint_index = -1
    checkpoint_deadline: int | None = None
    checkpoint_insns: int | None = None
    seen_checkpoints: dict[str, set[int]] = {}

    def note(kind: str) -> None:
        seen = seen_checkpoints.setdefault(kind, set())
        if checkpoint_index >= 0 and checkpoint_index not in seen:
            seen.add(checkpoint_index)
            tag = {
                "index": checkpoint_index,
                "deadline": checkpoint_deadline,
                "insns": checkpoint_insns,
                "scope": "frame-series",
            }
        else:
            tag = {
                "index": None,
                "deadline": None,
                "insns": None,
                "scope": "shutdown" if checkpoint_index >= 0 else "preamble",
            }
        checkpoints.setdefault(kind, []).append(tag)

    for raw_line in read_log_text(path).splitlines():
        line = raw_line.rstrip("\n")
        if line.startswith("[host] present "):
            sample: dict[str, Any] = {}
            for key, value in re.findall(r"(\w+)=([^\s]+)", line):
                try:
                    sample[key] = float(value) if "." in value else int(value)
                except ValueError:
                    sample[key] = value
            host_present_samples.append(sample)
        elif line.startswith("[nv2a-renderer] "):
            selected_renderer = line[len("[nv2a-renderer] ") :]
        elif line.startswith("[host] hard process-memory limit armed: "):
            memory_limit = line[len("[host] ") :]
        elif match := FRAME_RE.match(line):
            frame = parse_int_fields(match)
            frames.append(frame)
            checkpoint_index = len(frames) - 1
            checkpoint_deadline = frame["deadline"]
            checkpoint_insns = frame["insns"]
        elif match := PERF_RE.match(line):
            note("perf_samples")
            perf_samples.append(parse_int_fields(match))
        elif match := PERF_RESIDENT_RE.match(line):
            note("resident_samples")
            resident_samples.append(parse_counter_sample(match))
        elif match := PERF_TEXTURE_MODULATE_RE.match(line):
            note("texture_modulate_samples")
            texture_modulate_samples.append(parse_counter_sample(match))
        elif match := HOST_MEMORY_RE.match(line):
            note("host_memory_samples")
            host_memory_samples.append(parse_int_fields(match))
        elif match := UNICORN_MEMORY_RE.match(line):
            note("unicorn_memory_samples")
            unicorn_memory_samples.append(parse_int_fields(match))
        elif match := PERF_RENDERER_RE.match(line):
            note("renderer_samples")
            renderer_samples.append(parse_int_fields(match))
        elif match := PERF_RENDERER_RESIDENT_DETAIL_RE.match(line):
            note("renderer_resident_detail_samples")
            renderer_resident_detail_samples.append(parse_int_fields(match))
        elif match := PERF_RENDERER_RESOLVE_CONTEXT_RE.match(line):
            note("renderer_resolve_context_samples")
            renderer_resolve_context_samples.append(
                parse_renderer_resolve_contexts(match.group("top"))
            )
        elif match := PERF_GL_GPU_RE.match(line):
            note("gl_gpu_samples")
            gl_gpu_samples.append(parse_gl_gpu(match))
        elif match := PERF_EXCL_RE.match(line):
            note("perf_excl_samples")
            perf_excl_samples.append(parse_perf_excl(match.group("fields")))
        elif match := IDLE_CENSUS_RE.match(line):
            note("idle_census_samples")
            idle_census_samples.append(parse_idle_census(match.group("fields")))
        elif match := SEAM_CENSUS_RE.match(line):
            note("seam_census_samples")
            seam_census_samples.append(parse_seam_fields(match.group("fields")))
        elif match := SEAM_ANCHOR_RE.match(line):
            note("seam_anchor_samples")
            seam_anchor_samples.append(parse_seam_fields(match.group("fields")))
        elif match := DRAW_QUEUE_RE.match(line):
            note("draw_queue_samples")
            draw_queue_samples.append(parse_seam_fields(match.group("fields")))
        elif match := DRAW_DEPTH_RE.match(line):
            note("draw_depth_samples")
            draw_depth_samples.append(parse_seam_fields(match.group("fields")))
        elif match := DRAW_SPAN_RE.match(line):
            note("draw_span_samples")
            draw_span_samples.append(parse_seam_fields(match.group("fields")))
        elif match := PERF_WORKER_RE.match(line):
            note("perf_worker_samples")
            perf_worker_samples.append(parse_seam_fields(match.group("fields")))
        elif match := PERF_MEMORY_RE.match(line):
            note("perf_memory_samples")
            perf_memory_samples.append(parse_int_fields(match))
        elif match := PERF_GENERIC_SPLIT_RE.match(line):
            note("generic_split_samples")
            generic_split_samples.append(parse_int_fields(match))
        elif match := PERF_GENERIC_DETAIL_RE.match(line):
            note("generic_detail_samples")
            generic_detail_samples.append(parse_int_fields(match))
        elif match := PERF_GENERIC_ADMISSION_RE.match(line):
            note("generic_admission_samples")
            generic_admission_samples.append(parse_int_fields(match))
        elif match := PERF_GENERIC_TEXTURE_DETAIL_RE.match(line):
            note("generic_texture_detail_samples")
            generic_texture_detail_samples.append(parse_int_fields(match))
        elif match := PERF_NV2A_SPLIT_RE.match(line):
            note("nv2a_split_samples")
            nv2a_split_samples.append(parse_int_fields(match))
        elif match := PERF_VSH_PROGRAM_RE.match(line):
            note("vsh_program_samples")
            vsh_program_samples.append(parse_vsh_programs(match.group("top")))
        elif match := PERF_VERTEX_REUSE_RE.match(line):
            note("vertex_reuse_samples")
            vertex_reuse_samples.append(
                parse_vertex_reuse_programs(match.group("top"))
            )
        elif match := PERF_WITNESSED_PROFILE_RE.match(line):
            note("witnessed_profile_samples")
            witnessed_profile_samples.append(parse_witnessed_profiles(match.group("top")))
        elif match := PERF_GENERIC_RE.match(line):
            note("generic_perf_samples")
            generic_perf_samples.append(parse_generic_perf(match))
        elif match := PERF_GENERIC_FAMILY_RE.match(line):
            note("generic_family_samples")
            generic_family_samples.append(parse_generic_family_perf(match))
        elif match := FLIP_RE.match(line):
            flip = parse_int_fields(match)
        elif match := FINGERPRINT_CENSUS_RE.match(line):
            generic_fingerprints = line
            fingerprint_unique = int(match.group("unique"))
            fingerprint_census = parse_fingerprint_census(match.group("top"))
        elif match := GENERIC_RE.match(line):
            generic = parse_int_fields(match)
        elif match := SCANOUT_RE.match(line):
            scanout = parse_int_fields(match)
        elif match := RETIRED_RE.match(line):
            retired = parse_int_fields(match)
        elif match := OHCI_RE.match(line):
            ohci_endpoint2 = int(match.group("xid_endpoint2"))

        # Hash harvesting runs independently of the dispatch chain above: several
        # of these lines are also consumed as counter samples (scanout, the
        # generic-combiner census), and a profile line must be recorded even when
        # an earlier branch already claimed it.
        if hash_match := PROFILE_HASH_RE.match(line):
            label = hash_match.group("label")
            key = profile_hash_key(label)
            profile_hashes[key] = hash_match.group("hash").lower()
            profile_hash_labels[key] = label
        if hash_match := TRANSFORM_POSITION_RE.match(line):
            profile_hashes["transform_position_fnv64"] = hash_match.group("hash").lower()
            profile_hash_labels["transform_position_fnv64"] = "transform position-fnv64"
        if hash_match := SCANOUT_FNV_RE.match(line):
            profile_hashes["scanout_rgba_fnv64"] = hash_match.group("hash").lower()
            profile_hash_labels["scanout_rgba_fnv64"] = "scanout rgba-fnv64"

    final_perf = perf_samples[-1] if perf_samples else None
    final_frame = frames[-1] if frames else None

    census_digest = None
    if fingerprint_census is not None:
        canonical = ";".join(
            f"{fingerprint}:{count}"
            for fingerprint, count in sorted(fingerprint_census.items())
        )
        census_digest = hashlib.sha256(canonical.encode("utf-8")).hexdigest()

    # Guest flip writes are not host presents: even a headless replay has them.
    # Keep the old presented_frames JSON key for existing report consumers, with
    # its corrected scope. Actual SDL presents have a separate measured counter.
    wall_ns_total = sum(
        int(frame.get("wall_ns", 0)) for frame in frames if isinstance(frame, dict)
    )
    presented: dict[str, Any] = {
        "flips_counter_present": flip is not None,
        "flips_total": flip["write_increments"] if flip else None,
        "wall_ns_total": wall_ns_total,
        "flips_per_second": None,
        "scope": "whole-run average" if flip else "unknown",
        "note": (
            "guest flip writes/s = NV2A flip write-increments / summed frame-series "
            "wall time; the flip counter is emitted once at shutdown so no "
            "per-window rate is available"
            if flip
            else "no '[2bl] NV2A flip:' line in this log; guest flip rate unknown"
        ),
    }
    if flip and wall_ns_total:
        presented["flips_per_second"] = (
            float(flip["write_increments"]) * 1_000_000_000.0 / float(wall_ns_total)
        )

    return {
        "sample_checkpoints": checkpoints,
        "profile_hashes": profile_hashes,
        "profile_hash_labels": profile_hash_labels,
        "profile_hash_scope": "final",
        "telemetry_gated_profile_hashes": sorted(
            key for key in profile_hashes if key in TELEMETRY_GATED_PROFILE_HASHES
        ),
        "unexercised_profile_hashes": sorted(
            key
            for key, value in profile_hashes.items()
            if value == FNV1A64_OFFSET_BASIS
        ),
        "generic_fingerprint_census": fingerprint_census,
        "generic_fingerprint_unique": fingerprint_unique,
        "generic_fingerprint_census_sha256": census_digest,
        "flip": flip,
        "presented_frames": presented,
        "host_present_samples": host_present_samples,
        "host_present": next((s for s in reversed(host_present_samples)
                              if s.get("scope") == "cumulative"), None),
        "effective_fps": (
            final_frame.get("effective_fps") if final_frame else None
        ),
        "effective_fps_label": (
            "realtime-ratio x60 for the final frame-series window, not presented fps"
        ),
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
        # beads-xbox.1.14.6. `gl_gpu` is the cumulative run total (None when the
        # log predates the line or the run used no GL renderer); `gl_gpu_samples`
        # keeps every line seen, in order.
        "gl_gpu": gl_gpu_samples[-1] if gl_gpu_samples else None,
        "gl_gpu_samples": gl_gpu_samples,
        # beads-xbox.1.14.3. `perf_excl` is the cumulative partition emitted at
        # shutdown; the samples list holds every checkpoint interval in order.
        # Absent entirely from a noperf log, which is why both default to
        # None/[] rather than raising.
        "perf_excl": perf_excl_samples[-1] if perf_excl_samples else None,
        "perf_excl_samples": perf_excl_samples,
        # beads-xbox.1.14.4. Present in noperf logs too -- the census is not
        # gated on --perf-accounting.
        "idle_census": idle_census_samples[-1] if idle_census_samples else None,
        "idle_census_samples": idle_census_samples,
        # Stage (d) seam census. `seam_census` / `seam_anchors` are the CUMULATIVE
        # records (scope=cumulative, printed once at shutdown); the *_samples lists
        # keep every interval delta so a per-window rate is derivable. All four are
        # None/empty for a build without the seam, which is what makes this
        # additive: no existing key changes meaning.
        "seam_census": next(
            (s for s in reversed(seam_census_samples)
             if s.get("scope") == "cumulative"), None),
        "seam_census_samples": seam_census_samples,
        "seam_anchors": {
            s["anchor"]: s
            for s in seam_anchor_samples
            if s.get("scope") == "cumulative" and "anchor" in s
        },
        "seam_anchor_samples": seam_anchor_samples,
        # Stage (e) threaded draw consumer. Same shape and the same additive
        # rule: `draw_queue` is the cumulative record, `draw_depth` the cumulative
        # queue-depth histogram keyed by bucket name, and both are None/empty for
        # any build or run without the worker (including every threads=0 run,
        # which still prints the line with jobs_queued=0).
        "draw_queue": next(
            (s for s in reversed(draw_queue_samples)
             if s.get("scope") == "cumulative"), None),
        "draw_queue_samples": draw_queue_samples,
        # str() because two bucket names ("0", "1") parse as ints and a JSON
        # object cannot have mixed-type keys sorted.
        "draw_depth": {
            str(s["bucket"]): s.get("count")
            for s in draw_depth_samples
            if s.get("scope") == "cumulative" and "bucket" in s
        },
        "draw_depth_samples": draw_depth_samples,
        # Stage (e2) destination-span partition (beads-xbox.1.15.9). span_jobs
        # is the witness that work division ran: an A/B whose cumulative
        # span_jobs is 0 measured the inline path in both arms.
        "draw_span": next(
            (s for s in reversed(draw_span_samples)
             if s.get("scope") == "cumulative"), None),
        "draw_span_samples": draw_span_samples,
        # worker_raster_ns and friends: off-thread, excluded from the
        # [perf-excl] partition and from its residual by construction.
        "perf_worker": next(
            (s for s in reversed(perf_worker_samples)
             if s.get("scope") == "cumulative"), None),
        "perf_worker_samples": perf_worker_samples,
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


DEFAULT_ENGINE = "unicorn-2.1.1"
DEFAULT_CHUNK_INSNS = 4096


def split_command(command: str | None) -> list[str] | None:
    """Best-effort argv recovery from the recorded command string.

    The harness now records argv exactly (--argv-json); this only backfills
    provenance for reports produced from an older command string.
    """
    if not command:
        return None
    try:
        import shlex

        return shlex.split(command, posix=False)
    except ValueError:
        return command.split()


def chunk_insns_from_command(command: str | None) -> int | None:
    if not command:
        return None
    match = re.search(r"--machine-chunk-insns[= ]+(\d+)", command)
    return int(match.group(1)) if match else None


def perf_accounting_from_command(command: str | None) -> bool | None:
    if not command:
        return None
    return "--perf-accounting" in command


def load_json(path: pathlib.Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def expected_hashes_from(document: Any) -> dict[str, str]:
    """Accept either a bare {key: hash} mapping or a golden record."""
    if not isinstance(document, dict):
        raise SystemExit("expected-profile-hashes document must be a JSON object")
    source = document.get("profile_hashes", document)
    if not isinstance(source, dict):
        raise SystemExit("expected-profile-hashes: 'profile_hashes' must be an object")
    out: dict[str, str] = {}
    for key, value in source.items():
        if isinstance(value, str):
            out[str(key)] = value.lower()
    return out


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    stdout_path = args.stdout.resolve()
    stderr_path = args.stderr.resolve() if args.stderr else None
    frame_dump = args.frame_dump.resolve() if args.frame_dump else None
    exe = args.exe.resolve() if args.exe else None

    parsed = parse_stdout(stdout_path)
    stderr_text = ""
    if stderr_path and stderr_path.exists():
        stderr_text = read_log_text(stderr_path)

    argv = None
    if getattr(args, "argv_json", None):
        argv = load_json(args.argv_json)
        if not isinstance(argv, list):
            raise SystemExit("--argv-json must contain a JSON array of arguments")
        argv = [str(item) for item in argv]
    if argv is None:
        argv = split_command(args.command)

    chunk_insns = getattr(args, "chunk_insns", None)
    if chunk_insns is None:
        chunk_insns = chunk_insns_from_command(args.command)

    perf_accounting = getattr(args, "perf_accounting", None)
    if perf_accounting is None:
        perf_accounting = perf_accounting_from_command(args.command)

    quiescence = None
    if getattr(args, "quiescence_json", None):
        quiescence = load_json(args.quiescence_json)

    report: dict[str, Any] = {
        # v2 adds parsed.profile_hashes, parsed.sample_checkpoints,
        # parsed.presented_frames and the leg/quiescence provenance block
        # (beads-xbox.1.14.1, beads-xbox.1.14.8). v1 readers keep working: every
        # v1 key is still present and unchanged.
        "schema": "xboxlle.replay.measurement.v2",
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
            "argv": argv,
            "chunk_insns": chunk_insns,
            "engine": getattr(args, "engine", None) or DEFAULT_ENGINE,
            "perf_accounting": perf_accounting,
            "leg": {
                "index": getattr(args, "leg_index", None),
                "arm": getattr(args, "arm", None),
                "repetition": getattr(args, "repetition", None),
                "session": getattr(args, "session", None),
            },
            "host_quiescence": quiescence,
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

    def add_check(name: str, ok: bool, detail: str, *, skipped: bool = False) -> None:
        checks.append(
            {"name": name, "ok": ok, "detail": detail, "skipped": skipped}
        )

    # A golden record supplies the expected identity plus the expected hashes.
    golden: dict[str, Any] | None = None
    if getattr(args, "golden", None):
        loaded = load_json(args.golden)
        if not isinstance(loaded, dict):
            raise SystemExit("--golden must contain a JSON object")
        golden = loaded

    expected_final_sha = args.expect_final_sha
    if not expected_final_sha and golden:
        expected_final_sha = golden.get("final_frame_sha256")

    if expected_final_sha:
        actual = report["artifacts"]["frame_dump_sha256"]
        expect = expected_final_sha.lower()
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

    # ---- beads-xbox.1.14.8: per-profile output hash gates -------------------
    expected_hashes: dict[str, str] = {}
    expectation_source: str | None = None
    if getattr(args, "expected_profile_hashes", None):
        expected_hashes = expected_hashes_from(load_json(args.expected_profile_hashes))
        expectation_source = str(args.expected_profile_hashes)
    elif golden is not None and isinstance(golden.get("profile_hashes"), dict):
        expected_hashes = expected_hashes_from(golden)
        expectation_source = str(args.golden)

    if expected_hashes:
        actual_hashes: dict[str, str] = parsed["profile_hashes"]
        golden_perf = golden.get("perf_accounting") if golden else None
        report["profile_hash_expectation"] = {
            "source": expectation_source,
            "count": len(expected_hashes),
            "golden_perf_accounting": golden_perf,
            "actual_perf_accounting": perf_accounting,
        }
        for key in sorted(set(expected_hashes) | set(actual_hashes)):
            expect = expected_hashes.get(key)
            actual = actual_hashes.get(key)
            if expect is None:
                add_check(
                    f"profile_hash:{key}",
                    True,
                    f"actual={actual} not present in expectation set",
                    skipped=True,
                )
                continue
            if actual is None:
                add_check(
                    f"profile_hash:{key}",
                    False,
                    f"MISSING from log; expected={expect}",
                )
                continue
            # Two hashes only advance under --perf-accounting (see
            # TELEMETRY_GATED_PROFILE_HASHES above). Comparing them across arms
            # with different accounting settings is meaningless, so skip rather
            # than report a bogus mismatch -- or a bogus pass.
            if (
                key in TELEMETRY_GATED_PROFILE_HASHES
                and golden_perf is not None
                and perf_accounting is not None
                and bool(golden_perf) != bool(perf_accounting)
            ):
                add_check(
                    f"profile_hash:{key}",
                    True,
                    (
                        f"SKIPPED telemetry-gated hash: golden perf_accounting="
                        f"{bool(golden_perf)} actual={bool(perf_accounting)}; "
                        f"actual={actual} expected={expect}"
                    ),
                    skipped=True,
                )
                continue
            add_check(
                f"profile_hash:{key}",
                actual == expect,
                f"actual={actual} expected={expect}",
            )

    # ---- golden identity (PERF-METHODOLOGY section 3 item 5) ---------------
    if golden is not None:
        identity_fields = (
            ("chunk_insns", chunk_insns),
            ("engine", report["provenance"]["engine"]),
            ("exe_sha256", report["provenance"]["exe_sha256"]),
            ("git_head", report["provenance"]["git_head"]),
            ("perf_accounting", perf_accounting),
        )
        gate_identity = bool(getattr(args, "require_golden_identity", False))
        for field, actual in identity_fields:
            if field not in golden:
                continue
            expect = golden.get(field)
            matches = expect == actual
            add_check(
                f"golden_identity:{field}",
                True if (not gate_identity and not matches) else matches,
                f"actual={actual} golden={expect}",
                skipped=not gate_identity and not matches,
            )

    report["ok"] = all(
        check["ok"] for check in checks if not check.get("skipped")
    )
    report["skipped_checks"] = [
        check["name"] for check in checks if check.get("skipped")
    ]
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
    # beads-xbox.1.14.8
    parser.add_argument(
        "--expected-profile-hashes",
        type=pathlib.Path,
        help="JSON file: either {key: hash} or a golden record with profile_hashes.",
    )
    parser.add_argument(
        "--golden",
        type=pathlib.Path,
        help="Golden record JSON (tools/make_golden_record.py): supplies expected "
        "final SHA, profile hashes and the identity fields.",
    )
    parser.add_argument(
        "--require-golden-identity",
        action="store_true",
        help="Gate on chunk_insns/engine/exe_sha256/git_head/perf_accounting "
        "matching the golden record instead of only reporting the difference.",
    )
    # beads-xbox.1.14.1 provenance
    parser.add_argument("--argv-json", type=pathlib.Path, help="JSON array: exact xlle argv.")
    parser.add_argument("--chunk-insns", type=int, help="--machine-chunk-insns value used.")
    parser.add_argument("--engine", help=f"Execution engine id (default {DEFAULT_ENGINE}).")
    perf_group = parser.add_mutually_exclusive_group()
    perf_group.add_argument(
        "--perf-accounting",
        dest="perf_accounting",
        action="store_true",
        default=None,
        help="Record that this leg ran with --perf-accounting.",
    )
    perf_group.add_argument(
        "--no-perf-accounting",
        dest="perf_accounting",
        action="store_false",
        help="Record that this leg ran without --perf-accounting.",
    )
    parser.add_argument("--leg-index", type=int, help="0-based leg index in the session.")
    parser.add_argument("--arm", help="Arm label, e.g. A (baseline) or B (candidate).")
    parser.add_argument("--repetition", type=int, help="1-based repetition within the arm.")
    parser.add_argument("--session", help="Session id shared by every leg of one run.")
    parser.add_argument(
        "--quiescence-json",
        type=pathlib.Path,
        help="JSON file with the before/after foreign-process census for this leg.",
    )
    args = parser.parse_args()

    report = build_report(args)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    tmp = args.report.with_suffix(args.report.suffix + ".partial")
    tmp.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(tmp, args.report)

    parsed = report["parsed"]
    final_frame = parsed["final_frame"]
    presented = parsed["presented_frames"]
    print(f"wrote {args.report} ok={report['ok']}")
    print(
        "  effective_fps="
        f"{final_frame['effective_fps'] if final_frame else 'n/a'} "
        f"({parsed['effective_fps_label']})"
    )
    if presented["flips_per_second"] is not None:
        print(
            "  guest_flip_writes_per_second="
            f"{presented['flips_per_second']:.3f} "
            f"(flips={presented['flips_total']}, {presented['scope']}; "
            f"{presented['note']})"
        )
    else:
        print(f"  guest_flip_writes_per_second=unknown ({presented['note']})")
    host_present = parsed["host_present"]
    print(f"  host_presented_fps={host_present['presented_fps'] if host_present else 'unknown'} "
          "(successful SDL presents / interactive wall; not physical display scans)")
    print(
        f"  profile_hashes={len(parsed['profile_hashes'])} "
        f"telemetry_gated={parsed['telemetry_gated_profile_hashes']} "
        f"unexercised={len(parsed['unexercised_profile_hashes'])}"
    )
    quiescence = report["provenance"].get("host_quiescence")
    if isinstance(quiescence, dict):
        print(
            f"  host_quiescence contaminated={quiescence.get('contaminated')} "
            f"foreign={quiescence.get('foreign_processes')}"
        )
    for check in report["checks"]:
        if check.get("skipped"):
            status = "SKIP"
        else:
            status = "ok" if check["ok"] else "FAIL"
        print(f"check {check['name']}: {status} {check['detail']}")
    return 0 if report["ok"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
