"""Shared integrity checks and wrapper emission for reviewed native boundaries."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import re
from aot_inventory import parse_xbe


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
