#!/usr/bin/env python3
"""Bind the inspected cdecl MMX capability query, preserving its original body."""
import argparse
import json
from pathlib import Path
from native_binding import Binding

def bind(xbe,output,symbols,profile=None):
    context=Binding(xbe,output,symbols)
    proof=context.require_profile('cpu',profile,adapter='x86-mmx-cdecl-v1')
    functions=[f for f in context.functions if context.matches_body(f,proof['body'])]
    if len(functions)!=1: raise ValueError('expected one reviewed MMX capability query')
    function=functions[0]
    binding=dict(xbe_sha256=context.image['sha256'],body_sha256=proof['body']['sha256'],address=int(function['start'],16),
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
    parser.add_argument('--profile',type=Path)
    args=parser.parse_args(); print(json.dumps(bind(args.xbe,args.output,args.symbols,args.profile)))
