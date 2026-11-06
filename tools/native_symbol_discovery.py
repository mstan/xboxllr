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


def code_upper_bound(detector, start, declared_end):
    """Exclude a proven trailing switch table and its index-remapping data.

    MSVC can put a nonreturning error call immediately before the table. The
    donor's sequential walker skips the pointer table, then decodes the compact
    byte-index map after it. Neither table supplies additional executable code.
    A table with cases/branches beyond it remains an interleaved table instead.
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
        # Every indirect jump in the code prefix must have a known table;
        # an unresolved computed continuation could reach code after this one.
        if any(i.is_jump and i.jump_target is None and i.jump_table is None
               for i in prefix):
            continue
        return table
    return declared_end


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
                end = original_end(self, start, upper, upper)
                if not start < end <= declared_end:
                    raise ValueError(f'decoded body crosses PDB boundary: {start:#x}')
                return end
            owner_end = ranges.containing_end(start)
            if owner_end is not None:
                sec_end = min(sec_end, owner_end) if sec_end else owner_end
        return original_end(self, start, next_func, sec_end)

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
            for start, end in self._alias_entries.items():
                owner_end = ranges.containing_end(start)
                if owner_end is not None:
                    self._alias_entries[start] = min(end, owner_end)
        original_aliases(self)

    def alias_tails(self, sections):
        # Do not follow branches hallucinated inside index bytes into more
        # fabricated callable roots before rejecting the original false alias.
        prune_data(self)
        return original_alias_tails(self, sections)

    detector.detect_all = detect
    detector._find_function_end = function_end
    detector._build_alias_entries = aliases
    detector._pass_alias_tail_jumps = alias_tails
    detector._xboxllr_symbol_extension = True
