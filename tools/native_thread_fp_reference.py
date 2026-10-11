#!/usr/bin/env python3
"""Observe initial saved FP contexts through the owned original thread route.

The original creator writes the saved context; no FP initializer, kernel call,
thread body or cleanup routine is replaced. The existing authored worker bodies
and twelve lifecycle/resource checks are reused. This establishes saved startup
state, not lazy-NPX exceptions or a native callback's later FP transport.
"""
import argparse
import json
import os
import struct
from pathlib import Path

from native_thread_reference import ThreadReference


class FloatingPointReference(ThreadReference):
    def __init__(self, snapshot, library, image):
        self.startup_contexts = []
        super().__init__(snapshot, library, image)

    def call(self, ordinal, *args):
        result = super().call(ordinal, *args)
        if ordinal == 246 and result == 0 and args[0] != 0xfffffffe:
            thread = struct.unpack('<I', self.cpu.mem_read(args[2], 4))[0]
            top = self.word(thread + 0x1c)
            # The original context initializer reserves 528 bytes at stack top;
            # its first 512 bytes follow the published FXSAVE layout. Inspect
            # the actual initialized bytes before a worker can modify them.
            context = bytes(self.cpu.mem_read(top - 528, 512))
            control, status, tag = struct.unpack_from('<HHB', context)
            mxcsr = struct.unpack_from('<I', context, 24)[0]
            assert control == 0x027f, hex(control)
            assert status == 0 and tag == 0, (status, tag)
            assert mxcsr == 0x1f80, hex(mxcsr)
            assert context[32:160] == bytes(128), 'initial x87 storage'
            assert context[160:288] == bytes(128), 'initial XMM storage'
            self.startup_contexts.append(dict(control=control, status=status,
                abridged_tag=tag, mxcsr=mxcsr, x87_storage_zero=True,
                xmm_storage_zero=True))
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('snapshot', 'library', 'image', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--dependencies', type=Path)
    args = parser.parse_args()
    directory = os.add_dll_directory(str(args.dependencies.resolve())) if args.dependencies else None
    ref = FloatingPointReference(args.snapshot, args.library, args.image)
    try:
        result = ref.run()
        assert len(ref.startup_contexts) == 12
        result['saved_fp_startup'] = ref.startup_contexts
        result['limits'] = ['Saved startup context, not lazy-NPX exception behavior',
                            'No silicon timing or arbitrary FP arithmetic claim',
                            'Native callback FP transport remains separate']
    finally:
        ref.close()
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8', newline='\n')
    print('PASS: 12 original thread saved FP contexts, both start ABIs and original resource restoration')


if __name__ == '__main__':
    main()
