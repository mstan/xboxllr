#!/usr/bin/env python3
"""Symbolize a `[host-sampler]` report from an xlle stdout log.

Reads the `[host-sampler] off=0x... count=...` lines, converts module-relative
offsets to image VAs using the exe's ImageBase (objdump -p), and resolves them
through addr2line against the RelWithDebInfo build. Prints an aggregated
function-level hot list sorted by sample count.

Usage:
    py -3 tools\\symbolize_host_profile.py <stdout.log> [path\\to\\xlle.exe]
"""

import argparse
import re
import subprocess
import sys
from collections import defaultdict


def parse_sampler_report(path):
    base = None
    meta = {}
    windows = {}
    current_window = None
    line_re = re.compile(
        r"\[host-sampler\] off=0x([0-9a-fA-F]+) count=(\d+)(?: module=(.+))?"
    )
    with open(path, "rb") as handle:
        raw = handle.read()
    if raw.startswith(b"\xff\xfe"):
        text = raw.decode("utf-16")
    elif raw.startswith(b"\xef\xbb\xbf"):
        text = raw.decode("utf-8-sig")
    else:
        text = raw.decode("utf-8", errors="replace")
    for line in text.splitlines():
        if "[host-sampler]" not in line:
            continue
        if "module_base=" in line:
            match = re.search(r"module_base=0x([0-9a-fA-F]+)", line)
            if match:
                base = int(match.group(1), 16)
            for field in ("samples", "period_ms", "overflow"):
                m = re.search(rf"{field}=(\d+)", line)
                if m:
                    meta[field] = int(m.group(1))
            continue
        win_match = re.search(r"\[host-sampler\] window=(\d+) ", line)
        if win_match:
            current_window = int(win_match.group(1))
            windows.setdefault(current_window, {"samples": 0, "offsets": defaultdict(int)})
            sample_match = re.search(r"samples=(\d+)", line)
            if sample_match:
                windows[current_window]["samples"] = int(sample_match.group(1))
            continue
        match = line_re.search(line)
        if match and current_window is not None:
            module = (match.group(3) or "").strip() or "xlle.exe"
            key = (module, int(match.group(1), 16))
            windows[current_window]["offsets"][key] += int(match.group(2))
    return base, meta, windows


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
        chunk = addresses[start : start + batch_size]
        lines = ["set pagination off", "set confirm off"]
        for index, address in enumerate(chunk):
            lines.append(f'printf "==QUERY {index}\\n"')
            lines.append(f"info symbol 0x{address:x}")
        script = "\n".join(lines) + "\n"
        import os
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
            elif stripped.startswith("No symbol") or stripped.startswith("Address"):
                resolved[chunk[current]] = "?"
                current = None
        # Any unresolved entries default to '?'
        for address in chunk:
            functions[address] = resolved.get(address, "?")
    return functions


def demangle_short(name):
    # Collapse verbose templates/scope for readable top lists.
    name = name.replace("xlle::", "").replace("std::__cxx11::", "std::")
    return name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stdout_log")
    parser.add_argument("exe", nargs="?", default="build-release/xlle.exe")
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument(
        "--window",
        type=int,
        action="append",
        help="only report this window index (repeatable; default: all)",
    )
    parser.add_argument(
        "--collapse-anon",
        action="store_true",
        help="merge lambdas/anon-namespace entries into their enclosing function",
    )
    args = parser.parse_args()

    module_base, meta, windows = parse_sampler_report(args.stdout_log)
    if not windows:
        raise SystemExit("no [host-sampler] window lines found")
    image_base = read_image_base(args.exe)

    selected = [w for w in sorted(windows) if not args.window or w in args.window]
    print(f"period_ms={meta.get('period_ms')} image_base=0x{image_base:x} "
          f"module_base=0x{module_base:x} overflow={meta.get('overflow')}")
    for window in selected:
        offsets = windows[window]["offsets"]
        total = sum(offsets.values())
        if not total:
            continue
        main_offsets = {
            off: count
            for (module, off), count in offsets.items()
            if module.lower().startswith("xlle")
        }
        other_modules = defaultdict(int)
        for (module, off), count in offsets.items():
            if not module.lower().startswith("xlle"):
                other_modules[module] += count
        functions = {}
        if main_offsets:
            vAs = {off: image_base + off for off in main_offsets}
            functions = gdb_symbol_functions(args.exe, list(vAs.values()))

        per_function_samples = defaultdict(int)
        for (module, off), count in offsets.items():
            if module.lower().startswith("xlle"):
                name = demangle_short(functions[vAs[off]])
                if args.collapse_anon:
                    name = re.sub(r"::\{lambda\}.*#\d+", "", name)
                    name = re.sub(r"::_M_\w+.*", "", name)
                    name = re.sub(r"\[clone \.\w+\]", "", name).strip()
            else:
                name = f"[{module}]"
            per_function_samples[name] += count

        print(f"== window {window} insns={window * 1500000000}-"
              f"{(window + 1) * 1500000000} samples={total} "
              f"unique_offsets={len(offsets)}")
        ranked = sorted(per_function_samples.items(), key=lambda kv: -kv[1])
        print(f"{'count':>8} {'pct':>6}  function")
        for name, count in ranked[: args.top]:
            pct = 100.0 * count / total
            print(f"{count:>8} {pct:>5.1f}%  {name}")
        if len(ranked) > args.top:
            rest = sum(count for _, count in ranked[args.top:])
            print(f"{rest:>8} {100.0 * rest / total:>5.1f}%  (rest)")


if __name__ == "__main__":
    main()
