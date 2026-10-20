"""Admission checks for the classic XDK immediate vertex command path.

The caller must pass instructions already decoded from original functions
whose bodies were independently hash-checked. This module contains only the
shared packet/ABI contract; it does not embed image addresses or guest bytes.
"""
from __future__ import annotations

from collections.abc import Mapping, Sequence


_CONTRACT = {
    "D3DDevice_Begin": (4, ("PrimitiveType",)),
    "D3DDevice_End": (0, ()),
    "D3DDevice_SetVertexData2f": (12, ("Register", "a", "b")),
    "D3DDevice_SetVertexData4f": (20, ("Register", "a", "b", "c", "d")),
}


def _insns(code):
    return [(i.mnemonic.lower(), " ".join(i.op_str.lower().split()), i)
            for i in code]


def _has(code, mnemonic, operand):
    return any(m == mnemonic and o == operand for m, o, _ in code)


def _has_sequence(code, expected):
    at = 0
    for mnemonic, operand in expected:
        while at < len(code) and code[at][:2] != (mnemonic, operand):
            at += 1
        if at == len(code):
            return False
        at += 1
    return True


def _ret(code, size):
    if not code or code[-1][0] != "ret":
        return False
    operand = code[-1][1]
    if not operand:
        return size == 0
    try:
        return int(operand, 0) == size
    except ValueError:
        return False


def _check_interface(interface):
    for name, (stack_bytes, params) in _CONTRACT.items():
        row = interface.get(name)
        if (row is None or row.get("stack_bytes") != stack_bytes or
                tuple(p.get("name") for p in row.get("params", ())) != params or
                any(p.get("type") != "psh" for p in row.get("params", ()))):
            raise ValueError("unreviewed immediate interface: " + name)


def _check_begin(code, helper_address):
    # The dirty-state flush is an image-local helper; its address is supplied
    # by the checked profile, never assumed here.
    helper_call = next((n for n, (m, o, _) in enumerate(code)
                        if m == "call" and o == hex(helper_address)), None)
    header = next((n for n, (m, o, _) in enumerate(code)
                   if m == "mov" and o.endswith(", 0x417fc") and "dword ptr [" in o), None)
    if helper_call is None or header is None or helper_call >= header:
        raise ValueError("Begin must flush pending state before opening the primitive")
    if not any(m == "mov" and o.startswith("ecx, dword ptr [esp + 8]")
               for m, o, _ in code):
        raise ValueError("Begin primitive argument is not the first stack DWORD")
    if not _has(code, "mov", "dword ptr [eax + 4], ecx") or not _has(code, "add", "eax, 8"):
        raise ValueError("Begin packet payload/size changed")
    if not _has(code, "mov", "dword ptr [esi], eax"):
        raise ValueError("Begin command-buffer cursor update changed")
    if not _has(code, "or", "dword ptr [esi + 8], 0x800"):
        raise ValueError("Begin device active flag is not offset 8 / mask 0x800")
    if not _ret(code, 4):
        raise ValueError("Begin original RET convention changed")


def _check_end(code):
    if not (_has(code, "mov", "dword ptr [eax], 0x417fc") and
            _has(code, "mov", "dword ptr [eax + 4], 0") and
            _has(code, "add", "eax, 8") and
            _has(code, "mov", "dword ptr [esi], eax")):
        raise ValueError("End packet encoding/cursor update changed")
    test_at = next((n for n, (m, o, _) in enumerate(code)
                    if m == "test" and o == "ah, 0x10"), None)
    clear_at = next((n for n, (m, o, _) in enumerate(code)
                     if m == "and" and o == "dword ptr [esi + 8], 0xffffe7ff"), None)
    if test_at is None or clear_at is None or test_at + 1 >= clear_at:
        raise ValueError("End fence test or device flag clear changed")
    # Require the fence call to be on the fall-through path of the test, with
    # the equal branch skipping it. This is the conditional 0x1000 fence bit.
    branch = code[test_at + 1]
    if branch[0] != "je" or not any(m == "push" and o == "1"
                                      for m, o, _ in code[test_at + 2:clear_at]) or not any(
            m == "call" for m, _, _ in code[test_at + 2:clear_at]):
        raise ValueError("End fence call is not conditional on device flag 0x1000")
    if not _ret(code, 0):
        raise ValueError("End original RET convention changed")


def _check_vdata2f(code):
    required = (
        ("lea", "edx, [ecx*8 + 0x81880]"),
        ("mov", "ecx, dword ptr [esp + 0xc]"),
        ("mov", "dword ptr [eax], edx"),
        ("mov", "edx, dword ptr [esp + 0x10]"),
        ("mov", "dword ptr [eax + 4], ecx"),
        ("mov", "dword ptr [eax + 8], edx"),
        ("add", "eax, 0xc"),
        ("mov", "dword ptr [esi], eax"),
    )
    if (any(not _has(code, m, o) for m, o in required) or
            not _has_sequence(code, required[1:6]) or not _ret(code, 12)):
        raise ValueError("SetVertexData2f method/register/RET contract changed")


def _check_vdata4f(code):
    required = (
        ("cmp", "ecx, -1"), ("mov", "ecx, 0x1518"),
        ("add", "ecx, 0x1a0"), ("shl", "ecx, 4"),
        ("add", "ecx, 0x100000"),
        ("mov", "dword ptr [eax], ecx"),
        ("mov", "dword ptr [eax + 4], ecx"),
        ("mov", "dword ptr [eax + 8], edx"),
        ("mov", "dword ptr [eax + 0xc], ecx"),
        ("mov", "dword ptr [eax + 0x10], edx"),
        ("add", "eax, 0x14"), ("mov", "dword ptr [esi], eax"),
    )
    if any(not _has(code, m, o) for m, o in required) or not _ret(code, 20):
        raise ValueError("SetVertexData4f method/register/RET contract changed")
    sequence = (
        ("mov", "edx, dword ptr [esp + 0x10]"),
        ("mov", "dword ptr [eax], ecx"),
        ("mov", "ecx, dword ptr [esp + 0xc]"),
        ("mov", "dword ptr [eax + 4], ecx"),
        ("mov", "ecx, dword ptr [esp + 0x14]"),
        ("mov", "dword ptr [eax + 8], edx"),
        ("mov", "edx, dword ptr [esp + 0x18]"),
        ("mov", "dword ptr [eax + 0xc], ecx"),
        ("mov", "dword ptr [eax + 0x10], edx"),
    )
    if not _has_sequence(code, sequence):
        raise ValueError("SetVertexData4f stack-to-packet mapping changed")


def inspect(codes: Mapping[str, Sequence], interface: Mapping,
            entry_names: Sequence[str], *, flush_helper_address: int, enabled=False):
    """Validate decoded classic-XDK immediate functions and return admission.

    `codes` must be the decoded original functions after the caller has
    verified their exact body identities. `flush_helper_address` must come
    from that same profile's decoded Begin call target. `entry_names` are the
    canonical generator names present in the native D3D dispatch table.
    """
    if not enabled:
        return None
    _check_interface(interface)
    missing = set(_CONTRACT) - set(codes)
    if missing:
        raise ValueError("missing decoded immediate functions: " + ", ".join(sorted(missing)))
    if len(set(entry_names)) != len(entry_names):
        raise ValueError("duplicate canonical D3D entry names")
    absent = set(_CONTRACT) - set(entry_names)
    if absent:
        raise ValueError("immediate entries are not in canonical dispatch: " + ", ".join(sorted(absent)))
    decoded = {name: _insns(codes[name]) for name in _CONTRACT}
    _check_begin(decoded["D3DDevice_Begin"], flush_helper_address)
    _check_end(decoded["D3DDevice_End"])
    _check_vdata2f(decoded["D3DDevice_SetVertexData2f"])
    _check_vdata4f(decoded["D3DDevice_SetVertexData4f"])
    return {
        "version": 1,
        "entries": {name: "PILOT_D3D_" + name for name in _CONTRACT},
        "device_flags_offset": 8,
        "begin_active_mask": 0x800,
        "end_clear_mask": 0x1800,
        "end_fence_mask": 0x1000,
        "position_register_alias": 0xFFFFFFFF,
    }


def emit_defines(admission):
    """Emit optional generated-header aliases after successful admission."""
    if admission is None:
        return []
    if admission.get("version") != 1 or set(admission.get("entries", {})) != set(_CONTRACT):
        raise ValueError("invalid immediate admission record")
    names = admission["entries"]
    lines = ["#define PILOT_D3D_IMMEDIATE_VERSION 1u"]
    for key, name in (("BEGIN", "D3DDevice_Begin"), ("END", "D3DDevice_End"),
                      ("VERTEX_DATA2F", "D3DDevice_SetVertexData2f"),
                      ("VERTEX_DATA4F", "D3DDevice_SetVertexData4f")):
        if names[name] != "PILOT_D3D_" + name:
            raise ValueError("noncanonical immediate entry alias")
        lines.append(f"#define PILOT_D3D_IMMEDIATE_{key}_ENTRY {names[name]}")
    return lines
