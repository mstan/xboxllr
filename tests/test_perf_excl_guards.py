#!/usr/bin/env python3
"""Structural regression checks for the exclusive-time partition
(beads-xbox.1.14.3, src/machine/perf_excl.hpp).

WHY A STRUCTURAL TEST
---------------------
The value of perf_excl.hpp is a PARTITION: the buckets must be exhaustive over
the run and must not overlap. Neither property is a number anyone can assert --
they are properties of WHERE the regions are placed, and the failure mode is
silent. Delete the region from Nv2a::submit_vertex_batch and xlle still builds,
still runs, still prints a [perf-excl] line; the vertex cost simply reappears
inside NV2A_DISPATCH and nothing goes red. Six months later someone re-derives
"68-78 % of wall is the device path" as an inference, which is the exact hole
this module was built to close (docs/PERF-AUDIT-2026-09-02.md §6).

So each property below is one named assertion:

  * every consumer the survey identified has a region, and it is at the FUNCTION
    DEFINITION (or the single funnel the path passes through) rather than at a
    call site, so a new caller cannot bypass it;
  * the region targets the bucket the design says it targets -- a retarget would
    keep the sum right and make the attribution wrong, the harder bug to see;
  * the nesting stack is only ever driven by RAII, so pushes and pops are
    balanced by construction and an early return cannot leak a region;
  * every declared bucket is opened somewhere, and the printed field list is the
    declared bucket list;
  * --perf-accounting no longer drags NV2A detailed telemetry in with it, which
    is what made an instrumented run execute different hot-loop code from the
    run it was measuring;
  * the two known self-measurement defects stay fixed: one SLICE_OVERHEAD region
    per slice instead of three host_poll_devices scopes, and no ungated
    steady_clock read on the GL context-switch path.

Run standalone, or through ctest once wired:
    add_test(NAME perf_excl_guards COMMAND python
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_perf_excl_guards.py)
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

PERF_EXCL_H = SRC / "machine/perf_excl.hpp"
PERF_ACCOUNTING_H = SRC / "machine/perf_accounting.hpp"
MAIN_CPP = SRC / "app/main.cpp"
MACHINE_CPP = SRC / "machine/machine.cpp"
NV2A_CPP = SRC / "machine/nv2a.cpp"
UNICORN_CPP = SRC / "cpu/unicorn_floor.cpp"
GL_CPP = SRC / "host/nv2a_opengl_renderer.cpp"

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
    """Return the brace-matched body of the definition of function `name`.

    `name` may be qualified (Nv2a::drain_pull_queue) or a bare in-class method
    name (submit_generic_combiner). Only a DEFINITION matches: a call site ends
    in `);`, never in `) {`.
    """
    pattern = (r"[A-Za-z_][\w:<>,*&\s]*?\b" + re.escape(name) +
               r"\s*\([^;{]*?\)\s*(?:const\s*)?(?:override\s*)?(?:noexcept\s*)?\{")
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


# ── 1. Every identified consumer has a region, at its definition/funnel ──────
#
# (file, function, buckets that must be opened inside it). The bucket tokens are
# checked, not just the presence of a Scope, so a refactor cannot silently point
# a region at the wrong bucket.
SITES = [
    (UNICORN_CPP, "UnicornFloor::run_unicorn", ["kTcgExec"]),
    (SRC / "cpu/aot_executor.cpp", "AotExecutor::execute", ["kAotExec", "kAotBody"]),
    (UNICORN_CPP, "UnicornFloor::inject_interrupt", ["kIntrDelivery"]),
    (MACHINE_CPP, "XboxMachine::mmio_read", ["kMmioDispatch", "kOtherDevice"]),
    (MACHINE_CPP, "XboxMachine::mmio_write", ["kMmioDispatch", "kOtherDevice"]),
    (MACHINE_CPP, "XboxMachine::io_in", ["kIoPort"]),
    (MACHINE_CPP, "XboxMachine::io_out", ["kIoPort"]),
    (MACHINE_CPP, "XboxMachine::run", ["kSliceOverhead", "kHostPoll"]),
    (NV2A_CPP, "Nv2a::process_pushbuffer", ["kNv2aPfifo"]),
    (NV2A_CPP, "Nv2a::drain_pull_queue", ["kNv2aDispatch"]),
    (NV2A_CPP, "Nv2a::submit_vertex_batch", ["kNv2aVertex"]),
    (NV2A_CPP, "Nv2a::rasterize_witnessed_batch", ["kNv2aRasterWitnessed"]),
    (NV2A_CPP, "Nv2a::rasterize_generic_combiner_batch",
     ["kNv2aRasterGenericSw", "kNv2aTexture"]),
    (NV2A_CPP, "Nv2a::resolve_renderer_color_overlap", ["kNv2aGlResolve"]),
    (NV2A_CPP, "Nv2a::resolve_renderer_depth_overlap", ["kNv2aGlResolve"]),
    (NV2A_CPP, "Nv2a::clear_surface", ["kNv2aClear"]),
    (NV2A_CPP, "Nv2a::capture_scanout", ["kNv2aScanout"]),
    (GL_CPP, "submit_generic_combiner", ["kNv2aGl"]),
    (GL_CPP, "submit_resident_generic_combiner", ["kNv2aGl"]),
    (GL_CPP, "upload_resident_color_rows", ["kNv2aGl"]),
]

print("exclusive-time region placement")
for path, func, buckets in SITES:
    try:
        text = body(read(path), func, path)
    except AssertionError as exc:
        check("%s:%s region" % (path.name, func), False, str(exc))
        continue
    check("%s:%s opens a region" % (path.name, func),
          "perf_excl::Scope" in text,
          "no perf_excl::Scope/ScopeIf in the function body")
    for bucket in buckets:
        check("%s:%s targets %s" % (path.name, func, bucket),
              ("perf_excl::" + bucket) in text,
              "body does not open perf_excl::%s" % bucket)

# DIAG_CHECKPOINT lives in a lambda, so it is matched on its own shape: the
# region must sit AFTER the early-out, otherwise every 1 kHz host poll pays two
# clock reads for a checkpoint that is not due.
main_src = read(MAIN_CPP)
lam = re.search(r"auto capture_frame_series = \[&\]\(\) \{(.*?)\n            \};",
                main_src, re.S)
check("main.cpp defines capture_frame_series", lam is not None)
if lam:
    text = lam.group(1)
    check("capture_frame_series opens DIAG_CHECKPOINT",
          "perf_excl::Scope" in text and "kDiagCheckpoint" in text,
          "checkpoint cost would land in HOST_POLL")
    early = text.index("return;")
    check("the DIAG_CHECKPOINT region sits after the not-due early-out",
          text.index("kDiagCheckpoint") > early,
          "a checkpoint that is not due must open no region")

# ── 2. The nesting stack is driven only by RAII ─────────────────────────────
#
# enter()/exit_region() are the raw stack primitives. If any call site outside
# the header calls them directly, an early return or an exception can leave a
# region open, and the leaked time silently inflates whichever bucket was
# current. Every site must use Scope/ScopeIf.
raw_callers = []
for path in sorted(SRC.rglob("*.cpp")) + sorted(SRC.rglob("*.hpp")):
    if path.name == "perf_excl.hpp":
        continue
    text = read(path)
    if re.search(r"perf_excl::(enter|exit_region)\s*\(", text):
        raw_callers.append(str(path))
check("no site drives the nesting stack directly", not raw_callers,
      "raw enter()/exit_region() outside the header: %r" % raw_callers)

# ── 3. Bucket table integrity ───────────────────────────────────────────────
header = read(PERF_EXCL_H)

enum_text = re.search(r"enum Bucket\s*:\s*uint8_t\s*\{(.*?)\};", header, re.S)
check("Bucket enum found", enum_text is not None)
buckets = []
if enum_text:
    for token in re.findall(r"^\s*(k[A-Za-z0-9]+)\s*(?:=|,)", enum_text.group(1),
                            re.M):
        if token in ("kBucketCount", "kNone", "kInactive"):
            continue
        if token not in buckets:
            buckets.append(token)
check("bucket list non-empty", len(buckets) >= 15,
      "only %d buckets parsed" % len(buckets))

names = re.search(r"kBucketNames\[kBucketCount\]\s*=\s*\{(.*?)\};", header, re.S)
check("kBucketNames table found", names is not None)
if names and buckets:
    count = len(re.findall(r'"', names.group(1))) // 2
    check("kBucketNames has one name per bucket", count == len(buckets),
          "%d names for %d buckets -- a mismatch shifts every label in the "
          "report" % (count, len(buckets)))

# The printed field list IS the declared bucket list: format_report must walk
# kBucketNames over kBucketCount rather than hand-listing fields, which is how a
# bucket ends up declared, measured, and never reported.
report = body(header, "format_report", PERF_EXCL_H)
check("format_report prints every declared bucket",
      "kBucketNames[i]" in report and "kBucketCount" in report,
      "the report must iterate the declared table, not a hand-written list")
check("format_report emits the bucket_ns/bucket_n field pair",
      '" %s_ns=%llu %s_n=%llu"' in report,
      "every bucket must ship its ns AND its entry count: a sampler without "
      "its denominator is what forced field analysis to guess a ratio "
      "(docs/PERF-METHODOLOGY.md §2 rule 9)")
for field in ("wall_ns=", "unaccounted_ns=", "clock_reads=",
              "overhead_ticks_per_read="):
    check("format_report emits %s" % field, field in report,
          "the instrument must publish its own residual and cost")

# Every declared bucket must be opened somewhere. A bucket nobody opens is a
# hole in the partition wearing a name.
all_src = "\n".join(read(p) for p in
                    (list(SRC.rglob("*.cpp")) + list(SRC.rglob("*.hpp")))
                    if p.name != "perf_excl.hpp")
for bucket in buckets:
    check("%s is opened somewhere" % bucket,
          ("perf_excl::" + bucket) in all_src,
          "declared but never used as a region bucket")

# ── 4. Timing must not change the code being timed ──────────────────────────
telemetry = re.search(r"set_detailed_telemetry\(([^;]*)\);", main_src, re.S)
check("main.cpp calls set_detailed_telemetry", telemetry is not None)
if telemetry:
    check("detailed telemetry is decoupled from --perf-accounting",
          "perf_accounting" not in telemetry.group(1),
          "an instrumented run would again execute different hot-loop code "
          "from the run it is measuring (audit §6)")
    check("detailed telemetry has its own switch",
          "nv2a_detailed_telemetry" in telemetry.group(1),
          "the capability must stay reachable, just not implicitly")
check("--nv2a-detailed-telemetry is parsed",
      '"--nv2a-detailed-telemetry"' in main_src,
      "the decoupled switch must exist or the telemetry becomes unreachable")

check("--perf-accounting arms the exclusive partition",
      re.search(r"perf_excl::g_enabled\s*=\s*perf_accounting", main_src)
      is not None,
      "the partition must ride the existing flag")
init_at = main_src.find("perf_excl::init()")
arm_at = main_src.find("perf_excl::g_enabled = perf_accounting")
check("the clock is calibrated before the partition is armed",
      init_at != -1 and arm_at != -1 and init_at < arm_at,
      "the first measured region must not be the one paying for calibration")

check("the partition is reported per interval and cumulatively",
      '"interval"' in main_src and '"cumulative"' in main_src and
      main_src.count("perf_excl::format_report(") >= 3,
      "one interval site plus both end-of-run sites are required")

# The legacy [perf...] line family must survive verbatim: the comparison tools
# key on it, and removing a field silently turns a regression into a blank.
for legacy in ("[perf] cpu_ns=", "[perf-nv2a-split]", "[perf-renderer]",
               "[perf-gl-context]", "[perf-witnessed-profile]"):
    check("legacy report line %s is intact" % legacy,
          legacy in read(PERF_ACCOUNTING_H),
          "existing tooling parses this line")

# ── 5. The two self-measurement defects stay fixed ──────────────────────────
machine_src = read(MACHINE_CPP)
check("one host_poll_devices scope per slice, not three",
      machine_src.count("perf::g_ns_host_poll_devices") == 1,
      "found %d; three scopes per slice is what made that bucket report "
      "71.7 ns of its own clock reads (audit §6)"
      % machine_src.count("perf::g_ns_host_poll_devices"))
run_body = body(machine_src, "XboxMachine::run", MACHINE_CPP)
check("exactly one SLICE_OVERHEAD region per slice",
      run_body.count("perf_excl::kSliceOverhead") == 1,
      "the slice body must be one region, not several")

gl_src = read(GL_CPP)
code_lines = [l for l in gl_src.splitlines() if not l.lstrip().startswith("//")]
check("the GL context path reads no ungated clock",
      not any("steady_clock" in l for l in code_lines),
      "record_gl_context_site's callers must use perf::ScopedGlContextTimer, "
      "which gates on g_enabled (audit §6: the one always-on timing site)")
check("ScopedGlContextTimer exists and gates on g_enabled",
      "class ScopedGlContextTimer" in read(PERF_ACCOUNTING_H) and
      re.search(r"ScopedGlContextTimer\(size_t site\)[^{]*\{[^}]*active_\(g_enabled\)"
                r"|active_\(g_enabled\)", read(PERF_ACCOUNTING_H)) is not None,
      "the timer must be inert when accounting is off")

# ── 6. Always-cheap when off ────────────────────────────────────────────────
enter_body = body(header, "enter", PERF_EXCL_H)
check("enter() early-outs on the enable flag before any clock read",
      enter_body.lstrip().startswith("if (!g_enabled) return kInactive;"),
      "a default replay must pay one bool load, not a tick read")
check("enter() elides re-entry into the current bucket",
      "g_cur == static_cast<uint8_t>(bucket)" in enter_body,
      "re-entering the current bucket would spend two reads measuring an "
      "interval that is already being measured")
# Compensation moved into a single accrue() helper (2026-09-03 review fix 4), so
# the check is now "every boundary routes through the one place that compensates"
# rather than "each boundary mentions g_overhead_ticks". That is strictly
# stronger: the old form passed on a stray mention, and could not see a boundary
# that credited g_profile.ticks[] directly.
accrue_body = body(header, "accrue", PERF_EXCL_H)
exit_body = body(header, "exit_region", PERF_EXCL_H)
flush_body = body(header, "flush", PERF_EXCL_H)
check("accrue() compensates the measured clock-pair cost",
      "g_overhead_ticks" in accrue_body and "g_profile.ticks[bucket]" in accrue_body,
      "the one accrual helper must subtract the measured clock-pair cost from "
      "the region being closed")
check("every boundary accrues through accrue()",
      "accrue(" in enter_body and "accrue(" in exit_body and "accrue(" in flush_body,
      "enter/exit_region/flush must all credit time through the single "
      "compensated helper; a boundary touching g_profile.ticks[] directly "
      "escapes both the compensation and the plausibility clamp")
check("no boundary credits g_profile.ticks[] directly",
      all("g_profile.ticks[" not in b for b in (enter_body, exit_body, flush_body)),
      "only accrue() may write the tick accumulators")

# Plausibility clamp (2026-09-03 review fix 4). delta is unsigned; a backwards
# TSC read wraps to ~2^64 ticks and would poison a bucket for the rest of the run
# while looking like an ordinary large number in the log.
check("accrue() clamps implausible intervals",
      "g_max_plausible_ticks" in accrue_body,
      "an unsigned delta from a backwards TSC read must be discarded, not "
      "credited")
check("discarded intervals are counted, not silently dropped",
      "discarded_deltas" in accrue_body and
      "discarded_deltas" in read(PERF_EXCL_H).split("struct Profile")[1][:2000],
      "a clamp that hides its own rejections turns a clock fault into a quiet "
      "under-count")
check("discarded_deltas is exported on the report line",
      "discarded_deltas=%llu" in report,
      "the clamp's rejection count must be visible in the log, or a misbehaving "
      "clock is indistinguishable from a correct run")

print()
if failures:
    print("FAILED: %d assertion(s): %s" % (len(failures), ", ".join(failures)))
    sys.exit(1)
print("exclusive-time structural guards: all assertions hold")
sys.exit(0)
