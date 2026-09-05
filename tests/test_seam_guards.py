#!/usr/bin/env python3
"""Structural guards for the NV2A producer/consumer seam, stage (d).

Bead beads-xbox.1.15.4; design docs/DESIGN-NV2A-SEAM.md sections 1.1-1.5, 2.2(c),
3.1-3.2 and measurement M5.

Stage (d) introduces a latch/execute seam at the draw-batch boundary with an
INLINE executor and no behavior change. A compiler cannot check the properties
that make that claim true, so they are pinned here:

  1. The latch is AT the draw-batch boundary -- inside submit_vertex_batch,
     after vertex transform, before anything touches a pixel.
  2. execute_draw_job is the ONLY caller of the raster entry point, and the
     raster entry point is the only caller of the immediate-quad and
     generic-combiner bodies. One seam, not three.
  3. There is exactly ONE copy of the fragment loop. No #ifdef, no "software
     fallback" variant, no second implementation of the pixel math.
  4. The executor reads the JOB, not the live members it used to read.
  5. The DrawJob latches the section 1.2 inventory.
  6. The ten coherence anchors in the code are exactly the ten in the design
     document, in the same order, and every one of them is actually raised.
  7. The perf_excl exclusive-time regions are intact and unmoved.

Run: python tests/test_seam_guards.py   (exit 0 = all guards hold)
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NV2A_CPP = os.path.join(ROOT, "src", "machine", "nv2a.cpp")
NV2A_HPP = os.path.join(ROOT, "src", "machine", "nv2a.hpp")
MAIN_CPP = os.path.join(ROOT, "src", "app", "main.cpp")
DESIGN = os.path.join(ROOT, "docs", "DESIGN-NV2A-SEAM.md")

failures = []
checks = 0


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def check(label, condition, detail=""):
    global checks
    checks += 1
    if condition:
        print("  ok   %s" % label)
    else:
        print("  FAIL %s%s" % (label, (" -- " + detail) if detail else ""))
        failures.append(label)


def body(text, signature, terminator):
    """Return the source of one function, from its signature to the next one."""
    start = text.index(signature)
    end = text.index(terminator, start + len(signature))
    return text[start:end]


cpp = read(NV2A_CPP)
hpp = read(NV2A_HPP)
main = read(MAIN_CPP)
design = read(DESIGN)

SUBMIT = "bool Nv2a::submit_vertex_batch() {"
IMMQUAD = "bool Nv2a::rasterize_witnessed_immediate_quad(DrawJob& job, bool& executed) {"
WITNESSED = "bool Nv2a::rasterize_witnessed_batch(DrawJob& job, bool& executed) {"
GENERIC = "bool Nv2a::rasterize_generic_combiner_batch(DrawJob& job, bool& executed) {"

for sig in (SUBMIT, IMMQUAD, WITNESSED, GENERIC):
    if cpp.count(sig) != 1:
        print("FATAL: %r appears %d times" % (sig[:60], cpp.count(sig)))
        sys.exit(2)

submit = body(cpp, SUBMIT, IMMQUAD)
immquad = body(cpp, IMMQUAD, WITNESSED)
# Stage (e) (beads-xbox.1.15.5) split the witnessed path in two at the (3)/(4)
# boundary of design section 3.2: rasterize_witnessed_batch is now the LATCH half
# (profile match, subset validation, surface resolution, texture snapshots -- the
# emulation-thread work that can fault_) and execute_witnessed_fragments is the
# pure EXECUTE half a worker may run. `witnessed` below is still the whole
# witnessed path, so every stage (d) check keeps the meaning it had; the two
# halves are also available separately for the checks that are about the boundary.
WITNESSED_EXEC = "bool Nv2a::execute_witnessed_fragments(DrawJob& job, DrawJobTelemetry& out,"
if cpp.count(WITNESSED_EXEC) != 1:
    print("FATAL: %r appears %d times" % (WITNESSED_EXEC[:60], cpp.count(WITNESSED_EXEC)))
    sys.exit(2)
witnessed_latch = body(cpp, WITNESSED, WITNESSED_EXEC)
witnessed_exec = body(cpp, WITNESSED_EXEC, "void Nv2a::record_generic_skip(")
witnessed = witnessed_latch + witnessed_exec
generic = body(cpp, GENERIC, "Nv2a::ClassMethodStat Nv2a::class_method_stat")

# -------------------------------------------------------------------------
print("\nthe latch sits at the draw-batch boundary")
# -------------------------------------------------------------------------
check("submit_vertex_batch latches a DrawJob",
      "DrawJob job;" in submit and "latch_draw_job(job);" in submit)
check("the executor is called from the latch point",
      "execute_draw_job(job, raster_executed)" in submit)
i_transform = submit.rfind("emit_transformed_vertex(vertex);")
i_latch = submit.index("latch_draw_job(job);")
i_exec = submit.index("execute_draw_job(job, raster_executed)")
check("the latch follows vertex transform", -1 < i_transform < i_latch,
      "transform at %d, latch at %d" % (i_transform, i_latch))
check("the executor follows the latch", i_latch < i_exec)
check("the job is retired after execution, on every path",
      submit.index("retire_draw_job(job, raster_executed);") > i_exec and
      submit.index("retire_draw_job(job, raster_executed);") <
      submit.index("if (!raster_ok) return false;"))
check("submit_vertex_batch no longer calls a raster function directly",
      "rasterize_witnessed_batch(" not in submit and
      "rasterize_generic_combiner_batch(" not in submit and
      "rasterize_witnessed_immediate_quad(" not in submit)

# -------------------------------------------------------------------------
print("\nthe executor is the only caller of the raster functions")
# -------------------------------------------------------------------------
callers = {}
for name in ("rasterize_witnessed_batch", "rasterize_generic_combiner_batch",
             "rasterize_witnessed_immediate_quad"):
    sites = []
    for match in re.finditer(r"\b%s\s*\(" % name, cpp):
        line_start = cpp.rfind("\n", 0, match.start()) + 1
        line = cpp[line_start:cpp.index("\n", match.start())]
        if line.startswith("bool Nv2a::"):
            continue          # the definition
        sites.append(line.strip())
    callers[name] = sites

check("rasterize_witnessed_batch has exactly one call site",
      len(callers["rasterize_witnessed_batch"]) == 1,
      repr(callers["rasterize_witnessed_batch"]))
check("that call site is inside execute_draw_job",
      "return rasterize_witnessed_batch(job, executed);" in
      body(cpp, "bool Nv2a::execute_draw_job(DrawJob& job, bool& executed) {",
           "void Nv2a::seam_note_threadable("))
check("rasterize_witnessed_immediate_quad is reached only from the witnessed body",
      len(callers["rasterize_witnessed_immediate_quad"]) == 1 and
      "rasterize_witnessed_immediate_quad(job, executed)" in witnessed,
      repr(callers["rasterize_witnessed_immediate_quad"]))
check("rasterize_generic_combiner_batch is reached only from the witnessed body",
      len(callers["rasterize_generic_combiner_batch"]) == 1 and
      "rasterize_generic_combiner_batch(job, executed)" in witnessed,
      repr(callers["rasterize_generic_combiner_batch"]))
check("all three raster entry points take the job",
      hpp.count("bool rasterize_witnessed_immediate_quad(DrawJob& job, bool& executed);") == 1 and
      hpp.count("bool rasterize_witnessed_batch(DrawJob& job, bool& executed);") == 1 and
      hpp.count("bool rasterize_generic_combiner_batch(DrawJob& job, bool& executed);") == 1)

# -------------------------------------------------------------------------
print("\nthere is exactly one copy of the fragment loop")
# -------------------------------------------------------------------------
check("no preprocessor fork guards the executor",
      not re.search(r"^\s*#\s*(if|ifdef|ifndef)\b.*(?i:thread|seam|draw_?job)",
                    cpp, re.M))
check("one triangle rasterizer in the witnessed body",
      witnessed.count("auto rasterize_triangle =") == 1,
      str(witnessed.count("auto rasterize_triangle =")))
# The shared depth compare of the colour body is the single most-executed line in
# the emulator. Two of them would mean two fragment loops.
check("the shared colour depth compare exists exactly once in the whole file",
      cpp.count("if (incoming > (stored_depth_stencil >> 8)) continue;") == 1,
      str(cpp.count("if (incoming > (stored_depth_stencil >> 8)) continue;")))
check("no duplicated executor symbol",
      cpp.count("bool Nv2a::execute_draw_job(") == 1)

# -------------------------------------------------------------------------
print("\nthe executor reads the job, not the live members")
# -------------------------------------------------------------------------
LATCHED_MEMBERS = ["raster_state_", "transformed_batch_", "primitive_mode_",
                   "surface_format_", "surface_pitch_", "surface_clip_horizontal_",
                   "surface_clip_vertical_", "surface_color_offset_",
                   "surface_zeta_offset_", "profile_hashes_enabled_",
                   "current_raster_state_sample_index_"]
for region_name, region in (("immediate-quad", immquad), ("witnessed", witnessed),
                            ("generic", generic)):
    leaked = [m for m in LATCHED_MEMBERS
              if re.search(r"\b%s\b" % m, region)]
    check("%s body reads no latched member directly" % region_name,
          not leaked, repr(leaked))
check("the witnessed body binds its aliases to the job",
      "const auto& raster_state = job.raster_state;" in witnessed and
      "const auto& transformed_batch = job.vertices;" in witnessed and
      "const uint32_t primitive_mode = job.primitive_mode;" in witnessed)
check("the derived surface descriptors live in the job",
      "uint32_t& pitch = job.pitch;" in witnessed and
      "uint32_t& color_address = job.color_address;" in witnessed and
      "uint8_t*& color_direct = job.color_direct;" in witnessed)
check("the texture snapshots are read straight into the job",
      "std::vector<uint8_t>& texture_data = job.texture_data;" in witnessed and
      "std::vector<uint8_t>& inline_texture_data = job.inline_texture_data;" in witnessed)

# -------------------------------------------------------------------------
print("\nthe DrawJob latches the section 1.2 inventory")
# -------------------------------------------------------------------------
job_struct = body(hpp, "    struct DrawJob {", "    void latch_draw_job(")
INVENTORY = ["seq", "profile", "primitive_mode", "raster_state", "surface_format",
             "surface_pitch", "surface_clip_horizontal", "surface_clip_vertical",
             "surface_color_offset", "surface_zeta_offset",
             "raster_state_sample_index", "hash_on", "color_address",
             "depth_address", "pitch", "surface_width", "surface_height",
             "clip_x", "clip_y", "clip_width", "clip_height", "color_bytes",
             "depth_bytes", "uses_depth", "writes_depth", "color_profile",
             "inline_depth_test", "color_direct", "depth_direct", "vertices",
             "texture_data", "projected_texture_data", "linear_blit_texture_data",
             "multi_texture_data", "single_texture_data", "pitch_tex5_texture_data",
             "inline_texture_data", "palette_data", "threadable", "queueable"]
missing = [f for f in INVENTORY if not re.search(r"\b%s\b" % f, job_struct)]
check("every inventory field is present", not missing, repr(missing))
latch = body(cpp, "void Nv2a::latch_draw_job(DrawJob& job) {",
             "bool Nv2a::execute_draw_job(")
check("the raster state is copied whole, not referenced",
      "job.raster_state = raster_state_;" in latch)
check("the transform batch is MOVED, not copied",
      "job.vertices = std::move(transformed_batch_);" in latch)
check("the raster-sample index is carried BY VALUE (F16)",
      "job.raster_state_sample_index = current_raster_state_sample_index_;" in latch
      and "tracked_raster_sample" not in latch)
check("the transform buffer is handed back at retire (inline path)",
      "transformed_batch_ = std::move(job.vertices);" in
      body(cpp, "void Nv2a::retire_draw_job(", "void Nv2a::fold_draw_job("))
# A queued job keeps the buffer until the drain, so the producer must be handed a
# recycled one instead -- otherwise every threaded draw allocates a fresh vector
# and the "the move is free" property of stage (d) silently stops holding.
check("a queued job's transform buffer is recycled, not dropped",
      "draw_free_vertex_buffers_" in cpp and
      "transformed_batch_ = std::move(draw_free_vertex_buffers_.back());" in cpp)

# -------------------------------------------------------------------------
print("\nthe threadable flag follows the design's rules")
# -------------------------------------------------------------------------
check("direct pointers are required (section 1.3)",
      "seam_note_threadable(job, false, kSeamNoDirectPointer);" in witnessed and
      "const bool direct_ok = color_direct != nullptr &&" in witnessed)
check("the per-write hash fold forces inline (section 4.3 O3)",
      "seam_note_threadable(job, false, kSeamHashOn);" in witnessed)
check("an installed debug callback forces inline (F15)",
      "seam_note_threadable(job, false, kSeamDebugCallback);" in witnessed and
      "job.debug_cb_installed" in latch)
check("the GL path forces inline (F3)",
      generic.count("seam_note_threadable(job, false, kSeamRendererPath);") == 3,
      str(generic.count("seam_note_threadable(job, false, kSeamRendererPath);")))
check("the immediate-quad family is inline-only (no mem_direct_)",
      "seam_note_threadable(job, false, kSeamNoDirectPointer);" in immquad)
# Stage (d) measured the flag and acted on nothing. Stage (e) acts on it in
# EXACTLY ONE place -- the enqueue branch -- and nowhere else; that single `if` is
# the whole difference between --nv2a-draw-threads 0 and a threaded run.
retire = body(cpp, "void Nv2a::retire_draw_job(", "void Nv2a::fold_draw_job(")
branches = [m.group(0) for m in
            re.finditer(r"if\s*\(\s*(?:job\.)?threadable\s*\)", cpp)]
# Two readers, and only two: the census in retire_draw_job, and the enqueue
# branch in the latch half. Nothing else in the tree may branch on the flag.
check("the threadable flag is read by the census and by the enqueue branch only",
      len(branches) == 2 and "if (job.threadable) {" in retire and
      "if (job.threadable) {" in witnessed_latch and
      "threadable" not in submit and
      "threadable" not in body(cpp,
                               "bool Nv2a::execute_draw_job(DrawJob& job, bool& executed) {",
                               "void Nv2a::seam_note_threadable("),
      repr(branches))
enqueue = [m.group(0) for m in
           re.finditer(r"if \(draw_threads_ > 0\) \{", cpp)]
check("exactly one enqueue branch, in the latch half, and none in the executor",
      len(enqueue) == 1 and
      "if (draw_threads_ > 0) {" in witnessed_latch and
      "draw_threads_" not in witnessed_exec,
      repr(enqueue))
check("the latch half ends by calling the execute half, and is its only "
      "in-order caller",
      "return execute_witnessed_fragments(job, job.out, 0, 1);" in witnessed_latch
      and witnessed_latch.index(
          "return execute_witnessed_fragments(job, job.out, 0, 1);") >
      witnessed_latch.index("if (draw_threads_ > 0) {"))
# Whole-surface (parts == 1) callers: the latch tail, the stage (e) ring worker
# and the backpressure path. Destination-span (stage (e2), beads-xbox.1.15.9)
# callers: part 0 in execute_draw_spans and the helper in span_worker_main.
# Five call sites, one definition, and no sixth copy of the fragment loop.
check("the execute half is the only fragment loop, and it is reached from the "
      "latch half, the ring worker, the backpressure path and the two "
      "destination-span parts only",
      cpp.count("execute_witnessed_fragments(job, job.out, 0, 1);") == 3
      and cpp.count("execute_witnessed_fragments(job, out, part, parts);") == 1
      and cpp.count("execute_witnessed_fragments(job, job.out, 0, parts);") == 1,
      str(cpp.count("execute_witnessed_fragments(job")))
check("the M5 queue simulation is not gated on the telemetry switch",
      "job.queueable = threadable || reason == kSeamHashOn;" in cpp)
# The resolve anchor separates HIT (every call, the conservative policy) from
# DRAIN (only the resolves that actually move bytes into guest RAM, which is the
# real hazard). Both policies are then readable from one run.
check("the resolve anchor counts hits conservatively and drains tightly",
      cpp.count("seam_anchor(kSeamAnchorResolveOverlap, false);") == 2
      and cpp.count("seam_anchor_drain(kSeamAnchorResolveOverlap);") == 2
      and "seam_anchor(kSeamAnchorResolveOverlap, true);" not in cpp)
# set_frame() is called on every machine slice to advance the frame phase; the
# frame BOUNDARY is the inner condition. An anchor at the top of the function
# would count ~2.9 M slice ticks as vblanks (measured, and fixed).
check("the vblank anchor is the frame boundary, not the per-slice phase tick",
      re.search(r"if \(frame_no > frame_no_\) \{\s*\n\s*// Seam anchor 4", cpp)
      is not None)

# -------------------------------------------------------------------------
print("\nthe anchor list in code equals the anchor list in the design")
# -------------------------------------------------------------------------
enum_src = body(hpp, "    enum SeamAnchor : size_t {", "    static const char* seam_anchor_name")
code_anchors = [m for m in re.findall(r"kSeamAnchor(\w+)", enum_src) if m != "Count"]

# The design's table lives in section 2.2(c); each row's first cell is an anchor.
sec = design[design.index("#### (c) Conservative drain at guest-anchored NV2A anchors"):]
sec = sec[:sec.index("#### (d)")]
rows = [line for line in sec.split("\n")
        if line.startswith("| ") and not line.startswith("| Anchor")
        and not line.startswith("|---")]
doc_anchors = [r.split("|")[1].strip() for r in rows]

# Each code anchor must be identifiable in the corresponding design row by
# keyword; the ORDER must match too, because the enum's order is the table's.
KEYWORDS = [
    ("SemaphoreRelease", ["semaphore", "1d70"]),
    ("ClearSurface", ["clear_surface"]),
    ("Scanout", ["scanout"]),
    ("SetFrame", ["set_frame"]),
    ("FlipStallClear", ["flip stall", "resume_pushbuffers"]),
    ("TextureOverlap", ["texture snapshot", "pending destination"]),
    ("PgraphRegisterRead", ["pgraph register", "read"]),
    ("ResolveOverlap", ["resolve_renderer_"]),
    ("SurfaceRetarget", ["surface descriptor change"]),
    ("Checkpoint", ["frame-series checkpoint", "shutdown"]),
]
check("the design lists exactly ten anchors", len(doc_anchors) == 10,
      "%d: %r" % (len(doc_anchors), doc_anchors))
check("the code declares exactly ten anchors", len(code_anchors) == 10,
      repr(code_anchors))
check("code anchor order matches the design table order",
      code_anchors == [k for k, _ in KEYWORDS], repr(code_anchors))
for index, (name, words) in enumerate(KEYWORDS):
    if index >= len(doc_anchors):
        check("design row %d exists" % index, False)
        continue
    row = doc_anchors[index].lower()
    check("anchor %-20s matches design row %r" % (name, doc_anchors[index][:40]),
          all(w in row for w in words))

# Every anchor must actually be raised somewhere in the tree.
for name in code_anchors:
    token = "kSeamAnchor%s" % name
    raised = ("seam_anchor(%s," % token) in cpp
    if name == "TextureOverlap":
        raised = raised or "seam_texture_source_anchor(" in cpp
    if name == "Checkpoint":
        raised = raised and "seam_checkpoint_anchor()" in main
    check("anchor %s is raised in the source" % name, raised)
check("the texture-overlap anchor is raised on every witnessed texture snapshot",
      "seam_texture_source_anchor(address, bytes);" in witnessed)
check("the checkpoint anchor is raised by the frame-series checkpoint",
      "mach.nv2a()->seam_checkpoint_anchor();" in main)

# -------------------------------------------------------------------------
print("\nthe census is per batch, never per fragment")
# -------------------------------------------------------------------------
# The per-fragment path is the innermost loop; no seam counter may appear inside
# the triangle rasterizer lambdas.
tri = witnessed[witnessed.index("auto rasterize_triangle ="):]
tri = tri[:tri.index("    const size_t vertex_count = transformed_batch.size();")]
check("no seam counter inside the triangle rasterizer",
      not re.search(r"\bseam_", tri),
      repr(re.findall(r"\bseam_\w+", tri)[:5]))
check("the census is printed as an interval delta and a cumulative total",
      'scope=%s deadline=%llu jobs_latched' in main and
      '"interval"' in main and '"cumulative"' in main)

# -------------------------------------------------------------------------
print("\nthe perf_excl regions are intact")
# -------------------------------------------------------------------------
check("kNv2aVertex still wraps submit_vertex_batch, and now the latch with it",
      "perf_excl::Scope region(perf_excl::kNv2aVertex);" in submit and
      submit.index("perf_excl::kNv2aVertex") < i_latch)
check("kNv2aRasterWitnessed still opens the witnessed body",
      "perf_excl::Scope region(perf_excl::kNv2aRasterWitnessed);" in witnessed and
      witnessed.index("perf_excl::kNv2aRasterWitnessed") <
      witnessed.index("const auto& raster_state = job.raster_state;"))
check("kNv2aRasterGenericSw still opens the generic body",
      "perf_excl::Scope region(perf_excl::kNv2aRasterGenericSw);" in generic)
check("the raster timing bucket still wraps the executor call",
      "t.emplace(perf::g_ns_raster, perf::g_calls_raster);" in submit and
      submit.index("perf::g_ns_raster") < i_exec)

print("")
if failures:
    print("seam structural guards: %d of %d FAILED" % (len(failures), checks))
    for f in failures:
        print("  - %s" % f)
    sys.exit(1)
print("NV2A seam structural guards: all %d assertions hold" % checks)
