"""Bounded loaded-image bytes for discovery and CPU lowering.

File padding and neighbouring raw sections are not guest memory. A section's
declared virtual tail is zero-filled by the runtime, including instruction
operands which the linker omits from its file payload.
"""
import struct
import importlib


def section_window(blob, section, address, count):
    start, size, raw, initialized = section
    if (type(count) is not int or count <= 0 or type(address) is not int or
            not 0 <= start <= address < start+size <= 0x100000000 or
            not 0 <= initialized <= size or not 0 <= raw <= raw+initialized <= len(blob)):
        return None
    offset = address-start
    length = min(count, size-offset)
    available = max(0, min(length, initialized-offset))
    return blob[raw+offset:raw+offset+available] + bytes(length-available)


def install_discovery(loader):
    image_type = loader.BinaryImage
    if getattr(image_type, '_xboxllr_loaded_image_reads', False):
        return

    def coordinates(section):
        return section.virtual_addr, section.virtual_size, section.raw_addr, section.raw_size

    def read(image, address, count):
        if type(address) is not int or type(count) is not int or count <= 0:
            return None
        section = image.get_section_at_va(address)
        if section is not None:
            return section_window(image.raw_data, coordinates(section), address, count)
        if (image.raw_data[:4] == b'XBEH' and len(image.raw_data) >= 0x10c and
                type(address) is int and type(count) is int and count > 0):
            headers = struct.unpack_from('<I', image.raw_data, 0x108)[0]
            if image.base_address <= address < image.base_address+headers <= len(image.raw_data)+image.base_address:
                offset = address-image.base_address
                return image.raw_data[offset:min(headers, offset+count)]
        return None

    def read_u32(image, address):
        raw = read(image, address, 4)
        return struct.unpack('<I', raw)[0] if raw is not None and len(raw) == 4 else None

    def section_data(image, section):
        if not section.raw_size:
            return b''
        # Complete at most one x86 instruction at the raw boundary. Do not
        # materialize/sweep an arbitrary BSS region as executable opcodes.
        count = section.raw_size + min(15, max(0, section.virtual_size-section.raw_size))
        return section_window(image.raw_data, coordinates(section), section.virtual_addr, count) or b''

    image_type.read_bytes_at_va = read
    image_type.read_u32_at_va = read_u32
    image_type.get_section_data = section_data
    image_type._xboxllr_loaded_image_reads = True


def install_lifting(module):
    translator = module.FunctionTranslator
    if getattr(translator, '_xboxllr_loaded_image_reads', False):
        return

    def read_window(instance, start, count):
        if getattr(instance, '_xboxllr_image_source', None) is not instance.xbe_data:
            from aot_inventory import parse_xbe
            image = parse_xbe(instance.xbe_data)
            instance._xboxllr_image_sections = [(s['address'], s['size'], s['raw_offset'], s['raw_size'])
                                               for s in image['sections']]
            instance._xboxllr_image_source = instance.xbe_data
        for section in instance._xboxllr_image_sections:
            if section[0] <= start < section[0]+section[1]:
                return section_window(instance.xbe_data, section, start, count)
        return None

    def read_function(instance, start, end):
        if type(start) is not int or type(end) is not int or end <= start:
            return None
        raw = read_window(instance, start, end-start)
        return raw if raw is not None and len(raw) == end-start else None

    def ret_bytes(instance, start, max_insns=48):
        raw = read_window(instance, start, max_insns*8)
        if not raw:
            return 0
        for index, insn in enumerate(instance.disasm._cs.disasm(raw, start)):
            if index >= max_insns or insn.mnemonic in ('call', 'jmp'):
                break
            if insn.mnemonic in ('ret', 'retn'):
                try:
                    return int(insn.op_str, 0) if insn.op_str else 0
                except ValueError:
                    return 0
        return 0

    translator._read_func_bytes = read_function
    translator._stub_ret_bytes = ret_bytes
    lifter = importlib.import_module(module.__package__+'.lifter')
    def callee_pop(instance, target):
        if target not in instance._pop_cache:
            info = instance.func_db.get(target, {})
            end = info.get('end')
            if isinstance(end, str):
                try:
                    end = int(end, 16)
                except ValueError:
                    end = None
            raw = read_window(instance, target, end-target) if type(end) is int and end > target else None
            if raw is not None and len(raw) == end-target:
                from importlib import import_module
                instance._pop_cache[target] = import_module(module.__package__+'.hle').stack_cleanup(raw, target)
            else:
                instance._pop_cache[target] = None
        return instance._pop_cache[target]
    lifter.Lifter.callee_pop = callee_pop
    translator._xboxllr_loaded_image_reads = True
