#!/usr/bin/env python3
"""Inventory an owner-supplied XBE without copying code or certificate secrets.

Recursive IA-32 discovery is evidence, not proof of all possible code. Section
execute flags are candidates only. Indirect transfers, decoding conflicts and
unvisited bytes remain explicit. No donor runtime or kernel replacement is used.
"""
from __future__ import annotations

import argparse
from collections import Counter, deque
import hashlib
import json
from pathlib import Path
import struct

from xbe_identity import parse_identity


def sha(data):
    return hashlib.sha256(data).hexdigest()


def span(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError("XBE range outside file")
    return data[offset:offset + size]


def words(data, offset, count=1):
    return struct.unpack("<" + "I" * count, span(data, offset, count * 4))


def parse_xbe(data):
    if span(data, 0, 4) != b"XBEH":
        raise ValueError("expected an XBE image")
    base, headers, image_size = words(data, 0x104, 3)
    if headers < 0x178 or headers > len(data) or base + image_size > 0x100000000:
        raise ValueError("invalid XBE header/image extent")
    cert, count, table = words(data, 0x118, 3)
    if not 1 <= count <= 4096 or table < base or table - base + count * 56 > headers:
        raise ValueError("invalid XBE section table")
    sections = []
    for index in range(count):
        flags, address, size, raw, raw_size, name_va = words(data, table - base + index * 56, 6)
        if not size or raw_size > size or address < base or address + size > base + image_size:
            raise ValueError("invalid XBE section extent")
        payload = span(data, raw, raw_size)
        name_offset = name_va - base
        if name_offset < 0 or name_offset >= headers:
            raise ValueError("section name outside headers")
        name_bytes = span(data, name_offset, min(64, headers - name_offset))
        if b"\0" not in name_bytes:
            raise ValueError("unterminated section name")
        name = name_bytes.split(b"\0", 1)[0].decode("ascii", errors="replace")
        sections.append(dict(index=index, name=name, address=address, size=size,
                             raw_offset=raw, raw_size=raw_size, flags=flags,
                             executable_candidate=bool(flags & 4), preload=bool(flags & 2),
                             raw_sha256=sha(payload)))
    ordered = sorted(sections, key=lambda s: s["address"])
    for left, right in zip(ordered, ordered[1:]):
        if left["address"] + left["size"] > right["address"]:
            raise ValueError("overlapping XBE virtual sections")
    entry_raw = words(data, 0x128)[0]
    entries = [(kind, entry_raw ^ key) for kind, key in
               (("retail", 0xA8FC57AB), ("debug", 0x94859D4B))]
    entries = [(kind, va) for kind, va in entries if any(
        s["executable_candidate"] and s["address"] <= va < s["address"] + s["raw_size"]
        for s in sections)]
    if len(entries) != 1:
        raise ValueError("entry encoding is ambiguous or has no backed executable section")
    # Public title and launch constraints only, never LAN/signature keys.
    if cert < base or cert - base + 0xa4 > headers:
        raise ValueError("certificate identity outside headers")
    identity = parse_identity(data)
    allowed_media, region = words(data, cert - base + 0x9c, 2)
    libraries = []
    lib_count, lib_table = words(data, 0x160, 2)
    if lib_count > 4096 or (lib_count and (lib_table < base or lib_table - base + 16 * lib_count > headers)):
        raise ValueError("invalid XBE library table")
    for index in range(lib_count):
        record = span(data, lib_table - base + index * 16, 16)
        major, minor, build, flags = struct.unpack_from("<HHHH", record, 8)
        libraries.append(dict(name=record[:8].split(b"\0", 1)[0].decode("ascii", errors="replace"),
                              major=major, minor=minor, build=build))
    return dict(format="xbe", sha256=sha(data), base=base, image_size=image_size,
                **identity, allowed_media=allowed_media, region=region,
                entry=entries[0][1], encoding=entries[0][0],
                sections=sections, libraries=libraries)


def discover(data, image, seeds=(), limit=500000):
    # Certificate/profile setup only needs parse_xbe; keep the compiler out
    # of the portable player's import graph.
    import aot_compile
    import capstone as cs
    from capstone.x86_const import X86_OP_IMM
    if limit < 1:
        raise ValueError("instruction limit must be positive")
    decoder = cs.Cs(cs.CS_ARCH_X86, cs.CS_MODE_32)
    decoder.detail = True
    sections = image["sections"]

    def locate(va):
        return next((s for s in sections if s["executable_candidate"] and
                     s["address"] <= va < s["address"] + s["raw_size"]), None)

    roots = [{"address": image["entry"], "evidence": "image-entry"}]
    for seed in sorted(set(seeds)):
        if not locate(seed):
            raise ValueError(f"seed 0x{seed:08x} has no backed executable section")
        if seed != image["entry"]:
            roots.append({"address": seed, "evidence": "explicit-seed"})
    pending = deque(r["address"] for r in roots)
    queued = set(pending)
    occupied, decoded, edges, gaps = {}, {}, [], []
    while pending and len(decoded) < limit:
        pc = pending.popleft()
        if pc in decoded:
            continue
        section = locate(pc)
        if section is None:
            gaps.append(dict(address=pc, reason="target-outside-backed-executable-sections"))
            continue
        if pc in occupied:
            gaps.append(dict(address=pc, reason="overlapping-instruction-entry", owner=occupied[pc]))
            continue
        offset = section["raw_offset"] + pc - section["address"]
        size = min(15, section["address"] + section["raw_size"] - pc)
        insn = next(decoder.disasm(data[offset:offset + size], pc, count=1), None)
        if insn is None:
            gaps.append(dict(address=pc, reason="undecodable"))
            continue
        if any(at in occupied for at in range(pc, pc + insn.size)):
            gaps.append(dict(address=pc, reason="overlapping-instruction-body"))
            continue
        try:
            native = aot_compile.decode_one(bytes(insn.bytes), 0)
            supported = native.status == "supported" and native.length == insn.size
        except aot_compile.AotError:
            supported = False
        decoded[pc] = dict(address=pc, size=insn.size, mnemonic=insn.mnemonic,
                           lowering_supported=supported)
        occupied.update((at, pc) for at in range(pc, pc + insn.size))
        transfers = []
        call = insn.group(cs.CS_GRP_CALL)
        jump = insn.group(cs.CS_GRP_JUMP)
        terminal = insn.group(cs.CS_GRP_RET) or insn.group(cs.CS_GRP_IRET)
        if call or jump:
            kind = "call" if call else "branch"
            if len(insn.operands) == 1 and insn.operands[0].type == X86_OP_IMM:
                transfers.append((kind, insn.operands[0].imm & 0xffffffff))
            else:
                gaps.append(dict(address=pc, reason="unresolved-indirect-" + kind))
        if terminal:
            edges.append(dict(source=pc, kind="dynamic-return", target=None))
        elif insn.mnemonic in ("ud2", "hlt", "int", "int1", "int3", "sysenter", "sysexit"):
            gaps.append(dict(address=pc, reason="system-or-exception-transfer"))
        elif not jump or insn.mnemonic not in ("jmp", "ljmp"):
            transfers.append(("call-continuation" if call else "fallthrough", pc + insn.size))
        for kind, target in transfers:
            edges.append(dict(source=pc, kind=kind, target=target))
            if target not in queued:
                pending.append(target)
                queued.add(target)
    unvisited = []
    for section in sections:
        if not section["executable_candidate"]:
            continue
        cursor = section["address"]
        end = cursor + section["size"]
        for pc in sorted(p for p in decoded if cursor <= p < end):
            if pc > cursor:
                unvisited.append(dict(address=cursor, size=pc - cursor, section=section["index"]))
            cursor = pc + decoded[pc]["size"]
        if cursor < end:
            unvisited.append(dict(address=cursor, size=end - cursor, section=section["index"]))
    return dict(decoder=dict(package="capstone", version=cs.__version__, engine=list(cs.cs_version())),
                roots=roots, instructions=[decoded[p] for p in sorted(decoded)], edges=edges,
                gaps=gaps, pending_addresses=list(pending), unclassified_ranges=unvisited,
                summary=dict(recovered_instructions=len(decoded), recovered_bytes=len(occupied),
                             lowering_supported_instructions=sum(i["lowering_supported"] for i in decoded.values()),
                             gap_reasons=dict(sorted(Counter(g["reason"] for g in gaps).items())),
                             unclassified_bytes=sum(r["size"] for r in unvisited),
                             limit_reached=bool(pending), whole_program_complete=False))


def inventory(path, seeds=(), limit=500000):
    data = path.read_bytes()
    image = parse_xbe(data)
    return dict(schema="xboxlle.aot-inventory.v1", image=image,
                tool_sha256=sha(Path(__file__).read_bytes()),
                lowering_sha256=sha(Path(aot_compile.__file__).read_bytes()),
                discovery=discover(data, image, seeds, limit))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seed", type=lambda v: int(v, 0), action="append", default=[])
    parser.add_argument("--max-instructions", type=int, default=500000)
    args = parser.parse_args()
    try:
        if args.output.resolve() == args.image.resolve():
            raise ValueError("output must not overwrite the image")
        if not args.output.name.endswith(".aot-private.json"):
            raise ValueError("inventory output must use the guarded .aot-private.json suffix")
        result = inventory(args.image, args.seed, args.max_instructions)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    except (OSError, ValueError, ImportError) as error:
        parser.error(str(error))
    print(json.dumps(dict(image_sha256=result["image"]["sha256"], **result["discovery"]["summary"])))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
