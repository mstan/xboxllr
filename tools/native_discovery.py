"""Framework-owned discovery corrections around the pinned CPU tools.

An address-taken entry may follow a return directly, with no alignment padding
when the preceding body already ends on the compiler's alignment boundary.
Keep the donor's pointer, gap, decode, and stack-unwind checks. USB registration
records in XPP's metadata prefix also corroborate callbacks which ordinary data
section scans cannot see. Neither correction supplies symbol-derived seeds.
"""
from pathlib import Path
import runpy
import struct
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


def xpp_registration_entries(detector, sections):
    """USB registration records precede XPP's first strongly known body.

    Only a run with an already known callback corroborates a new target. The
    target must be in XPP code beyond the metadata, begin at a decoded terminal
    boundary, and decode completely without borrowing an existing entry.
    This does not scan executable sections generally for pointer-looking words.
    """
    added = False
    for section in sections:
        if section.name != 'XPP':
            continue
        low, high = section.virtual_addr, section.virtual_addr+section.raw_size
        strong = [f.start for f in detector.functions.values()
                  if low <= f.start < high and f.detection_method in
                  ('entry_point', 'call_target', 'prologue', 'prologue_alt')]
        if not strong:
            continue
        code_start = min(strong)
        data = detector.image.get_section_data(section)[:code_start-low]
        runs, run = [], []
        for offset in range(0, len(data)-3, 4):
            target = struct.unpack_from('<I', data, offset)[0]
            if code_start <= target < high:
                run.append(target)
            else:
                if len(run) >= 2:
                    runs.append(run)
                run = []
        if len(run) >= 2:
            runs.append(run)
        known = sorted(detector.functions)
        for run in runs:
            if not any(address in detector.functions for address in run):
                continue
            for address in sorted(set(run)):
                if address in detector.functions or address in detector._alias_entries:
                    continue
                if any(f.start < address < f.end for f in detector.functions.values()):
                    continue
                preceding = [detector.engine.instructions.get(p)
                             for p in range(address-15, address)]
                boundary = any(i is not None and i.end_address == address and
                               (i.is_ret or (i.is_jump and not i.is_cond_jump) or
                                i.mnemonic.lower() in ('int3', 'nop')) for i in preceding)
                if (address not in detector.engine.instructions or
                    detector.engine.instruction_covering(address) is not None or
                    not boundary or
                    detector.engine.entry_pops_unsaved(address)):
                    continue
                block_end = detector.engine.block_extent_end(address, max_insns=8192)
                if block_end is None or any(address < start < block_end for start in known):
                    continue
                tail = detector.engine.block_tail_jump(address, max_insns=8192)
                if tail is not None:
                    if tail not in detector.functions:
                        continue
                elif not detector.engine.probes_as_returning_body(address, max_insns=8192):
                    continue
                upper = next((start for start in known if start > address), high)
                end = detector._find_function_end(address, upper, high)
                instructions = detector.engine.get_instructions_in_range(address, end)
                if (not instructions or instructions[-1].end_address != end or
                    not (instructions[-1].is_ret or
                         (instructions[-1].is_jump and not instructions[-1].is_cond_jump))):
                    continue
                detector._alias_entries[address] = end
                added = True
    return added


def install(module):
    detector = module.FunctionDetector
    if getattr(detector, '_xboxllr_boundary_extension', False):
        return
    original = detector._is_padded_entry
    original_tables = detector._pass_data_ptr_targets

    def aligned_entry(self, address):
        return original(self, address) or aligned_boundary_entry(self, address)

    detector._is_padded_entry = aligned_entry

    def data_tables(self, sections):
        original_added = original_tables(self, sections)
        return xpp_registration_entries(self, sections) or original_added

    detector._pass_data_ptr_targets = data_tables
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
