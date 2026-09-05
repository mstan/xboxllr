#!/usr/bin/env python3
"""Structural regression checks for GL GPU-side timing
(beads-xbox.1.14.6, src/host/nv2a_opengl_renderer.cpp).

WHY A STRUCTURAL TEST
---------------------
The audit's finding was an ABSENCE: the renderer contained zero
GL_TIME_ELAPSED queries, zero fences, zero glFinish (grep = 0), so every
"renderer_*" number in the report was CPU issue time wearing a GPU label.
renderer_draw_ns of 4.2 ms over 14 468 draws said nothing about what the GPU
did, and renderer_resolve_ns of 3 998 ms over 1 308 readbacks (3.06 ms each,
against roughly 0.3 ms of actual 640x480 BGRA8 transfer) conflated three
unrelated costs.

What closes that hole is not a number but a PLACEMENT, and the failure mode is
silent in exactly the same way as before: add a new glDrawArrays site that calls
the raw function pointer, and xlle still builds, still runs, still prints a
[perf-gl-gpu] line -- with that draw's GPU time simply missing from it. Nothing
goes red, and the next session reads a total that is quietly short.

So each property below is one named assertion:

  * every draw, dispatch, upload and readback goes through a gpu_timed_* wrapper
    -- the raw pointers are called from nowhere else, so a new site cannot
    escape timing by being written the obvious way;
  * each wrapper opens a GL_TIME_ELAPSED region, and the resolve wrapper also
    carries the fence split and the bytes_read / dirty_bytes pair;
  * results are collected only when GL_QUERY_RESULT_AVAILABLE says so, never by
    arming and waiting (PRINCIPLES section 7 / methodology rule 10), with the
    one blocking drain confined to shutdown;
  * GL permits one GL_TIME_ELAPSED query at a time, so a nested site is counted,
    not asserted on -- an unguarded glBeginQuery would raise
    GL_INVALID_OPERATION into the submit paths' get_error_() checks and turn a
    measurement into a fault-stop;
  * the instrument is inert when --perf-accounting is off, and carries its own
    availability flag and drop counter (methodology rule 9: an instrument
    exports its denominators);
  * the printed field list IS the struct field list, and the parser's field list
    is the same list again -- three copies that drift silently otherwise;
  * the other agent's work in this file is intact: the three perf_excl::kNv2aGl
    regions and the six ScopedGlContextTimer sites, and no ungated steady_clock
    read reappears (tests/test_perf_excl_guards.py enforces the last one too;
    it is repeated here because this change is the one that would break it).

Run standalone, or through ctest once wired:
    add_test(NAME gl_timing_guards COMMAND python
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gl_timing_guards.py)
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
GL_CPP = ROOT / "src/host/nv2a_opengl_renderer.cpp"
MEASURE_PY = ROOT / "tools/measure_xbox_replay.py"

failures = []


def check(name, condition, detail=""):
    if condition:
        print("  ok   %s" % name)
    else:
        print("  FAIL %s: %s" % (name, detail))
        failures.append(name)


def read(path):
    return path.read_text(encoding="utf-8", errors="replace")


def body(source, name, path):
    """Brace-matched body of the definition of function `name`.

    Only a DEFINITION matches: a call site ends in `);`, never in `) {`.
    """
    pattern = (r"[A-Za-z_][\w:<>,*&\s]*?\b" + re.escape(name) +
               r"\s*\([^;{]*?\)\s*(?:const\s*)?(?:override\s*)?(?:noexcept\s*)?\{")
    match = re.search(pattern, source, re.S)
    if name.startswith("~"):
        pattern = (re.escape(name) +
                   r"\s*\(\s*\)\s*(?:override\s*)?(?:noexcept\s*)?\{")
        match = re.search(pattern, source, re.S)
    if not match:
        raise AssertionError("missing definition of %s in %s" % (name, path))
    start, depth, i = match.end(), 1, match.end()
    while i < len(source) and depth:
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
        i += 1
    return source[start:i - 1]


gl_src = read(GL_CPP)
gl_lines = gl_src.split("\n")


def lines_matching(pattern):
    rx = re.compile(pattern)
    return [(n + 1, line) for n, line in enumerate(gl_lines) if rx.search(line)]


# ── 1. Every GL submission/upload/readback goes through a timed wrapper ──────
#
# (raw function pointer, the wrapper(s) allowed to call it, expected call count
# at the wrapper). The counts are asserted so that deleting a call site is as
# visible as adding one.
# Counts updated 2026-09-05 for the stage (f) witnessed-profile GPU executor
# (beads-xbox.1.15.7): +1 draw (the batched witnessed flush), +1 buffer_data
# (its triangle SSBO), +3 tex_image_2d (its colour, zeta and sampled-texture
# uploads). Every one of those goes through the wrapper, which is the property
# this file exists to keep true.
# Updated again 2026-09-05 for beads-xbox.1.15.10 phase 2 session 2: the three
# witnessed uploads (colour, zeta, sampled texture) each gained a
# glTexSubImage2D partner, taken when the geometry is unchanged so the driver
# reuses the allocation instead of re-specifying storage; the glTexImage2D
# branch stays for the first upload into a slot, so the tex_image_2d_ count is
# unchanged and tex_sub_image_2d_ goes 1 -> 4.
# Updated again 2026-09-05 for beads-xbox.1.15.10 phase 4 (lever A): the
# projected profile's SECOND sampled image (ensure_witnessed_projected_texture)
# adds one glTexImage2D and one glTexSubImage2D, both through the wrapper.
# 9 -> 10 and 4 -> 5.
# Updated again 2026-09-05 for beads-xbox.1.15.10 phase 6 (stage f3): the
# GENERATED generic-combiner executor adds one glDrawArrays (its own batched
# draw), one glBufferData (its triangle SSBO) and two glTexImage2D (its
# destination-plane upload and its per-stage sampled textures). 3 -> 4, 7 -> 8
# and 10 -> 12. Every one of them goes through the wrapper, which is the
# property this file exists to keep true.
WRAPPED = [
    ("draw_arrays_", {"gpu_timed_draw_arrays": 4}),
    ("dispatch_compute_", {"gpu_timed_dispatch_compute": 6}),
    ("buffer_data_", {"gpu_timed_buffer_data": 8}),
    ("tex_image_2d_", {"gpu_timed_tex_image_2d": 12}),
    ("tex_sub_image_2d_", {"gpu_timed_tex_sub_image_2d": 5}),
    ("read_pixels_", {"gpu_timed_read_pixels": 2,          # the two GL probes
                      "gpu_timed_resolve_read_pixels": 2}),  # color + zeta
]

print("GPU-timed wrapper coverage")
for raw, wrappers in WRAPPED:
    wrapper_bodies = [body(gl_src, w, GL_CPP) for w in wrappers]
    # The raw pointer may be CALLED only from inside its wrapper bodies. Its
    # declaration (`GlDrawArrays draw_arrays_ = nullptr;`) and its load
    # (`load_gl(draw_arrays_, ...)`) are not calls and are excluded by requiring
    # the "(" that follows the name.
    raw_calls = lines_matching(r"(?<![\w])" + re.escape(raw) + r"\(")
    inside = sum(b.count(raw + "(") for b in wrapper_bodies)
    check("%s is called only from %s" % (raw, "/".join(wrappers)),
          len(raw_calls) == inside and inside == len(wrappers),
          "%d raw call(s) in the file, %d inside the wrapper(s) at lines %s"
          % (len(raw_calls), inside, [n for n, _ in raw_calls]))
    for wrapper, want_calls in wrappers.items():
        sites = lines_matching(r"(?<![\w])" + re.escape(wrapper) + r"\(")
        # Subtract the wrapper definition itself from the site count.
        site_count = len(sites) - 1
        check("%s has %d timed call site(s)" % (wrapper, want_calls),
              site_count == want_calls,
              "found %d, expected %d (lines %s)"
              % (site_count, want_calls, [n for n, _ in sites]))

for raw, wrappers in WRAPPED:
    for wrapper in wrappers:
        wrapper_body = body(gl_src, wrapper, GL_CPP)
        check("%s opens a GL_TIME_ELAPSED region" % wrapper,
              "ScopedGpuTimer" in wrapper_body,
              "the wrapper must bracket the call with a timer query")

# ── 2. Bucket routing ───────────────────────────────────────────────────────
#
# A wrapper pointed at the wrong bucket would keep the total right and make the
# attribution wrong, which is the harder failure to see.
BUCKETS = [
    ("gpu_timed_draw_arrays", "GpuTimerBucket::kDraw"),
    ("gpu_timed_dispatch_compute", "GpuTimerBucket::kDraw"),
    ("gpu_timed_buffer_data", "GpuTimerBucket::kUpload"),
    ("gpu_timed_tex_image_2d", "GpuTimerBucket::kUpload"),
    ("gpu_timed_tex_sub_image_2d", "GpuTimerBucket::kUpload"),
    ("gpu_timed_read_pixels", "GpuTimerBucket::kResolve"),
    ("gpu_timed_resolve_read_pixels", "GpuTimerBucket::kResolve"),
]
print("\nbucket routing")
for wrapper, bucket in BUCKETS:
    check("%s targets %s" % (wrapper, bucket.split("::")[-1]),
          bucket in body(gl_src, wrapper, GL_CPP),
          "wrong bucket keeps the sum right and the attribution wrong")

# ── 3. The readback fence split ─────────────────────────────────────────────
print("\nreadback fence split")
resolve_body = body(gl_src, "gpu_timed_resolve_read_pixels", GL_CPP)
check("a fence is inserted before the read",
      "fence_sync_(kGlSyncGpuCommandsComplete" in resolve_body and
      "client_wait_sync_(" in resolve_body and
      resolve_body.index("client_wait_sync_(") < resolve_body.index("read_pixels_("),
      "drain_ns must measure the GPU reaching the fence, before the transfer")
check("the fence is deleted",
      "delete_sync_(sync)" in resolve_body,
      "a leaked GLsync per resolve is 1 308 objects a run")
check("drain and readpixels land in separate buckets",
      "g_gl_gpu.drain_ns" in resolve_body and
      "g_gl_gpu.readpixels_ns" in resolve_body,
      "splitting the 3.06 ms resolve is the whole point of the fence")
check("the readback records bytes_read and dirty_bytes",
      "g_gl_gpu.bytes_read" in resolve_body and
      "g_gl_gpu.dirty_bytes" in resolve_body,
      "the scissored-readback gain must be measurable before it is implemented")
check("the fence is skipped entirely when timing is off",
      resolve_body.lstrip().startswith("if (gpu_timing_active()) {"),
      "a default replay must not pay a glClientWaitSync per resolve")
check("both resolve paths pass a dirty rect",
      gl_src.count("dirty_rect.bytes(4u)") == 2,
      "color and zeta readbacks each carry their tracked dirty area")
check("cpu_copy_ns is timed at both flip/convert sites",
      gl_src.count("perf::ScopedAdd copy_timer(g_gl_gpu.cpu_copy_ns") == 2,
      "the CPU half of a resolve is the third component of renderer_resolve_ns")

# The dirty union is only meaningful if it is cleared whenever the surface is.
for label, clear, reset in (
        ("color", r"(?:\.|->)dirty = false;", r"(?:\.|->)dirty_rect\.reset\(\);"),
        ("zeta", r"(?:\.|->)depth_dirty = false;",
         r"(?:\.|->)depth_dirty_rect\.reset\(\);")):
    clears = len(lines_matching(clear))
    resets = len(lines_matching(reset))
    check("every %s dirty clear also resets the tracked rect" % label,
          clears == resets and clears > 0,
          "%d clear(s) vs %d reset(s): a stale rect overstates dirty_bytes"
          % (clears, resets))

# ── 3a. A readback bracket is CLOSED, not merely opened ─────────────────────
#
# glEndQuery enqueues the end marker; it does not execute it. glReadPixels is
# synchronous, so the begin marker and the read run immediately and the end
# marker then sits in the command buffer until the next submission -- and
# GL_TIME_ELAPSED counts the host idle time in between. Measured before the fix
# (build/earlyz-perf-v1.report.json, 2026-09-04): gpu_resolve_ns 138.480 s over
# 1 308 resolves against readpixels_ns 1.799 s, a factor of 77.
#
# This is the same failure shape as the original audit finding -- a number that
# is printed, parsed and plausible, and measures something other than its label
# -- so it gets a structural assertion rather than a remembered number.
print("\nreadback bracket closure")
FLUSH_CALL = "flush_timed_readback_bracket()"
for wrapper in ("gpu_timed_read_pixels", "gpu_timed_resolve_read_pixels"):
    wrapper_body = body(gl_src, wrapper, GL_CPP)
    # The timed region must be a NESTED scope that closes before the flush: if
    # read_pixels_ and the flush shared the ScopedGpuTimer's scope, the flush
    # would run while the query was still open and close nothing.
    scoped_at = wrapper_body.index("ScopedGpuTimer")
    read_at = wrapper_body.index("read_pixels_(")
    flush_at = wrapper_body.find(FLUSH_CALL)
    check("%s flushes after the timed read" % wrapper,
          flush_at > read_at > scoped_at,
          "glEndQuery only enqueues the end marker; without a flush the query "
          "spans the host's idle time to the next submission")
    # Brace-count from the timer's scope: the flush must sit at wrapper depth,
    # i.e. after the closing brace of the block holding the ScopedGpuTimer.
    depth, closed_at = 0, None
    for i in range(scoped_at, len(wrapper_body)):
        ch = wrapper_body[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth < 0:
                closed_at = i
                break
    check("%s closes the timer scope before flushing" % wrapper,
          closed_at is not None and flush_at > closed_at,
          "the flush must run after ~ScopedGpuTimer has issued glEndQuery")

# Nobody may bracket a read_pixels_ call and skip the flush. Walk EVERY
# ScopedGpuTimer scope in the file, not just the two wrappers, so a third
# readback site written later cannot reintroduce the defect quietly.
timer_scopes_with_read = 0
timer_scopes_missing_flush = []
for match in re.finditer(r"ScopedGpuTimer\s+\w+\s*\(", gl_src):
    # Walk out to the end of the enclosing block.
    depth, i = 0, match.start()
    while i > 0 and gl_src[i] != "{":
        i -= 1
    start = i
    depth, j = 1, i + 1
    while j < len(gl_src) and depth:
        if gl_src[j] == "{":
            depth += 1
        elif gl_src[j] == "}":
            depth -= 1
        j += 1
    scope_src = gl_src[start:j]
    if "read_pixels_(" not in scope_src:
        continue
    timer_scopes_with_read += 1
    # The flush must appear in the enclosing function AFTER this scope ends.
    if FLUSH_CALL not in gl_src[j:j + 400]:
        timer_scopes_missing_flush.append(gl_src[:start].count(chr(10)) + 1)
check("every ScopedGpuTimer scope containing read_pixels_ is followed by a flush",
      timer_scopes_with_read >= 2 and not timer_scopes_missing_flush,
      "%d such scope(s); unflushed at line(s) %s"
      % (timer_scopes_with_read, timer_scopes_missing_flush))
check("the flush helper exists and is gated on the accounting flag",
      body(gl_src, "flush_timed_readback_bracket", GL_CPP).lstrip().startswith(
          "if (!gpu_timing_active()) return;"),
      "a noperf replay must not pay a glFlush per resolve")
check("glFlush is loaded through load_gl, outside the timing conjunction",
      re.search(r'load_gl\(flush_, "glFlush"\)', gl_src) is not None and
      "gpu_timing_available_ =" in gl_src and
      gl_src.index("gpu_timing_available_ =") <
      gl_src.index('load_gl(flush_, "glFlush")'),
      "glFlush is core GL 1.0; its absence must not disable GPU timing")

# ── 4. Collection never stalls the emulation thread ─────────────────────────
print("\nquery ring")
harvest = body(gl_src, "harvest_gpu_queries", GL_CPP)
check("results are read only when available",
      "kGlQueryResultAvailable" in harvest and "if (!available) return;" in harvest,
      "polling GL_QUERY_RESULT unconditionally would block on the GPU")
check("the blocking drain is reachable only through the `block` flag",
      "if (!block) {" in harvest,
      "shutdown may wait; the hot path may not")
check("the only blocking drain is at shutdown",
      gl_src.count("harvest_gpu_queries(/*block=*/true)") == 1 and
      "harvest_gpu_queries(/*block=*/true)" in body(
          gl_src, "~OpenGlNv2aRenderer", GL_CPP),
      "a blocking harvest anywhere else is an arm-then-capture stall")
begin = body(gl_src, "gpu_timer_begin", GL_CPP)
check("a nested GL_TIME_ELAPSED region is counted, not raised",
      "if (gpu_query_active_) {" in begin and
      "nested_skipped" in begin and "return false" in begin,
      "GL allows one such query at a time; glBeginQuery inside another raises "
      "GL_INVALID_OPERATION, which the submit paths' get_error_() checks would "
      "report as a draw failure")
check("a full ring drops the sample instead of stalling",
      "gpu_query_live_ >= kGpuQueryRingSize" in begin and
      "queries_dropped" in begin,
      "the drop must be counted so a short total is visible")
check("the ring is bounded and its size is named once",
      re.search(r"constexpr size_t kGpuQueryRingSize = \d+;", gl_src) is not None and
      "std::array<GpuQuerySlot, kGpuQueryRingSize> gpu_query_ring_" in gl_src,
      "eviction keeps memory bounded (PRINCIPLES section 7)")
check("query objects are deleted before the context goes away",
      "delete_queries_(1, &slot.id)" in body(
          gl_src, "~OpenGlNv2aRenderer", GL_CPP),
      "64 leaked query objects per run")

# ── 5. Inert when accounting is off; carries its own denominators ───────────
print("\ncost when off, and self-description")
check("gpu_timer_begin early-outs on the enable flag",
      begin.lstrip().startswith("if (!gpu_timing_active()) return false;"),
      "a default replay must pay one bool load, not a glBeginQuery")
check("gpu_timing_active() gates on perf::g_enabled",
      "perf::g_enabled" in body(gl_src, "gpu_timing_active", GL_CPP),
      "the flag must be the existing accounting flag, not a private one")
code_lines = [l for l in gl_lines if not l.lstrip().startswith("//")]
check("no ungated clock read reappears in this file",
      not any("steady_clock" in l for l in code_lines),
      "the CPU-side splits must reuse perf::ScopedAdd, which gates on "
      "g_enabled (audit section 6: the one always-on timing site)")
check("availability is reported rather than fatal",
      "gpu_timing_available_ =" in gl_src and
      "load_gl(gen_queries_" in gl_src and
      "gpu_timing_available.store(" in gl_src,
      "a driver without ARB_timer_query must still render")
init = body(gl_src, "initialize", GL_CPP)
optional_block = init[init.index("gpu_timing_available_ ="):]
check("a missing timing entry point does not fail initialize()",
      "return false" not in optional_block.split(";")[0],
      "the optional load must not be folded into the required-entry-point check")
for entry in ("glGenQueries", "glDeleteQueries", "glBeginQuery", "glEndQuery",
              "glGetQueryObjectuiv", "glGetQueryObjectui64v", "glFenceSync",
              "glClientWaitSync", "glDeleteSync"):
    check("%s is loaded through load_gl" % entry,
          re.search(r"load_gl\(\w+, \"" + entry + r"\"\)", gl_src) is not None,
          "every entry point uses the file's one SDL_GL_GetProcAddress helper")

# ── 6. One field list, three copies ─────────────────────────────────────────
print("\nfield list agreement")
struct_match = re.search(r"struct GlGpuTimingStats \{(.*?)\n\};", gl_src, re.S)
struct_fields = re.findall(r"std::atomic<uint64_t> (\w+)\{0\};",
                           struct_match.group(1) if struct_match else "")
report_body = body(gl_src, "format_gl_gpu_report", GL_CPP)
format_string = "".join(re.findall(r'"([^"]*)"', report_body))
printed = re.findall(r"(\w+)=%(?:llu|s)", format_string)
# beads-xbox.1.14.6 (2026-09-04): resolve_bracket_ok is DERIVED at print time
# from gpu_resolve_ns and readpixels_ns, so it is deliberately not a struct
# field -- it costs nothing at run time and cannot drift from the two counters it
# compares. It is the only such field; anything else appearing between `scope`
# and the end of the line must be backed by a counter.
COMPUTED_FIELDS = ["resolve_bracket_ok"]
check("the struct declares the 14 documented counters",
      len(struct_fields) == 14, "found %d: %s" % (len(struct_fields), struct_fields))
check("the printed field list IS the struct field list plus the computed checks",
      printed[:1] == ["scope"] and printed[1:] == struct_fields + COMPUTED_FIELDS,
      "printed=%s struct=%s computed=%s"
      % (printed, struct_fields, COMPUTED_FIELDS))
check("resolve_bracket_ok is computed from the two counters it compares",
      "g_gl_gpu.gpu_resolve_ns.load()" in report_body and
      "g_gl_gpu.readpixels_ns.load()" in report_body and
      "resolve_bracket_ok" in report_body,
      "the self-check must read the printed counters, not a private tally")
check("the acceptance band is the documented 1.5x",
      re.search(r"resolve_cpu_ns \+ resolve_cpu_ns / 2", report_body) is not None,
      "gpu_resolve_ns <= readpixels_ns * 1.5 (integer form, no float in the "
      "shutdown path)")
check("a zero-resolve run is reported ok rather than divided by zero",
      "resolve_cpu_ns == 0" in report_body,
      "readpixels_ns of 0 means no timed resolves; gpu_resolve_ns must be 0 too")
check("the line is tagged [perf-gl-gpu] and carries a scope",
      format_string.startswith("[perf-gl-gpu] scope=%s "),
      "the harness keys families on the bracket tag")

measure_src = read(MEASURE_PY)
regex_match = re.search(r"PERF_GL_GPU_RE = re\.compile\((.*?)\n\)", measure_src, re.S)
parsed_fields = re.findall(r"\(\?P<(\w+)>", regex_match.group(1) if regex_match else "")
check("measure_xbox_replay.py parses the same field list in the same order",
      parsed_fields == printed,
      "parser=%s printed=%s" % (parsed_fields, printed))
check("the computed field is parsed OPTIONALLY so older reports still read",
      r"(?: resolve_bracket_ok=(?P<resolve_bracket_ok>\d+))?$"
      in (regex_match.group(1) if regex_match else ""),
      "a report captured before this build carries the line without the field; "
      "making it mandatory would silently drop the whole gl_gpu family")
check("a missing optional field parses as None, never as 0",
      "value is None" in measure_src and "fields[key] = None" in measure_src,
      "resolve_bracket_ok=0 means the bracket check FAILED; an absent field must "
      "not be readable as a measured failure")
check("the parsed line reaches the report as gl_gpu",
      '"gl_gpu": gl_gpu_samples[-1] if gl_gpu_samples else None' in measure_src,
      "a parsed-but-unreported line is the audit's own finding repeated")
check("the parser keeps scope as a label, not an int",
      "def parse_gl_gpu(" in measure_src and
      'if key == "scope":' in measure_src and
      "fields[key] = value" in measure_src,
      "parse_int_fields would int() the scope and raise")

# ── 7. The report reaches stdout at all ─────────────────────────────────────
print("\nreport emission")
dtor = body(gl_src, "~OpenGlNv2aRenderer", GL_CPP)
check("the line is printed from the shutdown path",
      'format_gl_gpu_report("cumulative")' in dtor,
      "Nv2aRenderer has no report hook and main.cpp formats the perf:: buckets "
      "itself, so the destructor is this family's only print site")
check("the print is gated on perf::g_enabled",
      re.search(r"if \(perf::g_enabled\) \{\s*\n\s*if \(gpu_timing_available_\)",
                dtor) is not None,
      "match the other families: silent unless --perf-accounting")
check("the drain happens before the print",
      dtor.index("harvest_gpu_queries") < dtor.index("format_gl_gpu_report"),
      "queries still in flight would be reported as zero")
check("the context is made current before the drain",
      dtor.index("SDL_GL_MakeCurrent") < dtor.index("harvest_gpu_queries"),
      "harvesting against a foreign context reads nothing")
check("stdout is flushed",
      "std::fflush(stdout)" in dtor,
      "long probes redirect stdout to a file; an unflushed last line is lost")

# ── 8. The other agent's regions in this file are intact ────────────────────
print("\npre-existing instrumentation preserved")
for func in ("submit_generic_combiner", "submit_resident_generic_combiner",
             "upload_resident_color_rows"):
    check("%s still opens perf_excl::kNv2aGl" % func,
          "perf_excl::Scope gl_region(perf_excl::kNv2aGl);" in body(
              gl_src, func, GL_CPP),
          "the exclusive partition loses the GL path without it")
check("all six ScopedGlContextTimer sites survive",
      gl_src.count("perf::ScopedGlContextTimer") == 6,
      "found %d" % gl_src.count("perf::ScopedGlContextTimer"))
check("restore_previous_gl_context still uses the gated timer",
      "perf::ScopedGlContextTimer gl_timer(perf_site);" in body(
          gl_src, "restore_previous_gl_context", GL_CPP),
      "the context-restore path must not go back to a raw clock pair")

print()
if failures:
    print("FAILED: %d assertion(s): %s" % (len(failures), ", ".join(failures)))
    sys.exit(1)
print("GL GPU-timing structural guards: all assertions hold")
sys.exit(0)
