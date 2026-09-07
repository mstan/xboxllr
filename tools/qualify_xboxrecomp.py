#!/usr/bin/env python3
"""Synthetic qualification for pinned sp00nznet/xboxrecomp AOT reuse.

This tool imports an externally supplied xboxrecomp source tree and asks its
upstream translator to lower authored synthetic IA-32 byte sequences. It writes
all generated C, compiled executables, and logs under --build-dir. It does not
vendor upstream code, game code, private assets, or generated proprietary output
into this repository.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FIXTURE = ROOT / "tests" / "fixtures" / "xboxrecomp_qualification.json"
DEFAULT_COMPILER = Path(r"C:/msys64/mingw64/bin/gcc.exe")
DEFAULT_UNICORN_BUILD = ROOT / "build-release" / "_deps" / "unicorn-build"
DEFAULT_UNICORN_INCLUDE = ROOT / "build-release" / "_deps" / "unicorn-src" / "include"

RESULT_RE = re.compile(
    r"^RESULT (?P<name>\S+) (?P<status>PASS|FAIL) "
    r"got=0x(?P<got>[0-9A-Fa-f]{8}) expect=0x(?P<expect>[0-9A-Fa-f]{8})$"
)
UNICORN_RE = re.compile(
    r"^UNICORN (?P<name>\S+) err=(?P<err>\d+) "
    r"eax=0x(?P<eax>[0-9A-Fa-f]{8}) ebx=0x(?P<ebx>[0-9A-Fa-f]{8}) "
    r"ecx=0x(?P<ecx>[0-9A-Fa-f]{8}) eip=0x(?P<eip>[0-9A-Fa-f]{8}) "
    r"mem1804=0x(?P<mem1804>[0-9A-Fa-f]{8})$"
)


def parse_u32(value: str | int) -> int:
    if isinstance(value, int):
        return value & 0xFFFFFFFF
    return int(value, 0) & 0xFFFFFFFF


def hex32(value: int) -> str:
    return f"0x{value & 0xFFFFFFFF:08x}"


def load_fixture(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as fh:
        fixture = json.load(fh)
    if fixture.get("schema_version") != 1:
        raise ValueError(f"unsupported fixture schema: {fixture.get('schema_version')!r}")
    return fixture


def selected_source_hash(source: Path, required_files: list[str]) -> str:
    h = hashlib.sha256()
    for rel in required_files:
        path = source / rel
        if not path.is_file():
            raise FileNotFoundError(f"required xboxrecomp file missing: {path}")
        data = path.read_bytes()
        h.update(rel.encode("utf-8") + b"\0" + str(len(data)).encode("ascii") + b"\0" + data)
    return h.hexdigest()


def git_head(source: Path) -> str | None:
    git = shutil.which("git")
    if not git:
        return None
    try:
        root_result = subprocess.run(
            [git, "-C", str(source), "rev-parse", "--show-toplevel"],
            text=True,
            capture_output=True,
            check=True,
        )
        # An archived source directory may live under xboxlle's own worktree.
        # Do not report the parent repository revision as the upstream revision.
        reported_root = Path(root_result.stdout.strip()).resolve()
        if os.path.normcase(str(reported_root)) != os.path.normcase(str(source.resolve())):
            return None
        result = subprocess.run(
            [git, "-C", str(source), "rev-parse", "HEAD"],
            text=True,
            capture_output=True,
            check=True,
        )
    except subprocess.CalledProcessError:
        return None
    return result.stdout.strip()


def validate_source_identity(source: Path, fixture: dict[str, Any]) -> dict[str, Any]:
    identity = fixture["source_identity"]
    required_commit = identity["required_commit"].lower()
    required_hash = identity["required_file_sha256"].lower()
    required_files = list(identity["required_files"])
    content_hash = selected_source_hash(source, required_files)
    head = git_head(source)
    git_matches = head is not None and head.lower() == required_commit
    hash_matches = content_hash.lower() == required_hash
    # HEAD does not certify a checkout's current bytes: it may be dirty.
    # Archives and checkouts must both match the actual qualified source files.
    if not hash_matches:
        raise RuntimeError(
            "xboxrecomp source identity mismatch: "
            f"git_head={head!r}, selected_file_sha256={content_hash}; "
            f"expected selected hash {required_hash} from commit {required_commit}"
        )
    return {
        "required_commit": required_commit,
        "git_head": head,
        "selected_file_sha256": content_hash,
        "git_matches": git_matches,
        "hash_matches": hash_matches,
        "required_files": required_files,
    }


def write_synthetic_image(fixture: dict[str, Any], out_dir: Path) -> tuple[Path, Path, Path, Path, Path, bytes]:
    image_spec = fixture["synthetic_image"]
    base = parse_u32(image_spec["base"])
    size = parse_u32(image_spec["size"])
    image = bytearray(b"\x90" * size)
    funcs: list[dict[str, Any]] = []
    labels: list[dict[str, Any]] = []
    identified: list[dict[str, Any]] = []
    used_ranges: list[tuple[int, int, str]] = []

    for fn in image_spec["functions"]:
        start = parse_u32(fn["start"])
        code = bytes.fromhex(fn["code"])
        if not code:
            raise ValueError(f"empty function code: {fn['name']}")
        rel = start - base
        if rel < 0 or rel + len(code) > size:
            raise ValueError(f"function outside synthetic image: {fn['name']}")
        end = start + len(code)
        for a, b, other in used_ranges:
            if start < b and end > a:
                raise ValueError(f"function {fn['name']} overlaps {other}")
        used_ranges.append((start, end, fn["name"]))
        image[rel : rel + len(code)] = code
        funcs.append({"start": hex32(start), "end": hex32(end), "name": fn["name"], "num_instructions": 0})
        labels.append({"address": hex32(start), "name": fn["name"]})
        identified.append({"start": hex32(start), "category": "synthetic", "module": "qualification"})

    data_dir = out_dir / "data"
    data_dir.mkdir(parents=True, exist_ok=True)
    image_path = out_dir / "synthetic_code.bin"
    func_json = data_dir / "functions.json"
    labels_json = data_dir / "labels.json"
    identified_json = data_dir / "identified_functions.json"
    abi_json = data_dir / "abi_functions.json"
    image_path.write_bytes(image)
    func_json.write_text(json.dumps(funcs, indent=2), encoding="utf-8")
    labels_json.write_text(json.dumps(labels, indent=2), encoding="utf-8")
    identified_json.write_text(json.dumps(identified, indent=2), encoding="utf-8")
    abi_json.write_text("[]", encoding="utf-8")
    return image_path, func_json, labels_json, identified_json, abi_json, bytes(image)


def run_upstream_translator(source: Path, fixture: dict[str, Any], out_dir: Path) -> dict[str, Any]:
    sys.path.insert(0, str(source))
    from tools.recomp import config  # type: ignore
    from tools.recomp.config import Section  # type: ignore
    from tools.recomp.translator import BatchTranslator  # type: ignore

    image_path, func_json, labels_json, identified_json, abi_json, _ = write_synthetic_image(fixture, out_dir)
    image_spec = fixture["synthetic_image"]
    base = parse_u32(image_spec["base"])
    size = parse_u32(image_spec["size"])
    config._install([Section(".text", base, size, 0, size, True)], base, base, "xboxlle synthetic qualification")
    gen_dir = out_dir / "gen"
    gen_dir.mkdir(exist_ok=True)
    translator = BatchTranslator(
        str(image_path),
        str(func_json),
        labels_json_path=str(labels_json),
        identified_json_path=str(identified_json),
        abi_json_path=str(abi_json),
        output_dir=str(gen_dir),
        seh_prolog=0xFFFFFFFF,
        seh_epilog=0xFFFFFFFE,
    )
    funcs = translator.get_functions_by_category(categories={"synthetic"})
    return translator.translate_batch(funcs, output_file=str(gen_dir / "generated.c"), verbose=False)


def generated_runtime_c() -> str:
    return r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "recomp_types.h"

uint32_t g_eax, g_ecx, g_edx, g_esp;
uint32_t g_ebx, g_esi, g_edi;
uint32_t g_seh_ebp;
ptrdiff_t g_xbox_mem_offset;
volatile uint32_t g_icall_trace[ICALL_TRACE_SIZE];
volatile uint64_t g_icall_count;
volatile uint32_t g_icall_trace_idx;
static uint8_t qual_mem[0x4000];

typedef void (*recomp_func_t)(void);
void recomp_icall_fail_log(uint32_t va) { (void)va; }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return 0; }
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return 0; }
recomp_func_t recomp_lookup_kernel(uint32_t va) { (void)va; return 0; }

void qual_caller(void);
void qual_branch(void);
void qual_flags_caller(void);
void qual_unsupported_continues(void);
void qual_budget_ignored(void);

static void reset(void) {
    memset(qual_mem, 0, sizeof(qual_mem));
    g_eax = g_ecx = g_edx = g_ebx = g_esi = g_edi = g_seh_ebp = 0;
    g_esp = 0x3000;
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)qual_mem;
}
static uint32_t mem32(uint32_t addr) {
    uint32_t v;
    memcpy(&v, qual_mem + addr, sizeof(v));
    return v;
}
static void record(const char* name, int pass, uint32_t got, uint32_t expect) {
    printf("RESULT %s %s got=0x%08X expect=0x%08X\n", name, pass ? "PASS" : "FAIL", got, expect);
}
int main(void) {
    reset(); qual_caller();
    record("direct_call_eax", g_eax == 10, g_eax, 10);
    record("direct_call_mem", mem32(0x1800) == 5, mem32(0x1800), 5);
    record("direct_call_top_ret_esp", g_esp == 0x3004, g_esp, 0x3004);

    reset(); g_eax = 5; qual_branch();
    record("branch_taken", g_ebx == 2, g_ebx, 2);
    reset(); g_eax = 4; qual_branch();
    record("branch_not_taken", g_ebx == 1, g_ebx, 1);

    reset(); g_eax = 7; g_ebx = 7; qual_flags_caller();
    record("cross_function_flags", g_ecx == 2, g_ecx, 2);

    reset(); qual_unsupported_continues();
    record("unsupported_should_stop_eax", g_eax == 0x11111111u, g_eax, 0x11111111u);
    record("unsupported_should_not_write_after", mem32(0x1804) == 0, mem32(0x1804), 0);

    reset(); g_eax = 0; qual_budget_ignored();
    record("budget_one_expected_eax", g_eax == 1, g_eax, 1);
    return 0;
}
'''


def compile_generated(source: Path, out_dir: Path, compiler: Path) -> dict[str, Any]:
    runtime_c = out_dir / "qual_runtime.c"
    generated_c = out_dir / "gen" / "generated.c"
    exe = out_dir / "qual_runtime.exe"
    runtime_c.write_text(generated_runtime_c(), encoding="utf-8")
    cmd = [
        str(compiler),
        "-std=c11",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Wno-unused-label",
        "-Wno-unused-variable",
        f"-I{source / 'templates/runtime'}",
        str(runtime_c),
        str(generated_c),
        "-o",
        str(exe),
    ]
    result = subprocess.run(cmd, cwd=str(out_dir), text=True, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(f"generated compile failed\nSTDOUT:\n{result.stdout}\nSTDERR:\n{result.stderr}")
    run = subprocess.run([str(exe)], cwd=str(out_dir), text=True, capture_output=True)
    (out_dir / "qualification_stdout.txt").write_text(run.stdout, encoding="utf-8")
    (out_dir / "qualification_stderr.txt").write_text(run.stderr, encoding="utf-8")
    return {
        "compiler": str(compiler),
        "compile_command": cmd,
        "compile_stdout": result.stdout,
        "compile_stderr": result.stderr,
        "run_returncode": run.returncode,
        "stdout": run.stdout.splitlines(),
        "stderr": run.stderr.splitlines(),
        "exe": str(exe),
    }


def parse_generated_results(lines: list[str], fixture: dict[str, Any]) -> dict[str, Any]:
    observed = {}
    for line in lines:
        match = RESULT_RE.match(line)
        if match:
            observed[match.group("name")] = {
                "pass": match.group("status") == "PASS",
                "got": "0x" + match.group("got").lower(),
                "expected": "0x" + match.group("expect").lower(),
            }
    failures = []
    for check in fixture["generated_checks"]:
        name = check["name"]
        row = observed.get(name)
        if row is None:
            failures.append(f"missing generated check {name}")
            continue
        expected_pass = bool(check["expected_pass"])
        expected_value = hex32(parse_u32(check["expected"]))
        if row["pass"] != expected_pass:
            failures.append(f"{name}: pass={row['pass']} expected_pass={expected_pass}")
        if row["expected"] != expected_value:
            failures.append(f"{name}: embedded expected {row['expected']} fixture {expected_value}")
    return {"observed": observed, "failures": failures}


def c_array(name: str, data: bytes) -> str:
    lines = [f"static const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i : i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines)


def unicorn_compare_c(fixture: dict[str, Any], image: bytes) -> str:
    calls = []
    for case in fixture["unicorn_cases"]:
        calls.append(
            f'    run_case("{case["name"]}", image, sizeof(image), '
            f'{parse_u32(case["start"])}u, {parse_u32(case["end"])}u, '
            f'{int(case["count"])}ull, {parse_u32(case["eax"])}u, {parse_u32(case["ebx"])}u);'
        )
    return f'''
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unicorn/unicorn.h>

uint64_t xlle_guest_tsc_prefix = 0;
uint64_t cpu_get_tsc_x86_64(void* env) {{ (void)env; return xlle_guest_tsc_prefix; }}

{c_array("image", image)}

static void run_case(const char* name, const uint8_t* code, size_t code_size,
    uint32_t start, uint32_t end, uint64_t count, uint32_t eax0, uint32_t ebx0) {{
    uc_engine* uc = NULL;
    uc_err err = uc_open(UC_ARCH_X86, UC_MODE_32, &uc);
    uint32_t eax = 0, ebx = 0, ecx = 0, eip = 0, mem1804 = 0;
    if (err) {{ printf("UNICORN %s open_err=%u\\n", name, (unsigned)err); return; }}
    if ((err = uc_mem_map(uc, 0x1000, 0x2000, UC_PROT_ALL)) != UC_ERR_OK) {{
        printf("UNICORN %s map1_err=%u\\n", name, (unsigned)err); uc_close(uc); return;
    }}
    if ((err = uc_mem_map(uc, 0x3000, 0x1000, UC_PROT_ALL)) != UC_ERR_OK) {{
        printf("UNICORN %s map2_err=%u\\n", name, (unsigned)err); uc_close(uc); return;
    }}
    uc_mem_write(uc, 0x1000, code, code_size);
    uint32_t esp = 0x3800;
    uint32_t ret = end;
    uc_mem_write(uc, esp, &ret, sizeof(ret));
    uc_reg_write(uc, UC_X86_REG_EAX, &eax0);
    uc_reg_write(uc, UC_X86_REG_EBX, &ebx0);
    uc_reg_write(uc, UC_X86_REG_ESP, &esp);
    err = uc_emu_start(uc, start, end, 0, count);
    uc_reg_read(uc, UC_X86_REG_EAX, &eax);
    uc_reg_read(uc, UC_X86_REG_EBX, &ebx);
    uc_reg_read(uc, UC_X86_REG_ECX, &ecx);
    uc_reg_read(uc, UC_X86_REG_EIP, &eip);
    uc_mem_read(uc, 0x1804, &mem1804, 4);
    printf("UNICORN %s err=%u eax=0x%08X ebx=0x%08X ecx=0x%08X eip=0x%08X mem1804=0x%08X\\n",
        name, (unsigned)err, eax, ebx, ecx, eip, mem1804);
    uc_close(uc);
}}

int main(void) {{
{os.linesep.join(calls)}
    return 0;
}}
'''


def compile_unicorn(
    fixture: dict[str, Any],
    out_dir: Path,
    compiler: Path,
    unicorn_include: Path,
    unicorn_build: Path,
    image: bytes,
) -> dict[str, Any]:
    libs = [
        unicorn_build / "libunicorn-static.a",
        unicorn_build / "libx86_64-softmmu.a",
        unicorn_build / "libunicorn-common.a",
    ]
    missing = [str(p) for p in [unicorn_include / "unicorn" / "unicorn.h", *libs] if not p.exists()]
    if missing:
        raise FileNotFoundError("missing Unicorn build inputs: " + ", ".join(missing))
    source_c = out_dir / "unicorn_compare.c"
    exe = out_dir / "unicorn_compare.exe"
    source_c.write_text(unicorn_compare_c(fixture, image), encoding="utf-8")
    cmd = [
        str(compiler),
        "-std=c11",
        "-O2",
        f"-I{unicorn_include}",
        str(source_c),
        *(str(p) for p in libs),
        "-lpthread",
        "-lm",
        "-lkernel32",
        "-luser32",
        "-lgdi32",
        "-lwinspool",
        "-lshell32",
        "-lole32",
        "-loleaut32",
        "-luuid",
        "-lcomdlg32",
        "-ladvapi32",
        "-o",
        str(exe),
    ]
    result = subprocess.run(cmd, cwd=str(out_dir), text=True, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(f"Unicorn compare compile failed\nSTDOUT:\n{result.stdout}\nSTDERR:\n{result.stderr}")
    run = subprocess.run([str(exe)], cwd=str(out_dir), text=True, capture_output=True)
    (out_dir / "unicorn_compare_stdout.txt").write_text(run.stdout, encoding="utf-8")
    (out_dir / "unicorn_compare_stderr.txt").write_text(run.stderr, encoding="utf-8")
    return {
        "compiler": str(compiler),
        "compile_command": cmd,
        "compile_stdout": result.stdout,
        "compile_stderr": result.stderr,
        "run_returncode": run.returncode,
        "stdout": run.stdout.splitlines(),
        "stderr": run.stderr.splitlines(),
        "exe": str(exe),
    }


def parse_unicorn_results(lines: list[str], fixture: dict[str, Any]) -> dict[str, Any]:
    observed: dict[str, dict[str, Any]] = {}
    for line in lines:
        match = UNICORN_RE.match(line)
        if match:
            observed[match.group("name")] = {
                "err": int(match.group("err")),
                "eax": "0x" + match.group("eax").lower(),
                "ebx": "0x" + match.group("ebx").lower(),
                "ecx": "0x" + match.group("ecx").lower(),
                "eip": "0x" + match.group("eip").lower(),
                "mem1804": "0x" + match.group("mem1804").lower(),
            }
    failures = []
    for case in fixture["unicorn_cases"]:
        name = case["name"]
        row = observed.get(name)
        if row is None:
            failures.append(f"missing Unicorn case {name}")
            continue
        for key, expected in case["expect"].items():
            actual = row[key]
            expected_value = int(expected) if key == "err" else hex32(parse_u32(expected))
            if actual != expected_value:
                failures.append(f"{name}.{key}: got {actual!r} expected {expected_value!r}")
    return {"observed": observed, "failures": failures}


def strict_warning_probe(source: Path, out_dir: Path, compiler: Path) -> dict[str, Any]:
    runtime_c = out_dir / "qual_runtime.c"
    generated_c = out_dir / "gen" / "generated.c"
    exe = out_dir / "qual_runtime_strict.exe"
    cmd = [
        str(compiler),
        "-std=c11",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-I{source / 'templates/runtime'}",
        str(runtime_c),
        str(generated_c),
        "-o",
        str(exe),
    ]
    result = subprocess.run(cmd, cwd=str(out_dir), text=True, capture_output=True)
    return {"returncode": result.returncode, "stdout": result.stdout.splitlines(), "stderr": result.stderr.splitlines()}


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Qualify pinned xboxrecomp frontend/backend contracts using synthetic IA-32 input"
    )
    parser.add_argument("--source", required=True, type=Path, help="Externally pinned xboxrecomp source directory")
    parser.add_argument("--build-dir", required=True, type=Path, help="Ignored output directory for generated artifacts")
    parser.add_argument("--fixture", type=Path, default=DEFAULT_FIXTURE)
    parser.add_argument("--compiler", type=Path, default=DEFAULT_COMPILER)
    parser.add_argument("--unicorn-build", type=Path, default=DEFAULT_UNICORN_BUILD)
    parser.add_argument("--unicorn-include", type=Path, default=DEFAULT_UNICORN_INCLUDE)
    parser.add_argument("--skip-unicorn", action="store_true", help="Skip Unicorn C reference build/run")
    args = parser.parse_args()

    fixture = load_fixture(args.fixture)
    source = args.source.resolve()
    build_dir = args.build_dir.resolve()
    build_dir.mkdir(parents=True, exist_ok=True)
    if not args.compiler.exists():
        raise FileNotFoundError(f"compiler not found: {args.compiler}")

    identity = validate_source_identity(source, fixture)
    translation_stats = run_upstream_translator(source, fixture, build_dir)
    generated = compile_generated(source, build_dir, args.compiler)
    generated_results = parse_generated_results(generated["stdout"], fixture)
    _, _, _, _, _, image = write_synthetic_image(fixture, build_dir)
    strict = strict_warning_probe(source, build_dir, args.compiler)

    unicorn = None
    unicorn_results = None
    if not args.skip_unicorn:
        unicorn = compile_unicorn(fixture, build_dir, args.compiler, args.unicorn_include, args.unicorn_build, image)
        unicorn_results = parse_unicorn_results(unicorn["stdout"], fixture)

    summary = {
        "schema_version": 1,
        "python_version": sys.version,
        "capstone_version": importlib.metadata.version("capstone"),
        "source_identity": identity,
        "translation_stats": translation_stats,
        "generated_run": generated,
        "generated_results": generated_results,
        "strict_warning_probe": strict,
        "unicorn_run": unicorn,
        "unicorn_results": unicorn_results,
        "recommendation": "adapt_frontend_ideas_reject_direct_backend_runtime",
    }
    (build_dir / "qualification_summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))

    if generated["run_returncode"] != 0 or (unicorn and unicorn["run_returncode"] != 0):
        print("qualification executable failed", file=sys.stderr)
        return 4
    if generated_results["failures"]:
        print("generated result expectation mismatch", file=sys.stderr)
        return 2
    if unicorn_results and unicorn_results["failures"]:
        print("Unicorn result expectation mismatch", file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
