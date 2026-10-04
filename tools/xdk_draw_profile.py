"""Prepare a private, byte-pinned dashboard DrawIndexedVertices ABI profile.

Uses the phobos/XbSymbolDatabase discovery output, but validates the actual
machine code rather than accepting the scanner's mismatched register ABI.
Generated headers contain private title bytes and must remain under build/.
"""
import argparse
import collections
import hashlib
import json
import re
import struct
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_32


def prepare(xbe, symbols):
    data = xbe.read_bytes()
    u32 = lambda at: struct.unpack_from("<I", data, at)[0]
    if data[:4] != b"XBEH":
        raise ValueError("not an XBE")
    matches = [s for s in json.loads(symbols.read_text(encoding="utf-8"))["symbols"]
               if s.get("name") == "D3DDevice_DrawIndexedVertices"]
    if len(matches) != 1:
        raise ValueError("need one discovered DrawIndexedVertices")
    entry = matches[0]["address"]
    section_table = u32(0x120) - u32(0x104)
    for i in range(u32(0x11c)):
        at = section_table + i * 56
        va, size, raw, raw_size = (u32(at + j) for j in (4, 8, 12, 16))
        if va <= entry < va + min(size, raw_size):
            start = raw + entry - va
            code = data[start:min(start + 4096, raw + raw_size)]
            break
    else:
        raise ValueError("draw entry is not backed by XBE section bytes")
    insns = []
    for ins in Cs(CS_ARCH_X86, CS_MODE_32).disasm(code, entry):
        insns.append(ins)
        if ins.mnemonic == "ret":
            break
    if not insns or insns[-1].mnemonic != "ret" or insns[-1].op_str != "0xc":
        raise ValueError("unsupported draw return ABI; requires three stack arguments")
    def one(mnemonic, operands):
        found = [i.address for i in insns if i.mnemonic == mnemonic and i.op_str == operands]
        if len(found) != 1:
            raise ValueError(f"unverified draw boundary: {mnemonic} {operands}")
        return found[0]
    prepared = one("mov", "ecx, dword ptr [ebp + 8]")
    cleanup = one("mov", "eax, dword ptr [ecx + 8]")
    one("or", "dword ptr [esi + 8], 0x800")
    one("mov", "dword ptr [eax], 0x417fc")
    one("mov", "dword ptr [ebx + 4], 0")
    one("mov", "edx, dword ptr [ebp + 0xc]")
    calls = collections.Counter(int(i.op_str, 16) for i in insns if i.mnemonic == "call")
    reserves = [pc for pc, count in calls.items() if count == 2]
    globals_eax = collections.Counter(int(m.group(1), 16) for i in insns
        if i.mnemonic == "mov" and (m := re.fullmatch(r"eax, dword ptr \[(0x[0-9a-f]+)\]", i.op_str)))
    growth = [pc for pc, count in globals_eax.items() if count == 2]
    device = [int(m.group(1), 16) for i in insns if i.mnemonic == "mov"
              and (m := re.fullmatch(r"esi, dword ptr \[(0x[0-9a-f]+)\]", i.op_str))]
    if len(reserves) != 1 or len(growth) != 1 or len(device) != 1:
        raise ValueError("unsupported allocator/device ABI shape")
    code = code[:insns[-1].address + insns[-1].size - entry]
    # The boundary checks above aid diagnostics; they do not prove the control
    # flow of another XDK revision. Only the implementation audited for this
    # spike may use its frame layout and allocator continuation contract.
    if hashlib.sha256(code).hexdigest() != "c11c26926c88b7ba39112f093216748f0dd70d5795b13fd34d269e4fd0760d0a":
        raise ValueError("draw implementation needs an ABI audit before native replacement")
    return dict(entry=entry, prepared=prepared, cleanup=cleanup, reserve=reserves[0],
                growth=growth[0], device=device[0]), code, hashlib.sha256(data).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--xbe", type=Path, required=True)
    ap.add_argument("--symbols", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    if not any(p.startswith("build") for p in args.output.resolve().parts):
        raise ValueError("private ABI header must be placed in a build directory")
    profile, code, sha = prepare(args.xbe, args.symbols)
    lines = ["// XLLE_PRIVATE_GUEST_ARTIFACT", "#pragma once", "#include <cstdint>",
             "namespace xlle::xdk_draw_profile {", f'constexpr char image_sha256[] = "{sha}";']
    lines.extend(f"constexpr uint32_t {k} = 0x{v:08x}u;" for k, v in profile.items())
    lines += ["constexpr uint8_t code[] = {", ",".join(f"0x{x:02x}" for x in code), "};", "}"]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("Verified three stack arguments, RET 12, original preparation/allocation/completion helpers")
    print(json.dumps(profile))


if __name__ == "__main__":
    main()
