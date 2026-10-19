"""Shared integrity checks and wrapper emission for reviewed native boundaries."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import re
from aot_inventory import parse_xbe

PROFILE_SCHEMA = "native-title-profile.v1"
PROFILE_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PROFILES = (PROFILE_ROOT / "pilots/native-title/profiles/blinx-ntsc-u.json",)


def validate_body(proof):
    """Validate metadata, never treat a name/signature as a body proof."""
    if not isinstance(proof, dict) or not isinstance(proof.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", proof["sha256"]):
        raise ValueError("invalid reviewed body SHA256")
    if "bytes" in proof and (type(proof["bytes"]) is not int or proof["bytes"] <= 0):
        raise ValueError("invalid reviewed body extent")
    if "stack_bytes" in proof and (type(proof["stack_bytes"]) is not int or not 0 <= proof["stack_bytes"] <= 256 or proof["stack_bytes"] % 4):
        raise ValueError("invalid reviewed stdcall cleanup")
    return proof


def load_profile(image_sha256, profile=None):
    """Load exactly one title identity, without enabling any adapter."""
    candidates = (Path(profile),) if profile is not None else DEFAULT_PROFILES
    matches = []
    for path in candidates:
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict) or data.get("schema") != PROFILE_SCHEMA:
            raise ValueError("unknown native title profile schema")
        if not isinstance(data.get("id"), str) or not re.fullmatch(r"[a-z0-9][a-z0-9._-]*", data["id"]):
            raise ValueError("invalid native title profile identity")
        if not isinstance(data.get("xbe_sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", data["xbe_sha256"]):
            raise ValueError("invalid native title profile XBE SHA256")
        if not isinstance(data.get("bindings"), dict):
            raise ValueError("invalid native title binding families")
        if data["xbe_sha256"] == image_sha256:
            matches.append((path, data))
    if len(matches) != 1:
        raise ValueError("expected one reviewed profile for exact XBE SHA256")
    return matches[0]


def has_profile_binding(image_sha256, family, profile=None):
    """Only for elective family selection; required binders still verify proof."""
    _, data = load_profile(image_sha256, profile)
    return family in data["bindings"]


def select_profile(image_sha256, family, profile=None, *, adapter):
    """Select explicit reviewed metadata; an unknown image/family cannot opt in.

    Family includes are data only, confined to this framework checkout or the
    selected profile's own directory. They allow independently reviewed title
    repositories/adapters to maintain their own proof fragment.
    No profile can select/import executable Python code.
    """
    path, data = load_profile(image_sha256, profile)
    proof = data["bindings"].get(family)
    if isinstance(proof, dict) and "include" in proof:
        if set(proof) != {"include"} or not isinstance(proof["include"], str):
            raise ValueError("invalid native title proof include")
        fragment = (path.parent / proof["include"]).resolve()
        if not (fragment.is_relative_to(PROFILE_ROOT) or fragment.is_relative_to(path.parent.resolve())):
            raise ValueError("native title proof include leaves profile/framework roots")
        proof = json.loads(fragment.read_text(encoding="utf-8"))
    adapters = (adapter,) if isinstance(adapter, str) else adapter
    if not isinstance(adapters, tuple) or not adapters or any(not isinstance(a, str) for a in adapters):
        raise ValueError("invalid supported adapter selection")
    if not isinstance(proof, dict) or proof.get("adapter") not in adapters:
        raise ValueError(f"unreviewed {family} adapter for profile {data['id']}")
    return proof


class Binding:
    def __init__(self, xbe: Path, output: Path, symbols: Path):
        self.blob = xbe.read_bytes()
        self.image = parse_xbe(self.blob)
        self.output, self.generated = output, output / "gen"
        self.manifest_path = output / "generation.aot-private.json"
        self.manifest = json.loads(self.manifest_path.read_text())
        if self.manifest["sha256"] != self.image["sha256"]:
            raise ValueError("generated code belongs to a different XBE")
        for name, digest in self.manifest["files"].items():
            if hashlib.sha256((self.generated / name).read_bytes()).hexdigest() != digest:
                raise ValueError(f"generated artifact changed: {name}")
        self.functions = json.loads((output / "disasm/functions.json").read_text())
        self.symbols = json.loads(symbols.read_text())["symbols"]

    def require_profile(self, family, profile=None, *, adapter):
        return select_profile(self.image["sha256"], family, profile, adapter=adapter)

    def matches_body(self, function, proof):
        validate_body(proof)
        return ("bytes" not in proof or function["size"] == proof["bytes"]) and self.digest(function) == proof["sha256"]

    def digest(self, function):
        start, end = int(function["start"], 16), int(function["end"], 16)
        if end - start != function["size"]:
            raise ValueError("inconsistent function extent")
        for section in self.image["sections"]:
            if section["address"] <= start < end <= section["address"] + section["raw_size"]:
                offset = section["raw_offset"] + start - section["address"]
                return hashlib.sha256(self.blob[offset:offset + end - start]).hexdigest()
        return None

    def symbol(self, name, stack_bytes):
        matches = [s for s in self.symbols if s["name"] == name and s["library"] == "XAPILIB"
                   and s.get("call") == "stdcall" and s.get("stack_bytes") == stack_bytes]
        if len(matches) != 1:
            raise ValueError(f"expected one identified {name} with {stack_bytes} stack bytes")
        functions = [f for f in self.functions if int(f["start"], 16) == matches[0]["address"]]
        if len(functions) != 1:
            raise ValueError(f"expected one generated {name} body")
        return functions[0]

    def emit(self, kind, function, original, binding, wrapper_code):
        name = function["name"]
        if not re.fullmatch(r"sub_[0-9A-Fa-f]{8}", name):
            raise ValueError("invalid generated function identifier")
        wrapper = self.generated / f"pilot_{kind}_binding.c"
        record = self.output / f"{kind}-binding.aot-private.json"
        if wrapper.exists():
            if json.loads(record.read_text()) != binding or wrapper.read_text() != wrapper_code:
                raise ValueError("existing binding differs")
            return binding
        needle = f"void {name}(void)\n{{"
        found = [(p, p.read_text(encoding="utf-8")) for p in self.generated.glob("recomp_*.c")]
        found = [(p, code) for p, code in found if needle in code]
        if len(found) != 1 or found[0][1].count(needle) != 1:
            raise ValueError("expected exactly one generated function body")
        path, code = found[0]
        path.write_text(code.replace(needle, f"void {original}(void)\n{{", 1), encoding="utf-8")
        wrapper.write_text(wrapper_code, encoding="utf-8")
        record.write_text(json.dumps(binding, indent=2) + "\n")
        self.manifest[f"{kind}_binding"] = binding
        self.manifest["files"] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in sorted(self.generated.iterdir()) if p.is_file()}
        self.manifest_path.write_text(json.dumps(self.manifest, indent=2) + "\n")
        return binding
