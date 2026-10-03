#!/usr/bin/env python3
"""Symbolize and account a `[host-sampler]` report from an xlle stdout log.

What this tool guarantees (docs/PERF-METHODOLOGY.md section 2 rule 9):

* **The denominator is the window's TRUE sample count**, read from the
  `window=... samples=N` line, never the sum of the lines that happened to be
  printed. The previous version summed the printed offsets and divided by that,
  which inflated every percentage in `build/oxalpha-sampler-12b-v6.stdout.log`
  by about 2.2x (window 7 printed 4866 of 10740 samples = 45.3%).
* **Coverage is always stated.** When the leaf table is complete the tool says
  so; when it is truncated (any log produced before the profiler emitted the
  full table) the missing remainder is printed as its own row so the column
  sums to 100% and the shortfall cannot be overlooked.
* **Function names are only produced against a verified binary.** The profile
  header carries the running image's SHA-256; if the on-disk exe does not hash
  to the same value the tool refuses to symbolize. Logs written before the
  header existed carry no identity at all and are symbolized only under an
  explicit `--allow-binary-mismatch`, with every name flagged as unverified.

Usage:
    py -3 tools\\symbolize_host_profile.py <stdout.log> [path\\to\\xlle.exe]
    py -3 tools\\symbolize_host_profile.py <stdout.log> --no-symbols
    py -3 tools\\symbolize_host_profile.py <stdout.log> --window 7 --top 30

With no exe argument the tool looks for an archived binary under
`build/profiles/<sha256>/` (see tools/archive_profile_binary.py) before falling
back to `build-release/xlle.exe`.
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from collections import defaultdict

JIT_MODULE = "[no-module]"
JIT_LABEL = "[jit-tcg]"
# No xlle.exe image is anywhere near 128 MiB. Logs written before the profiler
# emitted a `module=` field recorded every non-xlle leaf as the low 32 bits of
# the raw RIP under the default module name, so an "xlle" offset at or above
# this bound is a truncated foreign address, not a function in our binary.
LEGACY_OFFSET_LIMIT = 0x08000000

# Header/summary lines -------------------------------------------------------
RE_HEADER = re.compile(r"\[host-sampler\] samples=(\d+) ")
RE_BINARY = re.compile(
    r"\[host-sampler\] binary sha256=(\S+) size=(\d+) path=(.*)$"
)
RE_MODULE = re.compile(
    r"\[host-sampler\] module id=(\d+) base=0x([0-9a-fA-F]+) "
    r"size=0x([0-9a-fA-F]+) name=(.*)$"
)
RE_WINDOW = re.compile(r"\[host-sampler\] window=(\d+) ")
RE_JIT = re.compile(r"\[host-sampler\] jit window=(\d+) ")
RE_ATTRIB = re.compile(r"\[host-sampler\] attribution window=(\d+) ")
RE_COVERAGE = re.compile(r"\[host-sampler\] coverage window=(\d+) ")

# Data lines -----------------------------------------------------------------
RE_LEAF = re.compile(
    r"\[host-sampler\] leaf w=(\d+) m=(\d+) off=0x([0-9a-fA-F]+) n=(\d+)"
)
RE_TOP = re.compile(
    r"\[host-sampler\] top window=(\d+) rank=(\d+) count=(\d+) "
    r"pct=([0-9.]+) off=0x([0-9a-fA-F]+) module=(.*)$"
)
# Legacy (pre-fix) human line; the only leaf data older logs carry.
RE_LEGACY_LEAF = re.compile(
    r"\[host-sampler\] off=0x([0-9a-fA-F]+) count=(\d+)(?: module=(.+))?$"
)
RE_PAIR = re.compile(
    r"\[host-sampler\] pair leaf-module=(\S+) off=0x([0-9a-fA-F]+) "
    r"xlle-caller=0x([0-9a-fA-F]+) count=(\d+)(?: method=(\w+))?"
)


def read_text(path):
    with open(path, "rb") as handle:
        raw = handle.read()
    if raw.startswith(b"\xff\xfe"):
        return raw.decode("utf-16")
    if raw.startswith(b"\xef\xbb\xbf"):
        return raw.decode("utf-8-sig")
    return raw.decode("utf-8", errors="replace")


def scalar_fields(line):
    """All `key=value` pairs on a line, values kept as strings."""
    return dict(re.findall(r"(\w+)=(-?[\w.:\\/\[\]-]*)", line))


def as_int(fields, key, default=0):
    value = fields.get(key)
    if value is None:
        return default
    try:
        return int(value, 16) if value.lower().startswith("0x") else int(value)
    except ValueError:
        return default


def as_float(fields, key, default=0.0):
    try:
        return float(fields.get(key, default))
    except (TypeError, ValueError):
        return default


def new_window():
    return {
        "samples": 0,
        "leaves_outside_xlle": -1,   # -1 = the log predates the counter
        "jit_samples": -1,          # -1 = the log predates the JIT bucket
        "unique_leaves": 0,
        "unique_pairs": 0,
        "span_ms": 0.0,
        "hz": 0.0,
        "leaves": defaultdict(int),   # (module_name, offset) -> count
        "leaves_source": None,        # "full" | "top" | "legacy"
        "coverage": None,
        "attribution": None,
        "pairs": [],
        "instruction_range": None,
        "sampled_progress": None,
        "range_source": "unavailable",
    }


def parse_window_range(fields, index, meta):
    """Keep nominal intervals distinct from actual inclusive sampled extrema."""
    text = fields.get("insns", "")
    match = re.fullmatch(r"(\d+)-(\d+|unbounded)", text)
    if not match:
        if as_int(meta, "window_layout") >= 2:
            raise ValueError("missing or invalid sampler instruction range")
        return None, None, "unavailable"
    begin = int(match.group(1))
    end = None if match.group(2) == "unbounded" else int(match.group(2))
    if as_int(meta, "window_layout") >= 2:
        tail = fields.get("range") == "tail"
        if fields.get("range") not in ("bounded", "tail") or tail != (end is None):
            raise ValueError("inconsistent sampler range kind")
        if end is not None and end <= begin:
            raise ValueError("empty or reversed sampler interval")
        try:
            low, high = int(fields["sampled_min"]), int(fields["sampled_max"])
        except (KeyError, ValueError) as exc:
            raise ValueError("missing or invalid sampled extrema") from exc
        if low < begin or high < low or high > (1 << 64) - 1 or (end is not None and high >= end):
            raise ValueError("sampled progress outside its nominal interval")
        return {"begin": begin, "end_exclusive": end}, {"minimum": low, "maximum": high}, "recorded"
    # Old last buckets silently absorbed every later sample. The printed upper
    # bound cannot be recovered as truth from the old log or its sample count.
    if as_int(meta, "windows") > 0 and index == as_int(meta, "windows") - 1:
        return {"begin": begin, "end_exclusive": None}, None, "legacy-clamped-tail"
    return {"begin": begin, "end_exclusive": end}, None, "legacy-nominal"


def parse_sampler_report(path):
    meta = {}
    binary = {"sha256": None, "size": None, "path": None}
    modules = {0: {"base": 0, "size": 0, "name": JIT_MODULE}}
    windows = {}
    current = None
    for line in read_text(path).splitlines():
        if "[host-sampler]" not in line:
            continue

        if RE_HEADER.search(line):
            meta = scalar_fields(line)
            meta["module_base"] = as_int(meta, "module_base")
            continue

        match = RE_BINARY.search(line)
        if match:
            binary = {
                "sha256": match.group(1),
                "size": int(match.group(2)),
                "path": match.group(3).strip(),
            }
            continue

        match = RE_MODULE.search(line)
        if match:
            modules[int(match.group(1))] = {
                "base": int(match.group(2), 16),
                "size": int(match.group(3), 16),
                "name": match.group(4).strip(),
            }
            continue

        match = RE_WINDOW.search(line)
        if match:
            current = int(match.group(1))
            window = windows.setdefault(current, new_window())
            fields = scalar_fields(line)
            window["samples"] = as_int(fields, "samples")
            # `leaves_outside_xlle` is the corrected name; `no_module_leaves`
            # was the same quantity under a wrong label in older logs.
            window["leaves_outside_xlle"] = as_int(
                fields, "leaves_outside_xlle",
                as_int(fields, "no_module_leaves", -1))
            window["jit_samples"] = as_int(fields, "jit_samples", -1)
            window["unique_leaves"] = as_int(fields, "unique_leaves")
            window["unique_pairs"] = as_int(fields, "unique_pairs")
            window["span_ms"] = as_float(fields, "span_ms")
            window["hz"] = as_float(fields, "hz")
            (window["instruction_range"], window["sampled_progress"],
             window["range_source"]) = parse_window_range(fields, current, meta)
            continue

        match = RE_JIT.search(line)
        if match:
            fields = scalar_fields(line)
            target = windows.setdefault(int(match.group(1)), new_window())
            target["jit"] = {
                "samples": as_int(fields, "samples"),
                "lo": as_int(fields, "lo"),
                "hi": as_int(fields, "hi"),
            }
            continue

        match = RE_ATTRIB.search(line)
        if match:
            fields = scalar_fields(line)
            target = windows.setdefault(int(match.group(1)), new_window())
            target["attribution"] = {
                "pairs": as_int(fields, "pairs"),
                "walk": as_int(fields, "walk"),
                "walk_mean_depth": as_float(fields, "walk_mean_depth"),
                "scan": as_int(fields, "scan"),
                "scan_mean_qwords": as_float(fields, "scan_mean_qwords"),
                "unattributed": as_int(fields, "unattributed"),
            }
            continue

        match = RE_COVERAGE.search(line)
        if match:
            fields = scalar_fields(line)
            target = windows.setdefault(int(match.group(1)), new_window())
            target["coverage"] = {
                "top": as_int(fields, "top"),
                "top_samples": as_int(fields, "top_samples"),
            }
            continue

        match = RE_LEAF.search(line)
        if match:
            index = int(match.group(1))
            target = windows.setdefault(index, new_window())
            module_id = int(match.group(2))
            name = modules.get(module_id, {}).get(
                "name", "[module-%d]" % module_id)
            target["leaves"][(name, int(match.group(3), 16))] += int(
                match.group(4))
            target["leaves_source"] = "full"
            continue

        match = RE_TOP.search(line)
        if match:
            index = int(match.group(1))
            target = windows.setdefault(index, new_window())
            if target["leaves_source"] == "full":
                continue  # the full table already carries this entry
            target["leaves"][
                (match.group(6).strip(), int(match.group(5), 16))
            ] += int(match.group(3))
            target["leaves_source"] = "top"
            continue

        match = RE_PAIR.search(line)
        if match and current is not None:
            windows[current]["pairs"].append({
                "leaf_module": match.group(1),
                "leaf_off": int(match.group(2), 16),
                "caller_rva": int(match.group(3), 16),
                "count": int(match.group(4)),
                "method": match.group(5) or "unknown",
            })
            continue

        match = RE_LEGACY_LEAF.search(line)
        if match:
            if current is None:
                # Oldest format (2026-08-21 first cut): one flat table for the
                # whole run, no per-window lines at all. Synthesise window -1
                # so the denominator is still the header's true sample count.
                current = -1
                target = windows.setdefault(current, new_window())
                target["samples"] = as_int(meta, "samples")
                target["unique_leaves"] = as_int(meta, "buckets")
                target["leaves_outside_xlle"] = -1
            target = windows[current]
            if target["leaves_source"] in ("full", "top"):
                continue
            module = (match.group(3) or "").strip() or "xlle.exe"
            target["leaves"][(module, int(match.group(1), 16))] += int(
                match.group(2))
            target["leaves_source"] = "legacy"
            continue

    return meta, binary, modules, windows


# --- binary identity ---------------------------------------------------------

def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_archived_binary(sha, repo_root):
    if not sha or len(sha) != 64:
        return None
    archive = os.path.join(repo_root, "build", "profiles", sha)
    if not os.path.isdir(archive):
        return None
    for name in sorted(os.listdir(archive)):
        if name.lower().endswith(".exe"):
            return os.path.join(archive, name)
    return None


def resolve_binary(args, binary, repo_root):
    """Returns (exe_path or None, status string, verified bool)."""
    header_sha = binary.get("sha256")
    usable_header_sha = (
        header_sha if header_sha and len(header_sha) == 64 else None
    )

    exe = args.exe
    if exe is None:
        exe = find_archived_binary(usable_header_sha, repo_root)
    if exe is None:
        exe = os.path.join(repo_root, "build-release", "xlle.exe")

    if args.no_symbols:
        return None, "symbolization disabled (--no-symbols)", False
    if not os.path.isfile(exe):
        return None, "exe not found: %s" % exe, False

    disk_sha = sha256_file(exe)
    if usable_header_sha is None:
        status = ("profile carries NO binary identity (log predates the "
                  "provenance header); on-disk %s sha256=%s cannot be matched"
                  % (exe, disk_sha))
        if not args.allow_binary_mismatch:
            return None, status + " -- refusing to symbolize", False
        return exe, status + " -- symbolizing anyway (UNVERIFIED)", False
    if disk_sha == usable_header_sha:
        return exe, "binary VERIFIED: %s sha256=%s" % (exe, disk_sha), True
    status = ("binary MISMATCH: profile sha256=%s, %s sha256=%s"
              % (usable_header_sha, exe, disk_sha))
    if not args.allow_binary_mismatch:
        return None, status + " -- refusing to symbolize", False
    return exe, status + " -- symbolizing anyway (UNVERIFIED)", False


# --- symbolization -----------------------------------------------------------

def read_image_base(exe):
    out = subprocess.run(
        ["objdump", "-p", exe], capture_output=True, text=True, check=True
    ).stdout
    match = re.search(r"ImageBase\s+(?:0x)?([0-9a-fA-F]+)", out)
    if not match:
        raise SystemExit("could not find ImageBase in objdump -p output")
    return int(match.group(1), 16)


def gdb_symbol_functions(exe, addresses):
    """Resolve VAs to symbol strings with gdb (`info symbol`).

    addr2line on MinGW PE binaries fails to map this DWARF reliably, while
    gdb's `info symbol` resolves every sampled offset correctly.
    """
    functions = {}
    batch_size = 400
    for start in range(0, len(addresses), batch_size):
        chunk = addresses[start: start + batch_size]
        lines = ["set pagination off", "set confirm off"]
        for index, address in enumerate(chunk):
            lines.append('printf "==QUERY %d\\n"' % index)
            lines.append("info symbol 0x%x" % address)
        script = "\n".join(lines) + "\n"
        import tempfile

        fd, script_path = tempfile.mkstemp(suffix=".gdb", text=True)
        try:
            with os.fdopen(fd, "w") as handle:
                handle.write(script)
            result = subprocess.run(
                ["gdb", "-batch", "-nx", "-x", script_path, exe],
                capture_output=True,
                text=True,
                check=True,
            ).stdout
        finally:
            os.unlink(script_path)
        current = None
        resolved = {}
        for line in result.splitlines():
            stripped = line.strip()
            if stripped.startswith("==QUERY "):
                current = int(stripped.split()[1])
                continue
            if current is None or not stripped:
                continue
            if "in section" in stripped:
                symbol = stripped.rsplit(" in section", 1)[0].strip()
                # Strip "+N offsets" for function-level aggregation.
                symbol = re.sub(r" \+ \d+$", "", symbol)
                resolved[chunk[current]] = symbol
                current = None
            elif stripped.startswith("No symbol") or stripped.startswith(
                    "Address"):
                resolved[chunk[current]] = "?"
                current = None
        for address in chunk:
            functions[address] = resolved.get(address, "?")
    return functions


def demangle_short(name):
    # Collapse verbose templates/scope for readable top lists.
    return name.replace("xlle::", "").replace("std::__cxx11::", "std::")


def is_xlle(module):
    return module.lower().startswith("xlle")


def label_for(module, offset, functions, image_base, collapse_anon):
    if module == JIT_MODULE:
        return JIT_LABEL
    if not is_xlle(module):
        return "[%s]" % module
    if offset >= LEGACY_OFFSET_LIMIT:
        return "[non-xlle rip low32 0x%08x]" % offset
    if functions is None:
        return "xlle.exe+0x%08x" % offset
    name = demangle_short(functions.get(image_base + offset, "?"))
    if collapse_anon:
        name = re.sub(r"::\{lambda\}.*#\d+", "", name)
        name = re.sub(r"::_M_\w+.*", "", name)
        name = re.sub(r"\[clone \.\w+\]", "", name).strip()
    return name


# --- reporting ---------------------------------------------------------------

def report_window(index, window, functions, image_base, args, out):
    samples = window["samples"]
    if not samples:
        return None
    accounted = sum(window["leaves"].values())
    source = window["leaves_source"] or "none"
    complete = source == "full"

    per_label = defaultdict(int)
    for (module, offset), count in window["leaves"].items():
        per_label[label_for(module, offset, functions, image_base,
                            args.collapse_anon)] += count

    out.append("")
    if index < 0:
        out.append("== whole run (log has no per-window tables)  samples=%d  "
                   "(TRUE denominator)" % samples)
    else:
        bounds = window["instruction_range"]
        interval = ("[%d,%s)" % (bounds["begin"],
                    "unbounded" if bounds["end_exclusive"] is None else bounds["end_exclusive"])
                    if bounds else "unknown")
        out.append("== window %d  nominal_insns=%s  samples=%d  (TRUE denominator)"
                   % (index, interval, samples))
        observed = window["sampled_progress"]
        if observed:
            out.append("   sampled progress (inclusive): %d-%d"
                       % (observed["minimum"], observed["maximum"]))
        else:
            out.append("   sampled progress: unknown; range source=%s" % window["range_source"])
    jit = window.get("jit")
    jit_samples = jit["samples"] if jit else window["jit_samples"]
    jit_text = ("jit_samples=%d (%.2f%%)" % (jit_samples,
                                             100.0 * jit_samples / samples)
                if jit_samples is not None and jit_samples >= 0
                else "jit_samples=n/a (log predates the JIT bucket)")
    outside = window["leaves_outside_xlle"]
    outside_text = ("leaves_outside_xlle=n/a (log predates the counter)"
                    if outside < 0 else
                    "leaves_outside_xlle=%d (%.2f%%)"
                    % (outside, 100.0 * outside / samples))
    out.append("   %s   %s   unique_leaves=%d"
               % (outside_text, jit_text,
                  window["unique_leaves"] or len(window["leaves"])))
    attribution = window.get("attribution")
    if attribution:
        pairs = attribution["pairs"] or 1
        out.append("   blocking-pair attribution: pairs=%d walk=%d (mean depth "
                   "%.2f) scan=%d (mean %.2f qwords) unattributed=%d (%.1f%%)"
                   % (attribution["pairs"], attribution["walk"],
                      attribution["walk_mean_depth"], attribution["scan"],
                      attribution["scan_mean_qwords"],
                      attribution["unattributed"],
                      100.0 * attribution["unattributed"] / pairs))
    elif window["pairs"]:
        pair_total = sum(p["count"] for p in window["pairs"])
        unattributed = sum(p["count"] for p in window["pairs"]
                           if p["caller_rva"] == 0)
        out.append("   blocking-pair attribution: printed pairs=%d of "
                   "leaves_outside_xlle=%s; caller unknown in %d of those "
                   "(%.1f%% of printed). NOTE: only the top %d pairs were "
                   "printed by this build, so this is not the whole population."
                   % (pair_total,
                      "n/a" if outside < 0 else str(outside), unattributed,
                      100.0 * unattributed / (pair_total or 1),
                      len(window["pairs"])))
    if complete:
        out.append("   leaf table: COMPLETE (%d entries, %.2f%% of window)"
                   % (len(window["leaves"]), 100.0 * accounted / samples))
    else:
        out.append("   leaf table: TRUNCATED, source=%s (%d entries cover "
                   "%.2f%% of the window; the remaining %.2f%% was never "
                   "printed by this build of the profiler)"
                   % (source, len(window["leaves"]),
                      100.0 * accounted / samples,
                      100.0 * (samples - accounted) / samples))

    ranked = sorted(per_label.items(), key=lambda kv: -kv[1])
    out.append("   %8s %7s  %s" % ("count", "pct", "function"))
    for name, count in ranked[: args.top]:
        out.append("   %8d %6.2f%%  %s" % (count, 100.0 * count / samples,
                                           name))
    shown = sum(count for _, count in ranked[: args.top])
    if len(ranked) > args.top:
        rest = accounted - shown
        out.append("   %8d %6.2f%%  (remaining %d accounted entries)"
                   % (rest, 100.0 * rest / samples, len(ranked) - args.top))
    if not complete:
        missing = samples - accounted
        out.append("   %8d %6.2f%%  (NOT PRINTED BY THE PROFILER - unknown)"
                   % (missing, 100.0 * missing / samples))

    return {
        "window": index,
        "instruction_range": window["instruction_range"],
        "sampled_progress": window["sampled_progress"],
        "range_source": window["range_source"],
        "samples": samples,
        "accounted": accounted,
        "coverage_pct": 100.0 * accounted / samples,
        "complete": complete,
        "leaves_outside_xlle": None if outside < 0 else outside,
        "jit_samples": None if jit_samples is None or jit_samples < 0
                       else jit_samples,
        "attribution": attribution,
        "ranked": [
            {"name": name, "count": count, "pct": 100.0 * count / samples}
            for name, count in ranked
        ],
    }


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("stdout_log")
    parser.add_argument("exe", nargs="?", default=None)
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument(
        "--window", type=int, action="append",
        help="only report this window index (repeatable; default: all)")
    parser.add_argument(
        "--collapse-anon", action="store_true",
        help="merge lambdas/anon-namespace entries into their enclosing "
             "function")
    parser.add_argument(
        "--no-symbols", action="store_true",
        help="never run gdb; report module+offset only")
    parser.add_argument(
        "--allow-binary-mismatch", action="store_true",
        help="symbolize even though the exe does not hash to the SHA-256 in "
             "the profile header (names will be WRONG if the binary differs)")
    parser.add_argument("--json", help="also write the accounting as JSON")
    args = parser.parse_args()

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    meta, binary, _modules, windows = parse_sampler_report(args.stdout_log)
    if not windows:
        raise SystemExit("no [host-sampler] window lines found")

    exe, status, verified = resolve_binary(args, binary, repo_root)

    out = []
    out.append("profile: %s" % args.stdout_log)
    out.append("  samples=%s requested_period_ms=%s timer=%s"
               % (meta.get("samples", "?"), meta.get("period_ms", "?"),
                  meta.get("timer", "n/a")))
    if "mean_period_ms" in meta:
        out.append("  achieved: elapsed_ms=%s mean_period_ms=%s "
                   "achieved_hz=%s"
                   % (meta.get("elapsed_ms"), meta.get("mean_period_ms"),
                      meta.get("achieved_hz")))
    else:
        out.append("  achieved sample rate: NOT RECORDED by this build "
                   "(the requested period is an assumption, not a "
                   "measurement)")
    out.append("  overflow=%s suspend_failures=%s context_failures=%s"
               % (meta.get("overflow", "?"), meta.get("suspend_failures", "?"),
                  meta.get("context_failures", "?")))
    if binary.get("sha256"):
        out.append("  profile binary: sha256=%s size=%s path=%s"
                   % (binary["sha256"], binary["size"], binary["path"]))
    out.append("  %s" % status)
    if not verified:
        out.append("  !! function-level names in this report are NOT backed by "
                   "a verified binary.")

    image_base = None
    functions = None
    if exe is not None:
        image_base = read_image_base(exe)
        wanted = set()
        for index, window in windows.items():
            if args.window and index not in args.window:
                continue
            for (module, offset) in window["leaves"]:
                if is_xlle(module) and offset < LEGACY_OFFSET_LIMIT:
                    wanted.add(image_base + offset)
        if wanted:
            functions = gdb_symbol_functions(exe, sorted(wanted))

    accounting = []
    for index in sorted(windows):
        if args.window and index not in args.window:
            continue
        entry = report_window(index, windows[index], functions, image_base,
                              args, out)
        if entry:
            accounting.append(entry)

    print("\n".join(out))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump({
                "log": os.path.abspath(args.stdout_log),
                "meta": meta,
                "binary": binary,
                "binary_status": status,
                "binary_verified": verified,
                "windows": accounting,
            }, handle, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
