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


def _terminal_virtual_tail(detector, jump, table, prefix, targets):
    """Recognize a this-pointer vtable tail immediately before trailing data.

    This is a code/data boundary proof, not a callable-entry or stack-ABI proof.
    Keep unknown computed continuations conservative. In particular, a virtual
    jump earlier in a body cannot hide a shared return or cases after the table.
    """
    from capstone import CS_OP_MEM, CS_OP_REG
    if not _alignment_padding(detector, jump.end_address, table):
        return False
    for address in range(jump.address - 6, jump.address):
        if address not in detector.engine.instructions:
            continue
        raw = detector.image.read_bytes_at_va(address, jump.end_address-address)
        suffix = list(detector.engine._cs.disasm(raw or b'', address))
        if not 2 <= len(suffix) <= 6 or suffix[-1].address != jump.address:
            continue
        load, dispatch = suffix[0], suffix[-1]
        if load.mnemonic != 'mov' or len(load.operands) != 2:
            continue
        dst, src = load.operands
        if (dst.type != CS_OP_REG or dst.size != 4 or
                load.reg_name(dst.reg) not in ('eax', 'edx') or
                src.type != CS_OP_MEM or src.size != 4 or
                load.reg_name(src.mem.base) != 'ecx' or
                src.mem.index or src.mem.disp or src.mem.segment):
            continue
        if any(i.mnemonic != 'pop' or len(i.operands) != 1 or
               i.operands[0].type != CS_OP_REG or i.operands[0].size != 4 or
               i.reg_name(i.operands[0].reg) not in ('ebx', 'esi', 'edi', 'ebp')
               for i in suffix[1:-1]):
            continue
        if dispatch.mnemonic != 'jmp' or len(dispatch.operands) != 1:
            continue
        slot = dispatch.operands[0]
        if (slot.type != CS_OP_MEM or slot.size != 4 or
                slot.mem.base != dst.reg or slot.mem.index or slot.mem.segment or
                slot.mem.disp < 0 or slot.mem.disp % 4):
            continue
        # A branch into the suffix could bypass its vtable load/restoration.
        incoming = list(targets) + [i.jump_target for i in prefix
                                   if i.is_branch and i.jump_target is not None]
        if not any(address < target <= jump.address for target in incoming):
            return True
    return False


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
        if any(i.is_jump and i.jump_target is None and i.jump_table is None and
               not _terminal_virtual_tail(detector, i, table, prefix, targets)
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


def _bounded_table_after(self, address, upper):
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


def _catch_resume_context_entries(detector, sections, states, count, table):
    """Recognize constant resume addresses returned by original catch handlers.

    The bounded Win32 FuncInfo try map identifies each catch handler. A handler
    consisting of MOV EAX, address; RET returns a continuation in the original
    frame, not an ordinary function pointer. Preserve existing candidates for
    those continuations without seeding arbitrary address-looking constants.
    """
    if not 0 < count <= 4096:
        return set()
    blocks = detector.image.read_bytes_at_va(table, count * 20)
    if blocks is None or len(blocks) != count * 20:
        return set()
    resumes, total = set(), 0
    for index in range(count):
        low, high, catch_high, catches, handlers = struct.unpack_from(
            '<iiiII', blocks, index * 20)
        total += catches
        if not (0 <= low <= high <= catch_high < states and
                0 < catches <= 4096 and total <= 4096):
            return set()
        records = detector.image.read_bytes_at_va(handlers, catches * 16)
        if records is None or len(records) != catches * 16:
            return set()
        for catch in range(catches):
            _, _, _, handler = struct.unpack_from('<4I', records, catch * 16)
            if detector.image.get_section_at_va(handler) not in sections:
                return set()
            body = detector.image.read_bytes_at_va(handler, 6)
            if body is None or len(body) != 6 or body[0] != 0xb8 or body[5] != 0xc3:
                continue
            resume = struct.unpack_from('<I', body, 1)[0]
            if detector.image.get_section_at_va(resume) in sections:
                resumes.add(resume)
    return resumes


def _unwind_context_entries(detector, sections):
    """Retain frame-dependent actions/resumes referenced by bounded MSVC metadata.

    A decoded mov-EAX/near-jump thunk references FuncInfo. Its recognized magic,
    bounded unwind array, decreasing states and code-backed actions establish
    context entries, without treating them as ordinary callable functions.
    This only preserves candidates; it does not seed entries from metadata.
    """
    actions, seen = set(), set()
    for instruction in list(detector.engine.instructions.values()):
        if instruction.mnemonic != 'mov' or not instruction.op_str.startswith('eax, 0x'):
            continue
        following = detector.engine.instructions.get(instruction.end_address)
        if (following is None or not following.is_jump or following.is_cond_jump or
                following.jump_target is None):
            continue
        try:
            address = int(instruction.op_str[5:], 16)
        except ValueError:
            continue
        if address in seen:
            continue
        seen.add(address)
        header = detector.image.read_bytes_at_va(address, 28)
        if header is None or len(header) != 28:
            continue
        magic, count, table, tries, try_table, _, _ = struct.unpack('<7I', header)
        if magic & 0x1fffffff not in (0x19930520, 0x19930521, 0x19930522) or not 0 < count <= 4096:
            continue
        data = detector.image.read_bytes_at_va(table, count * 8)
        if data is None or len(data) != count * 8:
            continue
        entries = set()
        for state in range(count):
            previous, action = struct.unpack_from('<iI', data, state * 8)
            section = detector.image.get_section_at_va(action) if action else None
            if not -1 <= previous < state or (action and section not in sections):
                break
            if action:
                entries.add(action)
        else:
            actions.update(entries)
            actions.update(_catch_resume_context_entries(
                detector, sections, count, tries, try_table))
    return actions


def _unaligned_leaf_cleanup(detector, address):
    """A weak ordinary-entry guess cannot establish a packed-stack ABI."""
    from capstone import CS_GRP_CALL, CS_GRP_JUMP
    data = detector.image.read_bytes_at_va(address, 512)
    for index, instruction in enumerate(detector.engine._cs.disasm(data or b'', address)):
        if index >= 64:
            break
        if instruction.group(CS_GRP_CALL) or instruction.group(CS_GRP_JUMP) or instruction.mnemonic.startswith('loop'):
            break
        if instruction.mnemonic == 'ret':
            return bool(instruction.operands and instruction.operands[0].imm % 4)
    return False


def _text_literal_prefix(detector, address):
    """Recognize a bounded ASCIIZ prefix before admitting a weak code guess."""
    data = detector.image.read_bytes_at_va(address, 256)
    if not data or b'\0' not in data:
        return False
    text = data.split(b'\0', 1)[0]
    return (len(text) >= 8 and
            all(32 <= value <= 126 or value in (9, 10, 13) for value in text) and
            sum(65 <= value <= 90 or 97 <= value <= 122 for value in text) >= 6)


def _allocating_saved_frame(detector, address):
    """Recognize the FPO equivalent of an established saved-register frame."""
    from capstone.x86 import X86_OP_IMM, X86_OP_REG
    operations = list(detector.engine._cs.disasm(
        detector.image.read_bytes_at_va(address, 16) or b'', address, count=2))
    if len(operations) != 2:
        return False
    reserve, save = operations
    return (reserve.mnemonic == 'sub' and len(reserve.operands) == 2 and
            reserve.operands[0].type == X86_OP_REG and reserve.operands[0].size == 4 and
            reserve.reg_name(reserve.operands[0].reg) == 'esp' and
            reserve.operands[1].type == X86_OP_IMM and
            0 < reserve.operands[1].imm <= 4096 and reserve.operands[1].imm % 4 == 0 and
            save.mnemonic == 'push' and save.op_str in ('ebx', 'esi', 'edi', 'ebp'))


def _crossed_weak_callbacks(detector):
    """Find final callback guesses that cut an established caller's frame.

    A guessed immediate target with no caller/prologue is not an independent
    bound when it restores an unsaved register and the preceding body's own
    conditional branch crosses it. Keep real calls, seeds and frame-free
    callbacks. This runs only after all ordinary discovery passes have finished.
    """
    for start, body in sorted(detector.functions.items()):
        if detector.functions.get(start) is not body:
            continue
        weak = detector.functions.get(body.end)
        if (weak is None or weak.detection_method != 'imm_ref_target' or
                weak.called_by or weak.has_prologue):
            continue
        if not (body.called_by or body.has_prologue or
                body.detection_method == 'entry_point' or
                body.detection_method.startswith('seed') or
                _allocating_saved_frame(detector, start)):
            continue
        weak_instructions = detector.engine.get_instructions_in_range(weak.start, weak.end)
        if (not weak_instructions or not weak_instructions[-1].is_ret or
                weak_instructions[-1].end_address != weak.end):
            continue
        # An explicit stack reset may be an independently entered exception
        # handler. Its transfer contract needs separate original evidence.
        if any(i.mnemonic == 'leave' or i.mnemonic == 'mov' and
               i.op_str.split(',')[0].strip() in ('esp', 'sp') for i in weak_instructions):
            continue
        section = detector.image.get_section_at_va(start)
        if section is None:
            continue
        following = min((a for a in detector.functions if a > weak.start),
                        default=section.virtual_addr + section.virtual_size)
        instructions = detector.engine.get_instructions_in_range(start, body.end)
        if not any(i.is_cond_jump and i.jump_target is not None and
                   weak.start <= i.jump_target < following for i in instructions):
            continue
        if any(i.is_jump and not i.is_cond_jump and i.jump_target == weak.start
               for i in instructions):
            continue
        unsaved = set(detector.engine.entry_pops_unsaved(weak.start))
        if not unsaved:
            continue
        # Require a still-live original save before the crossing branch. Saves
        # after an earlier conditional are not a dominating frame witness.
        saved = set()
        branched = False
        for instruction in instructions:
            operand = instruction.op_str.split(',')[0].strip()
            if (instruction.is_call or instruction.is_ret or instruction.mnemonic in
                    ('pop', 'popal', 'popa', 'pushal', 'pusha', 'leave') or
                    instruction.is_jump and not instruction.is_cond_jump or
                    instruction.mnemonic in ('mov', 'add', 'lea', 'xchg') and
                    operand in ('esp', 'sp')):
                break
            if not branched and instruction.mnemonic == 'push':
                saved.add(operand)
            if instruction.is_cond_jump:
                branched = True
                if (instruction.jump_target is not None and
                        weak.start <= instruction.jump_target < following and
                        unsaved <= saved):
                    yield body, weak, following, section
                    break


def _repair_weak_callback_clamps(detector):
    """Remeasure only proved final frame contexts with the existing walker."""
    candidates = list(_crossed_weak_callbacks(detector))
    if not candidates:
        return False
    registered = _unwind_context_entries(detector, detector.image.get_code_sections())
    weak_starts = {weak.start for _, weak, _, _ in candidates}
    incoming = {}
    independent = set()
    for instruction in detector.engine.instructions.values():
        if instruction.imm_ref in weak_starts:
            independent.add(instruction.imm_ref)
        if instruction.is_call and instruction.call_target in weak_starts:
            independent.add(instruction.call_target)
        target = (instruction.call_target if instruction.is_call else
                  instruction.jump_target if instruction.is_branch else None)
        if target in weak_starts:
            incoming.setdefault(target, []).append(instruction.address)
    changed = False
    for body, weak, following, section in candidates:
        if (detector.functions.get(body.start) is not body or
                detector.functions.get(weak.start) is not weak or weak.start in registered):
            continue
        previous = (dict(detector.functions), dict(detector._candidates), set(detector._absorbed))
        end = detector._find_function_end(
            body.start, following, section.virtual_addr + section.virtual_size)
        instructions = detector.engine.get_instructions_in_range(body.start, body.end)
        if (end <= weak.start or any(not body.start <= source < end
                                    for source in incoming.get(weak.start, ())) or
                any(i.is_cond_jump and i.jump_target is not None and
                    weak.start <= i.jump_target < following and i.jump_target >= end
                    for i in instructions)):
            detector.functions, detector._candidates, detector._absorbed = previous
            continue
        removed = set(previous[0]) - detector.functions.keys()
        body.end = end
        body.num_instructions = len(detector.engine.get_instructions_in_range(body.start, end))
        # An address-taken context can coexist with its enclosing original
        # body. Its references require retaining that entry, but do not make
        # the enclosing body's own crossed branches into external calls.
        if weak.start not in independent:
            detector.functions.pop(weak.start, None)
            detector._candidates.pop(weak.start, None)
            detector._alias_entries.pop(weak.start, None)
            detector._absorbed.add(weak.start)
        for start in removed:
            detector._alias_entries.pop(start, None)
        changed = True
    if changed:
        for body in detector.functions.values():
            body.calls_to.clear()
            body.called_by.clear()
        detector._build_call_graph()
    return changed


def _switch_pointer_tail(detector, address):
    """Do not turn a sparse dispatch table's remaining words into code."""
    for table, end in detector.engine.jump_tables.items():
        if table <= address < end:
            return True
        gap = address - end
        if not 4 <= gap <= 16 or gap % 4 or (end - table) % 4:
            continue
        if detector.image.read_bytes_at_va(end, gap) != bytes(gap):
            continue
        original = detector.engine.jump_table_entries(table)
        raw = detector.image.read_bytes_at_va(address, 12)
        if len(original) < 2 or raw is None or len(raw) != 12:
            continue
        section = detector.image.get_section_at_va(original[0])
        if section is None or not section.executable:
            continue
        if all(target in detector.engine.instructions and
               detector.image.get_section_at_va(target) is section
               for target in struct.unpack('<3I', raw)):
            return True
    return False


def _switch_index_data_window(detector, address):
    """Prove a bounded byte-index run containing a weak sentinel entry.

    Sparse switches can retain byte indices equal to the pointer-entry count.
    The donor stops its byte scan on that sentinel value and can call it code.
    Require the original two-level consumer, local code targets, the complete
    low-byte run and alignment to an independently retained next entry.
    """
    from capstone.x86 import X86_OP_REG, X86_OP_MEM
    section = detector.image.get_section_at_va(address)
    if section is None:
        return None
    lower = max(section.virtual_addr, address - 512)
    raw = detector.image.read_bytes_at_va(lower, address - lower) or b''
    for offset in range(len(raw) - 1):
        if raw[offset:offset + 2] != b'\x0f\xb6':
            continue
        pair = list(detector.engine._cs.disasm(raw[offset:], lower + offset, count=3))
        if len(pair) < 2 or pair[0].mnemonic != 'movzx':
            continue
        load, jump = pair[:2]
        if jump.mnemonic == 'mov' and len(pair) == 3:
            # A scheduled absolute global store changes neither index register.
            ops = jump.operands
            if (len(ops) != 2 or ops[0].type != X86_OP_MEM or ops[0].size != 4 or
                    ops[1].type != X86_OP_REG or ops[0].mem.base or
                    ops[0].mem.index or ops[0].mem.segment or
                    detector.image.get_section_at_va(ops[0].mem.disp) is section):
                continue
            jump = pair[2]
        if jump.mnemonic != 'jmp':
            continue
        if (len(load.operands) != 2 or load.operands[0].type != X86_OP_REG or
                load.operands[0].size != 4 or load.operands[1].type != X86_OP_MEM or
                load.operands[1].size != 1 or len(jump.operands) != 1 or
                jump.operands[0].type != X86_OP_MEM):
            continue
        source, table_op = load.operands[1].mem, jump.operands[0].mem
        if (not source.base or source.index or source.segment or table_op.base or
                table_op.segment or table_op.index != load.operands[0].reg or
                table_op.scale != 4):
            continue
        table = table_op.disp
        if not jump.address + jump.size <= table < address:
            continue
        owners = [f for f in detector.functions.values()
                  if f.start <= load.address and jump.address + jump.size <= f.end and
                  load.address - f.start <= 4096 and f.detection_method != 'after_switch_table']
        if not any(any(i.address == load.address for i in detector.engine._cs.disasm(
                detector.image.read_bytes_at_va(f.start, load.address + load.size - f.start) or b'',
                f.start)) for f in owners):
            continue
        targets = []
        for index in range(17):
            word = detector.image.read_bytes_at_va(table + 4 * index, 4)
            target = struct.unpack('<I', word)[0] if word and len(word) == 4 else 0
            if not load.address - 32 <= target < table or detector.image.get_section_at_va(target) is not section:
                break
            targets.append(target)
        entries = len(targets)
        end = table + 4 * entries
        if not 2 <= entries <= 16 or not end - 4 <= source.disp <= end + 3:
            continue
        data = detector.image.read_bytes_at_va(end, 257) or b''
        first = next((i for i, byte in enumerate(data) if byte >= entries), len(data))
        tail = next((i for i, byte in enumerate(data) if byte > entries), len(data))
        if (first == len(data) or data[first] != entries or address != end + first or
                tail >= 256 or tail == len(data) or data[tail] not in (0x90, 0xcc)):
            continue
        padding = tail
        while padding < len(data) and padding - tail < 15 and data[padding] in (0x90, 0xcc):
            padding += 1
        next_entry = end + padding
        following = detector.functions.get(next_entry)
        if (next_entry % 16 or following is None or
                following.detection_method == 'after_switch_table'):
            continue
        return dict(consumer=load.address, dispatch=jump.address, table=table,
                    table_end=end, entries=entries, index_base=source.disp,
                    index_data_start=end, index_data_end=end + tail, next_entry=next_entry)
    return None


def _original_entry_contexts(detector, sections):
    entries = {i.jump_target for i in detector.engine.instructions.values()
               if i.is_branch and i.jump_target is not None}
    entries.update(i.call_target for i in detector.engine.instructions.values()
                   if i.is_call and i.call_target is not None)
    for table in detector.engine.jump_tables:
        entries.update(detector.engine.jump_table_entries(table))
    entries.update(_unwind_context_entries(detector, sections))
    return entries


def _parameter_data_windows(detector):
    """Prove minimum byte-table spans from a guarded parameter clamp loop.

    The counter is zero-extended from an equality-validated byte. A pointer
    and its negated initial value form indices 0..count-1 into readonly
    default/minimum/maximum tables. Validate the whole loop and its entries;
    table bytes are not CALL instructions merely because a sweep decodes E8.
    No names, library versions or inferred next-symbol sizes participate.
    """
    from capstone.x86 import X86_OP_REG, X86_OP_MEM, X86_OP_IMM
    engine = detector.engine
    instructions = list(engine.instructions.values())
    previous = {i.end_address: i for i in instructions}
    incoming = {}
    for i in instructions:
        target = i.call_target if i.is_call else i.jump_target if i.is_branch else None
        if target is not None:
            incoming.setdefault(target, []).append(i)
    decoded = {}
    def cs(i):
        if i.address not in decoded:
            raw = detector.image.read_bytes_at_va(i.address, i.size)
            decoded[i.address] = next(engine._cs.disasm(raw or b'', i.address, count=1), None)
        return decoded[i.address]
    def reg(i, n, value, size=None):
        return len(i.operands)>n and i.operands[n].type==X86_OP_REG and i.operands[n].reg==value and (size is None or i.operands[n].size==size)
    def memory(op):
        m=op.mem
        return (m.segment,m.base,m.index,m.scale,m.disp,op.size)
    def at(i, target):
        return len(i.operands)==1 and i.operands[0].type==X86_OP_IMM and i.operands[0].imm==target
    def cell(r):
        name=engine._cs.reg_name(r)
        for full, aliases in {'eax':('ax','al','ah'),'ebx':('bx','bl','bh'),'ecx':('cx','cl','ch'),'edx':('dx','dl','dh'),'esi':('si',),'edi':('di',),'ebp':('bp',),'esp':('sp',)}.items():
            if name==full or name in aliases:return full
        return name
    windows=set()
    pattern=['mov','mov','test','jne','mov','jmp','mov','cmp','jae','mov','jmp','mov','cmp','ja','mov','inc','inc','dec','jne']
    for back in instructions:
        if back.mnemonic!='jne' or back.jump_target is None or not 0<back.address-back.jump_target<=192:
            continue
        own=engine.get_instructions_in_range(back.jump_target,back.end_address)
        if len(own)!=len(pattern) or [i.mnemonic for i in own]!=pattern:
            continue
        loop=[cs(i) for i in own]
        if any(i is None for i in loop) or any(a.address+a.size!=b.address for a,b in zip(loop,loop[1:])):
            continue
        if loop[16].operands[0].type!=X86_OP_REG or loop[17].operands[0].type!=X86_OP_REG:
            continue
        index,counter=loop[16].operands[0].reg,loop[17].operands[0].reg
        if not reg(loop[16],0,index,4) or not reg(loop[17],0,counter,4) or index==counter:
            continue
        if loop[0].operands[0].type!=X86_OP_REG or loop[0].operands[1].type!=X86_OP_MEM or loop[1].operands[0].type!=X86_OP_REG:
            continue
        pointer,byte=loop[0].operands[0].reg,loop[1].operands[0].reg
        if not reg(loop[1],0,byte,1) or loop[1].operands[1].type!=X86_OP_MEM:
            continue
        m=loop[1].operands[1].mem
        if m.segment or m.base!=pointer or m.index or m.disp or loop[1].operands[1].size!=1:
            continue
        if not(reg(loop[2],0,byte) and reg(loop[2],1,byte)) or loop[6].operands[0].type!=X86_OP_REG:
            continue
        limit=loop[6].operands[0].reg
        if not all(reg(loop[k],0,byte) and reg(loop[k],1,limit) for k in (7,12)):
            continue
        if not all(at(loop[k],loop[t].address) for k,t in ((3,6),(5,14),(8,11),(10,15),(13,9),(18,0))):
            continue
        stores=[loop[k].operands for k in (9,14)]
        if any(len(o)!=2 or o[0].type!=X86_OP_MEM or o[0].size!=1 or o[0].mem.segment or o[0].mem.base!=index or o[0].mem.index or o[0].mem.disp for o in stores):
            continue
        if not reg(loop[9],1,limit,1) or not reg(loop[14],1,byte,1) or loop[15].operands[0].type!=X86_OP_MEM or memory(loop[15].operands[0])!=memory(loop[0].operands[1]):
            continue
        before=engine.get_instructions_in_range(loop[0].address-64,loop[0].address)
        if len(before)<7:continue
        init=[cs(i) for i in before[-7:]]
        if any(i is None for i in init) or [i.mnemonic for i in init]!=['movzx','dec','mov','js','mov','neg','inc'] or any(a.address+a.size!=b.address for a,b in zip(init,init[1:])) or init[-1].address+init[-1].size!=loop[0].address:
            continue
        if not(reg(init[0],0,counter,4) and reg(init[0],1,byte,1) and reg(init[1],0,counter) and reg(init[2],0,index) and init[2].operands[1].type==X86_OP_REG and init[4].operands[0].type==X86_OP_REG):
            continue
        base,bias=init[2].operands[1].reg,init[4].operands[0].reg
        if len({index,bias,counter})!=3 or not(reg(init[4],1,base) and reg(init[5],0,bias) and reg(init[6],0,counter)) or not at(init[3],back.end_address):
            continue
        cells={cell(index),cell(bias),cell(counter)}
        if any(cells & {cell(r) for r in i.regs_access()[1]} for i in loop[:16]):
            continue
        entries=[init[0].address]
        p=previous.get(init[0].address)
        if p is not None:
            p=cs(p)
            if p is not None and p.mnemonic=='mov' and len(p.operands)==2 and p.operands[0].type==X86_OP_MEM and p.operands[1].type==X86_OP_REG:
                entries.append(p.address)
        bound=None
        for entry in entries:
            p=previous.get(entry)
            if p is None or p.mnemonic not in ('jmp','ret','retf'):continue
            if entry<=detector.image.entry_point<back.end_address or any(entry<=a<back.end_address for a in detector._candidates):continue
            values=[];valid=True
            for target,sources in incoming.items():
                if not entry<=target<back.end_address:continue
                for source in sources:
                    if entry<=source.address<back.end_address:continue
                    compare=previous.get(source.address)
                    compare=cs(compare) if compare is not None else None
                    if target!=entry or source.mnemonic!='je' or compare is None or compare.mnemonic!='cmp' or not reg(compare,0,byte,1) or len(compare.operands)!=2 or compare.operands[1].type!=X86_OP_IMM or not 8<=compare.operands[1].imm<=255:
                        valid=False;break
                    values.append(compare.operands[1].imm)
                if not valid:break
            if valid and values:
                bound=min(values);break
        if bound is None:continue
        reads=[loop[k] for k in (4,6,11)]
        if not reg(reads[0],0,byte,1) or not all(reg(i,0,limit,1) for i in reads[1:]):continue
        if any(len(i.operands)!=2 or i.operands[1].type!=X86_OP_MEM or i.operands[1].size!=1 or i.operands[1].mem.segment or {i.operands[1].mem.base,i.operands[1].mem.index}!={index,bias} or i.operands[1].mem.scale!=1 for i in reads):
            continue
        starts={i.operands[1].mem.disp for i in reads}
        if len(starts)!=3:continue
        for start in starts:
            end=start+bound;section=detector.image.get_section_at_va(start)
            if section is None or section.writable or detector.image.get_section_at_va(end-1) is not section or not (end<=entries[-1] or start>=back.end_address):continue
            if any(start<=a<end for a in detector._candidates) or start<=detector.image.entry_point<end:continue
            if any(start<=target<end and any(not start<=s.address<end for s in sources) for target,sources in incoming.items()):continue
            raw=detector.image.read_bytes_at_va(start,bound)
            if raw is not None and len(raw)==bound:windows.add((start,end))
    return sorted(windows)


class _CallSourceView:
    def __init__(self, engine, targets):
        self.engine,self.targets=engine,targets

    def __getattr__(self, name):
        return getattr(self.engine,name)

    def get_call_targets(self):
        return self.targets


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
    original_table_after = detector._table_after
    original_add = detector._add_candidate
    original_calls = detector._pass_call_targets
    original_detect = detector.detect_all

    def detect(self, sections=None):
        original_detect(self, sections)
        _repair_weak_callback_clamps(self)
        # Final-output filtering leaves every earlier discovery/extent decision
        # intact. These unreferenced low-byte views have no call-graph edges.
        for address, body in list(self.functions.items()):
            if (body.detection_method == 'after_switch_table' and
                    not body.calls_to and not body.called_by and
                    _switch_index_data_window(self, address) is not None):
                del self.functions[address]
                self._candidates.pop(address, None)
                self._alias_entries.pop(address, None)
        return len(self.functions)

    detector.detect_all = detect

    def call_targets(self, sections):
        windows=_parameter_data_windows(self)
        if not windows:return original_calls(self,sections)
        engine=self.engine;inside=set();outside=set()
        for i in engine.instructions.values():
            if i.is_call and i.call_target is not None:
                (inside if any(a<=i.address<b for a,b in windows) else outside).add(i.call_target)
        targets=engine.get_call_targets()-(inside-outside)
        self.engine=_CallSourceView(engine,targets)
        try:return original_calls(self,sections)
        finally:self.engine=engine

    detector._pass_call_targets = call_targets

    def add_candidate(self, address, confidence, method):
        if method in ('cc_boundary', 'after_switch_table', 'imm_ref_target') and _text_literal_prefix(self, address):
            # Strings in executable SDK contributions can decode as PUSH/POP
            # and branches. A cleanup boundary or literal address does not
            # establish an entry there; original control/metadata still can.
            if address not in _original_entry_contexts(self, self.image.get_code_sections()):
                return
        if method in ('after_switch_table', 'imm_ref_target') and (
                _unaligned_leaf_cleanup(self, address) or self.engine.entry_pops_unsaved(address)):
            # Call targets and explicit metadata roots can establish another
            # convention. A table-adjacent byte stream or address-like literal
            # alone cannot establish a saved frame or packed-stack contract.
            if address not in _original_entry_contexts(self, self.image.get_code_sections()):
                return
        if method == 'after_switch_table':
            if _switch_pointer_tail(self, address):
                return
            for table, end in self.engine.jump_tables.items():
                if address != end + 256:
                    continue
                entries = (end - table) // 4
                indices = self.image.read_bytes_at_va(end, 257)
                if (entries >= 2 and indices is not None and len(indices) == 257 and
                        max(indices) < entries):
                    # Exhausting the donor's scan budget does not establish
                    # that the byte-index table ended. Other proven roots
                    # remain eligible through their original admission path.
                    return
        original_add(self, address, confidence, method)

    detector._add_candidate = add_candidate

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
        # The donor records these as overlapping context aliases after fixing
        # whole-function bounds. Keep that representation without treating the
        # data word as an independent saved-frame or ordinary ABI proof.
        return xpp_registration_entries(self, sections) or bool(
            set(self._alias_entries) - previous_aliases)

    detector._pass_data_ptr_targets = data_tables

    def table_after(self, address, upper):
        ending = [i for i in self.engine.get_instructions_in_range(address-15, address)
                  if i.end_address == address and i.is_jump]
        if any(i.jump_table is None and i.jump_target is None for i in ending):
            # An unresolved computed continuation has no declared owner here.
            # Keep the donor's conservative extent instead of assuming a tail
            # call; the supplied-PDB path has its own certified body bound.
            return original_table_after(self, address, upper)
        return _bounded_table_after(self, address, upper)

    detector._table_after = table_after
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
