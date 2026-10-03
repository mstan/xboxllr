#!/usr/bin/env python3
"""Authored scalar IA-32 fixtures, separate from private guest artifacts."""
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from aot_compile import compile_input

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
regions = []
tables = []


def add(name, code):
    regions.append(dict(name=name, base=0x1000, code=bytes(code).hex()))
    return "&xlle_aot_" + name


shifts = []
for operation, group in enumerate((4, 5, 7)):
    for register in range(8):
        for mode, count in [(0, 0), (1, 1)] + [(2, n) for n in (0, 1, 2, 31, 32, 33, 63, 64, 255)]:
            code = [0xd3 if mode == 0 else 0xd1 if mode == 1 else 0xc1, 0xc0 + group * 8 + register]
            if mode == 2:
                code.append(count)
            name = f"shift_{operation}_{register}_{mode}_{count}"
            shifts.append(f"    {{{add(name,code)}, {operation}, {register}, {mode}, {count}}}")
tables.append("struct ShiftCase { const xlle_aot_region* r; unsigned operation, reg, mode, count; };\n"
              "static const ShiftCase shift_cases[]={\n" + ",\n".join(shifts) + "\n};\n")

branches = []
for condition in range(16):
    for near in (0, 1):
        for direction in (0, 1, 2):
            length = 6 if near else 2
            backward, limited = direction == 1, direction == 2
            displacement = -length if backward else 1
            code = ([0x0f, 0x80 + condition] + list(displacement.to_bytes(4, "little", signed=True))
                    if near else [0x70 + condition, displacement & 255])
            code += [0x90] if limited else [0x90, 0x90]
            name = f"condition_{condition}_{near}_{direction}"
            branches.append(f"    {{{add(name,code)}, {condition}, {length}, {int(backward)}, {int(limited)}}}")
tables.append("struct BranchCase { const xlle_aot_region* r; unsigned condition, length, backward, limited; };\n"
              "static const BranchCase branch_cases[]={\n" + ",\n".join(branches) + "\n};\n")

extensions = []
for width, opcode in ((8, 0xb6), (16, 0xb7)):
    for destination in range(8):
        for source in range(8):
            name = f"extend_{width}_{destination}_{source}"
            extensions.append(f"    {{{add(name,[0x0f,opcode,0xc0+destination*8+source])}, {width}, {destination}, {source}}}")
tables.append("struct ExtendCase { const xlle_aot_region* r; unsigned width, destination, source; };\n"
              "static const ExtendCase extend_cases[]={\n" + ",\n".join(extensions) + "\n};\n")

# Exact access widths, unaligned/SIB/negative displacement and old-address alias.
for name, code in {
    "narrow_byte": "0fb606",
    "narrow_word": "0fb706",
    "narrow_alias": "0fb600",
    "narrow_word_sib": "0fb744b3fc",
    "narrow_word_stack": "0fb74dfc",
    "narrow_byte_loop": "0fb606ebfb",
    "narrow_word_loop": "0fb706ebfb",
    "narrow_then_wide": "0fb6068b0eebf9",
    "immediate_store": "c70678563412",
    "immediate_store_sib": "c744b3fc78563412",
    "immediate_store_stack": "c74424fc78563412",
    "immediate_smc": "c7069090909040909090",
    "scalar_cycle": "0fb606d1e0c1e802c1f8030fb717d3e109c283c60183c701ebe6",
}.items():
    add(name, bytes.fromhex(code))
for reg in range(8):
    add(f"immediate_reg_{reg}",[0xc7,0xc0+reg,0x78,0x56,0x34,0x12])

unary = []
for operation, group in enumerate((3, 2)):
    for reg in range(8):
        unary.append(f"    {{{add(f'unary_{operation}_{reg}',[0xf7,0xc0+group*8+reg])}, {operation}, {reg}}}")
tables.append("struct UnaryCase { const xlle_aot_region* r; unsigned operation, reg; };\n"
              "static const UnaryCase unary_cases[]={\n" + ",\n".join(unary) + "\n};\n")

reads = []
for operation, opcode in enumerate((0x03,0x2b,0x33,0x23,0x0b,0x3b,0x39,0x85)):
    for reg in range(8):
        # All base/destination aliases plus absolute and scaled negative SIB.
        for form in range(10):
            if form < 8:
                code = [opcode,0x40+reg*8+form]+([0x24] if form==4 else [])+[0]
            elif form == 8:
                code = [opcode,5+reg*8,0x00,0x28,0,0]
            else:
                code = [opcode,0x44+reg*8,0xb3,0xfc]
            reads.append(f"    {{{add(f'alu_read_{operation}_{reg}_{form}',code)}, {operation}, {reg}, {form}}}")
tables.append("struct AluReadCase { const xlle_aot_region* r; unsigned operation, reg, form; };\n"
              "static const AluReadCase alu_read_cases[]={\n" + ",\n".join(reads) + "\n};\n")

read_immediates=[]
for operation,opcode,group,size in [(5,0x81,7,4),(5,0x83,7,1),(7,0xf7,0,4)]:
    for immediate in ([0,1,0x7f,0x80,0xff] if size==1 else [0,1,0x7fffffff,0x80000000,0xffffffff]):
        code=[opcode,0x44+group*8,0xb3,0xfc]+list(immediate.to_bytes(size,'little'))
        value=immediate if size==4 or immediate<128 else immediate+0xffffff00
        name=f'alu_immediate_{opcode}_{immediate}'
        read_immediates.append(f"    {{{add(name,code)}, {operation}, 0x{value:08x}u}}")
tables.append("struct AluImmediateCase { const xlle_aot_region* r; unsigned operation; uint32_t immediate; };\n"
              "static const AluImmediateCase alu_immediate_cases[]={\n" + ",\n".join(read_immediates) + "\n};\n")
for name,code in {
    'leave32':'c9', 'leave_cycle':'c9ebfd',
    'read_cmp_loop':'3b06ebfc', 'read_test_loop':'f70678563412ebf8',
    'read_then_nop':'030690', 'leave_then_nop':'c990',
}.items():
    add(name,bytes.fromhex(code))
cycle=bytes.fromhex('03062b0e330e230e0b0e3b0e390e850e813effffffff833e80f70678563412f7d9f7d1')
add('alu_read_cycle',cycle+b'\xeb'+bytes([(-len(cycle)-2)&255]))

source = out / "scalar.json"
source.write_text(json.dumps(dict(independent_regions=True, regions=regions)), encoding="utf-8")
compile_input(source, out / "scalar.c", out / "scalar.h", out / "scalar.manifest.json")
(out / "scalar_cases.hpp").write_text('#pragma once\n#include "scalar.h"\n' + "\n".join(tables), encoding="utf-8")
