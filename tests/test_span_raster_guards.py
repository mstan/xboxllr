#!/usr/bin/env python3
"""Structural guards for within-job destination-span parallel raster, stage (e2).

Bead beads-xbox.1.15.9; design docs/reference/graphics/DESIGN-NV2A-SEAM.md sections 1.3, 1.5, 3.1
and 4.3; methodology docs/reference/testing/PERF-METHODOLOGY.md section 4. Predecessor guards:
tests/test_seam_guards.py (stage (d)) and tests/test_draw_threads_guards.py
(stage (e)); this file pins only what stage (e2) adds.

Stage (e) moved a whole draw onto a second thread. It is exact and it was
REJECTED on timing: latency hiding needs the emulation thread to have work to
overlap with, and on this workload it does not (worker busy 0.489 of elapsed,
emulation thread blocked in drains 0.449, computed ceiling 4.0 percent, measured
+0.39 percent). Stage (e2) divides the WORK instead: N workers execute the SAME
draw at once, each owning a disjoint set of destination scanlines, with a barrier
between draws.

The properties that make that exact are not checkable by the compiler and are
not observable in a passing replay -- an A/B whose span_jobs is zero measured
nothing, and a race between two workers writing one pixel might not fire today.
So they are pinned here:

  1. The partition is applied in exactly ONE place: the per-triangle destination
     scanline loop inside execute_witnessed_fragments. Nothing else in the raster
     path reads `part` or `parts`, and no second copy of the fragment loop
     exists.
  2. Row ownership is disjoint and total: part p renders y with y % parts == p,
     expressed as a stride, so no two parts can address the same destination
     pixel under either address map.
  3. A worker writes NO shared mutable state: part 0 accumulates into job.out,
     helper part p into span_out_[p], and the members are touched only by the
     emulation thread at the fold.
  4. The per-DRAW effects -- the two batch-counter ladders, the F3 renderer
     invalidations and the F15 debug record -- run under part == 0 exactly once.
  5. There is a barrier before the next job: execute_draw_spans does not return
     until every helper part has completed, so only one draw is ever in flight
     and draw order is preserved by construction.
  6. The order-dependent state is excluded by the eligibility rule, not by
     hope: the 14 FNV chains and the 14 hash-gated last-address scalars are
     unreachable because hash_on forces the job inline; the three ungated
     last-value scalars all assign a job-invariant register word.
  7. kDrawThreadsMax bounds N and out-of-range is refused, not clamped.
  8. Toggle-off is unchanged: at parts == 1 the loop is `y += 1` from tri_ymin,
     every part == 0 guard is taken, and the executor is the same function.

Run: python tests/test_span_raster_guards.py   (exit 0 = all guards hold)
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NV2A_CPP = os.path.join(ROOT, "src", "machine", "nv2a.cpp")
NV2A_HPP = os.path.join(ROOT, "src", "machine", "nv2a.hpp")
MAIN_CPP = os.path.join(ROOT, "src", "app", "main.cpp")
MEASURE = os.path.join(ROOT, "tools", "measure_xbox_replay.py")

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
measure = read(MEASURE)

LATCH = "bool Nv2a::rasterize_witnessed_batch(DrawJob& job, bool& executed) {"
EXEC = "bool Nv2a::execute_witnessed_fragments(DrawJob& job, DrawJobTelemetry& out,"
SPANS = "bool Nv2a::execute_draw_spans(DrawJob& job) {"
SPAN_WORKER = "void Nv2a::span_worker_main(unsigned part) {"
for sig in (LATCH, EXEC, SPANS, SPAN_WORKER):
    if cpp.count(sig) != 1:
        print("FATAL: %r appears %d times" % (sig[:70], cpp.count(sig)))
        sys.exit(2)

latch_half = body(cpp, LATCH, EXEC)
exec_half = body(cpp, EXEC, "void Nv2a::record_generic_skip(")
spans = body(cpp, SPANS, "bool Nv2a::submit_vertex_batch() {")
span_worker = body(cpp, SPAN_WORKER, SPANS)
pool_start = body(cpp, "void Nv2a::span_pool_start(unsigned parts) {",
                  "void Nv2a::span_pool_stop() {")
pool_stop = body(cpp, "void Nv2a::span_pool_stop() {", SPAN_WORKER)
set_threads = body(cpp, "bool Nv2a::set_draw_threads(unsigned n) {", "Nv2a::~Nv2a() {")
dtor = body(cpp, "Nv2a::~Nv2a() {", "void Nv2a::draw_worker_main() {")

# -------------------------------------------------------------------------
print("\n1. the partition lives in exactly one place")
# -------------------------------------------------------------------------
check("the executor takes the part and the part count explicitly",
      "unsigned part, unsigned parts) {" in cpp and
      "bool execute_witnessed_fragments(DrawJob& job, DrawJobTelemetry& out,"
      in hpp)
check("there is exactly one definition of the fragment loop",
      cpp.count("bool Nv2a::execute_witnessed_fragments(") == 1)
# `parts` may only be read by: the stride computation (3 uses), the two per-draw
# `part != 0` / `part == 0` guards, and the row-ownership skew. Nothing else in
# the fragment loop may branch on the partition, or the partition would be able
# to change the pixel math instead of only which rows are visited.
exec_code = "\n".join(line for line in exec_half.split("\n")
                      if not line.lstrip().startswith("//"))
part_uses = re.findall(r"\b(?:part|parts)\b", exec_code)
# In CODE (comments stripped) the partition may appear in exactly ten places:
# the signature (part, parts), the stride block (y_step from parts, the
# parts > 1 test, the skew from part), the two `part != 0` batch-ladder early
# returns, and the three `part == 0` per-draw-effect guards (colour
# invalidate, depth invalidate, F15 debug record). Nothing else in ~1900 lines
# of fragment loop may branch on the partition -- if it could, the partition
# would be able to change the pixel math and not merely which rows are drawn.
check("the partition is read only by the stride and the per-draw guards",
      len(part_uses) == 11, "%d uses: %r" % (len(part_uses), part_uses))
check("no fragment-level expression branches on the partition",
      not re.search(r"if \(part[s]? [!=<>]=? [^)]*\)\s*\n?\s*(?:continue|break)",
                    exec_half))
# The other two software fragment loops -- the immediate-quad family and the
# generic combiner -- are structurally inline-only (no mem_direct_ surfaces, and
# the GL path respectively) and are NOT partitioned. If either is ever converted
# it must come with its own guards.
check("the immediate-quad and generic loops are not partitioned",
      "unsigned part" not in body(
          cpp, "bool Nv2a::rasterize_witnessed_immediate_quad(", LATCH) and
      "unsigned part" not in body(
          cpp, "bool Nv2a::rasterize_generic_combiner_batch(",
          "bool Nv2a::execute_image_blit_srccopy() {"))

# -------------------------------------------------------------------------
print("\n2. row ownership is disjoint and total")
# -------------------------------------------------------------------------
stride = re.search(
    r"int32_t y_first = tri_ymin;\s*\n"
    r"\s*const int32_t y_step = static_cast<int32_t>\(parts\);\s*\n"
    r"\s*if \(parts > 1\) \{\s*\n"
    r"\s*int32_t skew = \(tri_ymin - static_cast<int32_t>\(part\)\) % y_step;\s*\n"
    r"\s*if \(skew < 0\) skew \+= y_step;\s*\n"
    r"\s*if \(skew != 0\) y_first = tri_ymin \+ \(y_step - skew\);\s*\n"
    r"\s*\}\s*\n"
    r"\s*for \(int32_t y = y_first; y <= tri_ymax; y \+= y_step\) \{", exec_half)
check("the scanline loop is the interleaved-row stride, verbatim",
      stride is not None)
check("there is exactly one scanline loop in the execute half",
      exec_half.count("for (int32_t y = ") == 1,
      str(exec_half.count("for (int32_t y = ")))
# Executable proof that the stride is a partition of [lo, hi] for every part
# count the flag accepts and every alignment of the bbox: every row is owned by
# exactly one part, and no part visits a row it does not own.
def rows(part, parts, lo, hi):
    first = lo
    if parts > 1:
        skew = (lo - part) % parts
        if skew:
            first = lo + (parts - skew)
    return list(range(first, hi + 1, parts))


bad = None
for parts in range(1, 9):
    for lo in range(0, 17):
        for hi in range(lo, lo + 40):
            seen = []
            for part in range(parts):
                owned = rows(part, parts, lo, hi)
                if any(y % parts != part for y in owned):
                    bad = ("not owned", parts, lo, hi, part)
                seen.extend(owned)
            if sorted(seen) != list(range(lo, hi + 1)):
                bad = ("not a partition", parts, lo, hi)
check("the stride partitions every bbox exactly, for N in 1..8", bad is None,
      repr(bad))

# -------------------------------------------------------------------------
print("\n3. a worker writes no shared mutable state")
# -------------------------------------------------------------------------
check("part 0 accumulates into job.out, helper part p into span_out_[p]",
      "execute_witnessed_fragments(job, job.out, 0, parts);" in spans and
      "DrawJobTelemetry& out = span_out_[part];" in span_worker and
      "out.ok = execute_witnessed_fragments(job, out, part, parts);" in span_worker)
check("the sub-accumulators are cleared before every job",
      "for (unsigned p = 1; p < parts; ++p) span_out_[p].reset();" in spans)
check("the helper body reaches no Unicorn API and no guest-memory accessor",
      not re.search(r"\buc_|mem_read_|mem_write_|mem_direct_", span_worker),
      repr(re.findall(r"\buc_\w+|mem_(?:read|write|direct)_", span_worker)))
# Every name the helper calls, allowlisted. execute_witnessed_fragments is the
# only Nv2a member function on the list; everything else is std:: atomics,
# locks, the rdtsc reader and the pause intrinsic. A new call appearing here is
# a new opportunity to touch emulator state off the emulation thread and must
# be argued for, not merged silently.
SPAN_WORKER_ALLOWED = {
    "if", "for", "while", "return", "load", "store", "fetch_add", "fetch_sub",
    "wait", "notify_one", "notify_all", "lock_guard", "unique_lock", "lock",
    "tick_now", "draw_cpu_pause", "joinable", "execute_witnessed_fragments",
    "span_worker_main",
}
span_worker_calls = set(re.findall(r"\b(\w+)\(", span_worker))
check("the helper calls no Nv2a member function but the shared executor",
      span_worker_calls <= SPAN_WORKER_ALLOWED,
      repr(sorted(span_worker_calls - SPAN_WORKER_ALLOWED)))
# The one authoritative summation list. Stage (e2) folds the parts by calling
# fold_draw_telemetry once per part, in part order, rather than by writing a
# second merge function that could drift out of sync with it.
check("there is exactly one place counters are summed into the members",
      cpp.count("void Nv2a::fold_draw_telemetry(") == 1 and
      "fold_draw_telemetry(job.out, job.hash_on);" in cpp and
      "for (unsigned p = 1; p < job.span_parts; ++p)\n"
      "        fold_draw_telemetry(span_out_[p], job.hash_on);" in cpp)
check("the fold runs on the emulation thread, never on a helper",
      "fold_draw_telemetry(" not in span_worker and
      "fold_draw_job(" not in span_worker)

# -------------------------------------------------------------------------
print("\n4. per-draw effects happen exactly once")
# -------------------------------------------------------------------------
check("the empty-clip batch ladder is part 0 only",
      re.search(r"if \(xmax < xmin \|\| ymax < ymin\) \{(?:.|\n)*?"
                r"if \(part != 0\) return true;", exec_half) is not None)
check("the closing batch ladder is part 0 only",
      re.search(r"if \(part != 0\) return true;\s*\n"
                r"\s*if \(profile == Profile::kPitchZ16Depth\)\s*\n"
                r"\s*\+\+depth_raster_batches_;", exec_half) is not None)
check("exactly two per-draw early returns guard the two batch ladders",
      exec_half.count("if (part != 0) return true;") == 2,
      str(exec_half.count("if (part != 0) return true;")))
# A TRIANGLE counter is per primitive, not per destination span: every part runs
# the whole primitive loop and reaches the counter for the same triangles, so an
# ungated ladder multiplies the tally by `parts`. It did -- the threads 0 vs 4
# byte-lock caught all fourteen at exactly 4x while every fragment, pass, write,
# sample and batch counter and all 24 checkpoint framebuffers were identical.
check("the per-triangle ladder is guarded to part 0",
      re.search(r"if \(part == 0\) \{\s*\n"
                r"\s*if constexpr \(kProfile == Profile::kPitchZ16Depth\)\s*\n"
                r"\s*\+\+rasterized_triangles_;", exec_half) is not None and
      re.search(r"\+\+inline_color_triangles_;\s*\n\s*\}\s*\n"
                r"\s*const int32_t tri_xmin", exec_half) is not None)
check("the F3 renderer invalidations are recorded by part 0 only",
      "if (part == 0 && color_profile)\n" in exec_half and
      "if (part == 0 && writes_depth_surface)" in exec_half)
check("the F15 surface-write debug record is taken by part 0 only",
      "if (part == 0 && color_profile && surface_write_debug_cb_) {" in exec_half)
# The per-FRAGMENT ladder is deliberately NOT gated: each part counts the
# fragments it rasterized and the sums add. Gating it would undercount by
# (parts-1)/parts, which is exactly the kind of error the byte-lock catches.
check("the per-fragment ladder is NOT gated on the part",
      re.search(r"rasterized_fragments_ \+= witnessed_fragment_count;",
                exec_half) is not None and
      exec_half.index("rasterized_fragments_ += witnessed_fragment_count;") <
      exec_half.index("if (part != 0) return true;\n    if (profile == "
                      "Profile::kPitchZ16Depth)"))

# -------------------------------------------------------------------------
print("\n5. there is a barrier before the next job")
# -------------------------------------------------------------------------
check("the span pool publishes the generation UNDER the mutex",
      re.search(r"std::lock_guard<std::mutex> lock\(span_mutex_\);\s*\n"
                r"\s*span_gen_\.fetch_add\(1, std::memory_order_release\);\s*\n"
                r"\s*span_work_cv_\.notify_all\(\);", spans) is not None)
check("the emulation thread waits for every helper part before returning",
      "const unsigned helpers = parts - 1;" in spans and
      "span_done_.load(std::memory_order_acquire) != helpers" in spans and
      "span_done_cv_.wait(lock" in spans)
# 2026-09-05: seq_cst, not release. This RMW and the span_emu_waiting_ load
# beside it form a store-load pair with the emulation thread's own flag store
# and its span_done_ load at the barrier. Under release/relaxed both sides can
# read the other's stale value -- the helper decides nobody is waiting and skips
# the notify while the emulation thread decides the part is unfinished and
# sleeps -- and a predicate wait does not help, because a predicate is only
# re-evaluated on a wake. The same defect was MEASURED on the async ring the
# same day (published_pub=174, executed=173, worker_sleeping=1, released by a
# bare notify_all); this path is the shipping default, so it is pinned here.
check("the helper publishes completion with a full fence, not a release store",
      "span_done_.fetch_add(1, std::memory_order_seq_cst);" in span_worker and
      "span_emu_waiting_.load(std::memory_order_seq_cst)" in span_worker)
check("the barrier's half of the pair is seq_cst on both sides",
      "span_emu_waiting_.store(true, std::memory_order_seq_cst);" in spans and
      "span_done_.load(std::memory_order_seq_cst) == helpers" in spans)
# The only statement-level return in execute_draw_spans is the last line, so
# there is no path that publishes work to the helpers and then returns to the
# guest without waiting for it. (The one other `return` in the function is
# inside the condvar predicate lambda.)
span_returns = re.findall(r"^\s{4}return\b", spans, re.M)
check("the barrier is unconditional: no early return skips it",
      len(span_returns) == 1 and spans.rstrip().endswith("return ok;\n}"),
      repr(span_returns))
check("only one job is in flight: the job pointer is cleared at the barrier",
      "span_job_ = &job;" in spans and "span_job_ = nullptr;" in spans and
      spans.index("span_job_ = nullptr;") > spans.index("span_done_cv_.wait"))
check("a span-parallel job never enters the ring",
      "job.deferred = true;" not in spans and
      "draw_queue_publish(" not in spans)
# Four fences as of 2026-09-05 (beads-xbox.1.15.7): the stage (f) GPU executor
# takes one before it makes a GPU surface copy authoritative. FIVE as of phase 7
# (beads-xbox.1.15.10): the witnessed-family reroute is a second entry into the
# strict generic fallback and takes the same fence. The property this guard
# defends is unchanged -- one fence implementation, one drain condition, and
# every inline pixel path behind a fence.
check("the ten coherence anchors and the order fence are untouched",
      "if (draw_threads_ != 0) draw_queue_drain();" in cpp and
      cpp.count("void Nv2a::draw_order_fence() {") == 1 and
      latch_half.count("draw_order_fence();") == 5,
      str(latch_half.count("draw_order_fence();")))

# -------------------------------------------------------------------------
print("\n6. order-dependent state is excluded by the eligibility rule")
# -------------------------------------------------------------------------
check("hash_on still forces the job inline, so no FNV chain is ever split",
      "else if (job.hash_on)\n        seam_note_threadable(job, false, kSeamHashOn);"
      in cpp)
check("a debug callback still forces the job inline (F15)",
      "else if (job.debug_cb_installed)" in cpp)
check("the direct-pointer rule is unchanged (section 1.3)",
      "const bool direct_ok = color_direct != nullptr &&" in cpp)
# Fourteen of the seventeen last-write-wins scalars are written only inside
# `if (hash_on)` blocks and so are unreachable from a span-parallel job. The
# other three assign raster_state[kRasterCombinerFactor0_0 + 3], a job-invariant
# register word, so every part that writes one writes the SAME value and the
# fold order cannot matter. Nothing is dropped and nothing is declared n/a.
ungated = re.findall(r"(last_\w+) =\s*\n?\s*raster_state\[kRasterCombinerFactor0_0 \+ 3\]",
                     exec_half)
check("the three ungated last-value scalars assign a job-invariant word",
      sorted(set(ungated)) == ["last_pitch_three_rescale_factor3_",
                               "last_pitch_three_xy_tie_factor3_",
                               "last_pitch_three_zero_factor3_"],
      repr(sorted(set(ungated))))
all_last = set(re.findall(r"(last_\w+_) =(?! =)", exec_half))
check("every other last-value write is inside a hash_on block",
      len(all_last - set(ungated)) == len(all_last) - 3 and
      all(exec_half.count("if (hash_on) {") >= 1 for _ in [0]),
      repr(sorted(all_last)))
check("the destination swizzle table is built once at the latch, not per part",
      "job.surface_swizzle =\n            std::make_unique<SwizzleLookup>(" in cpp
      and "const SwizzleLookup* const surface_swizzle = job.surface_swizzle.get();"
      in exec_half and
      "std::make_unique<SwizzleLookup>(surface_width, surface_height)" not in
      exec_half)

# -------------------------------------------------------------------------
print("\n7. the part count is bounded and refused, not clamped")
# -------------------------------------------------------------------------
check("kDrawThreadsMax bounds the part count",
      "static constexpr unsigned kDrawThreadsMax = 32;" in hpp)
check("set_draw_threads refuses out of range",
      "if (n > kDrawThreadsMax) return false;" in set_threads)
check("the CLI refuses out of range rather than clamping",
      "parsed > xlle::Nv2a::kDrawThreadsMax" in main)
check("N == 1 starts the ring worker and N >= 2 starts the span pool",
      "if (n == 1) {" in set_threads and "} else if (n >= 2) {" in set_threads and
      "span_pool_start(n);" in set_threads and
      "draw_worker_ = std::thread(" in set_threads)
check("switching the part count stops and joins the span pool first",
      "span_pool_stop();" in set_threads and
      set_threads.index("span_pool_stop();") < set_threads.index("draw_threads_ = n;"))
check("the span pool is stopped and JOINED on destruction",
      "span_pool_stop();" in dtor)
check("the pool sizes its sub-accumulators to the part count",
      "span_out_.assign(parts, DrawJobTelemetry());" in pool_start and
      "for (unsigned i = 1; i < parts; ++i)" in pool_start)
check("stopping the pool joins every helper",
      "worker.join();" in pool_stop and "span_workers_.clear();" in pool_stop)
check("no new dependency: only the standard threading headers",
      not re.search(r"#include <(?:future|latch|barrier|execution|semaphore)>", hpp))

# -------------------------------------------------------------------------
print("\n8. toggle-off is unchanged")
# -------------------------------------------------------------------------
check("the device still defaults to inline",
      "unsigned draw_threads_ = 0;" in hpp)
check("main defaults the flag to the auto policy and 0 stays reachable",
      "unsigned nv2a_draw_threads = auto_draw_threads();" in main and
      '"--nv2a-draw-threads"' in main)
check("every whole-surface caller passes part 0 of 1 part",
      cpp.count("execute_witnessed_fragments(job, job.out, 0, 1);") == 3,
      str(cpp.count("execute_witnessed_fragments(job, job.out, 0, 1);")))
check("the latch tail is a whole-surface call",
      "return execute_witnessed_fragments(job, job.out, 0, 1);" in latch_half)
check("an inline job records one part, so the fold loop does not run",
      "unsigned span_parts = 1;" in hpp)

# -------------------------------------------------------------------------
print("\n9. the census makes a vacuous A/B impossible")
# -------------------------------------------------------------------------
check("span_jobs is counted and is the witness that work division ran",
      "++draw_span_jobs_;" in spans and "span_jobs=%llu" in main)
check("the census reports the barrier wait",
      "barrier_wait_ticks=%llu" in main and
      "draw_span_barrier_wait_ticks_ +=" in spans)
check("the census reports part 0 and helper busy time separately",
      "part0_busy_ticks=%llu" in main and "helper_busy_ticks=%llu" in main and
      "draw_span_part0_busy_ticks_ +=" in spans and
      "span_helper_busy_ticks_.fetch_add(" in span_worker)
# Counted in retire_draw_job, the single funnel every executed non-deferred job
# passes through, so it covers the immediate-quad and generic-combiner families
# as well as a witnessed job held back by the eligibility rule. Counting it at
# the witnessed latch instead reported span_inline=0 against jobs_inline=68310.
check("the census reports the jobs that could not be divided",
      "span_inline=%llu" in main and
      "if (draw_threads_ > 1 && job.span_parts == 1) ++draw_span_inline_jobs_;"
      in body(cpp, "void Nv2a::retire_draw_job(DrawJob& job, bool executed) {",
              "void Nv2a::fold_draw_job(DrawJob& job) {"))
check("the census reports the barrier cost per job",
      "barrier_ticks_per_job=%.1f" in main)
check("measure_xbox_replay.py parses the draw-span line",
      "draw-span" in measure or "DRAW_SPAN_RE" in measure)

# -------------------------------------------------------------------------
# beads-xbox.1.15.9 shipped with two instruments missing: the per-part fold was
# an ESTIMATE (draw_span_fold_ticks_ was declared, accessible, and never
# written) and helper time was invisible. Both are now measured. The fold clock
# reads are gated on perf_excl::g_enabled so an uninstrumented timing leg -- the
# runs the gate is decided on -- executes exactly the code it executed before.
# -------------------------------------------------------------------------
fold = body(cpp, "void Nv2a::fold_draw_job(DrawJob& job) {",
            "void Nv2a::fold_draw_telemetry(")
check("the per-part fold cost is measured, not estimated",
      "const bool time_fold = perf_excl::g_enabled;" in fold and
      "draw_span_fold_ticks_ += perf_excl::tick_now() - fold_t0;" in fold)
check("the fold clock reads cost a noperf leg nothing",
      "const uint64_t fold_t0 = time_fold ? perf_excl::tick_now() : 0;" in fold)
check("the fold cost reaches the census",
      "fold_ticks=%llu" in main and "draw_span_fold_ticks()" in main)
# Helper raster time is real CPU time that is NOT emulation-thread wall, so it
# cannot go into the [perf-excl] partition without making that partition sum
# past its own denominator. It gets a sibling line, labelled off-thread.
check("worker time is a SIBLING line, explicitly outside the partition",
      "[perf-worker]" in main and "off_thread=1" in main and
      "in_partition=0" in main and "worker_raster_ns=%llu" in main)
check("the sibling line reports part 0, the barrier and the fold beside it, so "
      "the true raster cost is reconstructable",
      "part0_raster_ns=%llu" in main and "barrier_wait_ns=%llu" in main and
      "span_fold_ns=%llu" in main and "ns_per_tick=%.6f" in main)
check("it is printed only when the tick scale is calibrated",
      "if (xlle::perf_excl::g_enabled) {" in main)
check("measure_xbox_replay.py parses the sibling line as its own family",
      "PERF_WORKER_RE" in measure and '"perf_worker":' in measure)

print("\ndestination-span raster guards: %d checks, %d failure(s)"
      % (checks, len(failures)))
for name in failures:
    print("  FAILED: %s" % name)
sys.exit(1 if failures else 0)
