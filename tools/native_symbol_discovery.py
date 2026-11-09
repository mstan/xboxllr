"""Explicit, image-bound PDB body bounds for the pinned discovery pipeline.

PDB lengths bound the owning function, including its inline data. They are not
instruction counts, and an interior entry must not truncate the owning body.
Names remain private diagnostic metadata, never service replacement authority.
"""
from bisect import bisect_right
import hashlib
import json
from pathlib import Path
import re

from aot_inventory import parse_xbe
from native_guest_symbols import LIMIT, Symbols
from native_discovery import _bounded_table_after, _unwind_context_entries, code_upper_bound


class QualifiedFunctions:
    def __init__(self, document, blob):
        image = parse_xbe(blob)
        symbols = Symbols(document, image['sha256'])
        if (document.get('identity') != 'matching-codeview' or
                not re.fullmatch('[0-9a-f]{64}', document.get('source_sha256', '')) or
                not symbols.functions):
            raise ValueError('discovery requires matching-CodeView PDB metadata')
        self.ends = {}
        for row in symbols.functions:
            start, end = row['address'], row['address'] + row['size']
            if row.get('extent_kind') != 'pdb-length':
                raise ValueError('entry-only symbols cannot supply body bounds')
            if not any(s['executable_candidate'] and
                       s['address'] <= start < s['address'] + s['raw_size'] and
                       start < end <= s['address'] + min(s['size'], s['raw_size']+15)
                       for s in image['sections']):
                raise ValueError('PDB function has no executable file-backed entry or bounded image extent')
            self.ends[start] = end
        self.starts = sorted(self.ends)
        self.prefix_ends = []
        high = 0
        for start in self.starts:
            high = max(high, self.ends[start])
            self.prefix_ends.append(high)
        self.xbe_sha256 = image['sha256']

    @classmethod
    def load(cls, path, blob):
        # Reuse the existing bounded JSON reader and its structural validation.
        with Path(path).open('rb') as source:
            raw = source.read(LIMIT + 1)
        if len(raw) > LIMIT:
            raise ValueError('PDB discovery metadata exceeds size limit')
        qualified = cls(json.loads(raw), blob)
        qualified.metadata_sha256 = hashlib.sha256(raw).hexdigest()
        return qualified

    def containing_start(self, address):
        """The innermost declared extent; folded/nested entries may overlap."""
        index = bisect_right(self.starts, address) - 1
        while index >= 0 and self.prefix_ends[index] > address:
            start = self.starts[index]
            if address < self.ends[start]:
                return start
            index -= 1
        return None

    def containing_end(self, address):
        start = self.containing_start(address)
        return self.ends[start] if start is not None else None


def _repair_switch_tables(detector, qualified):
    """Use a bounded byte-index consumer to correct a pointer-shaped table tail.

    Reading the byte indices as more pointers can widen a table's backwards
    scan enough to consume a real epilogue. Only a complete CMP/JA/MOVZX/JMP
    chain, its actual indices and its owning PDB extent establish these bounds.
    Other tables, including negative-index dispatches, keep their original map.
    """
    engine = detector.engine
    consumers = {}
    entries = set(qualified.starts)
    for insn in engine.instructions.values():
        entries.update(t for t in (insn.call_target, insn.jump_target) if t is not None)
        if insn.jump_table is not None:
            table = engine._jt_by_disp.get(insn.jump_table, insn.jump_table)
            if table in engine.jump_tables:
                consumers.setdefault(table, []).append(insn)
    entries = sorted(entries)
    register = r'e(?:[abcd]x|[sd]i|[bs]p)'

    def previous(address):
        matches = [i for i in engine.get_instructions_in_range(address-15, address)
                   if i.end_address == address]
        return matches[0] if len(matches) == 1 else None

    def bounds(dispatch):
        load = previous(dispatch.address)
        guard = previous(load.address) if load is not None else None
        compare = previous(guard.address) if guard is not None else None
        # Scheduled SSE loads/stores change neither EFLAGS nor the integer
        # index. Keep the search bounded and reject entries into this chain.
        for _ in range(8):
            if compare is None or compare.mnemonic != 'movss':
                break
            compare = previous(compare.address)
        if (compare is None or compare.mnemonic != 'cmp' or guard.mnemonic != 'ja' or
                load.mnemonic != 'movzx'):
            return None
        jump = re.fullmatch(r'dword ptr \[('+register+r')\*4 \+ (0x[0-9a-f]+)\]', dispatch.op_str)
        lookup = re.fullmatch(r'('+register+r'), byte ptr \[('+register+r') \+ (0x[0-9a-f]+)\]', load.op_str)
        limit = re.fullmatch(r'('+register+r'), (0x[0-9a-f]+|[0-9]+)', compare.op_str)
        if (jump is None or lookup is None or limit is None or
                jump[1] != lookup[1] or lookup[2] != limit[1]):
            return None
        first = bisect_right(entries, compare.address)
        if first < len(entries) and entries[first] < dispatch.end_address:
            return None  # Another entry can bypass the range check.
        table, byte_table, count = int(jump[2], 16), int(lookup[3], 16), int(limit[2], 0)+1
        owner = qualified.containing_start(compare.address)
        if (owner is None or table != dispatch.jump_table or
                not owner <= compare.address < dispatch.end_address <= table < byte_table or
                not 0 < count <= 4096 or byte_table+count > qualified.ends[owner] or
                (byte_table-table) % 4 or not 0 < (byte_table-table)//4 <= 256):
            return None
        indices = detector.image.read_bytes_at_va(byte_table, count)
        targets = [detector.image.read_u32_at_va(a) for a in range(table, byte_table, 4)]
        if (indices is None or len(indices) != count or max(indices) >= len(targets) or
                any(t is None or not owner <= t < table or engine.get_instruction(t) is None
                    for t in targets)):
            return None
        return table, byte_table, owner

    for old_start, dispatches in consumers.items():
        proofs = [bounds(i) for i in dispatches]
        if not proofs or proofs[0] is None or any(p != proofs[0] for p in proofs):
            continue
        table, end, owner = proofs[0]
        if (old_start == table and engine.jump_tables[old_start] == end or
                not owner <= old_start <= table or
                table != old_start and table in engine.jump_tables or
                any(d != table and s == old_start for d, s in engine._jt_by_disp.items())):
            continue
        restored = []
        if old_start < table:
            # Restore the original stream from its certified entry, including
            # an instruction whose first bytes the backwards scan overlapped.
            prefix = list(engine._cs.disasm(detector.image.read_bytes_at_va(owner, table-owner) or b'', owner))
            if (not prefix or prefix[-1].address+prefix[-1].size != table or
                    not engine._classify_instruction(prefix[-1]).is_terminator):
                continue
            restored = [engine._classify_instruction(i) for i in prefix if i.address+i.size > old_start]
        del engine.jump_tables[old_start]
        engine.jump_tables[table] = end
        engine._jt_by_disp[table] = table
        for insn in restored:
            engine.instructions[insn.address] = insn
        if restored:
            engine._sorted_addrs = None


def bind(detector, qualified):
    if hashlib.sha256(detector.image.raw_data).hexdigest() != qualified.xbe_sha256:
        raise ValueError('PDB discovery metadata belongs to another image')
    detector._xboxllr_pdb_ranges = qualified
    for start in qualified.starts:
        # Exact-PDB entries can correct an out-of-phase linear sweep. decode_at
        # preserves competing x86 streams instead of evicting existing code.
        if detector.engine.get_instruction(start) is None:
            detector.engine.decode_at(start)
        if detector.engine.get_instruction(start) is None:
            raise ValueError(f'PDB entry does not decode: {start:#x}')
        detector._add_candidate(start, 1.0, 'matching_pdb')
    _repair_switch_tables(detector, qualified)


def install(module, qualified):
    detector = module.FunctionDetector
    if getattr(detector, '_xboxllr_symbol_extension', False):
        raise ValueError('PDB discovery is already configured in this process')
    original_detect = detector.detect_all
    original_end = detector._find_function_end
    original_aliases = detector._build_alias_entries
    original_alias_tails = detector._pass_alias_tail_jumps

    def detect(self, sections=None):
        bind(self, qualified)
        result = original_detect(self, sections)
        missing = set(qualified.starts) - self.functions.keys()
        if missing:
            raise ValueError(f'discovery lost {len(missing)} qualified PDB entries')
        return result

    def function_end(self, start, next_func, sec_end):
        ranges = getattr(self, '_xboxllr_pdb_ranges', None)
        if ranges is not None:
            declared_end = ranges.ends.get(start)
            if declared_end is not None:
                # Reuse switch-table/branch-aware body formation with the PDB
                # upper bound. Other candidate entries no longer cut this body.
                upper = code_upper_bound(self, start, declared_end)
                # This is a hard image/PDB bound, not a candidate entry. The
                # donor probes beyond gap-prologue candidates recursively;
                # passing the bound as one can reenter this same bounded body.
                end = original_end(self, start, None, upper)
                if not start < end <= declared_end:
                    raise ValueError(f'decoded body crosses PDB boundary: {start:#x}')
                return end
            owner_end = ranges.containing_end(start)
            if owner_end is not None:
                sec_end = min(sec_end, owner_end) if sec_end else owner_end
                if next_func is not None and next_func >= sec_end:
                    next_func = None
        return original_end(self, start, next_func, sec_end)

    def bounded_context(self, start, owner_end):
        first = next(self.engine._cs.disasm(
            self.image.read_bytes_at_va(start, 15) or b'', start, count=1), None)
        if first is None or first.address + first.size > owner_end:
            return None
        if self.engine.get_instruction(start) is None:
            self.engine.decode_at(start)
        end = self._find_function_end(start, None, owner_end)
        instructions = self.engine.get_instructions_in_range(start, end)
        if (start < end <= owner_end and instructions and
                instructions[-1].end_address == end):
            return end, len(instructions)
        return None

    def prune_crossing_interiors(self):
        ranges = getattr(self, '_xboxllr_pdb_ranges', None)
        if ranges is None:
            return
        registered = None
        for start, body in list(self.functions.items()):
            if start in ranges.ends:
                continue
            owner_end = ranges.containing_end(start)
            if owner_end is None:
                continue
            # Inspect only final views. Filtering before the detection passes
            # finish would let transient candidates change unrelated bounds.
            # Even a clamped alias cannot start with an instruction borrowing
            # bytes from the next owner. Complete body/ABI qualification is a
            # separate gate; this does not prove arbitrary interior streams.
            first = next(self.engine._cs.disasm(
                self.image.read_bytes_at_va(start, 15) or b'', start, count=1), None)
            crossing = (body.end > owner_end or
                        first is not None and first.address + first.size > owner_end)
            if crossing:
                # Existing aliases are skipped by the donor alias builder, so
                # clamping _alias_entries alone leaves their old overrun in
                # functions. A registered frame context has positive entry
                # authority; remeasure it within the original owning bound.
                # An instruction borrowing the next owner's bytes still fails.
                if first is not None and first.address + first.size <= owner_end:
                    if registered is None:
                        registered = _unwind_context_entries(
                            self, self.image.get_code_sections())
                    if start in registered:
                        bounded = bounded_context(self, start, owner_end)
                        if bounded is not None:
                            end, count = bounded
                            body.end = end
                            body.num_instructions = count
                            if start in self._alias_entries:
                                self._alias_entries[start] = end
                            continue
                self._candidates.pop(start, None)
                self.functions.pop(start, None)
                self._alias_entries.pop(start, None)

    def prune_data(self):
        ranges = getattr(self, '_xboxllr_pdb_ranges', None)
        if ranges is not None:
            data_bounds = {}
            for start in sorted(self.functions.keys() | self._alias_entries.keys()):
                if start in ranges.ends:
                    continue
                owner = ranges.containing_start(start)
                if owner is None:
                    continue
                if owner not in data_bounds:
                    data_bounds[owner] = code_upper_bound(self, owner, ranges.ends[owner])
                if data_bounds[owner] <= start < ranges.ends[owner]:
                    # A decoded pointer/index table can manufacture an alias
                    # just as it manufactures ordinary instructions. It is not
                    # a second callable body. Explicit PDB entries stay intact.
                    self._alias_entries.pop(start, None)
                    self.functions.pop(start, None)
                    self._candidates.pop(start, None)
    def aliases(self):
        prune_data(self)
        ranges = getattr(self, '_xboxllr_pdb_ranges', None)
        if ranges is not None:
            # Registered catches can have neither a prologue nor a returning
            # tail, so the heuristic passes may never create a candidate.
            # Their bounded HandlerType records provide entry authority;
            # require an enclosing exact PDB extent before admitting an alias.
            handlers = _unwind_context_entries(
                self, self.image.get_code_sections(), handlers_only=True)
            for start in handlers - self.functions.keys():
                owner_end = ranges.containing_end(start)
                if owner_end is not None:
                    bounded = bounded_context(self, start, owner_end)
                    if bounded is not None:
                        self._alias_entries[start] = bounded[0]
            for start, end in self._alias_entries.items():
                owner_end = ranges.containing_end(start)
                if owner_end is not None:
                    self._alias_entries[start] = min(end, owner_end)
        original_aliases(self)
        prune_crossing_interiors(self)

    def alias_tails(self, sections):
        # Do not follow branches hallucinated inside index bytes into more
        # fabricated callable roots before rejecting the original false alias.
        prune_data(self)
        return original_alias_tails(self, sections)

    detector.detect_all = detect
    detector._find_function_end = function_end
    detector._build_alias_entries = aliases
    detector._pass_alias_tail_jumps = alias_tails
    detector._table_after = _bounded_table_after
    detector._xboxllr_symbol_extension = True
