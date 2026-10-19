"""Recover script-method annotations from an owner's pinned dashboard XBE.

Names and function pointers come from the owned binary's FND records, including
literal constructor stores. Theseus documents the 24-byte record format; its
address/name table and implementation bodies are not imported. The output is
private evidence, not an assumption that every same-version address matches.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG


class Image:
    def __init__(self, data: bytes):
        self.data = data
        if len(data) < 0x124 or data[:4] != b"XBEH":
            raise ValueError("not an XBE")
        self.base, header = struct.unpack_from("<II", data, 0x104)
        count, table = struct.unpack_from("<II", data, 0x11C)
        at = table - self.base
        if not 0 < count <= 128 or at < 0 or at + count * 56 > min(header, len(data)):
            raise ValueError("invalid XBE section table")
        self.sections = []
        for i in range(count):
            flags, va, size, raw, length = struct.unpack_from("<IIIII", data, at + i * 56)
            if raw + length > len(data) or length > size or va + size > 0x100000000:
                raise ValueError("invalid XBE section extent")
            self.sections.append((flags, va, size, raw, length))

    def section(self, va, size=1):
        return next((s for s in self.sections if s[1] <= va and va + size <= s[1] + s[2]), None)

    def read(self, va, size):
        section = self.section(va, size)
        if section is None:
            raise ValueError("address outside XBE sections")
        _, base, _, raw, length = section
        offset = va - base
        available = min(size, max(0, length - offset))
        return self.data[raw + offset:raw + offset + available] + bytes(size - available)

    def executable(self, va, size=1):
        section = self.section(va, size)
        return section is not None and bool(section[0] & 4) and va + size <= section[1] + section[4]


def recover(image: Image):
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    decoder.skipdata = True
    stores = {}
    ambiguous = set()
    families = {}
    for family, aliases in (('eax', ('eax', 'ax', 'al', 'ah')), ('ebx', ('ebx', 'bx', 'bl', 'bh')),
                            ('ecx', ('ecx', 'cx', 'cl', 'ch')), ('edx', ('edx', 'dx', 'dl', 'dh')),
                            ('esi', ('esi', 'si')), ('edi', ('edi', 'di')),
                            ('ebp', ('ebp', 'bp')), ('esp', ('esp', 'sp'))):
        for alias in aliases:
            families[alias] = family
    for flags, va, _, raw, size in image.sections:
        if not flags & 4:
            continue
        constants = {}
        for instruction in decoder.disasm(image.data[raw:raw + size], va):
            if not instruction.id:
                constants.clear()
                continue
            operands = instruction.operands
            value = None
            if instruction.mnemonic == 'mov' and len(operands) == 2:
                source = operands[1]
                if source.type == X86_OP_IMM:
                    value = source.imm & 0xFFFFFFFF
                elif source.type == X86_OP_REG:
                    name = instruction.reg_name(source.reg)
                    parent = families.get(name)
                    if parent in constants:
                        value = constants[parent]
                        if name.endswith('h'):
                            value >>= 8
                        value &= (1 << (source.size * 8)) - 1
                destination = operands[0]
                if (value is not None and destination.type == X86_OP_MEM and
                    not destination.mem.base and not destination.mem.index and destination.size in (1, 2, 4)):
                    target = destination.mem.disp & 0xFFFFFFFF
                    section = image.section(target, destination.size)
                    if section is not None and section[0] & 1:
                        for i in range(destination.size):
                            byte = (value >> (i * 8)) & 255
                            if target + i in stores and stores[target + i][0] != byte:
                                ambiguous.add(target + i)
                            stores[target + i] = (byte, instruction.address)
            zero = (instruction.mnemonic == 'xor' and len(operands) == 2 and
                    operands[0].type == X86_OP_REG and operands[1].type == X86_OP_REG and
                    operands[0].reg == operands[1].reg and operands[0].size == 4)
            _, written = instruction.regs_access()
            for register in written:
                constants.pop(families.get(instruction.reg_name(register)), None)
            if len(operands) == 2 and operands[0].type == X86_OP_REG and operands[0].size == 4:
                name = instruction.reg_name(operands[0].reg)
                if name in families and (zero or (instruction.mnemonic == 'mov' and value is not None)):
                    constants[families[name]] = 0 if zero else value
            # Do not carry assumed register constants across control flow,
            # calls, skipped bytes, or unknown instructions' register writes.
            if instruction.mnemonic.startswith(('j', 'ret', 'call', 'loop')):
                constants.clear()

    def word(address):
        result = bytearray(image.read(address, 4))
        for i in range(4):
            if address + i in ambiguous:
                raise ValueError("conflicting constant stores")
            if address + i in stores:
                result[i] = stores[address + i][0]
        return struct.unpack("<I", result)[0]

    candidates = {address for address in stores if not address & 3}
    for flags, va, _, _, length in image.sections:
        if not flags & 4 or flags & 1:
            candidates.update(range((va + 3) & ~3, va + max(0, length - 23), 4))
    by_function = {}
    for address in sorted(candidates):
        try:
            if image.section(address, 24) is None:
                continue
            function = word(address)
            signature = word(address + 16)
            if not image.executable(function, 16) or not 1 <= signature <= 255:
                continue
            if any(word(address + offset) for offset in (4, 8, 12)):
                continue
            pointer = word(address + 20)
            name = ""
            for i in range(80):
                character = image.read(pointer + i * 2, 2).decode("utf-16le")
                if character == "\0":
                    break
                name += character
            else:
                continue
            if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name):
                continue
            # Static constructors may write only part of an entry. Preserve
            # their exact write sites and the source fingerprint for review.
            record = dict(name=name, descriptor=f"0x{address:08x}", signature=signature, record_bytes=24,
                          constructor_stores=[f"0x{site:08x}" for site in sorted({stores[a][1] for a in range(address, address + 24) if a in stores})])
            by_function.setdefault(function, []).append(record)
        except (ValueError, UnicodeError, struct.error):
            continue
    return [dict(address=f"0x{address:08x}", label=f"script_{records[0]['name']}_{address:08x}",
                 evidence="static owned FND format match; constructor execution, receiver class and complete function extent unverified",
                 code_sha256_16=hashlib.sha256(image.read(address, 16)).hexdigest(), methods=records)
            for address, records in sorted(by_function.items())]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xbe", type=Path, required=True)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = args.xbe.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    if sha != args.expected_sha256.lower():
        parser.error("dashboard XBE hash differs from the selected owned input")
    if args.output.exists():
        parser.error("choose a new output path")
    symbols = recover(Image(data))
    if not symbols:
        parser.error("no FND annotations recovered; do not substitute a borrowed address map")
    result = dict(schema="xboxllr.dashboard.symbols.v1", private_guest_output=True, xbe_sha256=sha,
                  validation="static candidates only; no runtime constructor or function-boundary qualification",
                  format_reference=dict(url="https://github.com/MrMilenko/Theseus/blob/5fc7ddddd03b4b5a7e6233769ad6a8fe46009965/docs/decomp/Node.md",
                                        role="external FND format reference; names and pointers independently recovered"),
                  symbols=symbols)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as destination:
        json.dump(result, destination, indent=2)
        destination.write("\n")
    print(f"Recovered {len(symbols)} script-method addresses from owned XBE metadata; class/extent review pending")


if __name__ == "__main__":
    main()
