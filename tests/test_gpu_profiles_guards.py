#!/usr/bin/env python3
"""Structural guards for the stage (f) witnessed-profile GPU executor.

Bead beads-xbox.1.15.7; design docs/reference/graphics/DESIGN-NV2A-SEAM.md section 5(f);
acceptance docs/reference/testing/PERF-METHODOLOGY.md section 3 (behavioral faithfulness).
Predecessors: tests/test_seam_guards.py (stage (d)),
tests/test_draw_threads_guards.py (stage (e)),
tests/test_span_raster_guards.py (stage (e2)).

Stage (f) executes the two dominant witnessed profiles -- kSwizzledRadialColor
and kPitchTexturedColor, 83 percent of all fragments -- on the GPU with the
destination surfaces RESIDENT across consecutive GPU jobs, so that guest memory
is touched only when a CPU consumer needs it.

The properties that make that faithful are not checkable by the compiler and
are not observable in a passing replay: a run with the flag off exercises none
of it, and a coherence hole only shows when a surface happens to stay resident
across the right pair of accesses. They are pinned here:

  1. Selection is an EXACT STATE PREDICATE, not a profile test. The predicate
     re-asserts the combiner words and the raster subset independently of the
     witnessed matcher, so relaxing a matcher cannot silently widen what the
     GPU claims to implement.
  2. The predicate is independent of the flag and of the renderer, so the
     residency census measures the same partition in both arms of an A/B.
  3. The CPU executor is structurally untouched: execute_witnessed_fragments
     is still the only implementation of the software pixel math and the GPU
     branch is one `if` in the latch half.
  4. Residency transitions are complete: a GPU job on a CPU-authoritative
     surface uploads first, a CPU consumer of a GPU-authoritative surface
     resolves first, and every renderer entry point that could observe a
     deferred batch flushes it.
  5. The GPU path does NOT resolve its own destination -- that resolve is the
     thing the design exists to avoid -- and the CPU fallback still does.
  6. No GL call is reachable from a worker thread: the executor runs in the
     latch half, on the emulation thread, before the enqueue branch.
  7. The flag defaults OFF and both spellings exist on the same binary.
  8. The always-on census exists and is exported, including the reject reasons,
     because a GPU arm whose jobs count is zero measured nothing.

Run: python tests/test_gpu_profiles_guards.py   (exit 0 = all guards hold)
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NV2A_CPP = os.path.join(ROOT, "src", "machine", "nv2a.cpp")
NV2A_HPP = os.path.join(ROOT, "src", "machine", "nv2a.hpp")
RENDERER_HPP = os.path.join(ROOT, "src", "machine", "nv2a_renderer.hpp")
GL_CPP = os.path.join(ROOT, "src", "host", "nv2a_opengl_renderer.cpp")
SHADERS = os.path.join(ROOT, "src", "host", "nv2a_witnessed_gl_shaders.hpp")
MAIN_CPP = os.path.join(ROOT, "src", "app", "main.cpp")

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
    start = text.index(signature)
    end = text.index(terminator, start + len(signature))
    return text[start:end]


cpp = read(NV2A_CPP)
hpp = read(NV2A_HPP)
rhpp = read(RENDERER_HPP)
gl = read(GL_CPP)
shaders = read(SHADERS)
main = read(MAIN_CPP)

PREDICATE = "bool Nv2a::gpu_profile_state_ok(const DrawJob& job) const {"
SUBMIT = "bool Nv2a::gpu_profile_submit(DrawJob& job, bool& submitted) {"
LATCH = "bool Nv2a::rasterize_witnessed_batch(DrawJob& job, bool& executed) {"
EXEC = "bool Nv2a::execute_witnessed_fragments(DrawJob& job, DrawJobTelemetry& out,"
for sig in (PREDICATE, SUBMIT, LATCH, EXEC):
    if cpp.count(sig) != 1:
        print("FATAL: %r appears %d times" % (sig[:70], cpp.count(sig)))
        sys.exit(2)

predicate = body(cpp, PREDICATE, "bool Nv2a::gpu_profile_non_finite_reaches_raster(")
submit = body(cpp, SUBMIT, LATCH)
latch_half = body(cpp, LATCH, EXEC)
exec_half = body(cpp, EXEC, "void Nv2a::record_generic_skip(")

# -------------------------------------------------------------------------
print("\n1. selection is an exact state predicate, per profile")
# -------------------------------------------------------------------------
# The words below are the fingerprints the two witnessed matchers pin. The
# predicate must re-assert them itself: if it only tested `profile ==`, a future
# relaxation of a matcher would hand the GPU state its shader does not implement.
RADIAL_WORDS = ["0x00000004", "0x00011103", "0x48480000", "0x2D2D0000",
                "0xC4CD0000", "0x3D3D1010", "0xD4DD1010", "0x000820D0",
                "0x0000000C", "0x00002080"]
TEXTURED_WORDS = ["0x00010C60", "0x00011108", "0xC149C14A", "0x0D0D0000",
                  "0xC10CC20D", "0xC40520C1", "0x000C30CD", "0xD159D15A",
                  "0x0C0B000F", "0x0C011180", "0xFF3F3F3F"]
for word in RADIAL_WORDS:
    check("the predicate pins the radial combiner word %s" % word,
          word in predicate)
for word in TEXTURED_WORDS:
    check("the predicate pins the textured combiner word %s" % word,
          word in predicate)
check("the predicate pins the shared raster subset (no blending, LEQUAL, "
      "full colour mask, smooth shading, fill mode, back-face cull)",
      all(token in predicate for token in
          ("kRasterBlendSrc] != 1", "kRasterBlendDst] != 0",
           "kRasterDepthFunc] != 0x0203", "kRasterColorMask] != 0x01010101",
           "kRasterShadeMode] != 0x1D01", "kRasterCullFace] != 0x0405",
           "kRasterZMinMaxControl] != 1")))
check("the predicate refuses polygon offset, which the depth expression omits",
      "kRasterEnables] & (1u << 13)" in predicate)
check("the predicate refuses the per-write FNV fold and the debug callbacks",
      "job.hash_on" in predicate and "job.debug_cb_installed" in predicate)
check("the predicate pins the destination layout per profile "
      "(swizzled for radial, pitch for textured)",
      "radial != (surface_type == 2)" in predicate)
check("the predicate requires the depth plane to be present and written",
      "!job.uses_depth || !job.writes_depth" in predicate)
check("the predicate bounds the fixed-point coordinates so the shader's "
      "double edge products stay exact",
      "kGpuFixedCoordinateLimitExact" in hpp and
      "kFixedLimit" in predicate)

# -------------------------------------------------------------------------
print("\n2. the predicate is flag-independent, so both A/B arms measure it")
# -------------------------------------------------------------------------
check("gpu_profile_state_ok does not read the flag or the renderer",
      "gpu_profiles_enabled_" not in predicate and
      "renderer_" not in predicate)
check("the flag and the renderer gate the SUBMISSION, at the call site",
      "gpu_profile_candidate && gpu_profiles_enabled_ && renderer_" in latch_half)
check("the residency census is driven by the same predicate",
      "const bool gpu_profile_candidate = gpu_profile_state_ok(job);"
      in latch_half)

# -------------------------------------------------------------------------
print("\n3. the CPU executor is structurally unchanged")
# -------------------------------------------------------------------------
check("execute_witnessed_fragments is still the only software pixel math",
      cpp.count("bool Nv2a::execute_witnessed_fragments(") == 1)
check("the executor contains no GPU-path branch at all",
      "gpu_profile" not in exec_half)
check("the executor still reaches no renderer_ member",
      "renderer_->" not in exec_half)
check("the GPU branch is one `if` in the latch half",
      latch_half.count("gpu_profile_submit(") == 1)
check("a declined GPU job falls through to the unchanged CPU path",
      "if (gpu_submitted) return true;" in latch_half and
      "++gpu_profile_declined_;" in latch_half)
check("the specialization lambda dispatch is untouched: still one "
      "emit_batch_primitives switch with 15 instantiations",
      exec_half.count("emit_batch_primitives(std::integral_constant<") == 15)

# -------------------------------------------------------------------------
print("\n4. residency transitions are complete")
# -------------------------------------------------------------------------
check("a GPU job on a non-resident target carries the surface snapshot, "
      "PER PLANE (phase 3): a zeta-only break must not read the colour plane",
      "witnessed_target_residency(" in submit and
      "packet.needs_color_upload = !color_resident;" in submit and
      "packet.needs_depth_upload = !depth_resident;" in submit and
      "if (!color_resident) {" in submit and
      "if (!depth_resident) {" in submit and
      "packet.color_input.resize(" in submit and
      "packet.depth_input.resize(" in submit)
check("the backend uploads only the planes that are not already resident",
      "const bool upload_colour = !colour_resident;" in gl and
      "const bool upload_depth = !depth_resident;" in gl)
check("a slot is matched on EITHER plane, so a colour-only invalidation "
      "cannot strand a dirty zeta in another slot. Phase 4 strengthened this: "
      "the two keys are now looked up INDEPENDENTLY and the planes may live "
      "in different slots, which is why the zeta fallback that used to steal "
      "a colour is gone.",
      "candidate.depth_address == depth_address" in gl and
      "void select_witnessed_slots(" in gl and
      "find_witnessed_depth_slot(depth_address, size)" in gl and
      "const ResidentSurface* select_witnessed_slot(" not in gl)
RANGE_SCOPED = {
    "bool resolve_resident_color_surface(",
    "bool resolve_resident_depth_surface(",
    "void invalidate_resident_color_surface(",
    "void invalidate_resident_depth_surface(",
    "bool upload_resident_color_rows(",
}
FLUSH_POINTS = [
    "bool resolve_resident_color_surface(",
    "bool resolve_resident_depth_surface(",
    "void invalidate_resident_color_surface(",
    "void invalidate_resident_depth_surface(",
    "bool upload_resident_color_rows(",
    "bool submit_generic_combiner(",
    "bool submit_resident_generic_combiner(",
]
for entry in FLUSH_POINTS:
    start = gl.index(entry)
    window = gl[start:start + 900]
    check("%s flushes any pending witnessed batch" % entry.split("(")[0].split()[-1],
          "flush_witnessed_batch" in window)
    # The colour/zeta resolves, the two invalidations and the region upload are
    # RANGE-scoped: they flush only when the request can observe the pending
    # batch. The generic submits and the clear are not range-scoped, because
    # they can retarget the FBO and rebind state the batch depends on.
    if entry in RANGE_SCOPED:
        check("%s scopes that flush to the batch's own range"
              % entry.split("(")[0].split()[-1],
              "flush_witnessed_batch_for" in window)
check("the renderer destructor flushes too, so no batch outlives the context",
      "flush_witnessed_batch_deferred();" in
      body(gl, "~OpenGlNv2aRenderer() override {",
           "const char* name() const override"))
check("a flush failure from a void entry point is latched, never dropped",
      "witnessed_flush_error_" in gl and
      "take_witnessed_flush_error" in gl)
check("a witnessed TEXTURE snapshot resolves an overlapping resident surface "
      "(render-to-texture inside one frame)",
      '"witnessed texture snapshot"' in cpp)
check("the immediate-quad family resolves before and invalidates after, "
      "closing the one witnessed path that had no residency query",
      '"immediate-quad destination"' in cpp and
      '"immediate-quad texture"' in cpp)
check("a colour readback detaches the zeta attachment, so the shared FBO's "
      "effective size is the surface's own",
      "cache_framebuffer_depth_texture(resident_fbo_, 0);" in gl)
check("a zeta readback attaches its own colour texture for the same reason",
      "cache_framebuffer_color_texture(resident_fbo_, surface->texture);\n"
      "        cache_framebuffer_depth_texture(resident_fbo_, "
      "surface->depth_texture);" in gl)
check("two resident surfaces may not alias the same guest range",
      "ALIASING GUARD" in gl)

# -------------------------------------------------------------------------
print("\n5. a GPU job does not resolve its own destination")
# -------------------------------------------------------------------------
resolve_call = latch_half.index("const bool witnessed_resolve_ok =")
gpu_branch = latch_half.index("if (gpu_profile_candidate && gpu_profiles_enabled_")
check("the GPU branch runs BEFORE the witnessed destination resolve",
      gpu_branch < resolve_call)
check("a submitted GPU job returns without reaching that resolve",
      "if (gpu_submitted) return true;" in
      latch_half[gpu_branch:resolve_call])
check("the CPU path still resolves both planes",
      '"witnessed color surface"' in latch_half and
      '"witnessed zeta surface"' in latch_half)
check("the GPU job's own residency query is excluded from the census, "
      "not from the resolve",
      "gpu_res_in_gpu_draw_ = gpu_profile_candidate;" in latch_half)

# -------------------------------------------------------------------------
print("\n6. no GL call is reachable from a worker thread")
# -------------------------------------------------------------------------
check("the GPU branch precedes the enqueue branch, so a deferred job "
      "can never be a GPU job",
      latch_half.index("if (gpu_profile_candidate && gpu_profiles_enabled_") <
      latch_half.index("if (draw_threads_ > 0) {"))
check("the GPU submit is not reachable from execute_draw_spans",
      "gpu_profile_submit" not in
      body(cpp, "bool Nv2a::execute_draw_spans(DrawJob& job) {",
           "bool Nv2a::submit_vertex_batch() {"))
check("the GPU submit is not reachable from the draw worker",
      "gpu_profile_submit" not in
      body(cpp, "void Nv2a::draw_worker_main() {", "void Nv2a::draw_queue_publish("))
check("the GPU branch drains the ring first, so a queued software job "
      "cannot be overtaken",
      "draw_order_fence();" in
      latch_half[gpu_branch:gpu_branch + 900])

# -------------------------------------------------------------------------
print("\n7. the flag default is a named constant and both spellings exist")
# -------------------------------------------------------------------------
# Flipped ON 2026-09-05 (beads-xbox.1.15.10 phase 2 session 2) on a PASSING
# 3+3 interleaved gate: total 12 B wall median -6.11 pct / min -5.85 pct,
# 3 of 3 rounds, golden final SHA on every leg. The guard is that the default
# is a NAMED CONSTANT with both spellings on one binary, so either arm can be
# selected without a rebuild whichever way it points -- not that it points a
# particular way.
check("the default is recorded as a named constant",
      "constexpr bool kNv2aGpuProfilesDefault = " in main)
check("the default is ON, matching the gate that was passed",
      "constexpr bool kNv2aGpuProfilesDefault = true;" in main)
check("--nv2a-gpu-profiles exists", '"--nv2a-gpu-profiles"' in main)
check("--no-nv2a-gpu-profiles exists", '"--no-nv2a-gpu-profiles"' in main)
check("the DEVICE default stays off, so the host flag is the only switch "
      "and a device constructed outside main is inert",
      "bool gpu_profiles_enabled_ = false;" in hpp)
check("the per-profile mask exists so a difference is attributable, and "
      "it has one bit per executor profile (phase 4 added the third)",
      '"--nv2a-gpu-profile-mask"' in main and
      "gpu_profile_mask_ & (radial ? 1u : (textured ? 2u : 4u))" in cpp)
check("the exact/relaxed coordinate bound is a runtime switch, not a rebuild",
      '"--nv2a-gpu-relaxed-coords"' in main and
      "kGpuFixedCoordinateLimitRelaxed" in hpp)

# -------------------------------------------------------------------------
print("\n8. the census is always on and exported")
# -------------------------------------------------------------------------
check("the residency model counts uploads, resolves and their causes",
      all(field in hpp for field in
          ("uint64_t uploads = 0;", "uint64_t resolves = 0;",
           "std::array<uint64_t, kGpuResidencyCauses> cause{};",
           "uint64_t semaphore_resolves = 0;")))
check("the executor census counts jobs, triangles, draw calls and declines",
      all(name in hpp for name in
          ("gpu_profile_jobs()", "gpu_profile_triangles()",
           "gpu_profile_draw_calls()", "gpu_profile_declined()")))
check("every reject reason is named and exported",
      "gpu_reject_reason_name" in hpp and "kGpuRejectReasons" in hpp)
check("main prints the residency census, the executor census and the rejects",
      all(line in main for line in
          ("[host] gpu-residency scope=", "[host] gpu-executor scope=",
           "[host] gpu-executor-reject scope=")))
check("the census runs with the flag off too (it is the sizing instrument)",
      "gpu_res_note_gpu_draw(" in latch_half)

# -------------------------------------------------------------------------
print("\n9. the shader records match the std430 layout they claim")
# -------------------------------------------------------------------------
check("both record sizes are static_asserted against the array stride",
      "sizeof(witnessed_gl::RadialTriRecord) == 224" in gl and
      "sizeof(witnessed_gl::TexturedTriRecord) == 288" in gl)
check("the dvec4 offsets are static_asserted",
      "offsetof(witnessed_gl::RadialTriRecord, recip) == 192" in gl and
      "offsetof(witnessed_gl::TexturedTriRecord, recip) == 256" in gl)
check("the two records agree on the half written through the radial view",
      "offsetof(witnessed_gl::RadialTriRecord, t2) ==" in gl)
check("coverage is re-derived in the shader from the same integer edge "
      "functions, so the driver fill rule cannot add a fragment",
      "wp_edge(" in shaders and "wp_top_left(" in shaders and
      shaders.count("discard;") >= 2)
check("the depth expression is the software one, in the same order",
      "float(w0) * T.zi.x + float(w1) * T.zi.y" in shaders)
check("the perspective interpolation carries the software reciprocals "
      "rather than recomputing them",
      "inverse_area_double" in rhpp and "inverse_w[3]" in rhpp and
      "T.recip.x" in shaders)
check("the fragment shader marks its accumulators precise, so the compiler "
      "may not contract or reassociate them",
      shaders.count("precise float") >= 6)

# -------------------------------------------------------------------------
print("\n10. phase 2 (beads-xbox.1.15.10): the shader runs the software "
      "path's own int64 edge arithmetic, and the census that decided it")
# -------------------------------------------------------------------------
# The phase-2 census measured, per 12 G instructions, that the 1 126 jobs the
# 2^24 coordinate bound rejected caused 806 of the 1 110 witnessed-CPU-draw
# residency breaks -- 0.72 full surface round trips EACH, against 0.029 for
# kPitchProjectedColor and 0.000 for every other CPU profile, including
# kPitchZ16Depth's 142 842 draws. A rejected radial or pitch-textured draw is by
# construction a draw on the surface that is GPU-authoritative at that instant,
# which is why it is the most expensive kind of draw to leave on the CPU. Both
# mechanisms that had made the wider bound unsafe were in the SHADER, and both
# are pinned below.
check("the shader requires GL_ARB_gpu_shader_int64",
      shaders.count("#extension GL_ARB_gpu_shader_int64 : require") >= 4)
check("the edge function and the top-left rule are int64, textually the "
      "software path's own lambdas",
      "int64_t wp_edge(int64_t x0" in shaders and
      "bool wp_top_left(int64_t x0" in shaders and
      "double wp_edge(" not in shaders)
check("the sample point is the software path's int64 pixel centre",
      "int64_t px = int64_t(xg) * 16L + 8L;" in shaders and
      "int64_t py = int64_t(yg) * 16L + 8L;" in shaders)
check("the coverage test compares int64 edge values, not doubles",
      "int64_t w0 = wp_edge(" in shaders and "w0 < 0L" in shaders and
      "0.0LF && !wp_top_left" not in shaders)
check("the barycentric weights convert the int64 edge value the way the "
      "software path does",
      "double q0 = (double(w0) * inv_area) * T.recip.y;" in shaders)
check("the packet carries the fixed-point coordinates as exact int64, not "
      "as doubles",
      "int64_t ax = 0, ay = 0, bx = 0, by = 0, cx = 0, cy = 0;" in rhpp and
      "double ax = 0, ay = 0" not in rhpp)
check("the SSBO records declare i64vec4 for the coordinate pair, in every "
      "shader source (2 radial + 2 textured + 1 projected)",
      shaders.count("i64vec4 ab;") == 5 and shaders.count("i64vec4 cp;") == 5)
check("the CPU-side records match, so the std430 stride is unchanged",
      "int64_t ab[4];" in shaders and "int64_t cp[4];" in shaders and
      "sizeof(witnessed_gl::RadialTriRecord) == 224" in gl)
# Under-coverage is the ONE error the in-shader test cannot repair: it may
# remove a candidate fragment the driver generated, never add one. A triangle
# with a 2^34 coordinate cannot be placed by the fixed-function rasterizer in
# float clip space, so such a triangle is drawn as the software path's own
# scanline bounding box and the edge test carves the triangle out of it.
check("the packet carries the software path's scanline box and a wide flag",
      "bbox_xmin" in rhpp and "bbox_ymax" in rhpp and
      "bool wide = false;" in rhpp)
check("the wide flag is set from the 2^24 float-placement bound, not from "
      "the eligibility bound",
      "const int64_t kWide = 1ll << 24;" in cpp and
      "t.wide = is_wide(ax)" in cpp)
check("the scanline box is computed by the software path's own expression",
      "- 8.0) / 16.0))" in cpp and
      "t.bbox_xmin = scan_min(xmin, ax, bx, cx);" in cpp)
check("the vertex shader emits six vertices per record and switches on the "
      "wide flag",
      "int t = gl_VertexID / 6;" in shaders and
      "if (tris[t].cp.w != 0L) {" in shaders and
      "int t = gl_VertexID / 3;" not in shaders)
check("the draw issues six vertices per record to match",
      "static_cast<int>(count * 6u)" in gl and
      "static_cast<int>(count * 3u)" not in gl)
check("the relaxed bound is the default and the old one is an opt-in "
      "bisection tool, not a rebuild",
      '"--nv2a-gpu-exact-coords"' in main and
      "bool gpu_exact_coords_ = false;" in hpp and
      "gpu_exact_coords_ ? kGpuFixedCoordinateLimitExact" in cpp)
check("the per-profile residency attribution census exists, so the work "
      "order follows breaks rather than draw counts",
      "cpu_draws_profile" in hpp and "breaks_profile" in hpp and
      "gpu_res_profile_name" in hpp and
      "gpu-residency-profile" in main)
check("a break is attributed exactly once when both planes are resident",
      "resolves_before" in cpp and
      "gpu_res_.resolves == resolves_before" in cpp)
check("the attribution is taken at the classification point, before the "
      "residency queries that take the break",
      "gpu_res_cpu_profile_ = job.profile;" in cpp)


# -------------------------------------------------------------------------
print("\n11. phase 2 session 2 (beads-xbox.1.15.10): CPU/GPU overlap, "
      "single-precision fragment math, and the staging transforms")
# -------------------------------------------------------------------------
# The mechanism these pin was MEASURED, not argued. With the batch issued only
# when a resolve needed it, a matched instrumented 12 B pair read gpu_draw_ns
# 12.098 s of GPU work and drain_ns 9.639 s of emulation-thread time spent
# blocking on that same work; bounding the batch and submitting it took drain
# to 3.453 s and the whole wall from 81.716 s to 75.869 s. None of that is
# visible in a passing replay, and all of it silently reverts if a future edit
# drops the flush or the bound.
SUBMIT_GL = body(gl, "bool submit_witnessed_profile(", "bool flush_witnessed_batch(")
FLUSH_GL = body(gl, "bool flush_witnessed_batch(std::string& error) {",
                "// Flush ONLY when the requested guest range")
check("the pending batch has a triangle bound and the bound issues it",
      "witnessed_flush_triangles_ != 0 &&" in SUBMIT_GL and
      "witnessed_batch_count_ >= witnessed_flush_triangles_" in SUBMIT_GL and
      "flush_witnessed_batch(error)" in SUBMIT_GL)
check("issuing a batch SUBMITS it to the driver (glFlush) rather than "
      "leaving it for the next resolve to submit",
      "flush_();" in FLUSH_GL and
      FLUSH_GL.index("gpu_timed_draw_arrays(") < FLUSH_GL.index("flush_();"))
check("the submit is NOT gated on perf accounting -- it is the shipping "
      "path, not an instrument",
      "if (flush_ && witnessed_kick_flush_) {" in FLUSH_GL and
      "gpu_timing_active()" not in FLUSH_GL)
check("the frame boundary kicks a partial batch, and the kick is a pure "
      "issue: it neither reads back nor touches guest memory",
      "renderer_->kick_pending_gpu_work();" in cpp and
      "void kick_pending_gpu_work() override {" in gl and
      "flush_witnessed_batch_deferred();" in
      body(gl, "void kick_pending_gpu_work() override {",
           "void set_witnessed_flush_triangles("))
check("the bound, the submit and the frame kick are each a runtime switch "
      "so the overlap can be bisected without a rebuild",
      all(flag in main for flag in
          ('"--nv2a-gpu-flush-triangles"', '"--no-nv2a-gpu-vblank-kick"',
           '"--no-nv2a-gpu-kick-flush"')))
check("the SSBO is a ring, so a refill cannot land on a buffer an "
      "in-flight draw is still reading",
      "witnessed_ssbo_ring_" in gl and
      "witnessed_ssbo_ring_head_ + 1) % witnessed_ssbo_ring_size_" in gl and
      '"--nv2a-gpu-ssbo-ring"' in main)
check("the early-flush witness is always on and exported, so 'did the "
      "batch actually issue early' is measured, not inferred",
      "witnessed_flush_stats" in rhpp and
      "[host] gpu-executor-flush scope=" in main and
      "gl_flushes=%llu" in main)
# Caught by spot-checking a real log against the source: fp32_shader was being
# PASSED to printf and never appeared in the format string, so the census
# silently dropped the one field that says which shader variant ran. Every
# member of WitnessedFlushStats must therefore be named in the printed line,
# and the conversion count must match the argument count.
FLUSH_LINE = body(main, "const auto fs = nv->witnessed_flush_stats", "[host] gpu-executor-surfaces")
check("every WitnessedFlushStats member is named in the printed census line",
      all(field + "=" in FLUSH_LINE for field in
          ("bound", "size_flushes", "kick_flushes", "gl_flushes",
           "vblank_kick", "ssbo_ring", "kick_flush", "fp32_shader")))
def printf_arity(text, marker):
    """(conversions, arguments) of the std::printf call containing `marker`."""
    call = text[text.index(marker):]
    call = call[:call.index(");") + 1]
    conversions = (call.count("%s") + call.count("%llu") + call.count("%u") +
                   call.count("%d") + call.count("%.1f") + call.count("%x"))
    # Argument list: everything after the last string literal of the format.
    # Everything after the format literal begins with the comma that separates
    # it from the first argument; strip that one so it is not counted twice.
    tail = call[call.rindex('"') + 1:].lstrip()
    if tail.startswith(","):
        tail = tail[1:]
    depth, args = 0, 1
    for ch in tail:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            if depth == 0:
                break
            depth -= 1
        elif ch == "," and depth == 0:
            args += 1
    return conversions, args


_conv, _args = printf_arity(FLUSH_LINE, '"[host] gpu-executor-flush scope=')
check("the census printf has exactly as many conversions as arguments",
      _conv == _args, "conversions=%d args=%d" % (_conv, _args))
# Single precision. The record layout must be IDENTICAL between the variants,
# because that is what makes it a same-binary A/B rather than two builds.
check("there is ONE source per stage with a WP_FP32 switch, not two copies "
      "(the projected fragment program adds a third pair of branches)",
      shaders.count("#if WP_FP32") == 6 and
      shaders.count("#else") >= 6 and
      "#define WP_FP32" in gl)
check("the define is always emitted, so an undefined name cannot silently "
      "select a branch",
      '"#define WP_FP32 1\\n" : "#define WP_FP32 0\\n"' in gl)
check("the FP32 branch reads its reciprocals out of wv and the FP64 branch "
      "out of recip, so the std430 record is byte-identical either way",
      "float inv_area = T.wv.x;" in shaders and
      "double inv_area = T.recip.x;" in shaders and
      "sizeof(witnessed_gl::RadialTriRecord) == 224" in gl and
      "offsetof(witnessed_gl::RadialTriRecord, recip) == 192" in gl)
check("the CPU side writes the single-precision copies of exactly those "
      "four reciprocals into wv",
      "common->wv[0] = static_cast<float>(t.inverse_area_double);" in gl and
      "common->wv[3] = static_cast<float>(t.inverse_w[2]);" in gl)
check("coverage and depth stay INT64 in every variant and every program -- "
      "only the barycentric interpolation changes precision",
      shaders.count("int64_t w0 = wp_edge(") == 3 and
      "#if WP_FP32" not in shaders[:shaders.index("int64_t w0 = wp_edge(")])
check("the variant is a runtime switch with both spellings on one binary",
      '"--nv2a-gpu-fp32-shader"' in main and
      '"--no-nv2a-gpu-fp32-shader"' in main and
      "bool set_witnessed_fp32_shader(bool on, std::string& error)" in rhpp)
check("switching the variant rebuilds the programs and drops every cached "
      "program binding and uniform mirror",
      "witnessed_bound_program_ = 0;" in
      body(gl, "bool set_witnessed_fp32_shader(bool on, std::string& error) override {",
           "void set_witnessed_ssbo_ring(") and
      "uniforms_valid = false;" in
      body(gl, "bool set_witnessed_fp32_shader(bool on, std::string& error) override {",
           "void set_witnessed_ssbo_ring("))
# Staging. Both directions must use the SAME byte interpretation, or the
# round trip stops being the identity it is relied on to be.
ENSURE_GL = body(gl, "bool ensure_witnessed_surface(",
                 "bool compile_resident_dashboard(std::string& error) {")
check("the colour upload and the colour resolve both use GL_BGRA, so the "
      "guest A8R8G8B8 word is moved in place in both directions",
      ENSURE_GL.count("kGlBgra") == 2 and
      "kGlBgra, kGlUnsignedByte" in
      body(gl, "bool resolve_resident_color_surface(",
           "bool resolve_resident_depth_surface("))
check("a pitch destination stages by ROW, not by pixel",
      "if (!packet.swizzled_destination) {" in ENSURE_GL and
      ENSURE_GL.count("std::memcpy(dst_base + static_cast<size_t>(height - 1u - y) * row_bytes,") == 1)
check("the Morton scatter uses the separable x/y tables rather than "
      "recomputing the interleave per pixel",
      "swizzle_tables_.ensure(" in gl and
      "sx[x] | yb" in gl and
      gl.count("witnessed_swizzle_index(x, y,") == 0)
check("the separable identity is asserted where it is used, and the "
      "tables are built from the SAME function they replace",
      "witnessed_swizzle_index(i, 0, w, h)" in gl and
      "witnessed_swizzle_index(0, j, w, h)" in gl)
check("an unchanged geometry re-uploads through glTexSubImage2D, and every "
      "path that re-specifies the storage updates the tracked allocation -- "
      "there are five such paths after phase 7 (generic colour, witnessed "
      "colour, generic zeta, witnessed zeta, generated zeta) plus the "
      "generated colour and texture pair, and glTexImage2D appears in each "
      "exactly as often as the allocation is stamped",
      gl.count("tex_alloc_w = width;") == 2 and
      gl.count("tex_alloc_w = up_w;") == 2 and
      gl.count("slot.alloc_w = texture.width;") == 2 and
      gl.count("depth_alloc_w = width;") == 3)
check("the zero fill is kept exactly where the Morton map is not onto, "
      "so an unwritten hole cannot carry a previous surface's bytes",
      "static bool is_power_of_two(uint32_t v)" in gl and
      "buffer.assign(elements, T{});" in gl and
      gl.count("resize_or_zero(") >= 4)
# The Morton index is not merely non-surjective off the power-of-two case, it
# OVERSHOOTS: at 1280x480 its largest value is 718 847 against w*h - 1 =
# 614 399, so a swizzled destination of a non-power-of-two geometry would index
# past the end of a buffer sized to the surface, in the upload and in both
# resolves. The predicate must decline the job rather than the renderer
# clamping it, because a renderer refusal here is a fault and a decline is the
# CPU path.
check("the eligibility predicate declines a swizzled destination whose "
      "extents are not both powers of two",
      "power_of_two(job.surface_width)" in predicate and
      "power_of_two(job.surface_height)" in predicate and
      "surface_type == 2 &&" in predicate and
      "++gpu_profile_reject_[kGpuRejectSurface];" in predicate)
check("the readback scratch buffers are recycled rather than reallocated "
      "and zero-filled per resolve",
      "std::vector<uint32_t>& gl_depth = depth_readback_scratch_;" in gl and
      "resolve_readback_scratch_.resize(" in gl)


# -------------------------------------------------------------------------
print("\n12. phase 3 (beads-xbox.1.15.10): transfer volume")
perf = read(os.path.join(ROOT, "src", "machine", "perf_accounting.hpp"))
# -------------------------------------------------------------------------
# The phase-2 census was a SIMULATION with a one-active-target model. It
# reported 647 residency breaks where the backend actually performed 1336
# surface snapshots and 662 + 662 plane uploads, and it reported
# cause=clear_surface resolves=0 for what the direct measurement shows to be
# the single largest cause. Everything in this section exists so that number
# comes from the backend that performs the transfer, not from a model of it.
check("the invalidation interface carries an attribution token, and the "
      "backend stamps the plane it drops with it",
      rhpp.count("uint32_t cause) {") == 2 and
      "virtual void invalidate_resident_color_surface(uint32_t address, size_t size," in rhpp and
      "virtual void invalidate_resident_depth_surface(uint32_t address, size_t size," in rhpp and
      "stamp_invalidate(color_stamps_, surface.address, cause);" in gl and
      "stamp_invalidate(depth_stamps_, surface.depth_address, cause);" in gl)
check("every device invalidation site names its cause -- an unnamed one "
      "would land in `other` and hide a whole consumer",
      cpp.count("nv2a_invalidate_token(kNv2aInvalidate") >= 9 and
      "void Nv2a::invalidate_renderer_color_overlap(uint32_t address, size_t size," in cpp and
      "void Nv2a::invalidate_renderer_depth_overlap(uint32_t address, size_t size," in cpp and
      cpp.count("invalidate_renderer_color_overlap(out.invalidate_color_address") == 1)
check("the witnessed CPU draw carries its PROFILE in the token, so the "
      "census names which profile is paying for the round trips",
      cpp.count("nv2a_invalidate_token(kNv2aInvalidateWitnessedCpuDraw,") == 2 and
      exec_half.count("static_cast<uint32_t>(profile))") >= 2)
check("the stamp table reports its own overflow, so a full table cannot "
      "silently inflate the `cold` bucket",
      "++stamp_overflows_;" in gl and
      "out.stamp_overflows = stamp_overflows_;" in gl and
      "stamp_overflows=%llu" in main)
check("a slot handed to a different surface stamps the residency it ended, "
      "so an eviction is not reported as a first touch",
      "ResidentSurface* note_slot_reuse(ResidentSurface& surface)" in gl and
      "kNv2aInvalidateEvicted" in gl and
      gl.count("return note_slot_reuse(surface);") == 2)
check("an upload that would discard a plane the GPU wrote is COUNTED; it "
      "must stay zero and a nonzero value is silent loss of output",
      "++witnessed_upload_census_.upload_over_dirty_color;" in gl and
      "++witnessed_upload_census_.upload_over_dirty_depth;" in gl and
      "over_dirty_color=%llu" in main)
check("the residency predicate the device asks and the decision the backend "
      "acts on are ONE function, so they cannot drift",
      gl.count("select_witnessed_slots(") == 4 and
      "select_witnessed_slots(packet.color_address, packet.surface_size," in gl and
      "void select_witnessed_slots(" in gl)
check("an eligible job whose triangle list is empty reads NO guest surface "
      "memory: the backend discards it before ensure_witnessed_surface",
      "gpu_skip_empty_packets_ && packet.triangles.empty();" in cpp and
      "++gpu_profile_empty_packets_;" in cpp and
      "if (packet.triangles.empty()) return true;" in gl)
check("the empty-packet skip is bisectable on one binary and defaults ON",
      "bool gpu_skip_empty_packets_ = true;" in hpp and
      '"--no-nv2a-gpu-skip-empty-packets"' in main and
      '"--nv2a-gpu-skip-empty-packets"' in main)

# --- step 3: CLEAR_SURFACE as glClear ------------------------------------
check("clear_resident_surface has a CALLER. It existed since phase 1 with "
      "none, which is why the simulation modelled a glClear that never ran",
      "renderer_->clear_resident_surface(" in cpp)
check("the clear eligibility test has NO side effects and never faults: a "
      "decline must fall through to the software path, in its original order",
      "bool Nv2a::clear_plane_extent(" in cpp and
      "fault_" not in body(cpp, "bool Nv2a::clear_plane_extent(",
                           "bool Nv2a::clear_surface_plane(") and
      "resolve_renderer_" not in body(cpp, "bool Nv2a::clear_plane_extent(",
                                      "bool Nv2a::clear_surface_plane("))
check("only a WHOLE-PLANE, FULLY MASKED fill is eligible; a partial mask is "
      "a read-modify-write of guest bytes and cannot become a glClear",
      "if (write_mask != pixel_mask) return false;" in cpp and
      "if (x_bytes != 0 || row_bytes != pitch || ymin != 0) return false;" in cpp)
check("a GPU-served clear skips the resolve, the guest fill AND the "
      "invalidation -- resolving would write back the PRE-clear image",
      "if (skip_memory) return true;" in cpp and
      "uint32_t& first_address, bool skip_memory," in cpp and
      "bool skip_invalidate) {" in cpp)
check("a CLEAN resident plane keeps its guest fill and stays clean, so a "
      "glClear cannot manufacture a resolve for the next consumer",
      "needs_guest_fill = false;" in gl and
      "needs_guest_fill = true;" in gl and
      gl.count("needs_guest_fill") >= 5 and
      "bool resident_kept_clean_color = false;" in cpp and
      "if (skip_invalidate) return true;" in cpp and
      "resident_cleared_color ||" in cpp)
check("every fault check in clear_surface_plane still runs before the skip, "
      "so a malformed clear fails identically in both arms",
      body(cpp, "bool Nv2a::clear_surface_plane(", "bool Nv2a::clear_surface(")
          .index("if (skip_memory) return true;") >
      body(cpp, "bool Nv2a::clear_surface_plane(", "bool Nv2a::clear_surface(")
          .rindex("NV2A CLEAR_SURFACE %s DMA range is invalid"))
check("the two planes are decided INDEPENDENTLY -- the pair-only form "
      "declined every clear on the canonical workload",
      "bool resident_cleared_color = false;" in cpp and
      "bool resident_cleared_zeta = false;" in cpp and
      "for (int plane = 0; plane < 2; ++plane) {" in cpp)
check("the backend looks the two planes up separately and attaches ONLY "
      "the planes it clears, so a size mismatch cannot clip the clear",
      "ResidentSurface* color_surface = nullptr;" in gl and
      "ResidentSurface* depth_surface = nullptr;" in gl and
      "color_surface ? color_surface->texture : 0u" in gl and
      "depth_surface ? depth_surface->depth_texture : 0u" in gl)
check("the GPU clear keeps the overlap contract the software clear had: "
      "another slot covering the range is dropped",
      body(gl, "bool clear_resident_surface(", "bool resolve_resident_color_surface(")
          .count("resident_overlaps(other,") == 1 and
      body(gl, "bool clear_resident_surface(", "bool resolve_resident_color_surface(")
          .count("resident_depth_overlaps(other,") == 1)
check("a backend ERROR is a fault and a backend DECLINE is the software "
      "path; the two are distinguished by the error string",
      "} else if (!error.empty()) {" in cpp and
      "fault_(error.c_str());" in body(cpp, "bool Nv2a::clear_surface(",
                                       "bool Nv2a::set_vertex_array_format(") and
      "++gpu_clear_decline_resident_;" in cpp)
check("the clear path is bisectable on one binary and defaults ON",
      "bool gpu_clear_enabled_ = true;" in hpp and
      '"--no-nv2a-gpu-clear"' in main and '"--nv2a-gpu-clear"' in main)
check("the clear census reports acceptance AND every decline reason, so a "
      "workload where it never fires is diagnosable, not merely silent",
      "[host] gpu-clear scope=%s" in main and
      "decline_mask=%llu" in main and "decline_geometry=%llu" in main and
      "decline_resident=%llu" in main and
      "[host] gpu-clear-backend scope=%s" in main)
check("the upload census is exported per cause AND per profile, split by "
      "plane, with a total that can be checked against the device's own",
      "[host] gpu-upload-cause scope=%s" in main and
      "[host] gpu-upload-total scope=%s" in main and
      "device_color_uploads=%llu" in main and
      "[host] gpu-invalidate-extent scope=%s" in main)
check("the depth resolves are attributed too: half the readback was not "
      "recorded at all before phase 3",
      "record_renderer_depth_resolve_context(context," in cpp and
      "inline size_t renderer_depth_resolve_context_index(" in perf and
      '"zeta-witnessed-surface"' in perf and
      '"zeta-clear-surface"' in perf)
check("the four colour contexts that used to collapse into `other` have "
      "their own slots",
      '"witnessed-texture-snapshot"' in perf and
      '"immediate-quad-destination"' in perf and
      '"immediate-quad-texture"' in perf and
      '"vblank-sweep-color"' in perf)
check("no GL call was added to a worker thread: the clear is raised from "
      "clear_surface, which runs on the emulation thread behind anchor 2",
      "seam_anchor(kSeamAnchorClearSurface, true);" in
      body(cpp, "bool Nv2a::clear_surface(", "bool Nv2a::set_vertex_array_format("))

# -------------------------------------------------------------------------
print("\n13. phase 4 (beads-xbox.1.15.10): per-plane residency, "
      "kPitchProjectedColor on the GPU, resident-texture ceiling")
# -------------------------------------------------------------------------
# Lever C. The pre-phase-4 selector matched a slot on its COLOUR key and, on a
# miss, fell back to the ZETA key and wrote the packet's colour over whatever
# different valid colour surface that slot still held: 342 of 647 colour
# re-uploads (686.0 MB) per canonical 12 B run. The capacity knob was tried and
# is NULL (8 -> 16 changed the census by exactly nothing). The fix is per-plane
# selection, which is what the generic resident path has always done.
select = body(gl, "    void select_witnessed_slots(",
              "    ResidentSurface* find_reusable_resident_slot()")
ensure = body(gl, "    bool ensure_witnessed_surface(",
              "    bool compile_resident_dashboard(")
check("the two planes are looked up INDEPENDENTLY -- a colour key and a zeta "
      "key, not one slot for both",
      "color_slot =\n            find_resident_surface(" in select and
      "depth_slot = find_witnessed_depth_slot(depth_address, size);" in select)
check("the witnessed zeta key is (address, size) verbatim, NOT the generic "
      "five-field key, so the set of resident planes does not move with the "
      "change",
      "ResidentSurface* find_witnessed_depth_slot(uint32_t depth_address," in gl and
      "candidate.depth_address == depth_address &&" in gl and
      "candidate.depth_size == size" in gl)
check("a colour upload never lands on the slot the zeta of THIS packet lives "
      "in while that slot still holds a different valid colour",
      "if (depth_slot && !depth_slot->valid) {" in ensure and
      "slot = acquire_color_slot(depth_slot);" in ensure)
check("per-plane acquisition is per-plane: taking a slot's colour does not "
      "require its zeta to be clean, and vice versa",
      "ResidentSurface* acquire_color_slot(const ResidentSurface* exclude)" in gl and
      "ResidentSurface* acquire_depth_slot(const ResidentSurface* exclude)" in gl and
      "if (!surface.valid) return note_color_slot_reuse(surface);" in gl and
      "if (!surface.depth_valid) return note_depth_slot_reuse(surface);" in gl)
check("an acquisition that ends one plane's residency stamps ONLY that "
      "plane, so the other plane's next upload is not mislabelled `evicted`",
      "ResidentSurface* note_color_slot_reuse(ResidentSurface& surface) {" in gl and
      "ResidentSurface* note_depth_slot_reuse(ResidentSurface& surface) {" in gl and
      "note_slot_reuse(ResidentSurface& surface) {\n"
      "        note_color_slot_reuse(surface);\n"
      "        note_depth_slot_reuse(surface);" in gl)
check("a zeta upload can never overwrite a DIRTY zeta of a different guest "
      "range -- that would discard GPU depth writes no resolve moved back",
      "if (!slot->depth_valid || !slot->depth_dirty ||" in ensure and
      "depth_slot = acquire_depth_slot(slot);" in ensure)
check("the depth half of the upload writes depth_slot, not the colour slot",
      "depth_slot->depth_valid = true;" in ensure and
      "depth_slot->depth_address = packet.depth_address;" in ensure and
      "gen_textures_(1, &depth_slot->depth_texture);" in ensure)
check("the FBO is assembled from BOTH slots, at ensure time and at flush time",
      "cache_framebuffer_depth_texture(resident_fbo_, depth_slot->depth_texture);"
      in ensure and
      "cache_framebuffer_depth_texture(resident_fbo_,\n"
      "                                        depth_surface->depth_texture);" in gl)
check("the pending batch carries BOTH slots and marks the right plane dirty "
      "on each, and refuses to flush if either is missing",
      "ResidentSurface* witnessed_batch_depth_surface_ = nullptr;" in gl and
      "if (!surface || !depth_surface) {" in gl and
      "depth_surface->depth_dirty = true;" in gl and
      "depth_surface->depth_dirty_rect.add(" in gl)
check("the device's per-plane snapshot decision comes from the SAME selector "
      "the backend acts on, so the two cannot drift",
      "select_witnessed_slots(color_address, size, pitch, height, format,"
      in body(gl, "    void witnessed_target_residency(",
              "    Nv2aWitnessedUploadCensus witnessed_upload_census()"))
check("the split is a same-binary A/B: --no-nv2a-gpu-plane-split restores the "
      "pre-phase-4 pair-scoped rule verbatim, and it defaults ON",
      "if (witnessed_plane_split_) return;" in select and
      "const ResidentSurface* slot = colour_resident ? color_slot : depth_slot;"
      in select and
      '"--no-nv2a-gpu-plane-split"' in main and
      '"--nv2a-gpu-plane-split"' in main and
      "bool nv2a_gpu_plane_split = true;" in main and
      "bool witnessed_plane_split_ = true;" in gl)
check("the census names the new configuration, so the fix is a number: "
      "takeover falls, split rises",
      "uint64_t plane_split_hits = 0;" in rhpp and
      "uint64_t plane_split_uploads = 0;" in rhpp and
      "plane_split_hits=%llu" in main and "plane_split_uploads=%llu" in main and
      "++witnessed_upload_census_.plane_split_hits;" in gl and
      "++witnessed_upload_census_.color_slot_takeover;" in gl)
check("the always-on over-dirty counters still guard the upload, on the plane "
      "that is actually being written",
      "if (slot->valid && slot->dirty)\n"
      "                ++witnessed_upload_census_.upload_over_dirty_color;" in ensure and
      "if (depth_slot->depth_valid && depth_slot->depth_dirty)\n"
      "                ++witnessed_upload_census_.upload_over_dirty_depth;" in ensure)

# Lever A. kPitchProjectedColor: 10 336 CPU draws, 950 M fragments, and the
# only remaining witnessed profile that both alternates on a GPU-authoritative
# surface and carries real fragment work.
check("the state predicate re-asserts the PROJECTED fingerprint itself -- "
      "RS0 0x00010C61, CombinerControl 0x00011107, seven stages -- and does "
      "not merely test the profile enum",
      "} else if (projected) {" in predicate and
      "rs[kRasterShaderStageProgram] != 0x00010C61 ||" in predicate and
      "rs[kRasterCombinerControl] != 0x00011107 ||" in predicate and
      "0x4849484A, 0x0D0D0000, 0xCDCD0000, 0xCDCD0000," in predicate and
      "0x5859585A, 0, 0, 0, 0, 0, 0," in predicate and
      "0x000C30CD, 0x000000D0, 0x000000D0, 0x000000D0," in predicate and
      "for (size_t stage = 0; stage < 7; ++stage) {" in predicate)
check("stage 5 is the projected profile's ONLY free combiner pair (stage 6 is "
      "the textured profile's), and the packet carries whichever applies",
      "if (stage == 5) continue;" in predicate and
      "const size_t factor_stage = projected ? 5u : 6u;" in submit)
check("both sampled images are pinned by the predicate: CLAMP bilinear on the "
      "cube pair and REPEAT bilinear on the stage-0 projective image",
      "rs[kRasterTextureFormat0 + 0] != 0x0771062A ||" in predicate and
      "rs[kRasterTextureAddress0 + 0] != 0x00010101 ||" in predicate and
      "rs[kRasterTextureControl0_0 + 0] != 0x4003FFF0 ||" in predicate and
      "rs[kRasterTextureControl1_0 + 0] != 0x14000000 ||" in predicate and
      "rs[kRasterTextureAddress0 + 1] != 0x00030303 ||" in predicate and
      "rs[kRasterTextureControl0_0 + 1] != 0x4003FFC0 ||" in predicate)
check("a projected job with a missing or wrong-sized image is REJECTED, not "
      "submitted with a stale texture",
      "job.projected_texture_width == 0 ||" in predicate and
      "job.projected_texture_data.size() !=" in predicate and
      "++gpu_profile_reject_[kGpuRejectTexture];" in predicate)
check("the non-finite varying guard covers the projective texcoord too -- "
      "components 0, 1 and 3 of texture[0] -- in the predicate AND in the "
      "reaches-raster refinement",
      "if (projected && c < 2 && !std::isfinite(v.texture[0][c]))" in predicate and
      "if (projected && !std::isfinite(v.texture[0][3]))" in predicate and
      "if (projected && c < 2 && !std::isfinite(v.texture[0][c]))" in
      body(cpp, "bool Nv2a::gpu_profile_non_finite_reaches_raster(", SUBMIT))
check("the packet carries the projective texcoord in the software path's own "
      "component order (0, 1, then 3)",
      "t.tex_p[v][0] = corner[v]->texture[0][0];" in submit and
      "t.tex_p[v][1] = corner[v]->texture[0][1];" in submit and
      "t.tex_p[v][2] = corner[v]->texture[0][3];" in submit and
      "std::array<std::array<float, 3>, 3> tex_p{};" in rhpp)
check("the SSBO record and its 288-byte std430 stride are UNCHANGED: the "
      "projective texcoord rides in the .w lanes the textured profile zeroes",
      "d[3] = projected ? t.tex_p[v][0] : 0.0f;" in gl and
      "a[3] = projected ? t.tex_p[v][1] : 0.0f;" in gl and
      "u[3] = projected ? t.tex_p[v][2] : 0.0f;" in gl and
      "sizeof(witnessed_gl::TexturedTriRecord) == 288" in gl and
      "offsetof(witnessed_gl::TexturedTriRecord, recip) == 256" in gl and
      "const size_t stride = radial ? sizeof(witnessed_gl::RadialTriRecord)\n"
      "                                     : sizeof(witnessed_gl::TexturedTriRecord);"
      in gl)
check("the projected program reuses the TEXTURED vertex shader verbatim, so "
      "the wide-triangle expansion cannot drift between the two",
      "compile_graphics_program(textured_vs.c_str(), projected_fs.c_str()," in gl and
      "witnessed_fp32_source(witnessed_gl::kProjectedFragmentSource)" in gl)
check("the projected fragment shader recomputes coverage and depth from the "
      "SAME int64 edge functions and the same top-left rule",
      shaders.count("int64_t wp_edge(int64_t x0, int64_t y0, int64_t x1, int64_t y1,") == 3 and
      shaders.count("bool wp_top_left(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {") == 3 and
      shaders.count("gl_FragDepth = float(incoming) / 16777215.0;") == 3)
projected_fs = shaders[shaders.index("constexpr const char* kProjectedFragmentSource"):
                       shaders.index("// The CPU-side mirror of the SSBO records")]
check("the projected combiner body is the software path's expression order: "
      "two dot products in expanded space, r1 to the 16th by FOUR squarings, "
      "and the 63/255 scale",
      "r0 += expanded0 * expanded1;" in projected_fs and
      "r1 += expanded0 * expanded2;" in projected_fs and
      "for (int s = 0; s < 4; ++s) r1 = r1 * r1;" in projected_fs and
      "r1 = max(clamp(r1, -1.0, 1.0), 0.0);" in projected_fs and
      "* (63.0 / 255.0);" in projected_fs)
check("the stage-0 sampler is REPEAT, written as the software path's own "
      "wrap, and not through GLSL's sign-undefined `%`",
      "u = u - floor(u);" in projected_fs and
      "int wp_wrap(int v, int size) {" in projected_fs and
      "if (v < 0) return v + size;" in projected_fs and
      "%" not in projected_fs.split("int wp_wrap(int v, int size) {")[1].split("}")[0])
check("the cube pair is still CLAMP-to-edge and still uses texelFetch, so no "
      "hardware fixed-point bilinear weight enters the result",
      "vec3 wp_sample_clamped(float u, float v) {" in projected_fs and
      "u = clamp(u, 0.0, 1.0);" in projected_fs and
      projected_fs.count("texelFetch(texture0,") == 4 and
      projected_fs.count("texelFetch(texture1,") == 4)
check("the software path's two FAULT sites -- a zero projective w and a "
      "non-finite projected varying -- DISCARD here (design F13 forbids the "
      "executor from faulting)",
      "if (proj.z == 0.0) discard;" in projected_fs and
      "if (isinf(p0) || isnan(p0) || isinf(p1) || isnan(p1) ||" in projected_fs)
check("the second image has its own texture object, its own upload cache and "
      "its own unit, and unit 0 is left active afterwards",
      "bool ensure_witnessed_projected_texture(" in gl and
      "unsigned int witnessed_projected_texture_ = 0;" in gl and
      "cache_bind_texture_unit(kGlTexture1, witnessed_projected_texture_);" in gl and
      "// Leave unit 0 active: every other site in this file binds without" in gl)
check("the batch key includes the second image, so two projected packets with "
      "different stage-0 images cannot merge into one draw",
      "uint64_t projected_texture_hash = 0;" in gl and
      "projected_texture_hash == o.projected_texture_hash &&" in gl and
      "key.projected_texture_hash = packet.projected_texture_hash;" in gl)
check("the FP32/FP64 recompile deletes and rebinds the PROJECTED program too",
      "if (witnessed_projected_program_ && delete_program_) {" in gl and
      "witnessed_projected_.uniforms_valid = false;" in gl)
check("the profile is its own mask bit, so it is bisectable without a "
      "rebuild, and the surface census has a slot for it",
      "(radial ? 1u : (textured ? 2u : 4u))" in predicate and
      "parsed > 7" in main and
      "kGpuProfileSurfaceSlots = 3" in hpp and
      "projected_color=%08x" in main)
check("the GPU path counts the projected profile's batches and triangles, "
      "and the software path still counts them when it runs",
      "++pitch_projected_color_batches_;" in submit and
      "pitch_projected_color_triangles_ += triangle_tally;" in submit)
check("the CPU executor is structurally untouched by lever A: "
      "execute_witnessed_fragments still owns the software pixel math",
      "if constexpr (kProfile == Profile::kPitchProjectedColor)" in exec_half and
      "sample_cube_texture" in exec_half and
      "gpu_profile_submit" not in exec_half)

# Lever B is NOT implemented; what is implemented is the always-on measurement
# that says whether it could ever pay, plus a clearly-labelled unsound probe.
check("the witnessed texture-source resolves are counted always-on, so the "
      "size of lever B is a number rather than an argument",
      "++witnessed_texture_source_resolves_;" in cpp and
      "uint64_t witnessed_texture_source_resolves() const" in hpp and
      "[host] gpu-texture-source scope=%s" in main)
check("the ceiling probe is DEFAULT OFF, has no --no- spelling to be flipped "
      "into by accident, is named unsafe, and reports itself in the census",
      "bool gpu_unsafe_skip_texture_resolve_ = false;" in hpp and
      "bool nv2a_gpu_unsafe_skip_texture_resolve = false;" in main and
      '"--nv2a-gpu-unsafe-skip-texture-resolve"' in main and
      '"--no-nv2a-gpu-unsafe-skip-texture-resolve"' not in main and
      "unsafe_skip=%u" in main)
check("the probe skips ONLY the resolve, and the coherence comment that "
      "explains why the resolve exists is still at the call site",
      "if (gpu_unsafe_skip_texture_resolve_) {" in cpp and
      "++witnessed_texture_source_skipped_;" in cpp and
      '"witnessed texture snapshot")' in cpp)

# -------------------------------------------------------------------------
print("\n14. phase 5: the residency story is measured, not modelled")
# -------------------------------------------------------------------------
# Phase 4 section 5 read the -1.3 pct ceiling probe as "mostly the 148 317
# no-op residency queries, about 0.9 s". The direct measurement is 16 ms for
# ALL 312 079 no-op queries in the device and 6.37 s for the 2 258 that
# transfer. The instrument that produced that correction is pinned here so the
# claim cannot drift back to an inference.
check("every residency query is timed, and split by whether it MOVED bytes",
      "struct ResolveQueryTimer {" in cpp and
      "uint64_t color_noop_ticks = 0;" in hpp and
      "uint64_t color_moved_ticks = 0;" in hpp and
      "[host] gpu-resolve-cost scope=%s" in main)
check("the no-op and the transferring column each carry their own denominator, "
      "so a per-call cost is derivable and not assumed",
      "color_noop_ns_each=%.1f" in main and
      "color_moved_ns_each=%.1f" in main)
check("the transferring resolve is further split into the backend call and "
      "the guest-memory writeback, because that is where the seconds are",
      "color_backend_ns=%llu color_memwrite_ns=%llu color_memwrite_mb=%.1f"
      in main)
# The phase-4 rule: a break is attributed to the FIRST CPU consumer, so
# breaks_profile cannot answer "which families must move to free this plane".
check("the per-PLANE consumer table exists beside the per-profile break table, "
      "because only the plane table can answer which families gate a resolve",
      "struct GpuResSurfaceRow {" in hpp and
      "std::array<uint64_t, kGpuResProfileSlots> cpu_draws_profile{};" in hpp and
      "[host] gpu-residency-plane scope=%s" in main and
      "[host] gpu-residency-profile scope=%s" in main)
check("the plane table is taken at the SAME decision point as the pair-scoped "
      "model, so the two are directly comparable",
      latch_half.index("gpu_res_note_surface_consumer(gpu_res_color_surfaces_") <
      latch_half.index("gpu_res_note_gpu_draw(color_address"))
check("the plane table separates the colour and the zeta plane, which is what "
      "makes a depth-only profile visible at all",
      "gpu_res_color_surfaces()" in hpp and "gpu_res_depth_surfaces()" in hpp and
      'plane=color' not in cpp and '{"color", &nv->gpu_res_color_surfaces()}' in main
      and '{"zeta", &nv->gpu_res_depth_surfaces()}' in main)

# -------------------------------------------------------------------------
print("\n15. phase 6: the generated generic-combiner executor (stage f3)")
# -------------------------------------------------------------------------
# The phase-6 census measured that ALL 704 transferring
# `software-generic-color-surface` resolves of the canonical 12 G replay land
# on ONE plane, 0x03AA4000, from EIGHT combiner states across 1 339 software
# fallback batches. This section pins the properties that make the generated
# replacement faithful, none of which a passing replay can demonstrate on its
# own.
GEN_HPP = os.path.join(ROOT, "src", "host", "nv2a_combiner_glsl.hpp")
GEN_CPP = os.path.join(ROOT, "src", "host", "nv2a_combiner_glsl.cpp")
gen_hpp = read(GEN_HPP)
gen_cpp = read(GEN_CPP)
cml = read(os.path.join(ROOT, "CMakeLists.txt"))

# --- the census that ordered the work ------------------------------------
check("the generic-combiner SHADER census is ALWAYS ON and keyed on the words "
      "the generated source is a function of, not on the whole raster block",
      "struct GenericShaderCensusRow {" in hpp and
      "GenericShaderCensusRow* generic_shader_census_row(uint64_t" in hpp and
      "[host] generic-shader scope=%s" in main and
      "generic_shader_row_ = generic_shader_census_row(fp);" in cpp)
check("its resolve column is MEASURED at the call site from the same counter "
      "[host] gpu-resolve-cost's `moved` column comes from, not modelled",
      cpp.count("const uint64_t transfers_before = seam_resolve_transfers_;") >= 3 and
      cpp.count("generic_shader_row_->color_resolves +=") == 2 and
      "generic_shader_row_->texture_resolves" not in cpp.split(
          "row.texture_resolves += generic_batch_texture_resolves_;")[0][-200:])
check("the census records the DESTINATION PLANE of every row, which is the "
      "only thing that can say whether removing a consumer frees a plane",
      "std::array<uint32_t, 4> dest_address{};" in hpp and
      "[host] generic-shader-dest scope=%s" in main)
check("census overflow is COUNTED, never evicted, so a state the table already "
      "knows about cannot be under-reported",
      "++generic_shader_census_.overflow_batches;" in cpp and
      "overflow_batches=%llu" in main)

# --- the generator itself -------------------------------------------------
check("the generator is data driven: one state struct in, one GLSL string out, "
      "and it is built into the host library",
      "std::string nv2a_generate_combiner_fragment_glsl(" in gen_hpp and
      "src/host/nv2a_combiner_glsl.cpp" in cml)
check("the program cache key is a fingerprint over EXACTLY the state the "
      "source depends on -- the per-stage constants, the final factors, the "
      "texture dimensions and the alpha reference are UNIFORMS and must not "
      "appear in it, or one program would be compiled per constant value",
      "uint64_t nv2a_combiner_shader_fingerprint(" in gen_hpp and
      "state.stage_const" not in gen_hpp and
      "uniform vec4 stage_const0[8]" in gen_cpp and
      "uniform float alpha_ref" in gen_cpp)
check("the fingerprint folds only the stages the combiner actually runs",
      "if (i >= state.stage_count) continue;" in gen_hpp)
check("the backend caches compiled programs on that fingerprint and counts "
      "both halves, so 'one program per distinct state' is a reported number",
      "GeneratedProgram* find_generated_program(uint64_t fingerprint)" in gl and
      "++generated_stats_.cache_hits;" in gl and
      "++generated_stats_.programs;" in gl and
      "programs=%llu " in main and "cache_hits=%llu" in main)
check("a program that fails to compile is remembered as failed, so a driver "
      "without the extensions declines once instead of recompiling per batch",
      "gp.failed = true;" in gl and
      "return existing->failed ? nullptr : existing;" in gl and
      "compile_failures=%llu" in main)
check("the compile happens in supports_ , the one place where failure is still "
      "allowed, and NOT in submit_ where the device has already committed",
      "ensure_generated_program(descriptor) != nullptr;" in
      body(gl, "bool supports_generated_shader(", "bool generated_shader_target_resident("))

# --- the phase-1 exactness rules ------------------------------------------
check("coverage is recomputed in the fragment program from the SAME int64 edge "
      "functions and the SAME top-left rule at the SAME 1/16-pixel samples",
      "int64_t gc_edge(int64_t x0" in gen_cpp and
      "bool gc_top_left(int64_t x0" in gen_cpp and
      "int64_t px = int64_t(xg) * 16L + 8L;" in gen_cpp)
check("a triangle the fixed-function rasterizer might under-cover is drawn as "
      "the SOFTWARE PATH's own scanline bounding box instead, and the bound is "
      "a runtime value so the exactness control (0 = always) exists",
      "t.wide = kWide == 0 || is_wide(ax)" in cpp and
      "set_gpu_generic_shader_wide_pixels" in hpp and
      '"--nv2a-gpu-generic-shader-wide"' in main)
check("the perspective reciprocals are CARRIED from the device, in float AND "
      "in double, so the precision question is a runtime switch not a rebuild",
      "double inv_area_double = 0.0;" in rhpp and
      "double inv_w_double[3]" in rhpp and
      "bool fp64_interpolate = true;" in gen_hpp and
      '"--no-nv2a-gpu-generic-shader-fp64"' in main)
check("bilinear filtering is written out explicitly against texelFetch in the "
      "software sampler's own two-stage order, never left to a LINEAR sampler",
      "texelFetch(texture" in gen_cpp and
      "precise vec4 top = c00 + (c10 - c00) * fx;" in gen_cpp and
      "color = top + (bottom - top) * fy;" in gen_cpp)
check("swizzled and A8 textures are unswizzled and expanded by the DEVICE, "
      "which owns the Morton table; legacy upload identity retains FNV "
      "(content-ID transitions have a real GL readback test)",
      "src.swizzle->index(x, y)" in cpp and
      "0x00FFFFFFu |" in cpp and
      "dst.hash = fnv1a_bytes(dst.linear.data()" in cpp and
      "slot.hash == texture.hash" in gl and
      "++generated_stats_.texture_upload_skips;" in gl)
check("the software path's four exact final-combiner SHORTCUTS are reproduced "
      "as shortcuts, not re-derived through the general form, because they are "
      "not always equal to it",
      "kPrimaryColor" in gen_hpp and "kTexture0ModulatePrimaryAlpha" in gen_hpp and
      "kTexture0Passthrough" in gen_hpp and "kBlackAlphaOne" in gen_hpp and
      "st.final_mode = FinalMode::kPrimaryColor;" in cpp)

# --- the blend, which is where the first attempt was wrong ----------------
# MEASURED with --nv2a-gpu-generic-shader-verify: GL fixed-function blending
# differs from the software blend by exactly 1 LSB over ~90 pct of the plane on
# EVERY batch, because GL quantises the source and the factor to the
# framebuffer's 8 bits and the software path does not. Over the dashboard's
# ~500 blended layers that compounded to 38 LSB over 79 pct of the frame.
check("the blend, the colour mask and the X8R8G8B8 alpha rule are done IN THE "
      "SHADER against an image binding, using the software path's own "
      "expressions and its own floor(x*255+0.5) pack",
      "beginInvocationInterlockARB();" in gen_cpp and
      "imageStore(dest_image, store_at, stored);" in gen_cpp and
      "float gc_pack(float v) { return floor(clamp(v, 0.0, 1.0) * 255.0 + 0.5)"
      in gen_cpp)
check("the interlock is ORDERED, so overlapping fragments of one draw "
      "serialise in primitive order exactly as the scanline loop does",
      "layout(pixel_interlock_ordered) in;" in gen_cpp)
check("with the shader blending, the ROP is told to write NOTHING -- blending "
      "off and every channel masked -- so there is exactly one writer",
      'if (packet.state.image_blend && !gp->enhanced) {\n            // The blend, the colour mask'
      in gl and
      "cache_color_mask(0, 0, 0, 0);" in gl)
check("the image stores are ordered against the next draw, the resolve's "
      "readback and any texture fetch of the plane by a memory barrier",
      "memory_barrier_(kGlShaderImageAccessBarrierBit |" in gl and
      "kGlFramebufferBarrierBit |" in gl and
      "kGlTextureFetchBarrierBit);" in gl)
check("GL fixed-function blending is still reachable on the same binary, "
      "because that is the arm the difference was measured against",
      '"--no-nv2a-gpu-generic-shader-image-blend"' in main and
      "bool image_blend = true;" in gen_hpp)
check("the differential verifier that measured it is a DIAGNOSTIC: default "
      "off, and it runs BOTH paths from the same starting image",
      '"--nv2a-gpu-generic-shader-verify"' in main and
      "uint32_t gpu_generic_shader_verify_ = 0;" in hpp and
      "generic_shader_verify_before_" in hpp and
      "[host] generic-shader-verify batch=%u" in cpp)

# --- selection, decline census, and the fallback --------------------------
check("every decline is NAMED and counted, and the table covers both halves -- "
      "the device's own limits and the generator's unsupported-state answer",
      "enum class GenericShaderDeclineReason : uint32_t {" in hpp and
      "kGeneratorFirst" in hpp and
      "nv2a_combiner_shader_reject_name(" in hpp and
      "[host] generic-shader-decline scope=%s" in main)
check("the decision is taken BEFORE the destination resolve, because that "
      "resolve is the cost the executor exists to remove",
      cpp.index("if (gpu_generic_shader_ && renderer_ && effective_clip_valid) {") <
      cpp.index('"software generic color surface")'))
check("a decline after collection throws the whole triangle list away, does "
      "the resolve it skipped, and re-runs the IDENTICAL primitive loop",
      "const auto run_primitives = [&]() {" in cpp and
      cpp.count("run_primitives();") == 3 and
      "gpu_shader_packet.triangles.clear();" in cpp)
check("a batch that collected NO triangle is a completed batch, not a decline, "
      "and must not resolve either",
      "++generic_shader_empty_batches_;" in cpp and
      "empty=%llu" in main)
check("the generated executor carries the SAME aliasing guard the witnessed "
      "one does, so one guest range can never have two resident copies",
      "ALIASING GUARD, the same one supports_witnessed_profile carries" in gl)
check("the executor runs on the emulation thread in the generic-combiner "
      "batch body, which is inline-only, so no GL call is reachable from a "
      "worker",
      "seam_note_threadable(job, false, kSeamNoDirectPointer);" in cpp and
      "submit_generated_shader(gpu_shader_packet," in cpp and
      "submit_generated_shader" not in exec_half)
check("both flag spellings exist on one binary and the mask exists so a single "
      "generated program can be bisected out",
      '"--nv2a-gpu-generic-shader"' in main and
      '"--no-nv2a-gpu-generic-shader"' in main and
      '"--nv2a-gpu-generic-shader-mask"' in main and
      "[host] generic-shader-program scope=%s" in main)
check("the nodraw control exists, is default off and has no --no- spelling, "
      "because it is knowingly incorrect when on",
      '"--nv2a-gpu-generic-shader-nodraw"' in main and
      '"--no-nv2a-gpu-generic-shader-nodraw"' not in main and
      "bool generated_nodraw_ = false;" in gl)
check("the record layout the device writes and the layout the generated source "
      "declares are pinned to each other by a static_assert",
      "static_assert(sizeof(GeneratedTriRecord) == 448," in gl and
      "dvec4 recip;" in gen_cpp and "vec4 v[18];" in gen_cpp)

# -------------------------------------------------------------------------
print("\n16. phase 7: depth in the generated executor, the witnessed reroute, "
      "and the BGRA upload")
# -------------------------------------------------------------------------
# Phase 7 attacked the three items section 3j(8) left, in the order the plane
# census orders them. Two of the three are DEFAULT OFF and the measurement that
# put them there is the thing worth pinning: a guard that only checked "the
# feature exists" would let a later session flip a default that is known to be
# a regression or known to move the image.

# --- depth in the generated executor -------------------------------------
check("a depth-using generated batch does NOT take the image-blend path, "
      "because writing gl_FragDepth forces late-Z and an in-shader imageStore "
      "would then write colour for a fragment that loses the depth test",
      "st.image_blend = gpu_generic_shader_image_blend_ && !uses_depth;" in cpp)
check("the depth TEST-ENABLE bit and the depth WRITE mask travel separately, "
      "because the software path's 'test disabled, write enabled' case passes "
      "every fragment and still stores",
      "bool depth_test = false;" in rhpp and
      "gpu_shader_packet.depth_test = uses_depth && depth_test_enabled_bit;"
      in cpp and
      "gpu_shader_packet.depth_write =\n                uses_depth && raster_state[kRasterDepthMask] != 0;"
      in cpp and
      "cache_depth_test(packet.depth_test," in gl and
      "cache_depth_mask(packet.depth_write);" in gl)
check("the generated executor's zeta plane is looked up on the SAME "
      "(address, size) key the witnessed executor uses, so a plane one of them "
      "made resident is found by the other",
      "bool generated_shader_depth_resident(uint32_t depth_address," in rhpp and
      "return find_witnessed_depth_slot(depth_address, size) != nullptr;" in gl and
      "depth_slot = find_witnessed_depth_slot(packet.depth_address," in gl)
check("the zeta plane is snapshotted ONLY when the backend does not already "
      "hold it, exactly as the colour plane is",
      "renderer_->generated_shader_depth_resident(" in cpp and
      "gpu_shader_packet.needs_depth_upload = true;" in cpp)
check("the aliasing guard covers the ZETA range too once the executor writes "
      "one, or two GPU-authoritative copies of one guest range can exist",
      "const bool wants_depth = descriptor.state.uses_depth;" in gl and
      "if (wants_depth && overlaps(wantd0, wantd1, c0, c1))" in gl and
      "if (wants_depth && !depth_is_ours &&\n                        overlaps(wantd0, wantd1, d0, d1)) {"
      in gl)
check("a slot made depth-resident by this executor still carries the R32UI "
      "WORD form the resident dashboard fixture's depth-EQUAL test reads, and "
      "a slot made resident by a path that had none has it REBUILT from the "
      "depth attachment rather than faulting",
      "depth_slot->depth_word_texture" in gl and
      "bool ensure_depth_word_texture(ResidentSurface& slot," in gl and
      "!existing->depth_word_texture &&\n                !ensure_depth_word_texture(*existing, error)"
      in gl and
      "depth_word_rebuilds" in rhpp and
      "depth_word_rebuilds=%llu" in main)
check("depth support is DEFAULT OFF and both spellings exist on one binary, "
      "because on its own it is a MEASURED regression (3 G window: colour "
      "resolves 566 -> 761, zeta 180 -> 375) and only pays with the reroute",
      "bool gpu_generic_shader_depth_ = false;" in hpp and
      '"--nv2a-gpu-generic-shader-depth"' in main and
      '"--no-nv2a-gpu-generic-shader-depth"' in main and
      "bool nv2a_gpu_generic_shader_depth = false;" in main and
      "DEFAULT OFF, and the reason is MEASURED rather than cautious" in hpp)

# --- the witnessed-family reroute ----------------------------------------
check("the reroute is ONE mask over the eight families the plane census names, "
      "not eight hand-written GPU fixtures, and the bit order is fixed in one "
      "function",
      "inline int generic_shader_witnessed_bit(Profile profile) {" in cpp and
      "case Profile::kPitchMultiTextureColor:  return 0;" in cpp and
      "case Profile::kPitchAdditiveColor:      return 7;" in cpp)
check("the reroute is gated on the generated executor AND on its depth "
      "support, so either negative spelling restores the ladder exactly -- "
      "without them it would land these batches in the SOFTWARE evaluator",
      "witnessed_reroute_bit >= 0 && gpu_generic_shader_ &&" in cpp and
      "gpu_generic_shader_depth_ && renderer_ &&" in cpp)
reroute_start = cpp.index("++generic_shader_witnessed_reroutes_;")
reroute_body = cpp[reroute_start:cpp.index("    executed = true;", reroute_start)]
check("the rerouted batch takes the same ordering fence the ladder's own "
      "generic arm takes, because the generic path reaches the GL backend",
      "draw_order_fence();\n        return rasterize_generic_combiner_batch(job, executed);" in reroute_body)
check("the reroute is DEFAULT 0 and the per-family measurement that put it "
      "there is recorded, family by family, so it is not re-derived",
      "uint32_t gpu_generic_shader_witnessed_ = 0u;" in hpp and
      "uint32_t nv2a_gpu_generic_shader_witnessed = 0u;" in main and
      "bit 0 pitch_multi_texture  (120 draws)        0 px   EXACT" in hpp and
      "bit 2 pitch_tex5_projected (126)        286 151 px, max delta 255" in hpp and
      "unsupported-texture-shader-program=126" in hpp)
check("the cause is attributed to the GENERIC EVALUATOR and not to the GPU "
      "executor, by a leg with the generated executor masked off reproducing "
      "the same pixels",
      "--nv2a-gpu-generic-shader-mask 0, so the reroute lands in SOFTWARE" in hpp)
check("the reroute and the reroute count are exported by the always-on census, "
      "so a leg that rerouted nothing cannot be mistaken for one that did",
      "witnessed_mask=%02x" in main and
      "witnessed_reroutes=%llu" in main and
      "generic_shader_witnessed_reroutes()" in hpp)

# --- upload staging -------------------------------------------------------
check("the generated executor uploads guest A8R8G8B8 words as GL_BGRA with NO "
      "staging buffer for a texture, because the word's bytes in memory ARE "
      "B,G,R,A -- the same transform the witnessed executor already uses",
      "kGlBgra, kGlUnsignedByte,\n                                               texture.linear.data());"
      in gl and
      "PHASE 7 (upload staging). GL_BGRA, and NO staging buffer at all." in gl)
check("a colour-surface upload is a ROW COPY plus the vertical flip, and an "
      "unchanged geometry re-uploads through glTexSubImage2D instead of "
      "re-specifying storage",
      "PHASE 7 (upload staging), the same two changes the witnessed path" in gl and
      "slot->tex_alloc_w == up_w && slot->tex_alloc_h == up_h" in gl and
      "uint32_t alloc_w = 0;" in gl)
check("the pre-phase-7 per-pixel shuffle is still reachable on the SAME "
      "binary, because that is the arm the change is measured against",
      '"--no-nv2a-gpu-generic-shader-bgra-upload"' in main and
      "bool generated_bgra_upload_ = true;" in gl and
      "guest_words_to_rgba_unflipped(texture.linear, rgba," in gl and
      "virtual void set_generated_shader_bgra_upload(bool on)" in rhpp)
check("the BGRA state is exported by the census, so an A/B cannot silently "
      "run both arms with the same upload path",
      "bgra=%u" in main and
      "gpu_generic_shader_bgra_upload()" in hpp)

print("\n%d checks, %d failures" % (checks, len(failures)))
if failures:
    for name in failures:
        print("  FAILED: %s" % name)
    sys.exit(1)
sys.exit(0)
