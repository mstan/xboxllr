#!/usr/bin/env python3
"""Bounded IA-32-to-C AOT generator for xboxlle synthetic regions.

This tool intentionally supports only a small, explicit IA-32 subset. It emits
portable C that contains decoded operations, not a runtime opcode decoder.
Unsupported reachable opcodes become terminal AOT exits; malformed supported
forms are rejected at generation time.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

ABI_VERSION = 2
REG_NAMES = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]
NAME_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")


class AotError(ValueError):
    pass


@dataclass(frozen=True)
class Operand:
    kind: str
    value: int
    base: int = -1
    index: int = -1
    scale: int = 1


@dataclass
class DecodedInstruction:
    offset: int
    length: int
    mnemonic: str
    status: str = "supported"
    operands: list[Operand] = field(default_factory=list)
    edges: list[dict[str, Any]] = field(default_factory=list)

    @property
    def end(self) -> int:
        return self.offset + self.length


def u32(v: int) -> int:
    return v & 0xFFFFFFFF


def i8(v: int) -> int:
    return v - 0x100 if v & 0x80 else v


def i32(v: int) -> int:
    return v - 0x100000000 if v & 0x80000000 else v


def read_u8(code: bytes, offset: int, what: str) -> int:
    if offset >= len(code):
        raise AotError(f"truncated {what} at offset {offset}")
    return code[offset]


def read_u32(code: bytes, offset: int, what: str) -> int:
    if offset + 4 > len(code):
        raise AotError(f"truncated {what} at offset {offset}")
    return int.from_bytes(code[offset:offset + 4], "little")


def decode_modrm_reg_only(code: bytes, offset: int, mnemonic: str) -> tuple[int, int, int]:
    modrm = read_u8(code, offset, f"{mnemonic} ModRM")
    if (modrm & 0xC0) != 0xC0:
        return modrm, -1, -1
    reg = (modrm >> 3) & 7
    rm = modrm & 7
    return modrm, reg, rm


def branch_edge(kind: str, target: int, next_offset: int, size: int) -> list[dict[str, Any]]:
    edges: list[dict[str, Any]] = [{"kind": kind, "target": target}]
    if kind in ("je", "jne"):
        edges.append({"kind": "fallthrough", "target": next_offset})
    for e in edges:
        t = e["target"]
        e["internal"] = 0 <= t < size
    return edges


def decode_memory32(code: bytes, modrm_offset: int) -> tuple[Operand, int]:
    """32-bit ModRM/SIB address only; callers admit a specific memory opcode."""
    modrm = read_u8(code, modrm_offset, "memory ModRM")
    mod, base = modrm >> 6, modrm & 7
    if mod == 3:
        raise AotError("memory decoder received a register operand")
    end, index, scale, displacement = modrm_offset + 1, -1, 1, 0
    if base == 4:
        sib = read_u8(code, end, "memory SIB")
        end += 1
        base, index, scale = sib & 7, (sib >> 3) & 7, 1 << (sib >> 6)
        if index == 4:
            index = -1
    if mod == 0 and base == 5:
        base = -1
        displacement = read_u32(code, end, "absolute memory displacement")
        end += 4
    elif mod == 1:
        displacement = u32(i8(read_u8(code, end, "signed memory displacement")))
        end += 1
    elif mod == 2:
        displacement = read_u32(code, end, "memory displacement")
        end += 4
    return Operand("mem32", displacement, base, index, scale), end


def decode_one(code: bytes, offset: int) -> DecodedInstruction:
    if offset < 0 or offset >= len(code):
        raise AotError(f"decode offset outside region: {offset}")
    op = code[offset]

    if 0xB8 <= op <= 0xBF:
        imm = read_u32(code, offset + 1, "mov r32,imm32")
        return DecodedInstruction(offset, 5, "mov_r32_imm32", operands=[Operand("reg32", op - 0xB8), Operand("imm32", imm)])
    if 0xB0 <= op <= 0xB7:
        imm = read_u8(code, offset + 1, "mov r8,imm8")
        return DecodedInstruction(offset, 2, "mov_r8_imm8", operands=[Operand("reg8", op - 0xB0), Operand("imm8", imm)])
    if op in (0x89, 0x8B, 0x88, 0x8A, 0x01, 0x29, 0x31, 0x21, 0x39):
        modrm, reg, rm = decode_modrm_reg_only(code, offset + 1, f"opcode 0x{op:02x}")
        if reg < 0:
            if op in (0x89, 0x8B):
                memory, end = decode_memory32(code, offset + 1)
                name = "mov_r32_to_mem32" if op == 0x89 else "mov_mem32_to_r32"
                return DecodedInstruction(offset, end - offset, name,
                                          operands=[Operand("reg32", (modrm >> 3) & 7), memory])
            return DecodedInstruction(offset, 2, f"unsupported_0x{op:02x}_modrm_0x{modrm:02x}", status="unsupported")
        mapping = {
            0x89: "mov_r32_rm32_to_rm32",
            0x8B: "mov_rm32_to_r32",
            0x88: "mov_r8_rm8_to_rm8",
            0x8A: "mov_rm8_to_r8",
            0x01: "add_r32_to_rm32",
            0x29: "sub_r32_from_rm32",
            0x31: "xor_r32_to_rm32",
            0x21: "and_r32_to_rm32",
            0x39: "cmp_r32_rm32",
        }
        width = "reg8" if op in (0x88, 0x8A) else "reg32"
        return DecodedInstruction(offset, 2, mapping[op], operands=[Operand(width, reg), Operand(width, rm)])
    if op == 0x83:
        modrm = read_u8(code, offset + 1, "group-1 ModRM")
        if modrm >> 6 == 3 and ((modrm >> 3) & 7) == 5:
            immediate = u32(i8(read_u8(code, offset + 2, "SUB sign-extended immediate")))
            return DecodedInstruction(offset, 3, "sub_r32_imm32",
                                      operands=[Operand("reg32", modrm & 7), Operand("imm32", immediate)])
        return DecodedInstruction(offset, 2, "unsupported_group1", status="unsupported")
    if op in (0x05, 0x2D, 0x35, 0x25, 0x3D):
        imm = read_u32(code, offset + 1, f"opcode 0x{op:02x} imm32")
        mapping = {
            0x05: "add_eax_imm32",
            0x2D: "sub_eax_imm32",
            0x35: "xor_eax_imm32",
            0x25: "and_eax_imm32",
            0x3D: "cmp_eax_imm32",
        }
        return DecodedInstruction(offset, 5, mapping[op], operands=[Operand("imm32", imm)])
    if 0x40 <= op <= 0x47:
        return DecodedInstruction(offset, 1, "inc_r32", operands=[Operand("reg32", op - 0x40)])
    if 0x48 <= op <= 0x4F:
        return DecodedInstruction(offset, 1, "dec_r32", operands=[Operand("reg32", op - 0x48)])
    if op == 0x90:
        return DecodedInstruction(offset, 1, "nop")
    if op == 0xA1:
        addr = read_u32(code, offset + 1, "mov eax,moffs32")
        return DecodedInstruction(offset, 5, "mov_eax_moffs32", operands=[Operand("moffs32", addr)])
    if op == 0xA3:
        addr = read_u32(code, offset + 1, "mov moffs32,eax")
        return DecodedInstruction(offset, 5, "mov_moffs32_eax", operands=[Operand("moffs32", addr)])
    if op in (0x74, 0x75):
        rel = i8(read_u8(code, offset + 1, "jcc rel8"))
        target = offset + 2 + rel
        mnemonic = "je_rel8" if op == 0x74 else "jne_rel8"
        kind = "je" if op == 0x74 else "jne"
        inst = DecodedInstruction(offset, 2, mnemonic, operands=[Operand("rel8", rel)])
        inst.edges = branch_edge(kind, target, offset + 2, len(code))
        return inst
    if op == 0xEB:
        rel = i8(read_u8(code, offset + 1, "jmp rel8"))
        target = offset + 2 + rel
        inst = DecodedInstruction(offset, 2, "jmp_rel8", operands=[Operand("rel8", rel)])
        inst.edges = branch_edge("jmp", target, offset + 2, len(code))
        return inst
    if op == 0xE9:
        rel = i32(read_u32(code, offset + 1, "jmp rel32"))
        target = offset + 5 + rel
        inst = DecodedInstruction(offset, 5, "jmp_rel32", operands=[Operand("rel32", rel)])
        inst.edges = branch_edge("jmp", target, offset + 5, len(code))
        return inst

    return DecodedInstruction(offset, 1, f"unsupported_0x{op:02x}", status="unsupported")


def default_fallthrough(inst: DecodedInstruction, size: int) -> list[dict[str, Any]]:
    if inst.status != "supported":
        return []
    if inst.edges:
        return inst.edges
    target = inst.end
    if target == size:
        return [{"kind": "complete", "target": target, "internal": False}]
    return [{"kind": "fallthrough", "target": target, "internal": 0 <= target < size}]


def decode_reachable(code: bytes, entries: list[int]) -> dict[int, DecodedInstruction]:
    decoded: dict[int, DecodedInstruction] = {}
    occupied: dict[int, int] = {}
    worklist = deque(entries)
    queued = set(entries)

    while worklist:
        offset = worklist.popleft()
        if offset < 0 or offset > len(code):
            raise AotError(f"entry/branch target outside representable region: {offset}")
        if offset == len(code):
            continue
        if offset in decoded:
            continue
        if offset in occupied and occupied[offset] != offset:
            raise AotError(f"instruction entry at offset {offset} overlaps instruction at {occupied[offset]}")

        inst = decode_one(code, offset)
        for b in range(inst.offset, inst.end):
            previous = occupied.get(b)
            if previous is not None and previous != inst.offset:
                raise AotError(f"instruction at offset {inst.offset} overlaps instruction at {previous}")
        for b in range(inst.offset, inst.end):
            occupied[b] = inst.offset

        inst.edges = default_fallthrough(inst, len(code))
        decoded[offset] = inst
        if inst.status == "unsupported":
            continue
        for edge in inst.edges:
            target = int(edge["target"])
            if edge.get("internal") and target not in queued:
                queued.add(target)
                worklist.append(target)
    return decoded


def parse_region(raw: dict[str, Any]) -> dict[str, Any]:
    if not isinstance(raw, dict):
        raise AotError("region must be an object")
    name = raw.get("name")
    if not isinstance(name, str) or not NAME_RE.fullmatch(name):
        raise AotError(f"malformed region name: {name!r}")
    base = raw.get("base")
    if type(base) is not int or base < 0 or base > 0xFFFFFFFF:
        raise AotError(f"region {name}: base must be uint32")
    code_hex = raw.get("code")
    if not isinstance(code_hex, str) or len(code_hex) % 2:
        raise AotError(f"region {name}: code must be even-length hex string")
    try:
        code = bytes.fromhex(code_hex)
    except ValueError as exc:
        raise AotError(f"region {name}: code is not valid hex") from exc
    if not code:
        raise AotError(f"region {name}: code is empty")
    if len(code) > 65536:
        raise AotError(f"region {name}: prototype code limit is 65536 bytes")
    if base + len(code) > 0xFFFFFFFF:
        raise AotError(f"region {name}: base+code_size exceeds uint32")
    entries_raw = raw.get("entries", [0])
    if not isinstance(entries_raw, list) or not entries_raw:
        raise AotError(f"region {name}: entries must be a non-empty list")
    entries: list[int] = []
    for e in entries_raw:
        if type(e) is not int or e < 0 or e >= len(code):
            raise AotError(f"region {name}: entry must be an offset inside code: {e!r}")
        if e not in entries:
            entries.append(e)
    decoded = decode_reachable(code, entries)
    return {"name": name, "base": base, "code": code, "entries": entries, "decoded": decoded}


def operand_manifest(op: Operand) -> dict[str, Any]:
    if op.kind == "mem32":
        return {"kind": op.kind, "value": op.value,
                "base": op.base, "index": op.index, "scale": op.scale}
    return {"kind": op.kind, "value": op.value}


def manifest_for(input_bytes: bytes, regions: list[dict[str, Any]]) -> dict[str, Any]:
    out_regions = []
    for r in regions:
        decoded = []
        for offset in sorted(r["decoded"]):
            inst: DecodedInstruction = r["decoded"][offset]
            decoded.append({
                "offset": inst.offset,
                # Unsupported forms record only the bytes inspected, not a
                # guessed architectural instruction length for future dispatch.
                "length": inst.length if inst.status == "supported" else None,
                "examined_length": inst.length,
                "mnemonic": inst.mnemonic,
                "status": inst.status,
                "operands": [operand_manifest(o) for o in inst.operands],
                "edges": inst.edges,
            })
        out_regions.append({
            "name": r["name"],
            "base": r["base"],
            "code_size": len(r["code"]),
            "code_sha256": hashlib.sha256(r["code"]).hexdigest(),
            "context": "flat32_ram_v1",
            "entries": r["entries"],
            "decoded": decoded,
        })
    return {"version": 1, "abi_version": ABI_VERSION,
            "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "input_sha256": hashlib.sha256(input_bytes).hexdigest(), "regions": out_regions}


def c_ident(name: str) -> str:
    if not NAME_RE.fullmatch(name):
        raise AotError(f"bad identifier {name!r}")
    return name


def hex_bytes_initializer(data: bytes) -> str:
    chunks = []
    for i in range(0, len(data), 12):
        part = ", ".join(f"0x{b:02x}" for b in data[i:i + 12])
        chunks.append("    " + part)
    return ",\n".join(chunks)


def reg_expr(reg: int) -> str:
    return f"state->gpr[{reg}u]"


def address_expr(op: Operand) -> str:
    parts = [f"0x{op.value:08x}u"]
    if op.base >= 0:
        parts.append(reg_expr(op.base))
    if op.index >= 0:
        parts.append(f"({reg_expr(op.index)} * {op.scale}u)")
    return "(uint32_t)(" + " + ".join(parts) + ")"


def emit_set_eip_retire(next_eip: int) -> list[str]:
    return [f"            state->eip = 0x{next_eip:08x}u;", "            ++state->retired;"]


def emit_supported_body(inst: DecodedInstruction, region: dict[str, Any]) -> list[str]:
    base = int(region["base"])
    next_eip = u32(base + inst.end)
    ops = inst.operands
    m = inst.mnemonic
    lines: list[str] = []

    def finish(target: int | None = None) -> list[str]:
        return emit_set_eip_retire(next_eip if target is None else u32(base + target)) + ["            continue;"]

    if m == "mov_mem32_to_r32":
        lines.append("            uint32_t value = 0;")
        lines.append(f"            if (!xlle_aot_read32(bus, {address_expr(ops[1])}, &value)) return XLLE_AOT_MEMORY;")
        lines.append(f"            {reg_expr(ops[0].value)} = value;")
        lines += finish()
    elif m == "mov_r32_to_mem32":
        lines.append(f"            if (!xlle_aot_write32(bus, {address_expr(ops[1])}, {reg_expr(ops[0].value)})) return XLLE_AOT_MEMORY;")
        lines += finish()
    elif m == "sub_r32_imm32":
        reg = reg_expr(ops[0].value)
        lines.append(f"            {reg} = xlle_aot_sub(state, {reg}, 0x{ops[1].value:08x}u);")
        lines += finish()
    elif m == "mov_r32_imm32":
        lines.append(f"            {reg_expr(ops[0].value)} = 0x{ops[1].value:08x}u;")
        lines += finish()
    elif m == "mov_r8_imm8":
        lines.append(f"            xlle_aot_set8(state, {ops[0].value}u, 0x{ops[1].value:02x}u);")
        lines += finish()
    elif m == "mov_r32_rm32_to_rm32":
        lines.append(f"            {reg_expr(ops[1].value)} = {reg_expr(ops[0].value)};")
        lines += finish()
    elif m == "mov_rm32_to_r32":
        lines.append(f"            {reg_expr(ops[0].value)} = {reg_expr(ops[1].value)};")
        lines += finish()
    elif m == "mov_r8_rm8_to_rm8":
        lines.append(f"            xlle_aot_set8(state, {ops[1].value}u, xlle_aot_get8(state, {ops[0].value}u));")
        lines += finish()
    elif m == "mov_rm8_to_r8":
        lines.append(f"            xlle_aot_set8(state, {ops[0].value}u, xlle_aot_get8(state, {ops[1].value}u));")
        lines += finish()
    elif m == "add_r32_to_rm32":
        lines.append(f"            {reg_expr(ops[1].value)} = xlle_aot_add(state, {reg_expr(ops[1].value)}, {reg_expr(ops[0].value)});")
        lines += finish()
    elif m == "sub_r32_from_rm32":
        lines.append(f"            {reg_expr(ops[1].value)} = xlle_aot_sub(state, {reg_expr(ops[1].value)}, {reg_expr(ops[0].value)});")
        lines += finish()
    elif m == "xor_r32_to_rm32":
        lines.append(f"            {reg_expr(ops[1].value)} = xlle_aot_logic(state, {reg_expr(ops[1].value)} ^ {reg_expr(ops[0].value)});")
        lines += finish()
    elif m == "and_r32_to_rm32":
        lines.append(f"            {reg_expr(ops[1].value)} = xlle_aot_logic(state, {reg_expr(ops[1].value)} & {reg_expr(ops[0].value)});")
        lines += finish()
    elif m == "cmp_r32_rm32":
        lines.append(f"            (void)xlle_aot_sub(state, {reg_expr(ops[1].value)}, {reg_expr(ops[0].value)});")
        lines += finish()
    elif m == "add_eax_imm32":
        lines.append(f"            state->gpr[0] = xlle_aot_add(state, state->gpr[0], 0x{ops[0].value:08x}u);")
        lines += finish()
    elif m == "sub_eax_imm32":
        lines.append(f"            state->gpr[0] = xlle_aot_sub(state, state->gpr[0], 0x{ops[0].value:08x}u);")
        lines += finish()
    elif m == "xor_eax_imm32":
        lines.append(f"            state->gpr[0] = xlle_aot_logic(state, state->gpr[0] ^ 0x{ops[0].value:08x}u);")
        lines += finish()
    elif m == "and_eax_imm32":
        lines.append(f"            state->gpr[0] = xlle_aot_logic(state, state->gpr[0] & 0x{ops[0].value:08x}u);")
        lines += finish()
    elif m == "cmp_eax_imm32":
        lines.append(f"            (void)xlle_aot_sub(state, state->gpr[0], 0x{ops[0].value:08x}u);")
        lines += finish()
    elif m == "inc_r32":
        lines.append(f"            {reg_expr(ops[0].value)} = xlle_aot_inc(state, {reg_expr(ops[0].value)});")
        lines += finish()
    elif m == "dec_r32":
        lines.append(f"            {reg_expr(ops[0].value)} = xlle_aot_dec(state, {reg_expr(ops[0].value)});")
        lines += finish()
    elif m == "nop":
        lines += finish()
    elif m == "mov_eax_moffs32":
        lines.append("            uint32_t value = 0;")
        lines.append(f"            if (!xlle_aot_read32(bus, 0x{ops[0].value:08x}u, &value)) return XLLE_AOT_MEMORY;")
        lines.append("            state->gpr[0] = value;")
        lines += finish()
    elif m == "mov_moffs32_eax":
        lines.append(f"            if (!xlle_aot_write32(bus, 0x{ops[0].value:08x}u, state->gpr[0])) return XLLE_AOT_MEMORY;")
        lines += finish()
    elif m in ("je_rel8", "jne_rel8"):
        target = next(e["target"] for e in inst.edges if e["kind"] in ("je", "jne"))
        cond = "(state->eflags & 0x40u)" if m == "je_rel8" else "!(state->eflags & 0x40u)"
        lines.append(f"            const uint32_t target = 0x{u32(base + int(target)):08x}u;")
        lines.append(f"            if ({cond} && !xlle_aot_branch_allowed(bus, target)) return XLLE_AOT_CONTEXT;")
        lines.append(f"            state->eip = {cond} ? target : 0x{next_eip:08x}u;")
        lines.append("            ++state->retired;")
        lines.append("            continue;")
    elif m in ("jmp_rel8", "jmp_rel32"):
        target = int(inst.edges[0]["target"])
        lines.append(f"            if (!xlle_aot_branch_allowed(bus, 0x{u32(base + target):08x}u)) return XLLE_AOT_CONTEXT;")
        lines += finish(target)
    else:
        raise AotError(f"internal missing emitter for {m}")
    return lines


def emit_region_c(region: dict[str, Any]) -> str:
    name = c_ident(region["name"])
    base = int(region["base"])
    code: bytes = region["code"]
    decoded: dict[int, DecodedInstruction] = region["decoded"]
    lines: list[str] = []
    lines.append(f"static const uint8_t xlle_aot_{name}_code[{len(code)}] = {{")
    lines.append(hex_bytes_initializer(code))
    lines.append("};")
    lines.append("")
    lines.append(f"static xlle_aot_exit xlle_aot_{name}_run(const xlle_aot_region* region, xlle_aot_state* state, const xlle_aot_bus* bus, uint64_t budget) {{")
    lines.append(f"    if (!state || region != &xlle_aot_{name}) return XLLE_AOT_CONTEXT;")
    lines.append("    const uint64_t start = state->retired;")
    lines.append("    for (;;) {")
    lines.append("        xlle_aot_exit guard = xlle_aot_check(region, state, bus, budget, start);")
    lines.append("        if (guard != XLLE_AOT_CONTINUE) return guard;")
    lines.append("        switch (state->eip) {")
    for offset in sorted(decoded):
        inst = decoded[offset]
        lines.append(f"        case 0x{u32(base + offset):08x}u: {{ /* +0x{offset:x} {inst.mnemonic} */")
        if inst.status == "unsupported":
            lines.append("            return XLLE_AOT_UNSUPPORTED;")
        else:
            lines.extend(emit_supported_body(inst, region))
        lines.append("        }")
    lines.append("        default:")
    lines.append("            return XLLE_AOT_INVALID_ENTRY;")
    lines.append("        }")
    lines.append("    }")
    lines.append("}")
    lines.append("")
    lines.append(f"const xlle_aot_region xlle_aot_{name} = {{")
    lines.append(f"    {ABI_VERSION}u,")
    lines.append(f"    0x{base:08x}u,")
    lines.append(f"    {len(code)}u,")
    lines.append(f"    xlle_aot_{name}_code,")
    lines.append(f"    xlle_aot_{name}_run")
    lines.append("};")
    return "\n".join(lines)


def emit_c(regions: list[dict[str, Any]], header_name: str, private: bool = False) -> str:
    parts = [
        "/* Generated by tools/aot_compile.py. Do not edit by hand. */",
        f"#include \"{header_name}\"",
        "",
    ]
    for r in regions:
        parts.append(emit_region_c(r))
        parts.append("")
    if private:
        parts.append("const xlle_aot_region* const xlle_aot_private_regions[] = {")
        parts.extend(f"    &xlle_aot_{c_ident(r['name'])}," for r in regions)
        parts.append("};")
        parts.append(f"const size_t xlle_aot_private_region_count = {len(regions)}u;")
    return "\n".join(parts)


def emit_h(regions: list[dict[str, Any]], guard: str, private: bool = False) -> str:
    lines = [
        "/* Generated by tools/aot_compile.py. Do not edit by hand. */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include \"xlle/aot_runtime.h\"",
        "",
        "#ifdef __cplusplus",
        "extern \"C\" {",
        "#endif",
        "",
    ]
    for r in regions:
        lines.append(f"extern const xlle_aot_region xlle_aot_{c_ident(r['name'])};")
    if private:
        lines.append("extern const xlle_aot_region* const xlle_aot_private_regions[];")
        lines.append("extern const size_t xlle_aot_private_region_count;")
    lines += [
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
        f"#endif /* {guard} */",
        "",
    ]
    return "\n".join(lines)


def output_header_include(output_h: Path) -> str:
    return output_h.name


def include_guard_for(path: Path) -> str:
    stem = re.sub(r"[^A-Za-z0-9]", "_", path.name).upper()
    return f"XLLE_GENERATED_{stem}"


def compile_input(input_path: Path, output_c: Path, output_h: Path, manifest_path: Path) -> None:
    paths = [p.resolve() for p in (input_path, output_c, output_h, manifest_path)]
    if len(set(paths)) != len(paths):
        raise AotError("input and output paths must be distinct")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", output_h.name):
        raise AotError("output header name must be safe for a C include")
    input_bytes = input_path.read_bytes()
    try:
        doc = json.loads(input_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise AotError(f"input is not valid UTF-8 JSON: {exc}") from exc
    if not isinstance(doc, dict):
        raise AotError("input root must be an object")
    independent = doc.get("independent_regions", False)
    if type(independent) is not bool:
        raise AotError("independent_regions must be boolean")
    raw_regions = doc.get("regions")
    if not isinstance(raw_regions, list) or not raw_regions:
        raise AotError("input must contain a non-empty regions array")
    seen_names: set[str] = set()
    seen_symbols: set[str] = set()
    seen_ranges: list[tuple[int, int, str]] = []
    regions = []
    for raw in raw_regions:
        region = parse_region(raw)
        if region["name"] in seen_names:
            raise AotError(f"duplicate region name: {region['name']}")
        symbols = {f"xlle_aot_{region['name']}{suffix}" for suffix in ("", "_code", "_run")}
        if symbols & seen_symbols:
            raise AotError(f"generated symbol collision for region {region['name']}")
        seen_symbols.update(symbols)
        start_addr = region["base"]
        end_addr = region["base"] + len(region["code"])
        for other_start, other_end, other_name in seen_ranges:
            if not independent and start_addr < other_end and other_start < end_addr:
                raise AotError(f"region {region['name']} overlaps region {other_name}")
        seen_names.add(region["name"])
        seen_ranges.append((start_addr, end_addr, region["name"]))
        regions.append(region)

    manifest = manifest_for(input_bytes, regions)
    manifest["independent_regions"] = independent
    private = doc.get("private_guest_input", False)
    if type(private) is not bool:
        raise AotError("private_guest_input must be boolean")
    manifest["private_guest_input"] = private
    if private:
        if doc.get("source_format") != "xboxlle.blockpack.v1":
            raise AotError("private_guest_input requires source_format=xboxlle.blockpack.v1")
        source_sha256 = doc.get("source_sha256")
        if not isinstance(source_sha256, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", source_sha256):
            raise AotError("private_guest_input requires a 64-hex source_sha256")
        manifest["source_format"] = "xboxlle.blockpack.v1"
        manifest["source_sha256"] = source_sha256.lower()
    marker = "/* XLLE_" + "PRIVATE_GUEST_ARTIFACT */\n" if private else ""
    output_h.parent.mkdir(parents=True, exist_ok=True)
    output_c.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    output_h.write_text(marker + emit_h(regions, include_guard_for(output_h), private), encoding="utf-8", newline="\n")
    output_c.write_text(marker + emit_c(regions, output_header_include(output_h), private), encoding="utf-8", newline="\n")
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="Generate bounded portable C AOT regions from synthetic IA-32 JSON")
    ap.add_argument("--input", required=True, type=Path)
    ap.add_argument("--output-c", required=True, type=Path)
    ap.add_argument("--output-h", required=True, type=Path)
    ap.add_argument("--manifest", required=True, type=Path)
    return ap.parse_args(list(argv))


def main(argv: Iterable[str] | None = None) -> int:
    ns = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        compile_input(ns.input, ns.output_c, ns.output_h, ns.manifest)
    except (AotError, OSError) as exc:
        print(f"aot_compile: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
