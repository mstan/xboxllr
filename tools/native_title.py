#!/usr/bin/env python3
"""Build and launch any declared native title through the shared xboxllr runtime.

Title definitions contain identity, selected services and content policy. Private
paths and generated CPU code stay outside the definition and release sources.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import tomllib
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "xboxllr.native-title.v1"
BINDINGS = {name: "native_" + name + "_binding" for name in
            ("media", "cache", "cpu", "jump", "d3d", "input", "audio", "startup", "network")}
INCOMPLETE_BIND = "native-title-bind-incomplete.json"
INCOMPLETE_RECOVERY = "native-title-recovery-incomplete.json"
DEPENDENCIES = {
    "PILOT_D3D_SOURCE": "d3d_source", "PILOT_DSP_SOURCE": "dsp_source",
    "PILOT_DSP_LIBRARY": "dsp_library", "PILOT_ADPCM_SOURCE": "adpcm_source",
    "PILOT_SDL2_DIR": "sdl2_dir",
    "PILOT_SAVE_KEYS_FILE": "save_keys",
    "PILOT_CONSOLE_SETTINGS_FILE": "console_settings",
    "PILOT_SECTION_MOD_SOURCE": "section_mod_source",
}


def digest(path):
    with Path(path).open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def framework_identity():
    """Prevent a new framework pin/source tree from blessing an old binary.

    Clean source binds to its commit. Explicit development trees also bind to
    tracked edits and new source inputs; ignored outputs are never inventoried.
    """
    git = 'C:/Program Files/Git/cmd/git.exe' if os.name == 'nt' else 'git'
    command = [git, '-C', str(ROOT)]
    revision = subprocess.check_output(command + ['rev-parse', 'HEAD'], text=True).strip()
    changes = subprocess.check_output(command + ['diff', '--no-ext-diff', '--binary', 'HEAD', '--'])
    new = subprocess.check_output(command + ['ls-files', '--others', '--exclude-standard', '-z', '--',
        '*.c', '*.cpp', '*.h', '*.inc', '*.py', '*.cmake', 'CMakeLists.txt'])
    inputs = {}
    for name in filter(None, new.decode('utf-8').split('\0')):
        path = (ROOT / name).resolve()
        if not path.is_relative_to(ROOT):
            raise ValueError('framework source input leaves its checkout')
        inputs[name] = digest(path)
    return dict(revision=revision, tracked_diff_sha256=hashlib.sha256(changes).hexdigest(), new_sources=inputs)


def definition(path):
    path = Path(path).resolve()
    data = tomllib.loads(path.read_text(encoding="utf-8"))
    title = data.get("title", {})
    if data.get("schema") != SCHEMA:
        raise ValueError("unknown native title definition schema")
    for key in ("id", "executable"):
        pattern = r"[A-Za-z][A-Za-z0-9_-]*" if key == "executable" else r"[A-Za-z][A-Za-z0-9_.-]*"
        if not isinstance(title.get(key), str) or not re.fullmatch(pattern, title[key]):
            raise ValueError(f"invalid title {key}")
    if not isinstance(title.get("name"), str) or not title["name"].strip():
        raise ValueError("missing title display name")
    if not re.fullmatch(r"[0-9a-f]{64}", title.get("xbe_sha256", "")):
        raise ValueError("invalid title XBE identity")
    for key, value in data.get("build", {}).items():
        if not re.fullmatch(r"PILOT_[A-Z0-9_]+", key) or not isinstance(value, (str, int, bool)):
            raise ValueError("invalid native build setting")
        if key in ("PILOT_SECTION_MOD_SOURCE", "PILOT_TITLE_MOD_SOURCE"):
            raise ValueError("select a reviewed mod registry through its declared input")
    content = data.get("content", {})
    filename = content.get("image", "default.xbe")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+\.xbe", filename, re.I) or filename.startswith("."):
        raise ValueError("content image must be a plain XBE filename")
    device = content.get("device", "\\Device\\Cdrom0")
    if not re.fullmatch(r"\\Device\\(?:Cdrom0|Harddisk0\\Partition[1-5])", device, re.I):
        raise ValueError("unsupported content device")
    drive = content.get("drive", "D:")
    if not re.fullmatch(r"[A-Z]:", drive):
        raise ValueError("invalid content drive")
    if content.get("drive_owner", "HOST") not in ("HOST", "TITLE"):
        raise ValueError("content drive_owner must be HOST or TITLE")
    profile = data.get("generation", {}).get("profile")
    if profile:
        selected = json.loads((path.parent / profile).resolve().read_text(encoding="utf-8"))
        if selected.get("schema") != "native-title-profile.v1" or selected.get("xbe_sha256") != title["xbe_sha256"]:
            raise ValueError("binding profile differs from the selected title")
    for key, value in data.get("runtime", {}).items():
        if not re.fullmatch(r"XLLE_PILOT_[A-Z0-9_]+", key) or not isinstance(value, (str, int, bool)):
            raise ValueError("invalid native runtime setting")
    for key in ("unit_max_functions", "unit_max_bytes"):
        value = data.get("generation", {}).get(key, 0)
        if type(value) is not int or value < 0:
            raise ValueError("invalid generated C unit budget")
    modes = data.get('launcher', {}).get('frame_modes', [])
    if not isinstance(modes, list) or any(m not in ('interpolated', 'actual') for m in modes) or len(set(modes)) != len(modes):
        raise ValueError('declare unique qualified frame modes')
    title_mod_inputs(path, data)
    return path, data


def title_mod_inputs(source, data):
    """Bind the trusted registry and its declared local headers to the build."""
    mods = data.get('mods', {})
    registry = mods.get('registry')
    if not registry:
        return {}
    root = Path(source).parent.resolve()
    inputs = [registry, *mods.get('inputs', [])]
    if any(not isinstance(p, str) or not p for p in inputs):
        raise ValueError('invalid title registry input')
    result = {}
    for item in inputs:
        path = (root / item).resolve()
        if not path.is_relative_to(root) or not path.is_file():
            raise ValueError('title registry inputs must be files within the title root')
        result[item] = dict(path=str(path), sha256=digest(path))
    return result


def private_paths(path):
    path = Path(path).resolve()
    values = json.loads(path.read_text(encoding="utf-8"))
    # Runtime content can live on an unavailable share. Build/configure only
    # need its path spelling; actual mount/image verification owns access at
    # launch. Keep writable state and build inputs physically canonicalized.
    return {key: (Path(os.path.abspath(path.parent / value)) if key=='media_root'
                  else (path.parent / value).resolve()) for key, value in values.items()
            if isinstance(value, str) and value}


def check_image(title, paths):
    if "xbe" not in paths or digest(paths["xbe"]) != title["xbe_sha256"]:
        from xbe_identity import unsupported_image_message
        detail = unsupported_image_message(paths['xbe'], title) if 'xbe' in paths else 'No XBE was supplied.'
        raise ValueError("supplied XBE differs from the selected title. " + detail)


def check_generated(output, title):
    output = Path(output)
    manifest = json.loads((output / "generation.aot-private.json").read_text())
    if manifest["sha256"] != title["xbe_sha256"]:
        raise ValueError("generated CPU belongs to another title")
    generated = output / "gen"
    if any(p.is_symlink() or (p.is_dir() and p.name != "third_party") for p in generated.iterdir()):
        raise ValueError("unexpected generated directory or symlink")
    if not manifest.get("files") or set(manifest["files"]) != {p.name for p in generated.iterdir() if p.is_file()}:
        raise ValueError("generated artifact inventory differs from its receipt")
    for name, expected in manifest["files"].items():
        artifact = generated / name
        if Path(name).name != name or artifact.is_symlink() or not artifact.is_file() or digest(artifact) != expected:
            raise ValueError(f"changed generated artifact: {name}")
    return manifest


def write_runtime_profile(output, data):
    """Add declarative startup metadata without weakening any CPU/body receipt."""
    manifest = check_generated(output, data["title"])
    content = data.get("content", {})
    runtime = {
        "id": data["title"]["id"], "name": data["title"]["name"],
        "image": content.get("image", "default.xbe"),
        "device": content.get("device", "\\Device\\Cdrom0"),
        "drive": content.get("drive", "D:"),
        "drive_owner": content.get("drive_owner", "HOST"),
    }
    header = "/* Generated title policy; service implementations are shared. */\n"
    for key, value in runtime.items():
        header += f"#define PILOT_TITLE_{key.upper()} {json.dumps(value)}\n"
    destination = Path(output) / "gen/pilot_title_runtime.h"
    if not destination.exists() or destination.read_text() != header:
        destination.write_text(header, encoding="utf-8", newline="\n")
    manifest["title_runtime"] = runtime
    manifest["files"][destination.name] = digest(destination)
    (Path(output) / "generation.aot-private.json").write_text(json.dumps(manifest, indent=2) + "\n")


def prepare_command(source, data, paths, output):
    generation = data.get("generation", {})
    command = [sys.executable, str(ROOT / "tools/native_pilot.py"), "--donor", str(paths["cpu_donor"]),
               "--xbe", str(paths["xbe"]), "--output", str(output)]
    if generation.get("profile"):
        command += ["--profile", str((source.parent / generation["profile"]).resolve())]
    if generation.get("data_sections"):
        command += ["--data-sections", generation["data_sections"]]
    for key in ("unit_max_functions", "unit_max_bytes"):
        if generation.get(key):
            command += ["--" + key.replace("_", "-"), str(generation[key])]
    for family in generation.get("bindings", []):
        if family not in BINDINGS:
            raise ValueError("unknown native binding family")
        command += [f"--{family}-symbols", str(paths["xdk_symbols"])]
    for seed in generation.get("seeds", []):
        command += ["--seed-functions", str((source.parent / seed).resolve())]
    if paths.get("seed_functions"):
        command += ["--seed-functions", str(paths["seed_functions"])]
    return command


def configure_command(data, paths, output, build, cmake, generator, jobs, source=None):
    options = dict(data["build"])
    options.setdefault("PILOT_NATIVE_LINK_PROFILE", "FINAL")
    options.update(XBOXLLR_BUILD_MODE="NATIVE_TITLE", PILOT_GENERATED=str(Path(output) / "gen"),
                   PILOT_EXECUTABLE_NAME=data["title"]["executable"], PILOT_COMPILE_JOBS=str(jobs),
                   PILOT_SECTION_MOD_SOURCE="", PILOT_TITLE_MOD_SOURCE="", PILOT_ADPCM_SOURCE="")
    if data.get('mods', {}).get('registry'):
        if source is None:
            raise ValueError('title registry configuration requires its title definition')
        inputs = title_mod_inputs(source, data)
        options['PILOT_TITLE_MOD_SOURCE'] = inputs[data['mods']['registry']]['path']
    for option, key in DEPENDENCIES.items():
        if key in paths:
            if not paths[key].exists():
                raise ValueError(f"missing native dependency {key}")
            options[option] = str(paths[key])
    command = [str(cmake), "-S", str(ROOT), "-B", str(build), "-G", generator]
    if generator.startswith("Visual Studio"):
        command += ["-A", "x64"]
    else:
        options["CMAKE_BUILD_TYPE"] = "Release"
    command += [f"-D{key}={'ON' if value is True else 'OFF' if value is False else value}"
                for key, value in sorted(options.items())]
    return command


def executable(build, name):
    suffix = ".exe" if os.name == "nt" else ""
    candidates = [Path(build) / "native-title/Release" / (name + suffix),
                  Path(build) / "native-title" / (name + suffix)]
    return next((p for p in candidates if p.is_file()), candidates[0])


def identity(source, data, output, paths=None):
    """Bind configuration/build receipts to policy, proof input and generated bytes."""
    check_generated(output, data["title"])
    profile = data.get("generation", {}).get("profile")
    require_receipt(Path(output) / "native-title-preparation.json", preparation_identity(source, data, output))
    result = {"title": data["title"]["id"], "definition_sha256": digest(source),
            "framework": framework_identity(),
            "profile_sha256": profile_identity(source, profile),
            "generation_sha256": digest(Path(output) / "generation.aot-private.json"),
            "generated": str(Path(output).resolve())}
    build_policy = dict(data["build"])
    build_policy.setdefault("PILOT_NATIVE_LINK_PROFILE", "FINAL")
    result["build_policy_sha256"] = hashlib.sha256(
        json.dumps(build_policy, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()
    if paths and paths.get("section_mod_source"):
        selected = paths["section_mod_source"].resolve()
        result["section_mod"] = {"source": str(selected), "sha256": digest(selected)}
    inputs = title_mod_inputs(source, data)
    if inputs:
        result['title_mod'] = inputs
    return result


def preparation_identity(source, data, output):
    return _preparation_identity(source, data, output)


def selected_bindings(data):
    families = data.get("generation", {}).get("bindings", [])
    if not isinstance(families, list) or any(not isinstance(f, str) or f not in BINDINGS for f in families) or len(set(families)) != len(families):
        raise ValueError("declare unique reviewed binding families")
    return families


def admitted_bindings(manifest):
    families = set()
    for key, family in [(family + "_binding", family) for family in BINDINGS] + [("jump_abi", "jump")]:
        if key in manifest:
            if not isinstance(manifest[key], dict) or not manifest[key]:
                raise ValueError("invalid native binding admission: " + key)
            families.add(family)
    return sorted(families)


def generation_proof(manifest):
    """Fingerprint code/proof metadata, excluding configurable title policy."""
    proof = {key: value for key, value in manifest.items() if key not in ("files", "title_runtime")}
    proof["files"] = {key: value for key, value in manifest["files"].items() if key != "pilot_title_runtime.h"}
    return hashlib.sha256(json.dumps(proof, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()


def _preparation_identity(source, data, output, *, completing_bind=False, completing_recovery=False):
    if not completing_recovery and (Path(output) / INCOMPLETE_RECOVERY).exists():
        raise ValueError("native function recovery publication is incomplete; restore its retained history or prepare again")
    if not completing_bind and (Path(output) / INCOMPLETE_BIND).exists():
        raise ValueError("native binding refresh is incomplete; successfully bind every selected family before configuring or running")
    manifest = check_generated(output, data["title"])
    families = sorted(selected_bindings(data))
    if families != admitted_bindings(manifest):
        raise ValueError("selected native binding families differ from actual generation admissions")
    profile = data.get("generation", {}).get("profile")
    return {"xbe_sha256": data["title"]["xbe_sha256"],
            "profile_sha256": profile_identity(source, profile),
            "bindings": families, "generation_proof_sha256": generation_proof(manifest),
            "files": {key: value for key, value in manifest["files"].items() if key != "pilot_title_runtime.h"}}


def profile_identity(source, profile):
    if not profile:
        return None
    path = (Path(source).parent / profile).resolve()
    data = json.loads(path.read_text(encoding="utf-8"))
    hashes = {str(path): digest(path)}
    for proof in data.get("bindings", {}).values():
        if isinstance(proof, dict) and "include" in proof:
            if set(proof) != {"include"} or not isinstance(proof["include"], str):
                raise ValueError("invalid native title proof include")
            fragment = (path.parent / proof["include"]).resolve()
            if not (fragment.is_relative_to(ROOT) or fragment.is_relative_to(path.parent)):
                raise ValueError("native title proof include leaves profile/framework roots")
            hashes[str(fragment)] = digest(fragment)
    return hashes


def reject_staging_mutation(*paths):
    for path in paths:
        resolved = Path(path).resolve()
        if any((parent / "native-title-stage.json").exists() for parent in (resolved, *resolved.parents)):
            raise ValueError("running title staging is immutable; choose a separate build/output directory")


def partition_prepared(source, data, output):
    """Apply a lossless compilation-unit transform to an already proved title."""
    output = Path(output)
    manifest = check_generated(output, data["title"])
    preparation = output / "native-title-preparation.json"
    require_receipt(preparation, preparation_identity(source, data, output))
    generation = data.get("generation", {})
    functions, byte_limit = generation.get("unit_max_functions", 0), generation.get("unit_max_bytes", 0)
    if not functions and not byte_limit:
        raise ValueError("declare a generated C unit budget before partitioning")
    from native_unit_budget import apply as partition_units
    old_manifest = output / "generation.aot-private.json"
    # Retain the failed-build inputs once; repeated partition is idempotent.
    for path in (old_manifest, preparation):
        backup = path.with_name(path.name + ".before-unit-budget")
        if not backup.exists():
            shutil.copy2(path, backup)
    transform = partition_units(output / "gen", max_functions=functions or 250, max_bytes=byte_limit or 524288)
    files = {p.name: digest(p) for p in sorted((output / "gen").iterdir()) if p.is_file()}
    if files != manifest["files"]:
        manifest["unit_budget"] = transform
        manifest["files"] = files
        old_manifest.write_text(json.dumps(manifest, indent=2) + "\n")
        preparation.write_text(json.dumps(preparation_identity(source, data, output), indent=2) + "\n")
    return transform


def bind_prepared(source, data, paths, output):
    """Revalidate every selected service proof without retranslating the CPU."""
    output = Path(output)
    manifest = check_generated(output, data["title"])
    preparation = output / "native-title-preparation.json"
    prior = json.loads(preparation.read_text())
    current_files = {key: value for key, value in manifest["files"].items()
                     if key != "pilot_title_runtime.h"}
    if prior.get("xbe_sha256") != data["title"]["xbe_sha256"] or prior.get("files") != current_files:
        raise ValueError("changed preparation cannot receive incremental bindings")
    families = selected_bindings(data)
    if not families:
        raise ValueError("declare unique reviewed binding families")
    retained = set(admitted_bindings(manifest))
    # Current receipts also protect admission metadata. Legacy receipts can be
    # upgraded only here, after the same image/files check and complete rebinding.
    if ("bindings" in prior or "generation_proof_sha256" in prior) and (
            prior.get("bindings") != sorted(retained) or prior.get("generation_proof_sha256") != generation_proof(manifest)):
        raise ValueError("changed generation proof cannot receive incremental bindings")
    if not retained.issubset(families):
        raise ValueError("incremental binding must revalidate all previously admitted families")
    profile = data.get("generation", {}).get("profile")
    profile = (Path(source).parent / profile).resolve() if profile else None
    from native_binding import has_profile_binding
    display = "d3d" in families and has_profile_binding(data["title"]["xbe_sha256"], "display", profile)
    if "display_binding" in manifest and not display:
        raise ValueError("incremental binding cannot remove the original display proof")
    history = output / "binding-history" / str(time.time_ns())
    history.mkdir(parents=True)
    for path in (output / "generation.aot-private.json", preparation):
        shutil.copy2(path, history / path.name)
    # Each binder independently checks original XBE bodies/layouts and existing
    # generated wrappers. Even a rejection before the first artifact changes
    # leaves a persisted failure marker, so configuration cannot bless a failed
    # attempt. Retrying unchanged inputs is allowed; partial artifact changes
    # still have to satisfy the original preparation inventory above.
    incomplete = output / INCOMPLETE_BIND
    incomplete.write_text(json.dumps({"bindings": families, "history": str(history)}, indent=2) + "\n")
    for family in families:
        importlib.import_module(BINDINGS[family]).bind(paths["xbe"], output, paths["xdk_symbols"], profile=profile)
    if display:
        importlib.import_module("native_display_binding").bind(paths["xbe"], output, paths["xdk_symbols"], profile=profile)
    write_runtime_profile(output, data)
    result = _preparation_identity(source, data, output, completing_bind=True)
    preparation.write_text(json.dumps(result, indent=2) + "\n")
    incomplete.unlink()
    return {"bindings": families, "original_display": display,
            "generation_sha256": digest(output / "generation.aot-private.json"), "history": str(history)}


def recover_prepared(source, data, paths, output, plan_path):
    """Stage original native function recovery, rebind, then publish a receipt.

    CPU lowering is owned by the pinned donor/extent tool. A staged failure
    leaves the live preparation intact. Publication is guarded across files;
    an interrupted copy cannot acquire a valid launch receipt.
    """
    from types import SimpleNamespace
    from aot_inventory import parse_xbe
    from native_function_extent import load_plan, recover
    output=Path(output).resolve();plan_path=Path(plan_path).resolve()
    reject_staging_mutation(output)
    manifest=check_generated(output,data['title'])
    preparation=output/'native-title-preparation.json'
    require_receipt(preparation,preparation_identity(source,data,output))
    if 'cpu_donor' not in paths or 'xdk_symbols' not in paths:
        raise ValueError('native recovery requires the pinned CPU donor and selected SDK symbols')
    blob=paths['xbe'].read_bytes();image=parse_xbe(blob)
    if image['sha256']!=data['title']['xbe_sha256']:
        raise ValueError('native recovery original image changed')
    functions=json.loads((output/'disasm/functions.json').read_text())
    plan=load_plan(plan_path,blob,image,functions)
    retained={r['start']:r for r in manifest.get('function_extents',[])}
    for record in plan['extents']:
        if record['start'] in retained and retained[record['start']]!=record:
            raise ValueError('native recovery conflicts with an existing original extent')
    pending=[r for r in plan['extents'] if r['start'] not in retained]
    if not pending:return {'recovered':0,'idempotent':True}
    original_generation=digest(output/'generation.aot-private.json');original_preparation=digest(preparation)
    original_profile=profile_identity(source,data.get('generation',{}).get('profile'))
    # This stage may live on a temporary fast volume. It is never a run stage.
    temporary=Path(tempfile.gettempdir()).resolve()/'build'/'native-title-recovery'
    temporary.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix='candidate-',dir=temporary)).resolve()
    try:
        shutil.copytree(output/'gen',stage/'gen')
        for name in ('disasm','classification','abi'):
            if (output/name).is_dir():shutil.copytree(output/name,stage/name)
        records=list(output.glob('*binding.aot-private.json'))
        records += [output/'generation.aot-private.json']
        if (output/'function-extents.aot-private.json').exists():records.append(output/'function-extents.aot-private.json')
        for path in records:shutil.copy2(path,stage/path.name)
        for record in pending:
            result=recover(SimpleNamespace(output=stage,donor=paths['cpu_donor'],xbe=paths['xbe'],
                function=record['start'],end=record['end'],reason=record['reason'],
                new_entry=record.get('new_entry',False),terminal=record.get('terminal','return'),
                contained_entry=[r['start'] for r in record.get('contained_entries',[])]))
            if result!=record:raise ValueError('native compiler recovery differs from the reviewed plan')
        families=selected_bindings(data)
        profile=data.get('generation',{}).get('profile')
        profile=(Path(source).parent/profile).resolve() if profile else None
        for family in families:
            importlib.import_module(BINDINGS[family]).bind(paths['xbe'],stage,paths['xdk_symbols'],profile=profile)
        from native_binding import has_profile_binding
        if 'd3d' in families and has_profile_binding(data['title']['xbe_sha256'],'display',profile):
            importlib.import_module('native_display_binding').bind(paths['xbe'],stage,paths['xdk_symbols'],profile=profile)
        # Some legacy binders rewrite a unit through text mode. Retain the
        # original bytes when only CRLF/LF changed, avoiding an unrelated CPU
        # recompile and preserving the preceding file identity exactly.
        rebound=json.loads((stage/'generation.aot-private.json').read_text())
        retained_text=False
        for name in manifest['files']:
            if not name.endswith(('.c','.h')) or not (stage/'gen'/name).exists():continue
            old=(output/'gen'/name).read_bytes();new=(stage/'gen'/name).read_bytes()
            if old!=new and old.replace(b'\r\n',b'\n')==new.replace(b'\r\n',b'\n'):
                (stage/'gen'/name).write_bytes(old);rebound['files'][name]=digest(stage/'gen'/name);retained_text=True
        if retained_text:(stage/'generation.aot-private.json').write_text(json.dumps(rebound,indent=2)+'\n')
        write_runtime_profile(stage,data)
        candidate=check_generated(stage,data['title'])
        for family in BINDINGS:
            key=family+'_binding'
            if manifest.get(key)!=candidate.get(key):raise ValueError('native recovery changed service admissions: '+family)
        if manifest.get('jump_abi')!=candidate.get('jump_abi') or manifest.get('display_binding')!=candidate.get('display_binding'):
            raise ValueError('native recovery changed retained jump/display admissions')
        if not set(manifest['files']).issubset(candidate['files']):
            raise ValueError('native recovery removed a generated artifact')
        selected=_preparation_identity(source,data,stage)
        (stage/'native-title-preparation.json').write_text(json.dumps(selected,indent=2)+'\n')
        # Revalidate the live input after the long lowering/rebinding phase.
        check_generated(output,data['title'])
        if (digest(output/'generation.aot-private.json')!=original_generation or digest(preparation)!=original_preparation or
            profile_identity(source,data.get('generation',{}).get('profile'))!=original_profile):
            raise ValueError('live native preparation changed during recovery')
        relatives=['generation.aot-private.json','function-extents.aot-private.json',
                   'disasm/functions.json','native-title-preparation.json']
        relatives += ['gen/'+name for name,value in candidate['files'].items() if manifest['files'].get(name)!=value]
        relatives += [p.name for p in stage.glob('*binding.aot-private.json') if not (output/p.name).exists() or digest(output/p.name)!=digest(p)]
        history=output/'recovery-history'/str(time.time_ns());(history/'before').mkdir(parents=True)
        (history/'proposed').mkdir();shutil.copy2(plan_path,history/'plan.json')
        for relative in relatives:
            target=history/'proposed'/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(stage/relative,target)
            if (output/relative).exists():
                old=history/'before'/relative;old.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(output/relative,old)
        # Keep the exact compiler diagnostics with the durable transition.
        if (stage/'extent-recovery').is_dir():shutil.copytree(stage/'extent-recovery',history/'lowering')
        marker=output/INCOMPLETE_RECOVERY
        marker.write_text(json.dumps({'plan_sha256':digest(plan_path),'history':str(history),'files':relatives},indent=2)+'\n')
        for relative in relatives:
            target=output/relative;target.parent.mkdir(parents=True,exist_ok=True)
            os.replace(history/'proposed'/relative,target)
        require_receipt(preparation,_preparation_identity(source,data,output,completing_recovery=True))
        marker.unlink()
        return {'recovered':len(pending),'changed_files':relatives,'history':str(history),
                'generation_sha256':digest(output/'generation.aot-private.json')}
    finally:
        # Verified temporary path, native filesystem operations, no shell mix.
        if stage.parent!=temporary or not stage.name.startswith('candidate-'):
            raise ValueError('refusing recovery stage cleanup outside its temporary parent')
        shutil.rmtree(stage)


def require_receipt(path, expected):
    if not Path(path).is_file() or json.loads(Path(path).read_text()) != expected:
        raise ValueError("native build identity changed; configure and build the selected title again")


def launch(data, paths, output, build, automated=False, seconds=None, detach=False, source=None, input_replay=None, capture=False, debug_port=None,
           visual_options=None, launcher=None):
    check_image(data["title"], paths)
    check_generated(output, data["title"])
    if debug_port is not None and (type(debug_port) is not int or not 1 <= debug_port <= 65535):
        raise ValueError("debug port must be in 1..65535")
    if seconds is not None and not automated:
        raise ValueError("bounded unattended runs require --automated; owner runs have no deadline")
    if automated and seconds is None:
        raise ValueError("unattended title runs require a bounded --seconds duration")
    if seconds is not None and (not math.isfinite(seconds) or seconds <= 0):
        raise ValueError("run duration must be finite and positive")
    if detach and automated:
        raise ValueError("unattended runs retain their bounded process owner")
    if launcher and (automated or input_replay is not None):
        raise ValueError('Interactive launcher requires an owner-facing run')
    replay_bytes = None
    if input_replay is not None:
        replay_path = Path(input_replay).resolve()
        if not replay_path.is_file():
            raise ValueError("input replay must be an existing file")
        replay_bytes = replay_path.read_bytes()
        if not replay_bytes:
            raise ValueError("input replay must not be empty")
    binary = executable(build, data["title"]["executable"])
    if not binary.is_file():
        raise ValueError("build this native title before launching")
    if source is None:
        raise ValueError("launch requires the selected title definition")
    expected = identity(source, data, output, paths)
    expected["executable_sha256"] = digest(binary)
    require_receipt(Path(build) / "native-title-build.json", expected)
    for key in ("eeprom", "media_root", "state_dir"):
        if key not in paths or not paths[key].exists():
            raise ValueError(f"missing private {key}")
    runs = Path(build) / "runs"
    runs.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix=time.strftime("%Y%m%d-%H%M%S-"), dir=runs))
    selected_environment = {}
    from native_title_launcher import binary as launcher_binary, select as select_launcher
    use_launcher = launcher is True or (launcher is None and not automated and input_replay is None and
                                       bool(data.get('launcher')) and launcher_binary(build).is_file())
    if use_launcher:
        selection = select_launcher(source, data, paths, build, run, force=launcher is True)
        if selection is None:
            print('Launcher closed; game was not started.', flush=True)
            return
        paths = dict(paths, xbe=selection['image'])
        check_image(data['title'], paths)
        visual_options, selected_environment = selection['options'], selection['environment']
    mod_plan = None
    if visual_options and (visual_options.get('aspect') is not None or visual_options.get('height', 0) or
        visual_options.get('supersample', 1) != 1 or visual_options.get('antialiasing', 'original') != 'original' or
        visual_options.get('anisotropy', 1) != 1 or visual_options.get('frame_mode', 'original') != 'original'):
        from native_visual_mods import resolve
        mod_plan = resolve(source, data, run / 'mods.plan', **visual_options)
    # Freeze the actual executable and its adjacent transports. Future builds
    # cannot replace a running owner's binary or force their window closed.
    stage = run / "app"
    stage.mkdir()
    stage_identity = dict(expected)
    if replay_bytes is not None:
        stage_identity["input_replay_sha256"] = hashlib.sha256(replay_bytes).hexdigest()
    (stage / "native-title-stage.json").write_text(json.dumps(stage_identity, indent=2) + "\n")
    for item in binary.parent.iterdir():
        if item.is_file() and (item == binary or item.suffix.lower() == ".dll"):
            shutil.copy2(item, stage / item.name)
    if digest(stage / binary.name) != expected["executable_sha256"]:
        raise ValueError("native executable changed during staging; retry after build completes")
    environment = {k: v for k, v in os.environ.items() if not k.startswith("XLLE_PILOT_")}
    environment.update({key: str(value) for key, value in data.get("runtime", {}).items()})
    environment.update(selected_environment)
    if mod_plan is not None:
        environment['XLLE_PILOT_MOD_PLAN'] = str((run / 'mods.plan').resolve())
    if debug_port is not None:
        environment["XLLE_PILOT_DEBUG_PORT"] = str(debug_port)
        if automated and replay_bytes is None:
            environment["XLLE_PILOT_INPUT"] = "tcp"
    if replay_bytes is not None:
        replay_path = stage / "input-replay.txt"
        replay_path.write_bytes(replay_bytes)
        environment.update(XLLE_PILOT_INPUT="replay", XLLE_PILOT_INPUT_REPLAY=str(replay_path.resolve()))
    if capture:
        capture_directory = run / "captures"
        capture_directory.mkdir()
        environment.update(XLLE_PILOT_PRESENT_CAPTURE=str(capture_directory.resolve()),
                           XLLE_PILOT_PRESENT_CAPTURE_INTERVAL_MS="1000")
    environment["XLLE_PILOT_VISIBLE"] = "0" if automated else "1"
    environment["XLLE_PILOT_AUDIO_OUTPUT_MUTE"] = "1" if automated else "0"
    if automated:
        environment.update(RECOMP_VOLUME="0", SDL_AUDIODRIVER="dummy")
    else:
        for key in ("SDL_AUDIODRIVER", "SDL_AUDIO_DRIVER"):
            if environment.get(key, "").lower() == "dummy":
                environment.pop(key)
        environment["RECOMP_VOLUME"] = selected_environment.get('RECOMP_VOLUME', '100')
    command = [str(stage / binary.name), *(str(paths[key]) for key in ("xbe", "eeprom", "media_root", "state_dir"))]
    with (run / "title.log").open("w", encoding="utf-8") as log:
        child = subprocess.Popen(command, env=environment, stdout=log, stderr=subprocess.STDOUT)
        receipt = dict(title=data["title"]["id"], route="native-title", pid=child.pid,
                       executable_sha256=digest(stage / binary.name), command=command,
                       automated=automated, output=str(run))
        if mod_plan is not None:
            receipt['mod_plan'] = mod_plan
        if replay_bytes is not None:
            receipt.update(input_replay=str(replay_path), input_replay_sha256=stage_identity["input_replay_sha256"])
        if debug_port is not None:
            receipt["debug_endpoint"] = f"127.0.0.1:{debug_port}"
        if capture:
            receipt["captures"] = str(capture_directory)
        (run / "launch.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(json.dumps(receipt, indent=2), flush=True)
        if detach:
            return
        deadline = False
        try:
            child.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            deadline = True
            child.kill()
            child.wait()
        receipt.update(exit_code=child.returncode, stopped_at_deadline=deadline)
        (run / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
        return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("check", "prepare", "bind", "recover", "partition", "configure", "build", "build-launcher", "run"))
    parser.add_argument("--title", type=Path, required=True)
    parser.add_argument("--paths", type=Path, required=True)
    parser.add_argument("--generated", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--cmake", type=Path, default=Path("C:/Program Files/CMake/bin/cmake.exe") if os.name == "nt" else Path("cmake"))
    parser.add_argument("--generator", default="Visual Studio 17 2022" if os.name == "nt" else "Ninja")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--with-checks", action="store_true",
                        help="Build the title and native iteration check executables in one build graph; does not run tests")
    parser.add_argument("--recovery", type=Path, help="Original-image-bound function recovery plan; recover only")
    parser.add_argument("--link-profile", choices=("FINAL", "ITERATIVE"),
                        help="Select build-only linking policy; ITERATIVE keeps optimized CPU code on MSVC")
    parser.add_argument("--automated", action="store_true")
    parser.add_argument("--seconds", type=float)
    parser.add_argument("--detach", action="store_true")
    parser.add_argument("--input-replay", type=Path, help="Freeze a native controller replay into this run's staging")
    parser.add_argument("--capture", action="store_true", help="Capture diagnostic present images; unsuitable for performance measurement")
    parser.add_argument("--reuse-discovery", action="store_true",
                        help="Prepare using the existing image-bound function discovery; original bodies are regenerated")
    parser.add_argument("--debug-port", type=int, help="Opt-in localhost native input/presented-screenshot server")
    ui = parser.add_mutually_exclusive_group()
    ui.add_argument('--launcher', action='store_const', const=True, default=None, dest='launcher')
    ui.add_argument('--no-launcher', action='store_const', const=False, dest='launcher')
    parser.add_argument('--widescreen', help='Trusted title camera mod: adaptive or 4:3 through 32:9')
    parser.add_argument('--internal-resolution', type=int, choices=(0, 1080, 1440, 1800, 2160), default=0)
    parser.add_argument('--supersampling', type=int, choices=(1, 2, 3), default=1)
    parser.add_argument('--antialiasing', choices=('original', 'fxaa'), default='original')
    parser.add_argument('--anisotropy', type=int, choices=(1, 2, 4, 8, 16), default=1)
    parser.add_argument('--frame-mode', choices=('original', 'interpolated', 'actual'), default='original')
    parser.add_argument('--frame-rate', type=int, choices=(60, 120), default=60)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 32 or (args.seconds is not None and (not math.isfinite(args.seconds) or args.seconds <= 0)):
        parser.error("invalid job count or run duration")
    if (args.input_replay is not None or args.capture or args.debug_port is not None) and args.action != "run":
        parser.error("--input-replay, --capture and --debug-port apply only to run")
    if args.with_checks and args.action != "build":
        parser.error("--with-checks applies only to build")
    if args.reuse_discovery and args.action != "prepare":
        parser.error("--reuse-discovery applies only to prepare")
    if (args.action == "recover") != (args.recovery is not None):
        parser.error("recover requires --recovery, which applies only to recover")
    source, data = definition(args.title)
    if args.link_profile:
        data["build"]["PILOT_NATIVE_LINK_PROFILE"] = args.link_profile
    paths = private_paths(args.paths)
    check_image(data["title"], paths)
    if args.action == 'build-launcher':
        from native_title_launcher import build_launcher
        print(build_launcher(source, data, paths, args.build_dir, args.cmake, args.generator, args.jobs))
        return
    if args.action in ("prepare", "bind", "recover", "partition", "configure", "build"):
        reject_staging_mutation(args.generated, args.build_dir)
    if args.action == "prepare":
        command=prepare_command(source, data, paths, args.generated)
        if args.reuse_discovery:command.append("--reuse-discovery")
        subprocess.run(command, check=True)
        write_runtime_profile(args.generated, data)
        (args.generated / "native-title-preparation.json").write_text(json.dumps(_preparation_identity(source, data, args.generated, completing_bind=True, completing_recovery=True), indent=2) + "\n")
        (args.generated / INCOMPLETE_BIND).unlink(missing_ok=True)
        (args.generated / INCOMPLETE_RECOVERY).unlink(missing_ok=True)
    elif args.action == "bind":
        print(json.dumps(bind_prepared(source, data, paths, args.generated), indent=2))
    elif args.action == "recover":
        print(json.dumps(recover_prepared(source,data,paths,args.generated,args.recovery),indent=2))
    elif args.action == "partition":
        print(json.dumps(partition_prepared(source, data, args.generated), indent=2))
    elif args.action == "configure":
        require_receipt(args.generated / "native-title-preparation.json", preparation_identity(source, data, args.generated))
        write_runtime_profile(args.generated, data)
        subprocess.run(configure_command(data, paths, args.generated, args.build_dir, args.cmake, args.generator, args.jobs, source), check=True)
        (args.build_dir / "native-title-configure.json").write_text(json.dumps(identity(source, data, args.generated, paths), indent=2) + "\n")
    elif args.action == "build":
        selected = identity(source, data, args.generated, paths)
        require_receipt(args.build_dir / "native-title-configure.json", selected)
        target = "pilot_native_iteration" if args.with_checks else "xlle_native_title"
        subprocess.run([str(args.cmake), "--build", str(args.build_dir), "--config", "Release",
                        "--target", target, "--parallel", str(args.jobs)], check=True)
        selected["executable_sha256"] = digest(executable(args.build_dir, data["title"]["executable"]))
        (args.build_dir / "native-title-build.json").write_text(json.dumps(selected, indent=2) + "\n")
    elif args.action == "run":
        options = dict(aspect=args.widescreen, height=args.internal_resolution, supersample=args.supersampling,
                       antialiasing=args.antialiasing, anisotropy=args.anisotropy,
                       frame_mode=args.frame_mode, frame_rate=args.frame_rate)
        result = launch(data, paths, args.generated, args.build_dir, args.automated, args.seconds, args.detach, source, args.input_replay, args.capture, args.debug_port,
                        options, args.launcher)
        if result is not None and not result["stopped_at_deadline"]:
            return result["exit_code"]
    else:
        identity(source, data, args.generated, paths)
        print("Native title identity and generated artifacts verified.")


if __name__ == "__main__":
    raise SystemExit(main())
