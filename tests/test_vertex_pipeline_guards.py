#!/usr/bin/env python3
"""Structural guards for the phase-5 vertex-pipeline work and its censuses.

Bead beads-xbox.1.15.10 phase 5; acceptance docs/PERF-METHODOLOGY.md section 3
(behavioural faithfulness). Companion to tests/test_gpu_profiles_guards.py.

Three things landed and each has a property a compiler cannot check and a
passing replay does not exercise:

  1. THE PARSED-PROGRAM CACHE. nv2a_vsh_parse_program() was called once per
     vertex batch (455 812 per canonical 12 G run) to produce a byte-identical
     result 93.7 percent of the time. The cache is only exact while it is
     invalidated by BOTH of the parse's inputs -- the program words and the
     start slot -- and while the malloc it now owns is freed exactly once. A
     missed invalidation is a WRONG VERTEX PROGRAM, which is not a rounding
     difference; a double free or a leak is a crash or a slow leak. Neither is
     visible in a run that never re-uploads a program.

  2. THE PER-VERTEX HOISTS. Skipping attribute slots whose count is zero is
     exact only because fetch_vertex_attribute's first statement returns for
     exactly those slots, and skipping the input-register zero-fill is exact
     only because all sixteen slots are written before the interpreter runs.
     Both are pinned here so a later edit to either cannot silently break the
     precondition it relies on.

  3. THE ALWAYS-ON CENSUSES. Phase 4 estimated the no-op residency queries at
     ~0.9 s from a ceiling probe minus a byte-rate model; the direct
     measurement is 16 ms. The instruments that produced that correction --
     the split resolve timer, the per-plane consumer table and the vertex
     split -- are pinned so the next session inherits the measurement rather
     than the inference.

Run: python tests/test_vertex_pipeline_guards.py   (exit 0 = all guards hold)
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NV2A_CPP = os.path.join(ROOT, "src", "machine", "nv2a.cpp")
NV2A_HPP = os.path.join(ROOT, "src", "machine", "nv2a.hpp")
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
main = read(MAIN_CPP)

SUBMIT = "bool Nv2a::submit_vertex_batch() {"
FETCH = "bool Nv2a::fetch_vertex_attribute(uint32_t slot, uint32_t index) {"
if cpp.count(SUBMIT) != 1 or cpp.count(FETCH) != 1:
    print("FATAL: submit_vertex_batch/fetch_vertex_attribute not found once")
    sys.exit(2)

submit = body(cpp, SUBMIT, "bool Nv2a::rasterize_witnessed_immediate_quad(")
fetch = body(cpp, FETCH, "bool Nv2a::commit_immediate_vertex() {")

# -------------------------------------------------------------------------
print("\n1. the parsed-program cache is exact: both inputs invalidate it")
# -------------------------------------------------------------------------
# The parse reads transform_program_[transform_program_start_ ...] over
# (136 - start) slots. Exactly two method handlers can move either input, and
# both must invalidate. If a third way to write the program words ever appears,
# the third check below fails and forces the invalidation to be added.
check("writing a transform program word invalidates the cache",
      "transform_program_[transform_program_load_][slot & 3u] = parameter;\n"
      "        // Phase 5 item 3: one of exactly TWO inputs the parsed-program "
      "cache\n"
      "        // depends on. Writing any program word invalidates it.\n"
      "        invalidate_vsh_program_cache();" in cpp)
check("changing the program start slot invalidates the cache",
      "if (transform_program_start_ != static_cast<uint8_t>(parameter))\n"
      "            invalidate_vsh_program_cache();\n"
      "        transform_program_start_ = static_cast<uint8_t>(parameter);" in cpp)
# The ONLY write to the program words in the whole device. A second one would
# be a second input to the cache and would need its own invalidation.
check("there is exactly one writer of the transform program words",
      cpp.count("transform_program_[transform_program_load_]") == 1,
      str(cpp.count("transform_program_[transform_program_load_]")))
check("the cache is keyed on nothing else: the parse arguments are exactly "
      "the program words at the start slot and (136 - start) slots",
      "static_cast<uint32_t>(transform_program_.size()) - transform_program_start_"
      in submit and
      "transform_program_[transform_program_start_].data(), slots" in submit)

# -------------------------------------------------------------------------
print("\n2. the cache owns its malloc and every destroy site is keyed on that")
# -------------------------------------------------------------------------
# nv2a_vsh_parse_program mallocs program.steps. Before the cache the batch owned
# it and freed it on every exit path. Now the CACHE owns it whenever it was
# installed, so a destroy keyed on `program_parsed` would free the cached copy
# and leave vsh_cache_steps_ dangling -- a use-after-free on the next batch.
check("no destroy site is keyed on program_parsed any more",
      "if (program_parsed) nv2a_vsh_program_destroy(&program);" not in cpp)
check("every in-function destroy is keyed on program_owned",
      cpp.count("nv2a_vsh_program_destroy(&program);") == 2 and
      cpp.count("if (program_owned) nv2a_vsh_program_destroy(&program);") == 2,
      str(cpp.count("nv2a_vsh_program_destroy(&program);")))
check("installing into the cache transfers ownership away from the batch",
      "vsh_cache_steps_ = program.steps;" in submit and
      "vsh_cache_valid_ = true;" in submit and
      "program_owned = false;" in submit)
check("the cache frees the PREVIOUS program before installing a new one",
      submit.index("destroy_vsh_program_cache();") <
      submit.index("vsh_cache_steps_ = program.steps;"))
check("the device destructor releases the cached program",
      "Nv2a::~Nv2a() {" in cpp and
      "destroy_vsh_program_cache();" in
      body(cpp, "Nv2a::~Nv2a() {", "\nvoid Nv2a::") )
check("invalidate goes through destroy, so a stale pointer cannot survive",
      "void Nv2a::invalidate_vsh_program_cache() {" in cpp and
      "vsh_cache_valid_ = false;" in cpp and
      "destroy_vsh_program_cache();" in
      body(cpp, "void Nv2a::invalidate_vsh_program_cache() {", "\nNv2a::~Nv2a"))
check("turning the cache OFF at runtime drops what it already holds",
      "void set_vertex_program_cache_enabled(bool on) {" in hpp and
      "if (!on) invalidate_vsh_program_cache();" in hpp)
check("vsh_program_writes_context is cached beside the steps, not re-walked, "
      "and the cached answer is the one used on a hit",
      "vsh_cache_writes_context_ = program_writes_context;" in submit and
      "program_writes_context = vsh_cache_writes_context_;" in submit)

# -------------------------------------------------------------------------
print("\n3. the per-vertex hoists are exact, and their preconditions hold")
# -------------------------------------------------------------------------
# The active-slot list is exact ONLY because an inactive slot's fetch is a
# no-op. That is a property of fetch_vertex_attribute's first statement.
check("fetch_vertex_attribute still returns immediately for an inactive slot, "
      "before it reads or writes anything",
      fetch.index("if (!attr.count) return true;") <
      fetch.index("attr.dma_b"))
check("the active-slot list is built once per batch, outside the vertex loop",
      "std::array<uint8_t, 16> active_attribute_slots{};" in submit and
      submit.index("active_attribute_slots[active_attribute_count++]") <
      submit.index("for (size_t vertex_index = 0;"))
check("the vertex loop fetches through the active list in slot order",
      "for (uint32_t i = 0; i < active_attribute_count; ++i) {" in submit and
      "fetch_vertex_attribute(active_attribute_slots[i]," in submit)
check("arm A of the A/B restores the full sixteen-slot list verbatim",
      "if (!fast_fetch || vertex_attributes_[slot].count)" in submit)
# The zero-fill removal is exact only because all 16 slots are written.
check("the input register file is fully written before the interpreter runs, "
      "which is what makes dropping its zero-fill exact",
      "std::array<float, 16 * 4> input_regs;" in submit and
      "std::memcpy(&input_regs[slot * 4],\n"
      "                                    vertex_attributes_[slot].value.data(),"
      in submit and
      "for (uint32_t slot = 0; slot < vertex_attributes_.size(); ++slot) {\n"
      "                        std::memcpy(&input_regs[slot * 4]," in submit)
check("arm A restores the zero-fill",
      "if (!fast_fetch) input_regs.fill(0.0f);" in submit)
# The other three register files ARE still zeroed: the interpreter can read a
# temp or an output the program never wrote.
check("the temp, output and address register files are STILL zero-filled",
      "std::array<float, 13 * 4> output_regs_storage{};" in submit and
      "std::array<float, 12 * 4> temp_regs{};" in submit and
      "std::array<float, 4> address_reg{};" in submit)

# -------------------------------------------------------------------------
print("\n4. both changes are same-binary A/B-able and default ON")
# -------------------------------------------------------------------------
for flag in ("--nv2a-vertex-program-cache", "--nv2a-vertex-fast-fetch"):
    on = '"%s"' % flag
    off = '"--no-%s"' % flag[2:]
    check("%s has BOTH spellings on one binary, so the A/B needs no rebuild"
          % flag, on in main and off in main,
          "on=%s off=%s" % (on in main, off in main))
check("both default ON in main and in the device",
      "bool nv2a_vertex_program_cache = true;" in main and
      "bool nv2a_vertex_fast_fetch = true;" in main and
      "bool vertex_program_cache_enabled_ = true;" in hpp and
      "bool vertex_fast_fetch_ = true;" in hpp)
check("the per-vertex census is DEFAULT OFF -- it takes two clock reads per "
      "vertex and must never ride in a shipping run",
      "bool nv2a_vertex_census = false;" in main and
      "bool vertex_census_enabled_ = false;" in hpp and
      '"--no-nv2a-vertex-census"' not in main)
check("the census reports its own arming state, so a zero denominator is "
      "distinguishable from a zero measurement",
      "fast_fetch=%u census=%u" in main and
      "nv->vertex_census_enabled() ? 1u : 0u" in main)

# -------------------------------------------------------------------------
print("\n5. the always-on censuses that re-ranked the work")
# -------------------------------------------------------------------------
check("a residency query is timed and credited to noop or moved by whether it "
      "actually wrote guest memory, not by a model",
      "struct ResolveQueryTimer {" in cpp and
      "query_timer.moved = true;" in cpp and
      cpp.count("query_timer.moved = true;") == 2)
check("the moved bucket is set at the SAME point seam_resolve_transfers_ is, "
      "so the two instruments cannot disagree",
      "++seam_resolve_transfers_;\n        query_timer.moved = true;" in cpp)
check("the witnessed texture-snapshot call site is attributed apart",
      "resolve_query_from_texture_source_ = true;" in cpp and
      "resolve_query_from_texture_source_ = false;" in cpp and
      "texsrc_noop_n=%llu" in main)
check("the transferring resolve is split into its backend half and its "
      "guest-writeback half",
      "color_backend_ticks" in hpp and "color_memwrite_ticks" in hpp and
      "color_backend_ns=%llu color_memwrite_ns=%llu" in main)
check("the census timer applies the same plausibility clamp perf_excl uses, "
      "so one backwards TSC read cannot poison a bucket",
      "if (delta > perf_excl::g_max_plausible_ticks) return;" in cpp)
check("the tick scale is calibrated UNCONDITIONALLY, so an always-on census "
      "in a noperf run reports nanoseconds and not raw ticks",
      "xlle::perf_excl::init();" in main and
      "if (perf_accounting) xlle::perf_excl::init();" not in main)
check("perf_excl stays DISARMED in a noperf run: calibration is not arming",
      "xlle::perf_excl::g_enabled = perf_accounting;" in main)

check("the per-plane consumer table is a direct count at the GPU/CPU decision "
      "point, taken for the colour and the zeta plane separately",
      "void Nv2a::gpu_res_note_surface_consumer(" in cpp and
      "gpu_res_note_surface_consumer(gpu_res_color_surfaces_, color_address," in cpp and
      "gpu_res_note_surface_consumer(gpu_res_depth_surfaces_, depth_address," in cpp)
check("the depth-only profile is visible in the zeta table: the colour half is "
      "gated on color_profile and the zeta half on uses_depth_surface",
      "if (color_profile) {\n        gpu_res_note_surface_consumer("
      "gpu_res_color_surfaces_, color_address," in cpp and
      "if (uses_depth_surface) {\n        gpu_res_note_surface_consumer("
      "gpu_res_depth_surfaces_, depth_address," in cpp)
check("the table counts GPU-taken and CPU-executed draws apart, and only the "
      "CPU ones carry a family",
      "if (on_gpu) {\n        ++row->gpu_draws;\n        return;\n    }" in cpp and
      "++row->cpu_draws_profile[profile];" in cpp)
check("the table counts overflow instead of evicting, so a plane it already "
      "knows about can never be under-reported",
      "++table.overflow;" in cpp and "address=OVERFLOW" in main)
check("every plane is printed with its CPU families by name, which is what "
      "makes 'this plane has no CPU consumer left' checkable",
      "[host] gpu-residency-plane scope=%s" in main and
      "cpu_families=%s" in main)

print("\n%d checks, %d failures" % (checks, len(failures)))
if failures:
    for name in failures:
        print("  FAILED: %s" % name)
    sys.exit(1)
sys.exit(0)
