#!/usr/bin/env python3
"""Bind the inspected cdecl MMX capability query, preserving its original body."""
import argparse
import json
from pathlib import Path
from native_binding import Binding

IMAGE='e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
PROBE='46171d02670c8e7bef5125047135fb769ee152787dfa9a411146f54cbe3e2aef'


def bind(xbe,output,symbols):
    context=Binding(xbe,output,symbols)
    if context.image['sha256']!=IMAGE: raise ValueError('unreviewed CPU probe image')
    functions=[f for f in context.functions if f['size']==50 and context.digest(f)==PROBE]
    if len(functions)!=1: raise ValueError('expected one reviewed MMX capability query')
    function=functions[0]
    binding=dict(xbe_sha256=IMAGE,body_sha256=PROBE,address=int(function['start'],16),
                 contract='cdecl, zero arguments, Boolean MMX capability of configured Xbox CPU')
    wrapper=('extern void pilot_cpu_mmx_available(void);\n'
             'extern void pilot_original_cpu_mmx_available(void);\n'
             f'void {function["name"]}(void) {{\n'
             '#ifdef PILOT_CPU_PROBE_NATIVE\n    pilot_cpu_mmx_available();\n'
             '#else\n    pilot_original_cpu_mmx_available();\n#endif\n}\n')
    return context.emit('cpu',function,'pilot_original_cpu_mmx_available',binding,wrapper)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['xbe','output','symbols']: parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args(); print(json.dumps(bind(args.xbe,args.output,args.symbols)))
