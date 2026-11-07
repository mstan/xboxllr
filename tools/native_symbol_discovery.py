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
from native_discovery import _alignment_padding, code_upper_bound


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


def install(module, qualified):
    detector = module.FunctionDetector
    if getattr(detector, '_xboxllr_symbol_extension', False):
        raise ValueError('PDB discovery is already configured in this process')
    original_detect = detector.detect_all
    original_end = detector._find_function_end
    original_aliases = detector._build_alias_entries
    original_alias_tails = detector._pass_alias_tail_jumps

    def table_after(self, address, upper):
        # A nearby table does not make intervening instructions alignment.
        # In particular a virtual tail jump can precede a shared RET and then
        # the trailing switch tables. Skipping that RET walks index bytes as
        # code, even past an exact PDB length. Keep real interleaved tables,
        # but require the bounded alignment which the donor only assumes.
        dispatches = [i for i in self.engine.get_instructions_in_range(address - 15, address)
                      if i.end_address == address and i.is_jump and i.jump_table is not None]
        # A backwards/direct or virtual tail jump can also end immediately
        # before a table used elsewhere in this body. It does not dispatch
        # through that table and must still terminate once its targets are
        # covered. In particular, index bytes may resemble more code pointers.
        tables = {self.engine._jt_by_disp.get(i.jump_table, i.jump_table) for i in dispatches}
        for table in range(address, address + 16):
            end = self.engine.jump_tables.get(table)
            if (table in tables and end is not None and end <= upper and
                    _alignment_padding(self, address, table)):
                return end
        return None

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
    detector._table_after = table_after
    detector._xboxllr_symbol_extension = True
