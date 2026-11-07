"""Framework-owned discovery corrections around the pinned CPU tools.

An address-taken entry may follow a return directly, with no alignment padding
when the preceding body already ends on the compiler's alignment boundary.
Keep the donor's pointer, gap, decode, and stack-unwind checks. USB registration
records in XPP's metadata prefix also corroborate callbacks which ordinary data
section scans cannot see. Bink assembly also uses multi-byte/zero alignment
and standalone code sections. These corrections do not supply symbol seeds.
"""
from pathlib import Path
import bisect
import hashlib
import importlib
import runpy
import struct
import sys

from native_image_reads import install_discovery as install_image_reads


class _TableDataView:
    """Preserve image bytes while constraining weak Bink coefficient pointers."""
    def __init__(self, detector):
        self.image = detector.image
        self.detector = detector
        self.starts = sorted(detector.functions)

    def __getattr__(self, name):
        return getattr(self.image, name)

    def get_section_data(self, section):
        data = self.image.get_section_data(section)
        if section.name != 'BINKDATA':
            return data
        data = bytearray(data)
        for offset in range(0, len(data)-3, 4):
            target = struct.unpack_from('<I', data, offset)[0]
            if target in self.detector.functions or target in self.detector._alias_entries:
                continue
            index = bisect.bisect_right(self.starts, target)-1
            if (index >= 0 and target < self.detector.functions[self.starts[index]].end and
                    not self.detector._is_padded_entry(target)):
                # Numeric coefficient pairs can resemble pointers into existing
                # bodies. They do not establish a fresh callable ABI. Real
                # padded entries and already-qualified aliases remain eligible.
                struct.pack_into('<I', data, offset, 0)
        return bytes(data)


def _alignment_padding(detector, start, end, *, zero=False):
    """Unreachable alignment bytes after a decoded terminal, at most 15 bytes."""
    if not 0 <= end-start <= 15:
        return False
    registers = {'eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp', 'esp'}
    while start < end:
        insn = detector.engine.instructions.get(start)
        if insn is not None and insn.end_address <= end:
            mnemonic = insn.mnemonic.lower()
            operands = [value.strip() for value in insn.op_str.split(',')]
            neutral = (mnemonic in ('nop', 'int3') or
                       (len(operands) == 2 and operands[0] in registers and
                        ((mnemonic == 'mov' and operands[1] == operands[0]) or
                         (mnemonic == 'lea' and operands[1] == '['+operands[0]+']'))))
            if neutral:
                start = insn.end_address
                continue
        # Bink assembly sections also use zero-filled unreachable alignment.
        if zero and detector.image.read_bytes_at_va(start, 1) == b'\0':
            start += 1
            continue
        return False
    return True


def _terminal_boundary(detector, address, *, zero=False):
    section = detector.image.get_section_at_va(address)
    return any(
        insn is not None and insn.end_address <= address and
        (insn.is_ret or (insn.is_jump and not insn.is_cond_jump)) and
        detector.image.get_section_at_va(insn.address) is section and
        _alignment_padding(detector, insn.end_address, address, zero=zero)
        for insn in (detector.engine.instructions.get(previous)
                     for previous in range(address - 30, address)))


def code_upper_bound(detector, start, declared_end):
    """Exclude a trailing switch table, preserving interleaved continuations.

    This is also the exact-PDB path's bound check. Unaided discovery additionally
    requires a decoded terminal/alignment boundary at the returned table.
    """
    instructions = detector.engine.get_instructions_in_range(start, declared_end)
    for instruction in instructions:
        table = instruction.jump_table
        if table is None or not instruction.address < table < declared_end:
            continue
        table_end = detector.engine.jump_tables.get(table)
        targets = detector.engine.jump_table_entries(table)
        if (table_end is None or table_end > declared_end or not targets or
                any(not start <= target < table for target in targets)):
            continue
        prefix = [i for i in instructions if i.address < table]
        if any(i.is_branch and i.jump_target is not None and
               table <= i.jump_target < declared_end for i in prefix):
            continue
        if any(i.is_jump and i.jump_target is None and i.jump_table is None
               for i in prefix):
            continue
        return table
    return declared_end


def aligned_boundary_entry(detector, address):
    if address % 16 or address not in detector.engine.instructions:
        return False
    if detector.engine.instruction_covering(address) is not None:
        return False
    # Inspect decoded terminal ends, not a raw C3/E9 byte in an immediate.
    # Multi-byte neutral alignment can separate a terminal from the entry.
    # Bink also starts standalone assembly kernels at code-section boundaries.
    section = detector.image.get_section_at_va(address)
    bink = (section is not None and section.name.startswith('BINK') and
            not section.name.startswith('BINKDATA'))
    boundary = bink and address == section.virtual_addr
    if not boundary:
        boundary = _terminal_boundary(detector, address, zero=bink)
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
    # Bink's constant/dispatch tables use a library-specific DATA section.
    # Xbox headers still mark it executable. Keep real BINK* code sections;
    # classifying this exact data section also exposes its callable tables to
    # the existing data-pointer pass instead of disassembling them as opcodes.
    loader = importlib.import_module(module.__package__ + '.loader')
    config = importlib.import_module(module.__package__ + '.config')
    install_image_reads(loader)
    loader.DATA_SECTION_NAMES = loader.DATA_SECTION_NAMES | {'BINKDATA'}
    config._DATA_SECTION_NAMES = config._DATA_SECTION_NAMES | {'BINKDATA'}
    original = detector._is_padded_entry
    original_tables = detector._pass_data_ptr_targets
    original_end = detector._find_function_end

    def aligned_entry(self, address):
        return original(self, address) or aligned_boundary_entry(self, address)

    detector._is_padded_entry = aligned_entry

    def function_end(self, start, next_func, sec_end):
        end = original_end(self, start, next_func, sec_end)
        upper = code_upper_bound(self, start, end)
        if upper < end and _terminal_boundary(self, upper):
            return original_end(self, start, upper, upper)
        return end

    detector._find_function_end = function_end

    def data_tables(self, sections):
        previous_aliases = set(self._alias_entries)
        image = self.image
        self.image = _TableDataView(self)
        try:
            original_tables(self, sections)
        finally:
            self.image = image
        # A numeric pair can land on a short returning epilogue before its
        # enclosing assembly body is discovered. The donor's short gap probe
        # checks reachability of a return, but not the required saved frame.
        # Preserve existing/context aliases and actual branch continuations.
        new_aliases = set(self._alias_entries) - previous_aliases
        if new_aliases:
            branches = {i.jump_target for i in self.engine.instructions.values()
                        if i.is_branch and i.jump_target is not None}
            for table in self.engine.jump_tables:
                branches.update(self.engine.jump_table_entries(table))
            starts = sorted(self.functions)
            for address in new_aliases:
                index = bisect.bisect_right(starts, address) - 1
                interior = (index >= 0 and starts[index] < address <
                            self.functions[starts[index]].end)
                if (not interior and address not in branches and
                        not self._starts_after_boundary(address) and
                        self.engine.entry_pops_unsaved(address)):
                    del self._alias_entries[address]
        return xpp_registration_entries(self, sections) or bool(
            set(self._alias_entries) - previous_aliases)

    detector._pass_data_ptr_targets = data_tables
    detector._xboxllr_boundary_extension = True


def install_cache_key(module, metadata_sha256=None):
    """Keep unaided and supplied-symbol outputs in distinct cache identities."""
    original = module.AnalysisCache._opts_key
    identity = hashlib.sha256(Path(__file__).read_bytes())
    identity.update(Path(__file__).with_name('native_image_reads.py').read_bytes())
    if metadata_sha256 is not None:
        identity.update(Path(__file__).with_name('native_symbol_discovery.py').read_bytes())
        identity.update(metadata_sha256.encode('ascii'))
    suffix = identity.hexdigest()

    def options(*args, **kwargs):
        return hashlib.sha256((original(*args, **kwargs) + suffix).encode('ascii')).hexdigest()

    module.AnalysisCache._opts_key = staticmethod(options)


def main():
    import argparse
    donor = Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(donor))
    from tools.disasm import functions
    if not Path(functions.__file__).resolve().is_relative_to(donor):
        raise ValueError('discovery module resolved outside selected donor')
    install(functions)
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--discovery-symbols', type=Path)
    options, remaining = parser.parse_known_args(sys.argv[2:])
    metadata_sha256 = None
    if options.discovery_symbols:
        from native_symbol_discovery import QualifiedFunctions, install as install_symbols
        qualified = QualifiedFunctions.load(options.discovery_symbols, Path(remaining[0]).read_bytes())
        install_symbols(functions, qualified)
        metadata_sha256 = qualified.metadata_sha256
    from tools.disasm import cache
    install_cache_key(cache, metadata_sha256)
    sys.argv = ['tools.disasm', *remaining]
    runpy.run_module('tools.disasm', run_name='__main__')


if __name__ == '__main__':
    main()
