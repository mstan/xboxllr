#!/usr/bin/env python3
"""Prepare a private, hash-bound native title pilot from pinned Phobos tools.

Only generated CPU code and its register-model header are used at this stage.
The donor's kernel, acknowledgement threads and success stubs are not linked.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from aot_inventory import parse_xbe

DONOR_PIN = "63f18f0334fbd8c00313c4bb565accf00627a4fd"
ROOT = Path(__file__).resolve().parent.parent

def generated_file_times(directory):
    """Keep the actual pre-generation bytes and times of compiler inputs."""
    saved={}
    for path in Path(directory).glob('*'):
        if path.is_file() and path.suffix in ('.c','.h'):
            stat=path.stat()
            saved[path.name]=(hashlib.sha256(path.read_bytes()).digest(),stat.st_atime_ns,stat.st_mtime_ns)
    return saved

def retain_identical_file_times(directory,saved):
    """Preserve incremental compilation only when final bytes are identical."""
    count=0
    for name,(digest,access,modified) in saved.items():
        path=Path(directory)/name
        if path.is_file() and hashlib.sha256(path.read_bytes()).digest()==digest:
            os.utime(path,ns=(access,modified));count+=1
    return count


def validate_seeds(seeds, image, discovery=None):
    roots=set()
    for seed in seeds:
        contract=seed.with_suffix('.contract.json')
        if contract.exists():
            proof=json.loads(contract.read_text())
            if proof['xbe_sha256']!=image['sha256'] or proof['seed_sha256']!=hashlib.sha256(seed.read_bytes()).hexdigest():
                raise ValueError('seed evidence belongs to another image or changed seed file')
        for entry in json.loads(seed.read_text()):
            value=entry['start'] if isinstance(entry,dict) else entry
            roots.add(int(value,0) if isinstance(value,str) else int(value))
    if discovery is not None:
        starts={int(f['start'],16) for f in json.loads(discovery.read_text())}
        if roots-starts:raise ValueError('discovery omits requested seed roots: '+str(sorted(roots-starts)))
    return sorted(roots)


def harden_generated(directory):
    """Replace donor missing-body success with nonreturning unsupported faults."""
    counts = dict(missing_targets=0, failed_translations=0)
    for path in sorted(directory.glob("*.c")):
        code = path.read_text(encoding="utf-8")
        code, count = re.subn(
            r'void (\w+)\(void\) \{ recomp_stub_missing\((0x[0-9A-F]+u)\);[^\n]*\}',
            r'void \1(void) { RECOMP_UNIMPL("undiscovered direct target", \2); }', code)
        counts["missing_targets"] += count
        code, count = re.subn(
            r'/\* FAILED: (\w+) at (0x[0-9A-F]+) \*/\s*'
            r'void \1\(void\) \{ /\* translation failed \*/ \}',
            r'void \1(void) { RECOMP_UNIMPL("translation failed", \2u); }', code)
        counts["failed_translations"] += count
        if "stubs_unresolved" in path.name:
            # Keep only definitions and their include, not the obsolete
            # upstream comments advertising fabricated stack cleanup.
            code = '#include "recomp_funcs.h"\n' + "\n".join(
                line for line in code.splitlines() if line.startswith("void ")) + "\n"
            destination = path.with_name("recomp_unsupported_targets.c")
            destination.write_text(code, encoding="utf-8")
            path.unlink()
        else:
            path.write_text(code, encoding="utf-8")
        if re.search(r"translation failed \*/|recomp_stub_missing\(|/\*[^\n]*unhandled[^\n]*\*/|/\* FPU:", code):
            raise ValueError(f"unhandled donor lowering in {path.name}")
    # Diagnostic rings must be per-thread along with the CPU registers. The
    # donor's shared volatile counters race when bootstrap and workers overlap.
    types = directory / "recomp_types.h"
    header = types.read_text(encoding="utf-8")
    header = re.sub(r"extern volatile (uint(?:32|64)_t g_icall_(?:trace(?:_idx)?|count)\b)",
                    r"extern RECOMP_TLS volatile \1", header)
    types.write_text(header, encoding="utf-8")
    from native_thread_checkpoints import apply
    counts['thread_checkpoints'] = apply(directory)
    return counts


def retire_recovered_entry_units(directory, records):
    """Full regeneration owns these bodies now; avoid duplicate local units."""
    for record in records:
        if not record.get('new_entry'):
            continue
        start=record['start'];old=directory/f'recomp_entry_{start:08x}.c'
        if not old.exists():
            continue
        needle=f'void sub_{start:08X}(void)\n{{'
        count=sum(path.read_text(encoding='utf-8').count(needle)
                  for path in directory.glob('recomp_[0-9]*.c'))
        if count!=1:
            raise ValueError('full regeneration did not replace recovered entry')
        old.unlink()


def prepare(args):
    for key in ('unit_max_functions', 'unit_max_bytes'):
        if getattr(args, key, 0) < 0:
            raise ValueError('generated C unit budgets cannot be negative')
    donor = args.donor.resolve()
    git = "C:/Program Files/Git/cmd/git.exe" if sys.platform == "win32" else "git"
    pin = subprocess.check_output([git, "-C", str(donor), "rev-parse", "HEAD"], text=True).strip()
    if pin != DONOR_PIN:
        raise ValueError(f"expected donor {DONOR_PIN}, got {pin}")
    subprocess.run([git, "-C", str(donor), "diff", "--exit-code", "HEAD", "--",
                    "tools", "templates/runtime", "LICENSE", "LICENSE.upstream-MIT",
                    "NOTICE", "LICENSES"], check=True, stdout=subprocess.DEVNULL)
    image = parse_xbe(args.xbe.read_bytes())
    out = args.output.resolve()
    # Title-derived generated C contains proprietary code; keep it out of Git.
    if "build" not in out.parts:
        raise ValueError("pilot output must be under a private build/ directory")
    seed_roots=validate_seeds(args.seed_functions,image,out/'disasm/functions.json' if args.reuse_discovery else None)
    out.mkdir(parents=True, exist_ok=True)
    notices=out/'gen'/'third_party'/'phobos-xboxrecomp'
    notices.mkdir(parents=True,exist_ok=True)
    notice_paths=[donor/name for name in ('LICENSE','LICENSE.upstream-MIT','NOTICE')]
    notice_paths += [p for p in (donor/'LICENSES').rglob('*') if p.is_file()]
    for path in notice_paths:
        target=notices/path.relative_to(donor);target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(path.read_bytes())
    (notices/'PROVENANCE.txt').write_text('https://github.com/phobos665/xboxrecomp\nRevision '+pin+
        '\nGenerated CPU code/helper reference pilot. Changes: explicit unsupported\nfaults, TLS diagnostics, guest-thread checkpoints, framework-owned x87 CPU-flag\nconditional moves and reviewed ABI bindings.\n')
    identity = out / "identity.aot-private.json"
    if identity.exists() and json.loads(identity.read_text())["sha256"] != image["sha256"]:
        raise ValueError("output belongs to a different XBE; use a separate directory")
    previous_file_times=generated_file_times(out/'gen')
    (out / "gen" / "pilot_image.h").unlink(missing_ok=True)
    (out / "gen" / "pilot_media_binding.c").unlink(missing_ok=True)
    (out / "gen" / "pilot_cache_binding.c").unlink(missing_ok=True)
    (out / "gen" / "pilot_d3d_binding.c").unlink(missing_ok=True)
    (out / "gen" / "pilot_d3d_bindings.h").unlink(missing_ok=True)
    (out / "gen" / "pilot_jump_bindings.h").unlink(missing_ok=True)
    (out / "gen" / "pilot_cpu_binding.c").unlink(missing_ok=True)
    for family in ('input', 'audio', 'startup', 'network'):
        (out / 'gen' / f'pilot_{family}_binding.c').unlink(missing_ok=True)
        (out / 'gen' / f'pilot_{family}_bindings.h').unlink(missing_ok=True)
    (out / 'gen' / 'pilot_poll_wait_units.json').unlink(missing_ok=True)
    identity.write_text(json.dumps(image, indent=2) + "\n")

    def run(name, argv):
        print(name, flush=True)
        with (out / (name + ".log")).open("w", encoding="utf-8") as log:
            subprocess.run([sys.executable, *argv], cwd=donor, stdout=log,
                           stderr=subprocess.STDOUT, check=True)

    xbe = str(args.xbe.resolve())
    # Discovery consumes the parser's metadata. Keep both outputs private and
    # pass the path explicitly; fresh titles need no sidecar beside their XBE.
    analysis_path = out / "analysis.aot-private.json"
    run("parse", ["-m", "tools.xbe_parser", xbe, "--json", str(analysis_path)])
    if not args.reuse_discovery:
        cmd = ["-m", "tools.disasm", xbe, "--analysis-json", str(analysis_path),
               "-o", str(out / "disasm")]
        if args.data_sections:
            cmd += ["--data-sections", args.data_sections]
        for seed in args.seed_functions:
            cmd += ['--seed-functions',str(seed.resolve())]
        run("discover", cmd)
        validate_seeds(args.seed_functions,image,out/'disasm/functions.json')
    extents=out/'function-extents.aot-private.json'
    extent_records=[]
    if extents.exists():
        from native_function_extent import apply_extents
        record=json.loads(extents.read_text())
        if record['sha256']!=image['sha256']: raise ValueError('function extents belong to a different image')
        extent_records=record['extents'];functions_path=out/'disasm/functions.json'
        updated=apply_extents(args.xbe.read_bytes(),image,json.loads(functions_path.read_text()),extent_records)
        functions_path.write_text(json.dumps(updated,indent=2)+'\n',newline='\n')
    x87_report=out/'x87-lowering.aot-private.json'
    run("lift", [str(ROOT/'tools/native_x87_lifting.py'),str(donor),str(x87_report),xbe, "--all", "--split", "250",
                 "--disasm-dir", str(out / "disasm"), "--func-id-dir", str(out / "classification"),
                 "--abi-dir", str(out / "abi"), "-o", str(out / "lift"),
                 "--hle-impl", str(ROOT / "pilots/native-title/runtime.c"),
                 "--gen-dir", str(out / "gen"), "--trace-all-entries"])
    unit_budget = None
    if getattr(args, 'unit_max_bytes', 0) or getattr(args, 'unit_max_functions', 0):
        from native_unit_budget import apply as partition_units
        unit_budget = partition_units(out/'gen',
            max_functions=getattr(args, 'unit_max_functions', 0) or 250,
            max_bytes=getattr(args, 'unit_max_bytes', 0) or 524288)
    retire_recovered_entry_units(out/'gen',extent_records)
    from native_cpu_helpers import install as install_cpu_helpers
    runtime_helpers = install_cpu_helpers(out / 'gen')
    counts = harden_generated(out / "gen")
    counts['runtime_helpers'] = runtime_helpers
    counts['x87_instruction_lowering']=json.loads(x87_report.read_text())
    if unit_budget is not None:
        counts['unit_budget'] = unit_budget
    from native_dispatch import apply as compact_dispatch
    counts['compact_dispatch'] = compact_dispatch(out/'gen')
    from native_interrupt_lowering import apply as lower_interrupts
    counts['interrupt_flags'] = lower_interrupts(out/'gen', args.xbe)
    if getattr(args, 'diagnostic_continuations', False):
        from native_continuation_lowering import apply as lower_continuations
        counts['diagnostic_continuations'] = lower_continuations(out/'gen', args.xbe)
    else:
        # Fresh default generation must not inherit a previous diagnostic guard.
        (out/'gen'/'pilot_continuation_schema.h').unlink(missing_ok=True)
    analysis = json.loads((out / "analysis.aot-private.json").read_text())
    imports = analysis["kernel_imports"]
    header = ["/* Private input identity, regenerated from the owner's XBE. */",
              f'#define PILOT_XBE_SHA256 "{image["sha256"]}"',
              f'#define PILOT_ENTRY 0x{image["entry"]:08x}u',
              'static const struct { unsigned address, ordinal; const char *name; } pilot_imports[] = {']
    header += [f'{{ {i["thunk_addr"]}u, {i["ordinal"]}u, "{i["name"]}" }},' for i in imports]
    header += ["};"]
    (out / "gen" / "pilot_image.h").write_text("\n".join(header) + "\n")
    manifest = dict(donor=pin, sha256=image["sha256"], entry=image["entry"],
                    function_extents=extent_records,
                    data_sections=args.data_sections, seeds=[str(p.resolve()) for p in args.seed_functions], seed_roots=seed_roots, **counts,
                    files={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in sorted((out / "gen").iterdir()) if p.is_file()})
    (out / "generation.aot-private.json").write_text(json.dumps(manifest, indent=2) + "\n")
    profile = getattr(args, 'profile', None)
    if args.media_symbols:
        from native_media_binding import bind
        bind(args.xbe, out, args.media_symbols, profile=profile)
    if args.cache_symbols:
        from native_cache_binding import bind
        bind(args.xbe, out, args.cache_symbols, profile=profile)
    if args.d3d_symbols:
        from native_d3d_binding import bind
        bind(args.xbe, out, args.d3d_symbols, profile=profile)
        from native_binding import has_profile_binding
        if has_profile_binding(image['sha256'], 'display', profile):
            from native_display_binding import bind
            bind(args.xbe, out, args.d3d_symbols, profile=profile)
    if args.jump_symbols:
        from native_jump_binding import bind
        bind(args.xbe, out, args.jump_symbols, profile=profile)
    if args.cpu_symbols:
        from native_cpu_binding import bind
        bind(args.xbe, out, args.cpu_symbols, profile=profile)
    if args.input_symbols:
        from native_input_binding import bind
        bind(args.xbe, out, args.input_symbols, profile=profile)
    if args.audio_symbols:
        from native_audio_binding import bind
        bind(args.xbe, out, args.audio_symbols, profile=profile)
    if getattr(args, 'title_symbols', None):
        from native_title_binding import bind
        bind(args.xbe, out, args.title_symbols, profile=profile)
    if getattr(args, 'startup_symbols', None):
        from native_startup_binding import bind
        bind(args.xbe, out, args.startup_symbols, profile=profile)
    if getattr(args, 'network_symbols', None):
        from native_network_binding import bind
        bind(args.xbe, out, args.network_symbols, profile=profile)
    if getattr(args, 'poll_waits', None):
        from native_poll_waits import apply as bind_poll_waits
        path = out / 'generation.aot-private.json'
        manifest = json.loads(path.read_text())
        contract = json.loads(args.poll_waits.read_text(encoding='utf-8'))
        manifest['poll_waits'] = bind_poll_waits(args.xbe.read_bytes(), image, out/'gen', contract)
        manifest['files'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in sorted((out/'gen').iterdir()) if p.is_file()}
        path.write_text(json.dumps(manifest, indent=2) + '\n')
    from native_gpu_callback_effects import emit as emit_callback_effects
    emit_callback_effects(args.xbe, out)
    retained=retain_identical_file_times(out/'gen',previous_file_times)
    manifest_path=out/'generation.aot-private.json'
    manifest=json.loads(manifest_path.read_text())
    manifest['identical_compiler_input_times_retained']=retained
    manifest_path.write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps({k: v for k, v in manifest.items() if k != "files"}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--donor", type=Path, required=True)
    parser.add_argument("--xbe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--profile", type=Path,
                        help="Validated title/XDK binding profile; exact legacy identity selection is retained")
    parser.add_argument("--data-sections", default="")
    parser.add_argument("--unit-max-functions", type=int, default=0,
                        help="Optional generated C unit function budget; zero preserves donor grouping")
    parser.add_argument("--unit-max-bytes", type=int, default=0,
                        help="Optional generated C unit source-byte budget; functions remain indivisible")
    parser.add_argument("--reuse-discovery", action="store_true")
    parser.add_argument('--diagnostic-continuations', action='store_true',
                        help='Restricted continuation research output; requires a separate diagnostic harness')
    parser.add_argument('--poll-waits', type=Path,
                        help='Reviewed image-bound guest-callback poll contract; native idle is separately selected at build time')
    parser.add_argument("--seed-functions", type=Path, action="append", default=[], help="Measured/recovered indirect roots for this title revision")
    parser.add_argument("--media-symbols", type=Path, help="Bind a reviewed media helper using this XDK symbol inventory")
    parser.add_argument("--cache-symbols", type=Path, help="Bind reviewed utility-drive service using this XDK symbol inventory")
    parser.add_argument("--d3d-symbols", type=Path, help="Bind reviewed graphics family using this XDK symbol inventory")
    parser.add_argument("--jump-symbols", type=Path, help="Qualify original CRT nonlocal jumps against this image inventory")
    parser.add_argument("--cpu-symbols", type=Path, help="Bind the reviewed CPU capability probe")
    parser.add_argument("--input-symbols", type=Path, help="Bind the reviewed whole XAPI input family")
    parser.add_argument("--audio-symbols", type=Path, help="Bind the reviewed whole DirectSound API")
    parser.add_argument("--title-symbols", type=Path, help="Bind source-hashed static title function hooks")
    parser.add_argument("--startup-symbols", type=Path, help="Bind reviewed native kernel startup compatibility")
    parser.add_argument("--network-symbols", type=Path, help="Bind a reviewed offline network-service boundary and unsupported-entry guards")
    prepare(parser.parse_args())
