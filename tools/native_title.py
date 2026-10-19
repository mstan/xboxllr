#!/usr/bin/env python3
"""Build and launch any declared native title through the shared XboxLLR runtime.

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
DEPENDENCIES = {
    "PILOT_D3D_SOURCE": "d3d_source", "PILOT_DSP_SOURCE": "dsp_source",
    "PILOT_DSP_LIBRARY": "dsp_library", "PILOT_ADPCM_SOURCE": "adpcm_source",
    "PILOT_SDL2_DIR": "sdl2_dir",
    "PILOT_SAVE_KEYS_FILE": "save_keys",
    "PILOT_CONSOLE_SETTINGS_FILE": "console_settings",
}


def digest(path):
    with Path(path).open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


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
    return path, data


def private_paths(path):
    path = Path(path).resolve()
    values = json.loads(path.read_text(encoding="utf-8"))
    return {key: (path.parent / value).resolve() for key, value in values.items()
            if isinstance(value, str) and value}


def check_image(title, paths):
    if "xbe" not in paths or digest(paths["xbe"]) != title["xbe_sha256"]:
        raise ValueError("supplied XBE differs from the selected title")


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


def configure_command(data, paths, output, build, cmake, generator, jobs):
    options = dict(data["build"])
    options.update(XBOXLLR_BUILD_MODE="NATIVE_TITLE", PILOT_GENERATED=str(Path(output) / "gen"),
                   PILOT_EXECUTABLE_NAME=data["title"]["executable"], PILOT_COMPILE_JOBS=str(jobs))
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


def identity(source, data, output):
    """Bind configuration/build receipts to policy, proof input and generated bytes."""
    check_generated(output, data["title"])
    profile = data.get("generation", {}).get("profile")
    require_receipt(Path(output) / "native-title-preparation.json", preparation_identity(source, data, output))
    return {"title": data["title"]["id"], "definition_sha256": digest(source),
            "profile_sha256": profile_identity(source, profile),
            "generation_sha256": digest(Path(output) / "generation.aot-private.json"),
            "generated": str(Path(output).resolve())}


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


def _preparation_identity(source, data, output, *, completing_bind=False):
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


def require_receipt(path, expected):
    if not Path(path).is_file() or json.loads(Path(path).read_text()) != expected:
        raise ValueError("native build identity changed; configure and build the selected title again")


def launch(data, paths, output, build, automated=False, seconds=None, detach=False, source=None, input_replay=None, capture=False):
    check_image(data["title"], paths)
    check_generated(output, data["title"])
    if seconds is not None and not automated:
        raise ValueError("bounded unattended runs require --automated; owner runs have no deadline")
    if automated and seconds is None:
        raise ValueError("unattended title runs require a bounded --seconds duration")
    if seconds is not None and (not math.isfinite(seconds) or seconds <= 0):
        raise ValueError("run duration must be finite and positive")
    if detach and automated:
        raise ValueError("unattended runs retain their bounded process owner")
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
    expected = identity(source, data, output)
    expected["executable_sha256"] = digest(binary)
    require_receipt(Path(build) / "native-title-build.json", expected)
    for key in ("eeprom", "media_root", "state_dir"):
        if key not in paths or not paths[key].exists():
            raise ValueError(f"missing private {key}")
    runs = Path(build) / "runs"
    runs.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix=time.strftime("%Y%m%d-%H%M%S-"), dir=runs))
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
        environment["RECOMP_VOLUME"] = "100"
    command = [str(stage / binary.name), *(str(paths[key]) for key in ("xbe", "eeprom", "media_root", "state_dir"))]
    with (run / "title.log").open("w", encoding="utf-8") as log:
        child = subprocess.Popen(command, env=environment, stdout=log, stderr=subprocess.STDOUT)
        receipt = dict(title=data["title"]["id"], route="native-title", pid=child.pid,
                       executable_sha256=digest(stage / binary.name), command=command,
                       automated=automated, output=str(run))
        if replay_bytes is not None:
            receipt.update(input_replay=str(replay_path), input_replay_sha256=stage_identity["input_replay_sha256"])
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
    parser.add_argument("action", choices=("check", "prepare", "bind", "partition", "configure", "build", "run"))
    parser.add_argument("--title", type=Path, required=True)
    parser.add_argument("--paths", type=Path, required=True)
    parser.add_argument("--generated", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--cmake", type=Path, default=Path("C:/Program Files/CMake/bin/cmake.exe") if os.name == "nt" else Path("cmake"))
    parser.add_argument("--generator", default="Visual Studio 17 2022" if os.name == "nt" else "Ninja")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--automated", action="store_true")
    parser.add_argument("--seconds", type=float)
    parser.add_argument("--detach", action="store_true")
    parser.add_argument("--input-replay", type=Path, help="Freeze a native controller replay into this run's staging")
    parser.add_argument("--capture", action="store_true", help="Capture diagnostic present images; unsuitable for performance measurement")
    args = parser.parse_args()
    if not 1 <= args.jobs <= 32 or (args.seconds is not None and (not math.isfinite(args.seconds) or args.seconds <= 0)):
        parser.error("invalid job count or run duration")
    if (args.input_replay is not None or args.capture) and args.action != "run":
        parser.error("--input-replay and --capture apply only to run")
    source, data = definition(args.title)
    paths = private_paths(args.paths)
    check_image(data["title"], paths)
    if args.action in ("prepare", "bind", "partition", "configure", "build"):
        reject_staging_mutation(args.generated, args.build_dir)
    if args.action == "prepare":
        subprocess.run(prepare_command(source, data, paths, args.generated), check=True)
        write_runtime_profile(args.generated, data)
        (args.generated / "native-title-preparation.json").write_text(json.dumps(_preparation_identity(source, data, args.generated, completing_bind=True), indent=2) + "\n")
        (args.generated / INCOMPLETE_BIND).unlink(missing_ok=True)
    elif args.action == "bind":
        print(json.dumps(bind_prepared(source, data, paths, args.generated), indent=2))
    elif args.action == "partition":
        print(json.dumps(partition_prepared(source, data, args.generated), indent=2))
    elif args.action == "configure":
        require_receipt(args.generated / "native-title-preparation.json", preparation_identity(source, data, args.generated))
        write_runtime_profile(args.generated, data)
        subprocess.run(configure_command(data, paths, args.generated, args.build_dir, args.cmake, args.generator, args.jobs), check=True)
        (args.build_dir / "native-title-configure.json").write_text(json.dumps(identity(source, data, args.generated), indent=2) + "\n")
    elif args.action == "build":
        selected = identity(source, data, args.generated)
        require_receipt(args.build_dir / "native-title-configure.json", selected)
        subprocess.run([str(args.cmake), "--build", str(args.build_dir), "--config", "Release",
                        "--target", "xlle_native_title", "--parallel", str(args.jobs)], check=True)
        selected["executable_sha256"] = digest(executable(args.build_dir, data["title"]["executable"]))
        (args.build_dir / "native-title-build.json").write_text(json.dumps(selected, indent=2) + "\n")
    elif args.action == "run":
        result = launch(data, paths, args.generated, args.build_dir, args.automated, args.seconds, args.detach, source, args.input_replay, args.capture)
        if result is not None and not result["stopped_at_deadline"]:
            return result["exit_code"]
    else:
        identity(source, data, args.generated)
        print("Native title identity and generated artifacts verified.")


if __name__ == "__main__":
    raise SystemExit(main())
