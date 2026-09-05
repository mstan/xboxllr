#!/usr/bin/env python3
"""Structural regression checks for the NV097 dispatch table
(beads-xbox.1.15.1 stage (a), src/machine/nv2a.cpp).

WHY A STRUCTURAL TEST
---------------------
Nv2a::dispatch_method used to be 126 top-level `if / else if (method ...)` arms
in source order.  Method 0x1800 (ARRAY_ELEMENT16) is 76.9 % of the 83.3 M
dispatched method words in the canonical run and sat at arm #120, so almost
every dispatched word walked ~120 predicted-not-taken compares before reaching
its body (docs/PERF-AUDIT-2026-09-02.md §5; 17.4 s of 251 s wall at 209 ns/word).

The chain is now a 2048-entry byte table indexed by `method >> 2` feeding one
`switch`.  What that change can silently break is not the timing -- it is the
SEMANTICS OF ORDER.  The chain was first-match-wins; the table has to be, too.
A transposed pair of arms, a range whose end moved by one method, or a case that
lost its `break;` all still compile, still run, and change which handler a
method reaches.  Only the byte-lock would catch that, and only if the affected
method is exercised by the replay.

So each property below is one named assertion:

  * the chain is gone -- no top-level `else if (method ...)` arm survives in
    dispatch_method, including the two small 2D class chains;
  * every arm of the ORIGINAL chain is still present, at the same index, with
    the same method range: ARMS below is the hand-maintained transcription of
    the pre-change chain and is compared against the kNv097Arms catalogue in
    the source, entry by entry, in order;
  * every arm's handler is declared once in Nv097Handler and has exactly one
    `case` label in dispatch_method;
  * no case falls through into the next: each of the 126 case bodies ends with
    `break;`;
  * the arms are a PARTITION of their method sets -- OVERLAPPING_ARMS below is
    the hand-maintained list of pairs of original arms whose predicates could
    match the same method (it is empty; see the note on it), and the test
    recomputes the overlap set from ARMS and requires the two to agree, so a
    future arm that introduces an overlap fails here and has to be documented
    with the precedence argument rather than discovered by a hash mismatch;
  * the unhandled-method behavior is unchanged: a `default:` that only breaks,
    plus a pre-switch guard over exactly the domain the table covers;
  * the hot methods reach their body through the table, not through a compare;
  * the NV2A_DISPATCH perf_excl region still sits at the drain_pull_queue
    funnel, and dispatch_method is still called only from inside it, so the
    restructuring did not move the dispatch bucket's boundary.

Run standalone:
    python tests/test_dispatch_table_guards.py
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
NV2A_CPP = ROOT / "src/machine/nv2a.cpp"

# ── The original chain, transcribed by hand from nv2a.cpp before the change ──
#
# (begin, end) half-open method ranges, in CHAIN ORDER.  An exact-match arm
# `method == 0xNNNN` is (0xNNNN, 0xNNNN + 4); a ranged arm
# `method >= A && method < B` is (A, B).  Order is the semantics: the chain was
# first-match-wins and the table replay is too, so this list must stay in the
# order the arms appeared.
ARMS = [
    (0x0100, 0x0104), (0x0120, 0x0124), (0x0124, 0x0128), (0x0128, 0x012C),
    (0x012C, 0x0130), (0x0130, 0x0134), (0x0184, 0x0188), (0x0188, 0x018C),
    (0x0194, 0x0198), (0x0198, 0x019C), (0x019C, 0x01A0), (0x01A0, 0x01A4),
    (0x0200, 0x0204), (0x0204, 0x0208), (0x0208, 0x020C), (0x020C, 0x0210),
    (0x0210, 0x0214), (0x0214, 0x0218), (0x1D8C, 0x1D90), (0x1D90, 0x1D94),
    (0x1D98, 0x1D9C), (0x1D9C, 0x1DA0), (0x1D94, 0x1D98), (0x0260, 0x0280),
    (0x0288, 0x028C), (0x028C, 0x0290), (0x0290, 0x0294), (0x02C0, 0x02C4),
    (0x02E0, 0x02E4), (0x0300, 0x0304), (0x0304, 0x0308), (0x0308, 0x030C),
    (0x030C, 0x0310), (0x0310, 0x0314), (0x0318, 0x031C), (0x031C, 0x0320),
    (0x0320, 0x0324), (0x0324, 0x0328), (0x032C, 0x0330), (0x0330, 0x0334),
    (0x0334, 0x0338), (0x0338, 0x033C), (0x033C, 0x0340), (0x0340, 0x0344),
    (0x0344, 0x0348), (0x0348, 0x034C), (0x034C, 0x0350), (0x0350, 0x0354),
    (0x0354, 0x0358), (0x0358, 0x035C), (0x035C, 0x0360), (0x0360, 0x0364),
    (0x0364, 0x0368), (0x0368, 0x036C), (0x036C, 0x0370), (0x0370, 0x0374),
    (0x0374, 0x0378), (0x0378, 0x037C), (0x037C, 0x0380), (0x0384, 0x0388),
    (0x0388, 0x038C), (0x038C, 0x0390), (0x0390, 0x0394), (0x0394, 0x0398),
    (0x0398, 0x039C), (0x039C, 0x03A0), (0x03A0, 0x03A4), (0x17BC, 0x17C0),
    (0x17C0, 0x17C4), (0x1D78, 0x1D7C), (0x1E20, 0x1E24), (0x1E24, 0x1E28),
    (0x1E40, 0x1E60), (0x1E60, 0x1E64), (0x1E6C, 0x1E70), (0x1E70, 0x1E74),
    (0x1E74, 0x1E78), (0x1E78, 0x1E7C), (0x1B00, 0x1C00), (0x0294, 0x0298),
    (0x0298, 0x029C), (0x02A4, 0x02A8), (0x0314, 0x0318), (0x0328, 0x032C),
    (0x03A4, 0x03A8), (0x03A8, 0x03B4), (0x03B4, 0x03B8), (0x03B8, 0x03BC),
    (0x03BC, 0x03C0), (0x03C0, 0x0400), (0x0420, 0x0430), (0x0440, 0x0480),
    (0x0480, 0x0580), (0x0580, 0x0680), (0x0680, 0x06C0), (0x06C0, 0x07C0),
    (0x0840, 0x0940), (0x0A10, 0x0A1C), (0x0A20, 0x0A30), (0x0A60, 0x0A80),
    (0x0A80, 0x0AA0), (0x0AA0, 0x0AC0), (0x0AC0, 0x0AE0), (0x0AF0, 0x0B00),
    (0x0B00, 0x0B80), (0x0B80, 0x0C00), (0x1E94, 0x1E98), (0x1E9C, 0x1EA0),
    (0x1EA0, 0x1EA4), (0x1EA4, 0x1EA8), (0x1518, 0x1528), (0x1880, 0x1900),
    (0x1900, 0x1940), (0x1940, 0x1980), (0x1980, 0x1A00), (0x1A00, 0x1B00),
    (0x1720, 0x1760), (0x1760, 0x17A0), (0x17FC, 0x1800), (0x1800, 0x1804),
    (0x1808, 0x180C), (0x1810, 0x1814), (0x1818, 0x181C), (0x01A4, 0x01A8),
    (0x1D6C, 0x1D70), (0x1D70, 0x1D74),
]

# ── Overlapping predicates (the risk this change carries) ───────────────────
#
# Pairs (earlier_arm_index, later_arm_index) of ORIGINAL arms whose predicates
# can both match one method.  For such a pair the chain reached the EARLIER
# body, and the table replay must too.
#
# This list is EMPTY, and that is a finding, not an omission: the 126 arms are
# pairwise disjoint over the 791 methods they accept, so first-match-wins is a
# no-op and the table is a plain partition.  Three pairs sit adjacent enough to
# look like overlaps and are not, because every ranged arm is half-open:
#
#   arm  73 [0x1E40, 0x1E60)  vs arm  74 0x1E60   -- 0x1E60 is past the range
#   arm  86 [0x03A8, 0x03B4)  vs arm  87 0x03B4   -- 0x03B4 is past the range
#   arm 118 [0x1760, 0x17A0)  vs arm 119 0x17FC   -- 0x17FC is past the range
#
# If a future arm does overlap an earlier one, add the pair here together with
# the precedence argument; the assertion below will otherwise fail.
OVERLAPPING_ARMS = []

# Methods whose dispatch cost the canonical run is dominated by (audit §5).
HOT_METHODS = [
    (0x1800, "ARRAY_ELEMENT16, 76.9 % of dispatched words"),
    (0x1808, "ARRAY_ELEMENT32"),
    (0x1810, "DRAW_ARRAYS"),
    (0x1818, "INLINE_ARRAY"),
    (0x17FC, "SET_BEGIN_END"),
]

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
    """Return the brace-matched body of the definition of function `name`."""
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


def handler_name(begin, end):
    """The handler enumerator the source is required to use for an arm."""
    if end == begin + 4:
        return "kNv097M%04X" % begin
    return "kNv097R%04X_%04X" % (begin, end)


src = read(NV2A_CPP)
dispatch = body(src, "Nv2a::dispatch_method", NV2A_CPP)
dispatch_lines = dispatch.splitlines()

# ── 1. The if/else-if chain is gone ─────────────────────────────────────────
print("the method chain is gone")
residual = [l for l in dispatch_lines if re.search(r"else if \(method\b", l)]
check("no `else if (method ...)` arm survives in dispatch_method",
      not residual,
      "%d residual arm(s), first: %r" % (len(residual), residual[0] if residual else ""))
check("dispatch_method dispatches NV097 through the table",
      dispatch.count("switch (kNv097Table.handler[method >> 2]) {") == 1,
      "expected exactly one table-indexed switch")
check("the two 2D class chains are switches as well",
      dispatch.count("switch (method) {") == 2,
      "class 0x62 (context-surfaces-2d) and class 0x9F (image-blit) each keep "
      "one `switch (method)`")

# ── 2. Every original arm survives, in order, with its method range ─────────
print()
print("every arm of the original chain survives")
catalogue = re.findall(r"^    \{(0x[0-9A-F]{4}), (0x[0-9A-F]{4}), (kNv097[A-Z0-9_]+)\},$",
                       src, re.M)
check("kNv097Arms has one entry per original arm",
      len(catalogue) == len(ARMS),
      "source has %d entries, the transcribed chain has %d"
      % (len(catalogue), len(ARMS)))

mismatched = []
for index, (begin, end) in enumerate(ARMS):
    if index >= len(catalogue):
        mismatched.append("arm %d missing" % (index + 1))
        continue
    want = ("0x%04X" % begin, "0x%04X" % end, handler_name(begin, end))
    if catalogue[index] != want:
        mismatched.append("arm %d: source %r, chain %r"
                          % (index + 1, catalogue[index], want))
check("every arm keeps its index, its method range and its handler",
      not mismatched,
      "; ".join(mismatched[:4]))

enum_body = re.search(r"enum Nv097Handler : uint8_t \{(.*?)\n\};", src, re.S)
check("Nv097Handler is a uint8_t enum", enum_body is not None)
enum_names = re.findall(r"^    (kNv097[A-Za-z0-9_]+),", enum_body.group(1), re.M) \
    if enum_body else []
check("the enum starts at kNv097None = 0 (the unhandled-method id)",
      bool(enum_body) and "kNv097None = 0" in enum_body.group(1))
check("the enum declares the arms in chain order",
      enum_names == [handler_name(b, e) for b, e in ARMS],
      "enum order must mirror the chain so the table replay order is visible")
check("the handler ids fit the uint8_t table",
      len(ARMS) + 1 <= 256,
      "%d handlers + kNv097None exceeds a byte" % len(ARMS))

missing_cases = [handler_name(b, e) for b, e in ARMS
                 if dispatch.count("    case %s: {\n" % handler_name(b, e)) != 1]
check("every handler has exactly one case label in dispatch_method",
      not missing_cases,
      "missing or duplicated: %s" % ", ".join(missing_cases[:6]))

# ── 3. No case falls through into the next ─────────────────────────────────
print()
print("no case falls through")
case_starts = [i for i, l in enumerate(dispatch_lines)
               if re.match(r"^    case kNv097", l)]
check("dispatch_method has one case per arm",
      len(case_starts) == len(ARMS),
      "%d case labels for %d arms" % (len(case_starts), len(ARMS)))
no_break = []
for i in case_starts:
    j = i + 1
    depth = 1
    while j < len(dispatch_lines) and depth:
        depth += dispatch_lines[j].count("{") - dispatch_lines[j].count("}")
        if depth == 0:
            break
        j += 1
    # the last statement of the case body must be `break;`
    if dispatch_lines[j - 1].strip() != "break;":
        no_break.append(dispatch_lines[i].strip())
check("every case body ends in `break;`",
      not no_break,
      "falls through: %s" % ", ".join(no_break[:4]))

# ── 4. The arms are a partition; overlaps are documented, not discovered ────
print()
print("the arm predicates are a partition")
owner = {}
computed_overlaps = []
for index, (begin, end) in enumerate(ARMS):
    for method in range(begin, end, 4):
        if method in owner:
            pair = (owner[method] + 1, index + 1)
            if pair not in computed_overlaps:
                computed_overlaps.append(pair)
        else:
            owner[method] = index
check("the documented overlap list matches the arms",
      sorted(computed_overlaps) == sorted(OVERLAPPING_ARMS),
      "undocumented overlapping arm pairs %s -- add them to OVERLAPPING_ARMS "
      "with the precedence argument" % computed_overlaps)
check("the arms accept 791 distinct methods",
      len(owner) == 791,
      "%d methods accepted; the pre-change chain accepted 791" % len(owner))
check("every accepted method is a multiple of four below 0x2000",
      all(m % 4 == 0 and m < 0x2000 for m in owner),
      "the table is indexed by method >> 2 over a 0x2000-byte method space")

# ── 5. Unhandled methods behave exactly as they did ────────────────────────
print()
print("the unhandled-method path is unchanged")
check("the pre-switch guard covers exactly the table's domain",
      "if (method >= kNv097MethodLimit || (method & 3u) != 0) return;" in dispatch,
      "methods at or above 0x2000, and misaligned methods, matched no arm and "
      "were dropped silently")
check("kNv097MethodLimit is the 0x2000 method space",
      "constexpr uint32_t kNv097MethodLimit = 0x2000;" in src)
check("the table is 0x2000 / 4 entries indexed by method >> 2",
      "constexpr size_t kNv097TableSize = kNv097MethodLimit / 4;" in src and
      "uint8_t handler[kNv097TableSize];" in src)
check("an unhandled method still falls out of dispatch_method silently",
      re.search(r"\n    default:\n        break;\n    \}\n\Z", dispatch) is not None,
      "the chain had no tail work after it, so `default:` must only break")
check("the table replay is first-match-wins in arm order",
      "if (table.handler[index] == kNv097None) table.handler[index] = arm.handler;"
      in src,
      "a later arm must never overwrite an earlier arm's claim")
check("the table is built at compile time",
      "constexpr Nv097Table kNv097Table = build_nv097_table();" in src,
      "a runtime initializer would put a guard variable on the hot path")

# ── 6. The hot methods reach their body through the table ──────────────────
print()
print("the hot methods dispatch in O(1)")
for method, why in HOT_METHODS:
    name = handler_name(method, method + 4)
    check("0x%04X (%s) has a table case" % (method, why),
          "    case %s: {\n" % name in dispatch and
          ("    {0x%04X, 0x%04X, %s},\n" % (method, method + 4, name)) in src)
check("the texture-stage register block is one ranged arm",
      (0x1B00, 0x1C00) in ARMS and
      "    case kNv097R1B00_1C00: {\n" in dispatch,
      "texture stage state must not fan out to 64 case labels")
check("the combiner register blocks are ranged arms",
      all(r in ARMS for r in [(0x0A60, 0x0A80), (0x0A80, 0x0AA0),
                              (0x0AA0, 0x0AC0), (0x0AC0, 0x0AE0),
                              (0x1E40, 0x1E60), (0x0260, 0x0280)]))
before_switch = dispatch.split("switch (kNv097Table.handler[method >> 2]) {")[0]
check("nothing compares `method` against a constant before the switch",
      not re.search(r"method == 0x[0-9A-F]{3,4}",
                    "\n".join(l for l in before_switch.splitlines()
                              if not l.lstrip().startswith("//"))
                    .split("if (object_class_[subchannel] != 0x97) return;")[-1]),
      "a compare reintroduced before the switch would put the hot method back "
      "behind a chain")

# ── 7. The dispatch perf bucket did not move ──────────────────────────────
print()
print("the NV2A_DISPATCH region is intact")
drain = body(src, "Nv2a::drain_pull_queue", NV2A_CPP)
check("drain_pull_queue still opens NV2A_DISPATCH",
      "perf_excl::Scope region(perf_excl::kNv2aDispatch);" in drain,
      "the single funnel every queued PGRAPH method passes through")
check("the region is opened before the drain loop",
      drain.index("perf_excl::kNv2aDispatch") < drain.index("while ("),
      "an inner-loop region would re-enter the bucket per method")
check("dispatch_method is still called only from drain_pull_queue",
      len(re.findall(r"[^:\w]dispatch_method\(push\);", src)) == 2 and
      drain.count("dispatch_method(push);") == 2,
      "a call site outside the funnel would leave its time in the parent bucket")
check("the per-call dispatch timer is still gated on perf::g_enabled",
      "if (perf::g_enabled) {" in drain and
      "perf::g_ns_nv2a_dispatch_method" in drain,
      "the ns/word number this bead is measured on comes from this timer")
# Was "dispatch_method opens no perf_excl region at all". Relaxed 2026-09-03 to
# admit EXACTLY ONE: kNv2aRecorder around the per-method bookkeeping at the top
# (methods_.push, the clear-trace deques, method_counts_). The design doc requires
# that split before this bead's A/B can be read -- 209 ns/word is far more than
# ~124 predicted compares can explain, so without it a flat result cannot
# distinguish "the table did nothing" from "the table worked and the recorder is
# the bucket". Everything else in dispatch_method must still inherit the
# drain_pull_queue region.
recorder_scopes = re.findall(r"perf_excl::Scope\s+\w+\(perf_excl::(\w+)\)", dispatch)
check("dispatch_method opens exactly one perf_excl region, the recorder",
      recorder_scopes == ["kNv2aRecorder"],
      "the handler bodies are inside the drain_pull_queue region already; the "
      "only admissible region here is kNv2aRecorder around the recorder block. "
      "found: %r" % (recorder_scopes,))
check("the recorder region closes before the handler switch",
      dispatch.index("end NV2A_RECORDER region")
      < dispatch.index("kNv097Table.handler[method >> 2]"),
      "the recorder bucket must not swallow the table lookup or any handler "
      "body, or it stops being a recorder measurement")
check("the recorder region covers the method_counts_ histogram",
      dispatch.index("++method_counts_[method >> 2]")
      < dispatch.index("end NV2A_RECORDER region"),
      "method_counts_ is recorder work and belongs inside the bucket")

print()
if failures:
    print("FAILED: %d assertion(s): %s" % (len(failures), ", ".join(failures)))
    sys.exit(1)
print("NV097 dispatch-table structural guards: all assertions hold")
sys.exit(0)
