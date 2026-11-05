"""Framework-owned discovery corrections around the pinned CPU tools.

An address-taken entry may follow a return directly, with no alignment padding
when the preceding body already ends on the compiler's alignment boundary.
Keep the donor's pointer, gap, decode, and stack-unwind checks. This changes
only its aligned-entry corroboration; it never supplies symbol-derived seeds.
"""
from pathlib import Path
import runpy
import sys


def aligned_boundary_entry(detector, address):
    if address % 16 or address not in detector.engine.instructions:
        return False
    if detector.engine.instruction_covering(address) is not None:
        return False
    # Inspect decoded instruction ends, not a raw C3/E9 byte which might be
    # part of an immediate. Falling through from another instruction is not
    # evidence of an entry, even if an unrelated data word names the address.
    boundary = any(
        insn is not None and insn.end_address == address and
        (insn.is_ret or (insn.is_jump and not insn.is_cond_jump))
        for insn in (detector.engine.instructions.get(previous)
                     for previous in range(address - 15, address)))
    if not boundary or detector.engine.entry_pops_unsaved(address):
        return False
    first = detector.engine.instructions[address]
    if first.mnemonic.lower() in ('int3', 'nop'):
        return False
    # The next known entry must not clamp this body before its terminator.
    # A pointer-sized constant can also name an aligned fragment immediately
    # before a known entry; accepting it would create a callable prefix which
    # returns without executing the shared tail.
    end = detector.engine.block_extent_end(address, max_insns=8192)
    if end is None or any(address < start < end for start in detector.functions):
        return False
    tail = detector.engine.block_tail_jump(address, max_insns=8192)
    if tail is not None:
        return tail in detector.functions
    return detector.engine.probes_as_returning_body(address, max_insns=8192)


def install(module):
    detector = module.FunctionDetector
    if getattr(detector, '_xboxllr_boundary_extension', False):
        return
    original = detector._is_padded_entry

    def aligned_entry(self, address):
        return original(self, address) or aligned_boundary_entry(self, address)

    detector._is_padded_entry = aligned_entry
    detector._xboxllr_boundary_extension = True


def main():
    donor = Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(donor))
    from tools.disasm import functions
    if not Path(functions.__file__).resolve().is_relative_to(donor):
        raise ValueError('discovery module resolved outside selected donor')
    install(functions)
    sys.argv = ['tools.disasm', *sys.argv[2:]]
    runpy.run_module('tools.disasm', run_name='__main__')


if __name__ == '__main__':
    main()
