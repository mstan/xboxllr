#!/usr/bin/env python3
"""Structural regression checks for the witnessed raster loop specialization
(beads-xbox.1.15.2, docs/reference/graphics/DESIGN-NV2A-SEAM.md stage (b), src/machine/nv2a.cpp).

WHY A STRUCTURAL TEST
---------------------
Stage (b) is a performance change whose whole value is a SHAPE, and every part
of that shape can be undone by an edit that still builds, still runs, and still
produces the identical 22 hashes. The three defects the audit named
(https://github.com/mstan/xboxllr/blob/5faaafad269d671b39737edbebaed66c81b2b9fb/docs/PERF-AUDIT-2026-09-02.md §5) were:

  * ~57 runtime `profile ==` compares per fragment against a value that is
    invariant for the whole batch;
  * a 15-way `++…_fragments_` counter ladder executed once per fragment
    (8.59 G times per canonical run) whose arms are also loop-invariant;
  * a per-written-word FNV-1a fold (`h ^= w; h *= 1099511628211ull;`) that is a
    two-operation loop-carried dependency, i.e. the reason the loop cannot
    vectorize at all.

The fix makes the batch profile a compile-time template argument, hoists the
counter ladder to one add per batch, and puts every per-write telemetry store
behind one batch-latched bool. None of that is visible in any output. Someone
"simplifying" the tag back to a runtime `Profile profile` parameter, or putting
one `++radial_color_fragments_` back inside the loop, or adding a new profile
arm that folds a hash outside the gate, gets a green build and a silent
regression of the single largest exactness-neutral lever identified.

So each property below is one named assertion:

  * the fragment body is a template: the profile arrives as a tag, is recovered
    as a `constexpr` and every test against it is `if constexpr`, so a compare
    against a runtime `profile` cannot survive inside it;
  * the per-fragment counter ladder is gone, and each of the 14 profile fragment
    counters is advanced exactly once per batch, by the same ladder in the same
    arm order, so the totals are unchanged;
  * every FNV fold and every last-written-address store in the witnessed loop is
    inside a hash-gated block, and the gate is latched once per batch rather than
    read per fragment;
  * the specialization covers every profile the fingerprint ladder can select --
    a new profile that forgets its switch arm would silently rasterize nothing;
  * the generic-combiner path (deliberately NOT specialized) keeps its own
    per-write telemetry gate;
  * the two neighbouring structural gates -- the NV097 dispatch table and the
    exclusive-time partition -- still hold, because this edit sits in the same
    file and the same functions.

Run standalone, or through ctest once wired:
    add_test(NAME raster_specialization_guards COMMAND python
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_raster_specialization_guards.py)
"""

from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

NV2A_CPP = SRC / "machine/nv2a.cpp"
NV2A_HPP = SRC / "machine/nv2a.hpp"

FNV_PRIME = "1099511628211ull"

failures = []


def check(name, condition, detail=""):
    if condition:
        print("  ok   %s" % name)
    else:
        print("  FAIL %s: %s" % (name, detail))
        failures.append(name)


def read(path):
    return path.read_text(encoding="utf-8", errors="replace")


def blank_noncode(source):
    """Replace comment and string-literal bodies with spaces, length-preserving.

    Every check below is about CODE. This file's comments name the very
    constructs being asserted absent (`profile ==`, the FNV prime), and a
    fault_() message can contain a brace, so a naive grep would both false-alarm
    and mis-match braces. Blanking in place keeps every offset aligned with the
    original text, so a span found here is a span there.
    """
    out = list(source)
    i, n, state = 0, len(source), "code"
    while i < n:
        c = source[i]
        if state == "code":
            if c == "/" and i + 1 < n and source[i + 1] == "/":
                state = "line"
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == "/" and i + 1 < n and source[i + 1] == "*":
                state = "block"
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == '"':
                state = "str"
            i += 1
            continue
        if state == "line":
            if c == "\n":
                state = "code"
            else:
                out[i] = " "
            i += 1
            continue
        if state == "block":
            if c == "*" and i + 1 < n and source[i + 1] == "/":
                state = "code"
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c != "\n":
                out[i] = " "
            i += 1
            continue
        if state == "str":
            if c == chr(92):
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == '"':
                state = "code"
            else:
                out[i] = " "
            i += 1
            continue
    return "".join(out)


def match_brace(source, open_index):
    """Index just past the `}` matching the `{` at `open_index`."""
    assert source[open_index] == "{", source[open_index - 40:open_index + 40]
    depth, i = 0, open_index
    while i < len(source):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise AssertionError("unbalanced braces from %d" % open_index)


def body(source, name, path):
    """Brace-matched body of the DEFINITION of function `name`."""
    pattern = (r"[A-Za-z_][\w:<>,*&\s]*?\b" + re.escape(name) +
               r"\s*\([^;{]*?\)\s*(?:const\s*)?(?:override\s*)?(?:noexcept\s*)?\{")
    match = re.search(pattern, source, re.S)
    if not match:
        raise AssertionError("missing definition of %s in %s" % (name, path))
    end = match_brace(source, match.end() - 1)
    return source[match.end():end - 1]


def gated_spans(source, opener):
    """[(start, end)] of every `<opener> {` block body in `source`."""
    spans = []
    needle = opener + " {"
    at = source.find(needle)
    while at != -1:
        brace = at + len(needle) - 1
        spans.append((brace, match_brace(source, brace)))
        at = source.find(needle, brace)
    return spans


def inside(spans, index):
    return any(a <= index < b for a, b in spans)


def offsets(source, needle):
    out, at = [], source.find(needle)
    while at != -1:
        out.append(at)
        at = source.find(needle, at + 1)
    return out


cpp = blank_noncode(read(NV2A_CPP))
hpp = read(NV2A_HPP)
main = read(SRC / "app/main.cpp")

# Stage (e) (beads-xbox.1.15.5) split Nv2a::rasterize_witnessed_batch into a
# LATCH half (profile match, subset validation, surface resolution, texture
# snapshots -- emulation-thread work that can fault_) and a pure EXECUTE half,
# Nv2a::execute_witnessed_fragments, which a worker thread may run. Every check
# below is about the fragment loop, so it now reads the execute half; nothing is
# weakened, the region is simply where the loop moved to.
setup_half = body(cpp, "Nv2a::rasterize_witnessed_batch", NV2A_CPP)
witnessed = body(cpp, "Nv2a::execute_witnessed_fragments", NV2A_CPP)
generic = body(cpp, "Nv2a::rasterize_generic_combiner_batch", NV2A_CPP)

# The Profile enumerators, in declaration order, straight from the source so a
# new profile is picked up here instead of quietly escaping every check below.
# The enum moved to file scope with the split -- both halves need the tag -- so
# it is looked up in the translation unit and pinned to be outside any function.
enum_match = re.search(r"^enum class Profile \{(.*?)^\};", cpp, re.S | re.M)
assert enum_match, "nv2a.cpp no longer declares `enum class Profile` at file scope"
assert "enum class Profile" not in witnessed and        "enum class Profile" not in setup_half,     "the profile tag must be shared by the latch and execute halves, not re-declared"
PROFILES = re.findall(r"\bk[A-Za-z0-9]+", enum_match.group(1))
SPECIALIZED = [p for p in PROFILES if p != "kNone"]

# The specialized fragment body: the generic lambda that rasterizes one triangle.
tri_at = witnessed.index("auto rasterize_triangle =")
tri_open = witnessed.index(") {", tri_at) + 2
FRAGMENT_BODY = witnessed[tri_open:match_brace(witnessed, tri_open) - 1]
# Everything in the batch OUTSIDE the specialized body (per-batch code).
BATCH_ONLY = witnessed[:tri_at] + witnessed[match_brace(witnessed, tri_open):]

print("profiles declared: %d (%d specialized + kNone)"
      % (len(PROFILES), len(SPECIALIZED)))
print()

# ── 1. The fragment body is a template, not a runtime profile switch ────────
print("the fragment loop is specialized per profile")

check("the batch profile arrives as a compile-time tag",
      re.search(r"auto rasterize_triangle =\s*\n\s*\[&\]\(auto profile_tag,",
                witnessed) is not None,
      "rasterize_triangle must take a tag type as its first parameter; a plain "
      "`Profile profile` parameter would put the compares back at runtime")

check("the tag is recovered as a constexpr Profile",
      re.search(r"constexpr Profile kProfile\s*=\s*\n?\s*"
                r"static_cast<Profile>\(decltype\(profile_tag\)::value\);",
                FRAGMENT_BODY) is not None,
      "without a constexpr kProfile nothing below folds at compile time")

check("the tex5 family predicate is constexpr too",
      re.search(r"constexpr bool kPitchTex5Profile\s*=", FRAGMENT_BODY)
      is not None,
      "pitch_tex5_profile is a batch invariant derived from the profile; left "
      "as a runtime bool it keeps a branch per fragment in 2 of 15 profiles")

runtime_compares = [m.start() for m in
                    re.finditer(r"(?<![A-Za-z0-9_])profile\s*[=!]=", FRAGMENT_BODY)]
check("no runtime `profile ==` comparison survives in the fragment body",
      not runtime_compares,
      "%d site(s) still compare the runtime `profile`; every one must read "
      "`kProfile` so it folds" % len(runtime_compares))

plain_if = [m.start() for m in
            re.finditer(r"(?<![A-Za-z0-9_])if \(kProfile\b", FRAGMENT_BODY)]
check("every profile-only branch is `if constexpr`",
      not plain_if,
      "%d `if (kProfile ...)` site(s) are plain `if`; the dead arms are then "
      "still compiled into all 15 instantiations" % len(plain_if))

constexpr_sites = len(re.findall(r"if constexpr \(kProfile\b", FRAGMENT_BODY))
constexpr_sites += len(re.findall(r"if constexpr \(kPitchTex5Profile\)",
                                  FRAGMENT_BODY))
check("the fragment body still folds the profile in many places",
      constexpr_sites >= 40,
      "expected >= 40 `if constexpr` profile branches, found %d -- the audit "
      "counted ~57 per-fragment compares" % constexpr_sites)

check("the batch profile is never re-read inside the fragment body",
      not re.search(r"(?<![A-Za-z0-9_])profile(?![A-Za-z0-9_])",
                    FRAGMENT_BODY.replace("profile_tag", "")
                                 .replace("kPitchTex5Profile", "")),
      "a surviving read of the enclosing `profile` is a compare waiting to "
      "come back")

print()

# ── 2. The counter ladder is hoisted out of the fragment path ───────────────
print("the per-fragment counter ladder is hoisted to one add per batch")

LADDER_COUNTERS = [
    "rasterized_fragments_", "radial_color_fragments_",
    "textured_color_fragments_", "pitch_textured_color_fragments_",
    "projected_color_fragments_", "pitch_projected_color_fragments_",
    "pitch_multi_texture_fragments_", "pitch_three_texture_fragments_",
    "pitch_tex5_fragments_", "inline_linear_blit_fragments_",
    "pitch_single_texture_fragments_", "tex7_fan_fragments_",
    "texf_projected_fan_fragments_", "inline_color_fragments_",
]

check("no profile fragment counter is touched inside the fragment body",
      not [c for c in LADDER_COUNTERS if c in FRAGMENT_BODY],
      "still in the loop: %s" %
      ", ".join(c for c in LADDER_COUNTERS if c in FRAGMENT_BODY))

check("the fragment body advances exactly one local tally",
      FRAGMENT_BODY.count("++witnessed_fragment_count;") == 1,
      "expected exactly one `++witnessed_fragment_count;`, found %d -- two "
      "would double-count, none would lose the census entirely"
      % FRAGMENT_BODY.count("++witnessed_fragment_count;"))

check("the tally starts at zero once per batch",
      BATCH_ONLY.count("uint64_t witnessed_fragment_count = 0;") == 1,
      "the accumulator must be per batch, not per triangle and not a member")

bad = [c for c in LADDER_COUNTERS
       if len(re.findall(r"(?<![A-Za-z0-9_])%s \+= witnessed_fragment_count;" % c,
                         BATCH_ONLY)) != 1]
check("each profile fragment counter is advanced exactly once per batch",
      not bad,
      "wrong number of fold sites for: %s" % ", ".join(bad))

# The fold must select the SAME counter from the SAME predicate, in the SAME arm
# order, as the ladder that used to run per fragment. A reordered or remapped arm
# leaves the totals plausible and attributes one profile's fragments to another.
EXPECTED_FOLD = [
    ("Profile::kPitchZ16Depth", "rasterized_fragments_"),
    ("Profile::kSwizzledRadialColor", "radial_color_fragments_"),
    ("Profile::kSwizzledTexturedColor", "textured_color_fragments_"),
    ("Profile::kPitchTexturedColor", "pitch_textured_color_fragments_"),
    ("Profile::kSwizzledProjectedColor", "projected_color_fragments_"),
    ("Profile::kPitchProjectedColor", "pitch_projected_color_fragments_"),
    ("Profile::kPitchMultiTextureColor", "pitch_multi_texture_fragments_"),
    ("Profile::kPitchThreeTextureColor", "pitch_three_texture_fragments_"),
    ("pitch_tex5_profile", "pitch_tex5_fragments_"),
    ("Profile::kInlineLinearBlitColor", "inline_linear_blit_fragments_"),
    ("Profile::kPitchSingleTextureColor", "pitch_single_texture_fragments_"),
    ("Profile::kTex7FanColor", "tex7_fan_fragments_"),
    ("Profile::kTexfProjectedFanColor", "texf_projected_fan_fragments_"),
    ("else", "inline_color_fragments_"),
]
fold_pairs = []
for m in re.finditer(
        r"(?:(?:else )?if \(\s*(?:profile == (Profile::k\w+)|(pitch_tex5_profile))\s*\)"
        r"|else)\s*\n\s*(\w+) \+= witnessed_fragment_count;", BATCH_ONLY):
    fold_pairs.append((m.group(1) or m.group(2) or "else", m.group(3)))
check("the fold ladder keeps the predicate, counter and arm order of the "
      "per-fragment ladder",
      fold_pairs == EXPECTED_FOLD,
      "the ladder changed -- fragments would be attributed to another "
      "profile.\n         got %s" % fold_pairs)

check("the fold runs before the batch can return",
      BATCH_ONLY.index("inline_color_fragments_ += witnessed_fragment_count;")
      < BATCH_ONLY.index("if (raster_failed) return false;"),
      "a raster fault after the fold site would drop the whole batch's tally, "
      "which the per-fragment ladder did not do")

print()

# ── 3. Per-write telemetry sits behind one batch-latched gate ───────────────
print("per-write telemetry is gated (design section 4.3, option O3)")

# Stage (d) (beads-xbox.1.15.4) moved the source of the switch from the live
# member to the latched DrawJob: `profile_hashes_enabled_` is read once, at the
# latch, in Nv2a::latch_draw_job, and the executor reads `job.hash_on`. The
# invariant this guard pins is unchanged -- ONE read per batch into a local, and
# nothing per fragment.
check("the hash switch is latched once per batch",
      BATCH_ONLY.count("const bool hash_on = job.hash_on;") == 1
      and "profile_hashes_enabled_" not in FRAGMENT_BODY
      and "job.hash_on" not in FRAGMENT_BODY,
      "the switch must be read once per batch into a local; reading it per "
      "fragment reintroduces a load the compiler cannot hoist past the writes")

hash_spans = gated_spans(FRAGMENT_BODY, "if (hash_on)")
check("the fragment body has hash-gated blocks at all",
      len(hash_spans) >= 8,
      "expected >= 8 gated telemetry blocks (one per write path), found %d"
      % len(hash_spans))

ungated_fnv = [o for o in offsets(FRAGMENT_BODY, FNV_PRIME)
               if not inside(hash_spans, o)]
check("every FNV fold in the fragment body is hash-gated",
      not ungated_fnv,
      "%d ungated `*= %s` site(s) -- each is a two-cycle loop-carried "
      "dependency on the hottest loop in the emulator"
      % (len(ungated_fnv), FNV_PRIME))

last_addr = [m.start() for m in
             re.finditer(r"last_[a-z0-9_]*raster_address_ =", FRAGMENT_BODY)]
ungated_addr = [o for o in last_addr if not inside(hash_spans, o)]
check("every last-written-address store in the fragment body is hash-gated",
      last_addr and not ungated_addr,
      "%d of %d last-address stores are outside the gate"
      % (len(ungated_addr), len(last_addr)))

write_census = [m.start() for m in
                re.finditer(r"\+\+[a-z0-9_]*(?<!depth_test_passes)_writes_;",
                            FRAGMENT_BODY)]
ungated_writes = [o for o in write_census if not inside(hash_spans, o)]
check("the per-write census is gated with the hash it describes",
      write_census and not ungated_writes,
      "%d of %d write counters are outside the gate; a census that disagrees "
      "with its own hash is worse than no census"
      % (len(ungated_writes), len(write_census)))

check("the flag exists with a setter, and the DEVICE still defaults to ON",
      "void set_profile_hashes_enabled(bool enabled)" in hpp
      and "bool profile_hashes_enabled_ = true;" in hpp,
      "the device keeps the folds until something asks otherwise; the HOST "
      "default moved to OFF on 2026-09-05 (see the next check)")
# 2026-09-05: the folds are an instrument and they force every draw INLINE
# (design section 4.3 option O3), so leaving them on by default would silently
# disable the span raster that is now the default. Both spellings exist so an
# A/B can toggle the instrument on one binary.
check("the HOST defaults the folds off and both spellings exist",
      "static constexpr bool kNv2aProfileHashesDefault = false;" in main
      and "bool nv2a_profile_hashes = kNv2aProfileHashesDefault;" in main
      and '"--no-nv2a-profile-hashes"' in main
      and '"--nv2a-profile-hashes"' in main)

print()

# ── 4. Every selectable profile has an instantiation ────────────────────────
print("the fragment body is instantiated for every profile")

for name in SPECIALIZED:
    arm = re.search(r"case Profile::%s:\s*\n\s*emit_batch_primitives\("
                    r"std::integral_constant<\s*\n?\s*int, "
                    r"static_cast<int>\(Profile::%s\)>\{\}\);" % (name, name),
                    BATCH_ONLY)
    check("%s has a switch arm that instantiates the loop" % name,
          arm is not None,
          "a missing arm rasterizes nothing for that profile")

check("kNone is accounted for explicitly",
      re.search(r"case Profile::kNone:", BATCH_ONLY) is not None,
      "kNone returns through the generic combiner above; the arm documents "
      "that and keeps the switch exhaustive so -Wswitch stays useful")

check("the dispatch switch runs once per batch, not per triangle",
      BATCH_ONLY.count("switch (profile) {") == 1
      and "switch (profile)" not in FRAGMENT_BODY,
      "a switch inside rasterize_triangle would pay the dispatch per triangle "
      "and a switch inside the fragment loop would defeat the whole change")

print()

# ── 5. The generic combiner keeps its own per-write gate ────────────────────
print("the generic combiner path keeps its telemetry gate")

diag_spans = gated_spans(generic, "if (collect_diagnostics)")
gen_fnv = [o for o in offsets(generic, "generic_combiner_output_hash_")
           if not inside(diag_spans, o)]
check("the generic per-write FNV fold is gated on collect_diagnostics",
      not gen_fnv,
      "%d ungated generic-combiner hash site(s)" % len(gen_fnv))

# REVERSED 2026-09-03 after review. The b2 pass had folded the last-address store
# into the collect_diagnostics gate, and this guard pinned that. It was wrong:
# last_generic_combiner_raster_address_ is reported UNCONDITIONALLY, so gating it
# changed the canonical NOPERF replay's report from last=03d20840 to last=0 --
# a silent output regression that no output hash covers, because the field is not
# part of any of the 24 hashes. Restored to the HEAD 93d006d structure and pinned
# in that shape: counter gated, address store bare, FNV fold gated.
gen_addr = [o for o in offsets(generic, "last_generic_combiner_raster_address_ =")
            if not inside(diag_spans, o)]
check("the generic last-written-address store is NOT gated",
      len(gen_addr) == 1,
      "expected exactly 1 ungated store, found %d. This field is reported "
      "unconditionally; gating it on collect_diagnostics zeroes it in every "
      "noperf run" % len(gen_addr))

# The write counter is gated, but as a braceless single statement, so the
# brace-span scan above cannot see it. Match the restored three-line shape
# directly -- it is one site and the exact text is the specification.
restored = re.search(
    r"if \(collect_diagnostics\) \+\+generic_combiner_writes_;\s*"
    r"const uint32_t address =\s*"
    r"color_address \+ static_cast<uint32_t>\(offset\);\s*"
    r"last_generic_combiner_raster_address_ = address;\s*"
    r"if \(collect_diagnostics\) \{",
    generic)
check("the generic write census is gated, the address store between them is not",
      restored is not None,
      "the restored HEAD-93d006d ordering was not found: expected "
      "`if (collect_diagnostics) ++generic_combiner_writes_;` then a bare "
      "`last_generic_combiner_raster_address_ = address;` then the gated fold")

check("exactly one generic write-census site, and it is gated",
      len(offsets(generic, "++generic_combiner_writes_;")) == 1
      and restored is not None,
      "a second, ungated counter site would double-count the census")

check("the generic combiner is NOT specialized",
      "profile_tag" not in generic and "kProfile" not in generic,
      "the generic path decodes the bound combiner at runtime; it has no "
      "profile ladder to fold and templating it would only grow .text")

print()

# ── 6. The neighbouring structural gates still hold ─────────────────────────
print("the neighbouring structural gates still hold")

for script in ("test_dispatch_table_guards.py", "test_perf_excl_guards.py"):
    proc = subprocess.run([sys.executable, str(ROOT / "tests" / script)],
                          capture_output=True, text=True)
    check("%s still passes" % script, proc.returncode == 0,
          (proc.stdout + proc.stderr).strip()[-400:])

print()
if failures:
    print("FAILED: %d assertion(s): %s" % (len(failures), ", ".join(failures)))
    sys.exit(1)
print("witnessed-raster specialization guards: all assertions hold")
sys.exit(0)
