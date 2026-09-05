#!/usr/bin/env python3
"""Archive the exact binary a `[host-sampler]` profile came from.

docs/PERF-METHODOLOGY.md section 2 rule 9: "A profile is archived with the exact
binary SHA and symbol map it came from." Without this, a profile outlives its
binary and its function names become unverifiable -- exactly what happened to
`build/oxalpha-sampler-12b-v6.stdout.log` (profiled 2026-08-21; neither
`build-release/xlle.exe` nor `build/xlle.exe` is that image any more, so no
function-level claim from that log can be checked).

The profiler stamps the running image's path, size and SHA-256 into the log:

    [host-sampler] binary sha256=<hex> size=<bytes> path=<utf8>

This tool reads that line, verifies the on-disk exe hashes to the same value,
and copies the exe -- plus any sibling `.map` / `.pdb` / `.debug` symbol file
and the profile log itself -- into `build/profiles/<sha256>/`. It is idempotent:
re-running it over the same profile copies nothing new.

`tools/symbolize_host_profile.py` looks in `build/profiles/<sha256>/` for a
binary before falling back to `build-release/xlle.exe`, so archiving a profile
is what makes it symbolizable later.

Usage:
    py -3 tools\\archive_profile_binary.py build\\my-profile.stdout.log
    py -3 tools\\archive_profile_binary.py <log> --exe build-release\\xlle.exe
    py -3 tools\\archive_profile_binary.py <log> --exe <exe> --force
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

SYMBOL_SUFFIXES = (".map", ".pdb", ".debug", ".dbg", ".sym")
LOG_SIBLINGS = (".stderr.log", ".report.json", ".argv.json", ".quiescence.json")


def read_text(path):
    with open(path, "rb") as handle:
        raw = handle.read()
    if raw.startswith(b"\xff\xfe"):
        return raw.decode("utf-16")
    if raw.startswith(b"\xef\xbb\xbf"):
        return raw.decode("utf-8-sig")
    return raw.decode("utf-8", errors="replace")


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_binary_header(log_path):
    """Returns (sha256, size, path) from the profile header, or (None, ...)."""
    pattern = re.compile(
        r"\[host-sampler\] binary sha256=(\S+) size=(\d+) path=(.*)$")
    for line in read_text(log_path).splitlines():
        match = pattern.search(line)
        if match:
            sha = match.group(1)
            return (sha if len(sha) == 64 else None,
                    int(match.group(2)),
                    match.group(3).strip())
    return None, None, None


def candidate_exes(repo_root, recorded_path):
    seen = []
    if recorded_path:
        seen.append(recorded_path)
    seen.append(os.path.join(repo_root, "build-release", "xlle.exe"))
    seen.append(os.path.join(repo_root, "build", "xlle.exe"))
    out = []
    for path in seen:
        if path and os.path.isfile(path) and path not in out:
            out.append(path)
    return out


def symbol_siblings(exe):
    """Symbol files that sit next to the exe and share its stem."""
    directory = os.path.dirname(os.path.abspath(exe))
    stem = os.path.splitext(os.path.basename(exe))[0]
    found = []
    for suffix in SYMBOL_SUFFIXES:
        candidate = os.path.join(directory, stem + suffix)
        if os.path.isfile(candidate):
            found.append(candidate)
    return found


def copy_once(source, dest_dir, results):
    """Copies source into dest_dir unless an identical file is already there."""
    os.makedirs(dest_dir, exist_ok=True)
    dest = os.path.join(dest_dir, os.path.basename(source))
    source_size = os.path.getsize(source)
    if os.path.isfile(dest) and os.path.getsize(dest) == source_size and \
            sha256_file(dest) == sha256_file(source):
        results.append(("already archived", dest, source_size))
        return dest
    shutil.copy2(source, dest)
    results.append(("copied", dest, source_size))
    return dest


def git_head(repo_root):
    try:
        return subprocess.run(["git", "-C", repo_root, "rev-parse", "HEAD"],
                              capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("stdout_log", help="the xlle stdout log holding the "
                                           "[host-sampler] report")
    parser.add_argument("--exe", help="binary to archive (default: the path "
                                      "recorded in the profile header, then "
                                      "build-release/xlle.exe, then "
                                      "build/xlle.exe)")
    parser.add_argument("--root", help="archive root (default: "
                                       "<repo>/build/profiles)")
    parser.add_argument("--no-copy-log", action="store_true",
                        help="archive only the binary and symbols")
    parser.add_argument("--force", action="store_true",
                        help="archive even though the profile carries no "
                             "SHA-256 or the exe does not match it; the "
                             "manifest records the profile as UNVERIFIED")
    args = parser.parse_args()

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if not os.path.isfile(args.stdout_log):
        raise SystemExit("no such log: %s" % args.stdout_log)

    header_sha, header_size, header_path = parse_binary_header(args.stdout_log)
    verified = False

    if args.exe:
        exe = args.exe
        if not os.path.isfile(exe):
            raise SystemExit("no such exe: %s" % exe)
    else:
        exe = None
        for candidate in candidate_exes(repo_root, header_path):
            if header_sha is None:
                exe = candidate
                break
            if sha256_file(candidate) == header_sha:
                exe = candidate
                break
        if exe is None:
            raise SystemExit(
                "no on-disk binary hashes to the profile's sha256=%s; pass "
                "--exe explicitly if you know where that image is"
                % (header_sha or "<absent>"))

    exe_sha = sha256_file(exe)
    if header_sha is None:
        message = ("profile %s carries no binary identity (it predates the "
                   "provenance header), so the archive cannot prove %s is the "
                   "image that produced it" % (args.stdout_log, exe))
        if not args.force:
            raise SystemExit(message + "\nre-run with --force to archive it "
                                       "anyway, marked UNVERIFIED")
        print("WARNING: " + message)
    elif exe_sha != header_sha:
        message = ("MISMATCH: profile sha256=%s but %s hashes to %s"
                   % (header_sha, exe, exe_sha))
        if not args.force:
            raise SystemExit(message + "\nrefusing to archive the wrong "
                                       "binary; pass --force only if you "
                                       "intend to record the mismatch")
        print("WARNING: " + message)
    else:
        verified = True

    root = args.root or os.path.join(repo_root, "build", "profiles")
    dest_dir = os.path.join(root, exe_sha)
    results = []
    copy_once(exe, dest_dir, results)
    for symbol in symbol_siblings(exe):
        copy_once(symbol, dest_dir, results)
    archived_logs = []
    if not args.no_copy_log:
        copy_once(args.stdout_log, dest_dir, results)
        archived_logs.append(os.path.basename(args.stdout_log))
        stem = args.stdout_log
        if stem.endswith(".stdout.log"):
            stem = stem[: -len(".stdout.log")]
        for suffix in LOG_SIBLINGS:
            sibling = stem + suffix
            if os.path.isfile(sibling):
                copy_once(sibling, dest_dir, results)
                archived_logs.append(os.path.basename(sibling))

    manifest_path = os.path.join(dest_dir, "manifest.json")
    manifest = {}
    if os.path.isfile(manifest_path):
        try:
            with open(manifest_path, encoding="utf-8") as handle:
                manifest = json.load(handle)
        except (OSError, ValueError):
            manifest = {}
    manifest.setdefault("exe_sha256", exe_sha)
    manifest.setdefault("exe_name", os.path.basename(exe))
    manifest["exe_size"] = os.path.getsize(exe)
    manifest["exe_source"] = os.path.abspath(exe)
    manifest["symbol_files"] = sorted(
        {os.path.basename(p) for p in symbol_siblings(exe)} |
        set(manifest.get("symbol_files", [])))
    profiles = manifest.setdefault("profiles", [])
    entry = {
        "log": os.path.abspath(args.stdout_log),
        "archived": archived_logs,
        "header_sha256": header_sha,
        "header_size": header_size,
        "header_path": header_path,
        "verified": verified,
        "archived_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "git_head": git_head(repo_root),
    }
    if not any(p.get("log") == entry["log"] for p in profiles):
        profiles.append(entry)
    else:
        for index, existing in enumerate(profiles):
            if existing.get("log") == entry["log"]:
                profiles[index] = entry
                break
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)

    print("archive: %s" % dest_dir)
    print("  exe sha256=%s  verified=%s" % (exe_sha, verified))
    if not manifest["symbol_files"]:
        print("  NOTE: no .map/.pdb sits next to %s. A MinGW map is produced "
              "with -Wl,-Map=<name>.map; without it, symbolization depends "
              "entirely on the exe's own symbol table." % exe)
    for action, path, size in results:
        print("  %-16s %s (%d bytes)" % (action, path, size))
    print("  manifest: %s" % manifest_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
