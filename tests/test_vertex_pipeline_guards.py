#!/usr/bin/env python3
"""Structural guards for the phase-5 vertex-pipeline work and its censuses.

Bead beads-xbox.1.15.10 phase 5; acceptance docs/reference/testing/PERF-METHODOLOGY.md section 3
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

  4. THE COMPILED STEP FORM (beads-xbox.1.15.11, sections 6-8). The parse cache
     is pre-decoded once per distinct program into 32-byte records, and a
     read-before-write analysis deletes most of the 416-byte per-vertex register
     zero-fill. Both are exactness-critical rather than merely fast: the record
     must call the SAME vendored implementation the interpreter would, in the
     vendored opcode order; the operand fetch must keep the multiply that
     answers the sNaN question; the MAC/ILU pairing must survive; and the zero
     plan must cover every register the DEVICE reads back, not just the ones the
     program reads. A wrong answer here is a wrong vertex, not a rounding
     difference, and the replay would still finish.

Run: python tests/test_vertex_pipeline_guards.py   (exit 0 = all guards hold)
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NV2A_CPP = os.path.join(ROOT, "src", "machine", "nv2a.cpp")
NV2A_HPP = os.path.join(ROOT, "src", "machine", "nv2a.hpp")
VSH_HPP = os.path.join(ROOT, "src", "machine", "nv2a_vsh_compiled.hpp")
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
vsh = read(VSH_HPP)

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
# The other three register files are zeroed WHOLESALE on the interpreter arm,
# because the interpreter can read a temp or an output the program never wrote
# and their initial value is then observable. Only the compiled form
# (beads-xbox.1.15.11, section 7 below) can prove which of them are dead, and it
# is the only thing allowed to skip any of them.
check("the temp, output and address register files are zero-filled WHOLESALE "
      "unless the compiled form proved otherwise",
      "std::array<float, 13 * 4> output_regs_storage;" in submit and
      "if (use_compiled_vsh) {\n                    vsh_apply_zero_plan("
      in submit and
      "} else {\n                    output_regs_storage.fill(0.0f);" in submit)

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

# -------------------------------------------------------------------------
print("\n6. the compiled step form (beads-xbox.1.15.11)")
# -------------------------------------------------------------------------
# Phase 5 measured the VSH interpreter at 4.54 s and 118 ns per vertex and named
# it the next vertex lever. What landed is a PRE-DECODED form: the parse cache
# is compiled once per distinct program into 32-byte records whose operand
# bases, offsets, swizzles and writemasks are already resolved, plus a
# read-before-write analysis that deletes most of the 416-byte per-vertex
# register zero-fill. Every property below is one a passing replay does not
# exercise and a compiler cannot check, and each is load-bearing for EXACTNESS
# rather than for speed.
check("the compiled record holds the VENDORED implementation pointer, so the "
      "arithmetic is the same object code and not a transcription",
      "using Fn = void (*)(float* out, const float* inputs);" in vsh and
      "Fn fn = nullptr;" in vsh and
      "kVshDispatchTable" in cpp and "nv2a_vsh_cpu_lit," in cpp)
# The whole exactness argument is "the compiled record calls the function the
# vendored dispatch table would have called". That holds only if the table is
# the vendored one ENTRY FOR ENTRY, in the vendored Nv2aVshOpcode order -- a
# single transposition silently runs MUL where the program said ADD. So the
# order is pinned as the exact sequence, not by sampling a few entries.
VENDORED_DISPATCH = [
    "mov", "mul", "add", "mad", "dp3", "dph", "dp4", "dst", "min", "max",
    "slt", "sge", "arl", "rcp", "rcc", "rsq", "exp", "log", "lit",
]
table = body(cpp, "const VshCompiledOp::Fn kVshDispatchTable[] = {", "};")
import re as _re
check("that table is the vendored kDispatchTable ENTRY FOR ENTRY in the "
      "vendored order, and a static_assert pins its length to the enum",
      _re.findall(r"nv2a_vsh_cpu_(\w+)", table) == VENDORED_DISPATCH and
      table.count("nullptr") == 1 and
      table.index("nullptr") < table.index("nv2a_vsh_cpu_mov") and
      "static_cast<size_t>(NV2AOP_LIT) + 1," in cpp,
      str(_re.findall(r"nv2a_vsh_cpu_(\w+)", table)))
check("no operation is REIMPLEMENTED in the compiled header -- the transcribed "
      "inlined form was built, measured slower, and removed",
      "inline float vsh_fix_inf_mult(" not in vsh and "::powf(" not in vsh and
      "::log2f(" not in vsh and "vsh_apply_operation" not in vsh)
check("the operand fetch keeps the interpreter's multiply by +/-1.0f rather "
      "than a copy, which is the whole answer to the sNaN question",
      "const float mult = (op.in_negate_mask & (1u << k)) ? -1.0f : 1.0f;"
      in vsh and "out[0] = mult * in[swizzle & 3u];" in vsh)
check("relative context addressing reproduces the interpreter's uint32_t "
      "offset arithmetic, wrap included, not ptrdiff_t",
      "const uint32_t offset =" in vsh and
      "in = bases[kVshBaseContext] + offset * 4u;" in vsh)
check("the MAC/ILU pairing is preserved: the ILU operand is fetched BEFORE the "
      "MAC executes, so an ILU reading its own step's MAC sees the OLD value",
      vsh.index("vsh_fetch_operand(ilu_input, ilu, 0, bases);") <
      vsh.index("op.fn(result, mac_inputs);\n            vsh_store_operand("))
check("the compiler DECLINES the whole program for anything it cannot "
      "represent exactly, and the device then runs the interpreter unchanged",
      "return false;  // r13-r15: an out-of-bounds read either way" in cpp and
      "if (input.index >= 16) return false;" in cpp and
      "if (output.index >= 13) return false;" in cpp and
      "if (output.index >= 12) return false;" in cpp and
      "if (!found_final) return false;" in cpp)
check("there is no PARTIAL compile: a decline leaves vsh_cache_ops_valid_ "
      "false, which is the only thing the executor tests",
      "vsh_cache_ops_valid_ = false;\n    vsh_cache_ops_.clear();" in cpp and
      "const bool use_compiled_vsh = vsh_cache_ops_valid_;" in cpp)
check("a destination that provably stores nothing -- unused, an input "
      "register, or a zero vector writemask -- is resolved away at compile time",
      "if (output.type == NV2ART_NONE || output.type == NV2ART_INPUT) "
      "return true;" in cpp and "if (!mask) return true;" in cpp)
check("the compiled form is invalidated with the parse cache AND when the flag "
      "moves, so stale records can never be reachable",
      "vsh_cache_ops_valid_ = false;" in cpp and
      "vsh_cache_ops_valid_ = false;" in hpp)
check("both flag spellings exist on one binary, so the decision A/B needs no "
      "rebuild",
      '"--nv2a-vsh-compile"' in main and '"--no-nv2a-vsh-compile"' in main)
check("the record stays small enough to keep a program's decode in L1, pinned "
      "by a static_assert rather than by a comment",
      "static_assert(sizeof(VshCompiledOp) <= 32," in vsh)

# -------------------------------------------------------------------------
print("\n7. the read-before-write zero plan, and why it cannot change output")
# -------------------------------------------------------------------------
check("the interpreter arm still zero-fills all three register files, because "
      "without the analysis any of them is observable",
      "output_regs_storage.fill(0.0f);" in cpp and
      "temp_regs.fill(0.0f);" in cpp and "address_reg.fill(0.0f);" in cpp)
check("the analysis walks the COMPILED ops in issue order and respects the "
      "MAC/ILU pairing -- both halves read before either writes",
      "for (unsigned k = 0; k < ilu.input_count; ++k) note_read(ilu, k);" in cpp
      and cpp.index("for (unsigned k = 0; k < ilu.input_count; ++k) "
                    "note_read(ilu, k);")
      < cpp.index("note_write(op);\n            note_write(ilu);"))
check("a lane read before it is written marks its register for zeroing, and "
      "the register is then treated as written so the walk terminates",
      "plan.temp_mask |= static_cast<uint16_t>(1u << index);" in cpp and
      "temp_written[index] = 0xF;" in cpp)
check("the SEVEN output registers the device reads back afterwards are added "
      "unconditionally unless the program writes all four of their lanes",
      "kConsumedOutputs[] = {0, 3, 4, 9, 10, 11, 12};" in cpp and
      "if (0xFu & ~static_cast<unsigned>(output_written[index]))" in cpp)
check("the same seven registers are what the verifier compares, so the plan "
      "and its check cannot disagree about what is observable",
      "kConsumed[] = {0, 3, 4, 9, 10, 11, 12};" in cpp)
check("a relative operand conservatively marks a0 for zeroing",
      "plan.address = true;  // the fetch reads a0" in cpp)
check("the plan is reported per program, so 'how much zeroing is left' is a "
      "measured number and not a claim",
      "zero_outputs=%u zero_temps=%u " in main)

# -------------------------------------------------------------------------
print("\n8. the instruments: a program census, a ceiling probe and a verifier")
# -------------------------------------------------------------------------
check("the program census is ALWAYS ON and keyed on the two inputs the parse "
      "is a function of -- the program words and the start slot",
      "fold_word(transform_program_start_);" in cpp and
      "[host] nv2a-vsh-program-census scope=%s" in main)
check("it is folded once per PARSE, not once per batch: a cache hit reuses the "
      "row pointer the parse stored",
      "census_row = vsh_cache_census_row_;" in cpp and
      "vsh_cache_census_row_ = census_row;" in cpp)
check("overflow is counted, never evicted, so a program the table already "
      "knows about can never be under-reported",
      "++vsh_program_census_.overflow;" in cpp)
check("which of the two step forms actually ran is reported with its own "
      "denominator, so a compiled-path claim cannot be made without one",
      "vsh_compiled_vertices" in hpp and "vsh_interpreted_vertices" in hpp and
      "[host] nv2a-vsh-form scope=%s" in main)
check("the pre-decode's own cost is measured and reported, not assumed free",
      "vertex_census_.vsh_compile_ticks += perf_excl::compensated_delta(" in cpp and
      "compile_ns=%llu" in main)
check("the ceiling probe is DEFAULT OFF, has no --no- spelling and is a "
      "SEPARATE function, because a branch for it inside the executor loop "
      "changed what it measured",
      '"--nv2a-vsh-unsafe-probe"' in main and
      '"--no-nv2a-vsh-unsafe-probe"' not in main and
      "inline void vsh_execute_compiled_probe(" in vsh and
      "if (vsh_probe)" in cpp)
check("the differential verifier runs the VENDORED interpreter on a scratch "
      "state from the SAME inputs, and is default off",
      "nv2a_vsh_emu_execute(&state, &reference);" in cpp and
      '"--nv2a-vsh-verify"' in main and
      '"--no-nv2a-vsh-verify"' not in main)
check("it compares BITS, not floats, so a signalling NaN or a signed zero "
      "cannot pass as equal",
      "std::memcpy(&reference_bits, &output[element], sizeof(uint32_t));" in cpp
      and "if (reference_bits == compiled_bits) continue;" in cpp)
check("it reports its own denominator and its mismatch count, and bounds how "
      "much it prints",
      "checked=%llu mismatched=%llu" in main and
      "if (vsh_verify_reported_ < 32) {" in cpp)

print("\n%d checks, %d failures" % (checks, len(failures)))
if failures:
    for name in failures:
        print("  FAILED: %s" % name)
    sys.exit(1)
sys.exit(0)
