#!/usr/bin/env python3
"""Structural guards for the threaded NV2A draw consumer, stage (e).

Bead beads-xbox.1.15.5; design docs/DESIGN-NV2A-SEAM.md sections 1.3, 1.5, 2.2(c),
3.1, 4 and 5(e); methodology docs/PERF-METHODOLOGY.md section 4.

Stage (e) hands DrawJobs to one worker thread behind a default-off flag. The
properties that make that safe are not checkable by the compiler and are not
observable in a passing replay either -- a byte-lock that ran with an empty queue
proves nothing, and a data race that happens not to fire today is still a data
race. So they are pinned here:

  1. The flag is DEFAULT OFF and threads=0 runs today's code path: the enqueue
     branch is the only difference, and nothing else in the raster path branches
     on the thread count.
  2. A job is enqueued only when it is `threadable` AND threads > 0, and
     `threadable` still requires direct destination pointers (section 1.3), the
     per-write hash fold off (section 4.3 O3), no GL path (F3) and no diagnostic
     callback (F15).
  3. Every one of the ten coherence anchors is a real drain when threads > 0:
     they all funnel through Nv2a::seam_anchor_drain and that function drains.
  4. NOTHING on the worker path reaches Unicorn. The execute half touches no
     Nv2a member except an explicit four-name allowlist, and calls no Nv2a member
     function at all.
  5. Every counter, hash and last-value scalar the fragment loop writes is
     JOB-LOCAL, folded on the emulation thread in latch order.
  6. Ordering: one consumer, strictly in submission order, capped by
     kDrawThreadsMax == 1; a full ring drains and runs inline rather than
     dropping; the worker is joined on destruction.

Run: python tests/test_draw_threads_guards.py   (exit 0 = all guards hold)
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
for sig in (LATCH, EXEC):
    if cpp.count(sig) != 1:
        print("FATAL: %r appears %d times" % (sig[:60], cpp.count(sig)))
        sys.exit(2)

latch_half = body(cpp, LATCH, EXEC)
exec_half = body(cpp, EXEC, "void Nv2a::record_generic_skip(")
submit = body(cpp, "bool Nv2a::submit_vertex_batch() {",
              "bool Nv2a::rasterize_witnessed_immediate_quad(")

# -------------------------------------------------------------------------
print("\nthe flag is default off and threads=0 is today's path")
# -------------------------------------------------------------------------
check("the device defaults to inline",
      "unsigned draw_threads_ = 0;" in hpp)
# 2026-09-05: the DEVICE still defaults to inline (a freshly constructed Nv2a
# runs the faithful path until something asks otherwise); the HOST now opts in
# on behalf of the user, sized to the machine, with 0 still reachable by flag.
check("the host option defaults to the auto policy, not to a fixed count",
      "unsigned nv2a_draw_threads = auto_draw_threads();" in main)
check("the auto policy leaves the host two logical processors and refuses to "
      "thread a machine that cannot spare them",
      "static unsigned auto_draw_threads() {" in main and
      "if (hw < 2) return 0;" in main and
      "const unsigned budget = hw - 2;" in main and
      "return n < 2 ? 0 : n;" in main)
check("the auto policy is clamped by the device bound",
      "if (n > xlle::Nv2a::kDrawThreadsMax) n = xlle::Nv2a::kDrawThreadsMax;" in main)
check("the mode actually used is printed unconditionally, inline included",
      '"[host] nv2a-draw-threads=%u hw_concurrency=%u mode=%s "' in main and
      'nv2a_draw_threads == 0   ? "inline"' in main)
check("the option exists and is spelled --nv2a-draw-threads",
      '"--nv2a-draw-threads"' in main)
check("out-of-range worker counts are refused, not clamped",
      "parsed > xlle::Nv2a::kDrawThreadsMax" in main and
      "set_draw_threads(nv2a_draw_threads)" in main)
# The whole difference between threads=0 and threads>0 in the raster path is one
# `if`. Anything else that branched on the thread count would be a second code
# path, and the toggle-off claim would stop being structural.
raster_branches = re.findall(r"draw_threads_", latch_half + exec_half)
# Two reads, both inside the one enqueue branch: the branch itself and the
# stage (e) / stage (e2) split (== 1 versus >= 2, beads-xbox.1.15.9). Anything
# outside that branch would be a second code path and the toggle-off claim would
# stop being structural.
check("the raster path reads the thread count only inside the enqueue branch",
      len(raster_branches) == 2 and
      "if (draw_threads_ > 0) {" in latch_half and
      latch_half.count("draw_threads_") == 2,
      repr(raster_branches))
check("the execute half never reads the thread count",
      "draw_threads_" not in exec_half)
# At threads == 1 the enqueue branch still only sets a flag and returns -- it
# runs no pixel math on the emulation thread. At threads >= 2 it calls
# execute_draw_spans, which is the barriered whole-draw division and returns
# before the guest instruction retires (beads-xbox.1.15.9).
check("the enqueue branch defers to the ring at N==1 and to the span pool at N>=2",
      re.search(r"if \(draw_threads_ > 0\) \{\s*\n"
                r"\s*if \(job\.threadable\) \{\s*\n"
                r"\s*if \(draw_threads_ == 1\) \{", latch_half) is not None
      and re.search(r"job\.deferred = true;\s*\n\s*return true;\s*\n\s*\}",
                    latch_half) is not None
      and "return execute_draw_spans(job);" in latch_half)
# ORDERING. Anything that executes pixels on the emulation thread while the
# worker owns queued jobs must drain first, or it overtakes jobs that precede it
# in guest order. Three such paths exist and all three must be fenced -- the
# threads=1 byte-lock caught exactly this: the generic-combiner GL path
# overtaking queued jobs moved the read-only-depth profiles' depth-pass counts
# (pitch-single 74342400 -> 73844213, tex7-fan 5709338 -> 5575113).
fence_fn = body(cpp, "void Nv2a::draw_order_fence() {", "void Nv2a::draw_queue_drain_all()")
check("the ordering fence exists and drains only when the queue is non-empty",
      "if (draw_threads_ == 0) return;" in fence_fn and
      "if (draw_published_ == draw_retired_) return;" in fence_fn and
      "draw_queue_drain();" in fence_fn)
check("the immediate-quad family is fenced before it executes",
      re.search(r"draw_order_fence\(\);\s*\n\s*return rasterize_witnessed_immediate_quad\(",
                latch_half) is not None)
check("the generic-combiner GL path is fenced before it executes",
      re.search(r"draw_order_fence\(\);\s*\n\s*return rasterize_generic_combiner_batch\(",
                latch_half) is not None)
check("a non-threadable witnessed job is fenced before it executes inline",
      re.search(r"draw_order_fence\(\);\s*\n\s*\}\s*\n"
                r"\s*return execute_witnessed_fragments\(job, job\.out, 0, 1\);",
                latch_half) is not None)
# Four as of 2026-09-05 (beads-xbox.1.15.7): the stage (f) GPU executor takes a
# fence too. It does not write guest memory itself, but it makes the GPU copy of
# a surface authoritative, and a queued software job writing the same surface
# must retire before that happens.
check("every inline pixel path is fenced: four fences, no more, no fewer",
      latch_half.count("draw_order_fence();") == 4,
      str(latch_half.count("draw_order_fence();")))
check("the fourth fence is the stage (f) GPU executor's",
      re.search(r"draw_order_fence\(\);\s+bool gpu_submitted = false;",
                latch_half) is not None)
check("the fence count is reported so a regression is visible in the census",
      "order_fences" in main and "draw_order_fences()" in main)

# -------------------------------------------------------------------------
print("\na job is enqueued only when it is threadable")
# -------------------------------------------------------------------------
check("deferred is set in exactly one place",
      cpp.count("job.deferred = true;") == 1)
check("the only reader of deferred is retire_draw_job",
      cpp.count("job.deferred") == 2 and
      "if (job.deferred) {" in body(cpp, "void Nv2a::retire_draw_job(",
                                    "void Nv2a::fold_draw_job("))
check("the section 1.3 direct-pointer rule still gates threadable",
      "const bool direct_ok = color_direct != nullptr &&" in latch_half and
      "seam_note_threadable(job, false, kSeamNoDirectPointer);" in latch_half)
check("the per-write hash fold still forces inline (section 4.3 O3)",
      "seam_note_threadable(job, false, kSeamHashOn);" in latch_half)
check("a diagnostic callback still forces inline (F15)",
      "seam_note_threadable(job, false, kSeamDebugCallback);" in latch_half)
check("the GL path still forces inline (F3)",
      cpp.count("seam_note_threadable(job, false, kSeamRendererPath);") == 3)

# -------------------------------------------------------------------------
print("\nevery coherence anchor is a real drain when threads > 0")
# -------------------------------------------------------------------------
enum_src = body(hpp, "    enum SeamAnchor : size_t {",
                "    static const char* seam_anchor_name")
code_anchors = [m for m in re.findall(r"kSeamAnchor(\w+)", enum_src) if m != "Count"]
check("the ten anchors of design section 2.2(c) are still declared",
      len(code_anchors) == 10, repr(code_anchors))
drain_fn = body(cpp, "void Nv2a::seam_anchor_drain(SeamAnchor anchor) {",
                "void Nv2a::seam_texture_source_anchor(")
check("seam_anchor_drain performs the real drain",
      "if (draw_threads_ != 0) draw_queue_drain();" in drain_fn)
# The real drain must be unconditional on the simulation's state: the simulated
# `seam_pending_` counts QUEUEABLE jobs, which is a superset of what is actually
# in the ring, and an early return above the drain would fence nothing.
check("the real drain precedes the M5 simulation's early return",
      drain_fn.index("draw_queue_drain();") <
      drain_fn.index("if (seam_pending_ == 0) return;"))
anchor_fn = body(cpp, "void Nv2a::seam_anchor(SeamAnchor anchor, bool required) {",
                 "void Nv2a::seam_anchor_drain(")
check("every required anchor reaches seam_anchor_drain",
      "seam_anchor_drain(anchor);" in anchor_fn)
for name in code_anchors:
    token = "kSeamAnchor%s" % name
    raised = ("seam_anchor(%s," % token) in cpp
    if name == "TextureOverlap":
        raised = raised or "seam_texture_source_anchor(" in cpp
    if name == "Checkpoint":
        raised = raised and "seam_checkpoint_anchor()" in main
    check("anchor %s is still raised" % name, raised)
check("shutdown drains before any counter is read",
      "mach.nv2a()->seam_checkpoint_anchor();" in main and
      main.count("seam_checkpoint_anchor();") == 2)
check("the frame-series checkpoint drains before the scanout is captured",
      main.index("mach.nv2a()->seam_checkpoint_anchor();") <
      main.index("mach.nv2a()->capture_scanout(frame_rgba, scanout, scanout_error)"))

# -------------------------------------------------------------------------
print("\nnothing on the worker path reaches Unicorn or an Nv2a member")
# -------------------------------------------------------------------------
# The prologue of the execute half binds ~120 shadowing locals; everything after
# the surface construction is the fragment loop proper.
prologue_end = exec_half.index("    PagedRasterSurface color_surface(")
prologue = exec_half[:prologue_end]
fragment = exec_half[prologue_end:]

members = set(re.findall(r"\b([a-z][A-Za-z0-9_]*_)\b\s*(?:=|;|\{|\[|\()", hpp))
shadowed = set(re.findall(r"^\s+(?:const\s+)?[\w:<>,&\* ]*?&?\s*"
                          r"([a-z][A-Za-z0-9_]*_)\s*=", prologue, re.M))
touched = {}
for line in fragment.split("\n"):
    for m in re.finditer(r"(?<![\w.>])([a-z][A-Za-z0-9_]*_)\b", line):
        name = m.group(1)
        if name in members and name not in shadowed:
            touched.setdefault(name, line.strip()[:90])

# The ONLY Nv2a members the executor may touch, and why each is safe:
#   mem_read_ / mem_write_  -- passed to PagedRasterSurface's constructor, which
#       only binds the references. A threadable job has BOTH direct pointers
#       (section 1.3), so direct_base_ is non-null and neither is ever invoked.
#   mem_direct_             -- a pure pointer computation over the two RAM
#       aliases (machine.cpp), no Unicorn call; also only stored, not invoked,
#       because the latch already resolved the pointers into the job.
#   surface_write_debug_cb_ -- F15. Read as a bool and, if set, called. It is
#       installed once at startup and never written during the run, and any run
#       with it installed has debug_cb_installed == true, which forces every job
#       inline -- so a worker only ever reads a null std::function.
ALLOWED_MEMBERS = {"mem_read_", "mem_write_", "mem_direct_", "surface_write_debug_cb_"}
extra = sorted(set(touched) - ALLOWED_MEMBERS)
check("the execute half touches only the four allowlisted members",
      not extra,
      "; ".join("%s @ %s" % (n, touched[n]) for n in extra))
check("the three memory accessors appear only in the surface construction",
      all(touched.get(n, "").startswith("mem_read_, mem_write_, mem_direct_")
          for n in ("mem_read_", "mem_write_", "mem_direct_")),
      repr({n: touched.get(n) for n in ("mem_read_", "mem_write_", "mem_direct_")}))

# No Nv2a member FUNCTION may be called either -- fault_ and the two renderer
# invalidations are shadowed by local lambdas that record instead of acting.
member_fns = set(re.findall(
    r"^\s+(?:static\s+|virtual\s+|inline\s+)*[A-Za-z_][\w:<>,\*&\s]*?"
    r"\b([a-z][A-Za-z0-9_]*)\s*\(", hpp, re.M))
shadow_lambdas = set(re.findall(r"auto\s+([a-z][A-Za-z0-9_]*)\s*=\s*\[", prologue))
local_lambdas = set(re.findall(r"auto\s+([a-z][A-Za-z0-9_]*)\s*=", fragment))
called = {}
for line in fragment.split("\n"):
    for m in re.finditer(r"(?<![\w.>:])([a-z][A-Za-z0-9_]*)\s*\(", line):
        name = m.group(1)
        if name in ("if", "for", "while", "switch", "return", "sizeof"):
            continue
        if name in member_fns and name not in local_lambdas and name not in shadow_lambdas:
            called.setdefault(name, line.strip()[:90])
check("the execute half calls no Nv2a member function",
      not called, repr(called))
check("fault_ is shadowed by a recording lambda (F13)",
      "auto fault_ = [&out](const std::string& message) {" in prologue and
      "out.failed = true;" in prologue)
check("the renderer invalidations are shadowed by recording lambdas (F3)",
      "auto invalidate_renderer_color_overlap = [&out]" in prologue and
      "auto invalidate_renderer_depth_overlap = [&out]" in prologue)
check("the recorded fault is raised on the emulation thread at the fold",
      "if (out.failed) fault_(out.fault_message);" in
      body(cpp, "void Nv2a::fold_draw_job(DrawJob& job) {",
           "void Nv2a::fold_draw_telemetry("))
check("the recorded invalidations are applied on the emulation thread at the fold",
      "invalidate_renderer_color_overlap(out.invalidate_color_address," in
      body(cpp, "void Nv2a::fold_draw_job(DrawJob& job) {",
           "void Nv2a::fold_draw_telemetry("))
# Nothing in the worker path may name Unicorn directly either.
worker = body(cpp, "void Nv2a::draw_worker_main() {", "void Nv2a::draw_queue_publish(")
check("the worker body reaches no Unicorn API and no guest-memory accessor",
      not re.search(r"\buc_|\bmach\.|mem_read_|mem_write_", worker),
      repr(re.findall(r"\buc_\w+|\bmach\.\w+|mem_(?:read|write)_", worker)))
check("the worker calls the SAME executor the inline path calls",
      "job.out.ok = execute_witnessed_fragments(job, job.out, 0, 1);" in worker
      and cpp.count("bool Nv2a::execute_witnessed_fragments(") == 1)

# -------------------------------------------------------------------------
print("\nevery counter the fragment loop writes is job-local")
# -------------------------------------------------------------------------
tel = body(hpp, "    struct DrawJobTelemetry {", "    // ================== stage (d)")
tel_fields = set(re.findall(r"^\s+(?:uint64_t|DrawJobLastValue)\s+(\w+)", tel, re.M))
fold = body(cpp, "void Nv2a::fold_draw_telemetry(const DrawJobTelemetry& out, bool hash_on) {",
            "void Nv2a::seam_publish_job(")
folded = set(re.findall(r"\b([a-z][A-Za-z0-9_]*)_\s*(?:\+=|=)\s*out\.", fold))
folded |= set(re.findall(r"\bif \(out\.\w+\.set\) ([a-z][A-Za-z0-9_]*)_ =", fold))
check("the telemetry struct carries at least 120 job-local cells",
      len(tel_fields) >= 120, str(len(tel_fields)))
check("every telemetry cell is folded back into its member",
      tel_fields <= {f for f in folded} | {"busy_ticks"},
      repr(sorted(tel_fields - ({f for f in folded} | {"busy_ticks"}))[:8]))
check("pure accumulators are folded with +=, never assigned",
      fold.count(" += out.") >= 89, str(fold.count(" += out.")))
check("the order-dependent FNV chains are seeded at latch and only under hash_on",
      "void Nv2a::seed_draw_telemetry(DrawJob& job) {" in cpp and
      "if (job.hash_on) {" in body(cpp, "void Nv2a::seed_draw_telemetry(DrawJob& job) {",
                                   "bool Nv2a::execute_draw_job(") and
      "if (hash_on) {" in fold)
check("last-value scalars carry a set flag so a silent job cannot clobber",
      "struct DrawJobLastValue {" in hpp and
      fold.count(".set) ") == 17, str(fold.count(".set) ")))
check("the fold runs on the emulation thread only, from retire or from the drain",
      cpp.count("fold_draw_job(") == 4,
      str(cpp.count("fold_draw_job(")))

# -------------------------------------------------------------------------
print("\nordering, backpressure and lifetime")
# -------------------------------------------------------------------------
# beads-xbox.1.15.9 raised the bound from 1 to 8 (the reference host is 8C/16T)
# because N >= 2 no longer means N draws in flight -- it means one draw divided
# across N disjoint destination-scanline sets. Out-of-range is still refused,
# never clamped, so a typo cannot silently measure a different configuration.
check("the worker bound is the host core count; more is refused, not clamped",
      "static constexpr unsigned kDrawThreadsMax = 32;" in hpp and
      "if (n > kDrawThreadsMax) return false;" in
      body(cpp, "bool Nv2a::set_draw_threads(unsigned n) {", "Nv2a::~Nv2a() {"))
check("the ring is sized from M5's measured maximum with headroom",
      "static constexpr size_t kDrawQueueSlots = 1024;" in hpp)
publish = body(cpp, "void Nv2a::draw_queue_publish(DrawJob& job) {",
               "void Nv2a::draw_queue_drain() {")
check("a full ring drains and runs the job inline; it never drops",
      "const bool slots_full = draw_published_ - draw_retired_ >= kDrawQueueSlots;" in publish and
      "if (slots_full || bytes_full) {" in publish and
      "draw_queue_drain();" in publish and
      "++draw_backpressure_inline_;" in publish and
      "execute_witnessed_fragments(job, job.out, 0, 1);" in publish)
# Slots are not a memory bound -- a job owns its texture snapshots, which reach
# megabytes -- so the ring is bounded by bytes too, and a retired slot releases
# what it owns instead of holding it until the slot is next written.
drain = body(cpp, "void Nv2a::draw_queue_drain() {", "void Nv2a::draw_queue_drain_all()")
check("the ring is bounded by bytes as well as by slots",
      "static constexpr uint64_t kDrawQueueMaxBytes" in hpp and
      "const bool bytes_full = draw_queue_bytes_ + bytes > kDrawQueueMaxBytes;" in publish)
check("a folded slot releases the bytes it owned",
      "done = DrawJob();" in drain and "draw_queue_bytes_ -= " in drain)
check("the drain waits for EVERY published job, then folds in latch order",
      "const uint64_t target = draw_published_;" in drain and
      "while (draw_retired_ != target) {" in drain and
      "draw_executed_.load(std::memory_order_acquire) == target" in drain)
check("the worker consumes strictly in order with a single cursor",
      "uint64_t next = 0;" in worker and
      "draw_slots_[static_cast<size_t>(next % kDrawQueueSlots)]" in worker and
      "draw_executed_.store(next, std::memory_order_seq_cst);" in worker)
dtor = body(cpp, "Nv2a::~Nv2a() {", "void Nv2a::draw_worker_main() {")
check("the worker is stopped and JOINED on destruction",
      "draw_stop_.store(true);" in dtor and
      "if (draw_worker_.joinable()) draw_worker_.join();" in dtor)
check("the destructor is declared on the device",
      "~Nv2a() override;" in hpp)
check("switching the worker count drains and joins before restarting",
      "draw_queue_drain_all();" in
      body(cpp, "bool Nv2a::set_draw_threads(unsigned n) {", "Nv2a::~Nv2a() {"))
check("no new dependency: only the standard threading headers",
      "#include <thread>" in hpp and "#include <mutex>" in hpp and
      "#include <condition_variable>" in hpp and "#include <atomic>" in hpp)

# -------------------------------------------------------------------------
print("\nthe census makes a vacuous gate impossible")
# -------------------------------------------------------------------------
check("the queue census is printed for both scopes",
      "[host] draw-queue scope=%s deadline=%llu threads=%u jobs_queued=%llu " in main)
for field in ("jobs_queued", "jobs_inline", "backpressure_inline", "drains_empty",
              "drains_nonempty", "queue_depth_mean", "queue_depth_max",
              "worker_busy_ticks", "drain_wait_ticks", "worker_busy_frac",
              "drain_wait_frac", "worker_failures"):
    check("the census reports %s" % field, field in main)
check("the queue-depth histogram is printed per bucket",
      "[host] draw-depth scope=%s deadline=%llu bucket=%s count=%llu\\n" in main)
check("measure_xbox_replay.py parses both line families additively",
      "DRAW_QUEUE_RE" in measure and "DRAW_DEPTH_RE" in measure and
      '"draw_queue":' in measure and '"draw_depth":' in measure)

# -------------------------------------------------------------------------
print("\nF3: a renderer residency query drains before it asks (2026-09-05)")
# -------------------------------------------------------------------------
# beads-xbox.1.15.5 left this as an analysed-but-unproven residual risk. The
# executor only RECORDS its invalidations; fold_draw_job applies them at the
# drain. So between publish and drain the renderer can still claim to own bytes
# a queued job is about to overwrite, and resolving them back into guest RAM
# afterwards clobbers the software output. Every residency QUERY therefore
# fences first. Measured at N == 1 over 1 B instructions: 67 489 of 68 213
# queued draws hit a query with a non-empty ring, so the window is pervasive,
# not theoretical. At N >= 2 the ring is never used and the counter is 0.
query_fence = body(cpp, "void Nv2a::draw_renderer_query_fence() {",
                   "void Nv2a::draw_queue_drain_all()")
check("the query fence exists and drains only when the ring is non-empty",
      "if (draw_threads_ == 0) return;" in query_fence and
      "if (draw_published_ == draw_retired_) return;" in query_fence and
      "++draw_renderer_query_fences_;" in query_fence and
      "draw_queue_drain();" in query_fence)
check("it is counted separately from the ordering fence, so either stays readable",
      "uint64_t draw_renderer_query_fences_ = 0;" in hpp and
      "draw_renderer_query_fences()" in hpp)
for fn, tail in (("bool Nv2a::resolve_renderer_color_overlap(", "void Nv2a::invalidate_renderer_color_overlap("),
                 ("bool Nv2a::resolve_renderer_depth_overlap(", "void Nv2a::invalidate_renderer_depth_overlap(")):
    fn_body = body(cpp, fn, tail)
    check("%s fences before it queries the renderer" % fn.split("::")[1].rstrip("("),
          "draw_renderer_query_fence();" in fn_body and
          fn_body.index("draw_renderer_query_fence();") <
          fn_body.index("renderer_->resolve_resident"))
refresh = body(cpp, "bool Nv2a::refresh_resident_color_after_software_write(",
               "bool Nv2a::resolve_renderer_depth_overlap(")
check("the region-upload query fences before has_resident_color_surface",
      "draw_renderer_query_fence();" in refresh and
      refresh.index("draw_renderer_query_fence();") <
      refresh.index("renderer_->supports_resident_color_region_upload()"))
check("the generic-combiner residency questions fence at the function, not at "
      "one caller",
      "draw_renderer_query_fence();" in
      body(cpp, "bool Nv2a::rasterize_generic_combiner_batch(",
           "bool Nv2a::execute_image_blit_srccopy()"))
check("every residency query in the device is covered: four fence sites",
      cpp.count("draw_renderer_query_fence();") == 4,
      str(cpp.count("draw_renderer_query_fence();")))
check("the fence count reaches the census so the window is visible per run",
      "renderer_query_fences" in main and "draw_renderer_query_fences()" in main)

# -------------------------------------------------------------------------
print("\nwake-ups cannot be lost: the store-load pairs are seq_cst (2026-09-05)")
# -------------------------------------------------------------------------
# Measured failure, not a theoretical one: with the F3 fence wired in, the async
# ring hung at draw 174 with published_pub=174, executed=173, worker_sleeping=1.
# A release store followed by a load of the peer's flag is not ordered against
# that load on x86, so producer and consumer can each read the other's stale
# value -- the producer skips the notify and the consumer sleeps. The predicate
# form of condition_variable::wait does NOT cover this: a predicate is only
# re-evaluated on a wake, and the wake is what was lost.
publish = body(cpp, "void Nv2a::draw_queue_publish(DrawJob& job) {",
               "void Nv2a::draw_queue_drain()")
check("the ring publishes its cursor with a full fence",
      "draw_published_pub_.store(draw_published_, std::memory_order_seq_cst);" in publish)
check("the ring tests the consumer's sleep flag with a full fence",
      "draw_worker_sleeping_.load(std::memory_order_seq_cst)" in publish)
worker = body(cpp, "void Nv2a::draw_worker_main() {", "uint64_t Nv2a::draw_job_bytes(")
check("the consumer's half of the pair is seq_cst on both sides",
      "draw_worker_sleeping_.store(true, std::memory_order_seq_cst);" in worker and
      "draw_published_pub_.load(std::memory_order_seq_cst) != next" in worker)
check("the completion half of the pair is seq_cst too",
      "draw_executed_.store(next, std::memory_order_seq_cst);" in worker and
      "draw_emu_waiting_.load(std::memory_order_seq_cst)" in worker)
drain_body = body(cpp, "void Nv2a::draw_queue_drain() {", "void Nv2a::draw_order_fence()")
check("the draining side is seq_cst on both sides",
      "draw_emu_waiting_.store(true, std::memory_order_seq_cst);" in drain_body and
      "draw_executed_.load(std::memory_order_seq_cst) ==" in drain_body)
check("the span barrier -- the DEFAULT path -- uses the same full fences",
      "span_done_.fetch_add(1, std::memory_order_seq_cst);" in cpp and
      "span_emu_waiting_.load(std::memory_order_seq_cst)" in cpp and
      "span_emu_waiting_.store(true, std::memory_order_seq_cst);" in cpp and
      "span_done_.load(std::memory_order_seq_cst) == helpers" in cpp)
check("a drain that does not complete reports instead of hanging silently",
      "draw drain stalled >5s" in cpp and
      "draw_done_cv_.wait_for(" in drain_body,
      "an unbounded wait turns any ordering defect into a hang with no evidence")

print()
print("draw-thread guards: %d checks, %d failure(s)" % (checks, len(failures)))
if failures:
    for name in failures:
        print("  FAILED: %s" % name)
    sys.exit(1)
print("threaded draw consumer guards: all assertions hold")
