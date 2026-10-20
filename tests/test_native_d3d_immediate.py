import sys
from dataclasses import dataclass
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import native_d3d_immediate as immediate


@dataclass
class Operand:
    type: int = 2
    imm: int = 0


@dataclass
class Insn:
    mnemonic: str
    op_str: str = ""
    operands: tuple = ()


def ins(mnemonic, op_str=""):
    operands = (Operand(imm=int(op_str, 0)),) if mnemonic == "call" and op_str.startswith("0x") else ()
    return Insn(mnemonic, op_str, operands)


def contract():
    return {
        "D3DDevice_Begin": [
            ins("call", "0x4000"), ins("mov", "ecx, dword ptr [esp + 8]"),
            ins("mov", "dword ptr [eax], 0x417fc"),
            ins("mov", "dword ptr [eax + 4], ecx"), ins("add", "eax, 8"),
            ins("mov", "dword ptr [esi], eax"), ins("or", "dword ptr [esi + 8], 0x800"),
            ins("ret", "4"),
        ],
        "D3DDevice_End": [
            ins("mov", "dword ptr [eax], 0x417fc"), ins("mov", "dword ptr [eax + 4], 0"),
            ins("add", "eax, 8"), ins("mov", "dword ptr [esi], eax"),
            ins("mov", "eax, dword ptr [esi + 8]"), ins("test", "ah, 0x10"),
            ins("je", "0x1234"), ins("push", "1"), ins("call", "0x5000"),
            ins("and", "dword ptr [esi + 8], 0xffffe7ff"), ins("ret"),
        ],
        "D3DDevice_SetVertexData2f": [
            ins("lea", "edx, [ecx*8 + 0x81880]"),
            ins("mov", "ecx, dword ptr [esp + 0xc]"), ins("mov", "dword ptr [eax], edx"),
            ins("mov", "edx, dword ptr [esp + 0x10]"), ins("mov", "dword ptr [eax + 4], ecx"),
            ins("mov", "dword ptr [eax + 8], edx"), ins("add", "eax, 0xc"),
            ins("mov", "dword ptr [esi], eax"), ins("ret", "0xc"),
        ],
        "D3DDevice_SetVertexData4f": [
            ins("mov", "ecx, dword ptr [esp + 8]"), ins("cmp", "ecx, -1"),
            ins("jne", "0x1234"), ins("mov", "ecx, 0x1518"), ins("jmp", "0x1238"),
            ins("add", "ecx, 0x1a0"), ins("shl", "ecx, 4"),
            ins("mov", "edx, dword ptr [esp + 0x10]"), ins("add", "ecx, 0x100000"),
            ins("mov", "dword ptr [eax], ecx"),
            ins("mov", "ecx, dword ptr [esp + 0xc]"), ins("mov", "dword ptr [eax + 4], ecx"),
            ins("mov", "ecx, dword ptr [esp + 0x14]"),
            ins("mov", "dword ptr [eax + 8], edx"), ins("mov", "edx, dword ptr [esp + 0x18]"),
            ins("mov", "dword ptr [eax + 0xc], ecx"), ins("mov", "dword ptr [eax + 0x10], edx"),
            ins("add", "eax, 0x14"),
            ins("mov", "dword ptr [esi], eax"), ins("ret", "0x14"),
        ],
    }


def interface():
    names = {
        "D3DDevice_Begin": (4, ["PrimitiveType"]),
        "D3DDevice_End": (0, []),
        "D3DDevice_SetVertexData2f": (12, ["Register", "a", "b"]),
        "D3DDevice_SetVertexData4f": (20, ["Register", "a", "b", "c", "d"]),
    }
    return {n: {"stack_bytes": s, "params": [{"name": p, "type": "psh"} for p in ps]}
            for n, (s, ps) in names.items()}


def admitted(code=None):
    return immediate.inspect(code or contract(), interface(), list(interface()),
                             flush_helper_address=0x4000, enabled=True)


def test_admits_exact_classic_immediate_contract_and_emits_canonical_ids():
    result = admitted()
    assert result["device_flags_offset"] == 8
    assert result["begin_active_mask"] == 0x800
    assert result["end_clear_mask"] == 0x1800
    assert result["end_fence_mask"] == 0x1000
    assert result["entries"]["D3DDevice_Begin"] == "PILOT_D3D_D3DDevice_Begin"
    assert immediate.emit_defines(result) == [
        "#define PILOT_D3D_IMMEDIATE_VERSION 1u",
        "#define PILOT_D3D_IMMEDIATE_BEGIN_ENTRY PILOT_D3D_D3DDevice_Begin",
        "#define PILOT_D3D_IMMEDIATE_END_ENTRY PILOT_D3D_D3DDevice_End",
        "#define PILOT_D3D_IMMEDIATE_VERTEX_DATA2F_ENTRY PILOT_D3D_D3DDevice_SetVertexData2f",
        "#define PILOT_D3D_IMMEDIATE_VERTEX_DATA4F_ENTRY PILOT_D3D_D3DDevice_SetVertexData4f",
    ]
    assert immediate.emit_defines(None) == []


@pytest.mark.parametrize("name,mutator", [
    ("D3DDevice_Begin", lambda xs: xs.__setitem__(2, ins("mov", "dword ptr [eax], 0x417f8"))),
    ("D3DDevice_End", lambda xs: xs.__setitem__(-2, ins("and", "dword ptr [esi + 8], 0xffffefff"))),
    ("D3DDevice_SetVertexData2f", lambda xs: xs.__setitem__(-1, ins("ret", "8"))),
    ("D3DDevice_SetVertexData4f", lambda xs: xs.__setitem__(1, ins("cmp", "ecx, 0"))),
])
def test_rejects_changed_packet_flags_or_original_abi(name, mutator):
    code = contract()
    mutator(code[name])
    with pytest.raises(ValueError):
        admitted(code)


def test_begin_must_call_profile_local_flush_helper():
    code = contract()
    code["D3DDevice_Begin"][0] = ins("call", "0x4001")
    with pytest.raises(ValueError, match="flush pending state"):
        admitted(code)


def test_profile_flag_keeps_unadmitted_classic_paths_disabled():
    assert immediate.inspect({}, {}, [], flush_helper_address=0x4000) is None
