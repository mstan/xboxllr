"""Framework-owned floating-point extensions around the pinned CPU generator.

FCMOV reads CPU CF/ZF/PF, not x87 condition codes. Reuse the donor's existing
CFG-aware CPU flag expressions by lifting ordinary spans between these flag
consumers. Unsupported x87 instructions must never disappear as comments.
Read-only SSE control stores also preserve the existing CPU flag context.
"""
from copy import copy
import json
from pathlib import Path
import re
import runpy
import sys

CONDITIONS={'fcmovb':'jb','fcmove':'je','fcmovbe':'jbe','fcmovu':'jp',
    'fcmovnb':'jae','fcmovne':'jne','fcmovnbe':'ja','fcmovnu':'jnp'}
sites={}

def install(module):
    if getattr(module,'_xboxllr_x87_extension',False):return
    original_block=module.lift_basic_block;original_fpu=module.Lifter._lift_fpu
    original_instruction=module.Lifter.lift_instruction
    def guard(instruction,reason):
        sites[instruction.address]=dict(address=instruction.address,mnemonic=instruction.mnemonic,
            status='guarded',reason=reason)
        return f'RECOMP_UNIMPL("{reason}", 0x{instruction.address:08X}u);'
    def register_index(operand):
        if operand.type!='reg':return None
        match=re.fullmatch(r'st(?:\(([0-7])\)|([0-7]))?',operand.reg or '')
        return int((match.group(1) or match.group(2) or '0')) if match else None
    def lift_block(lifter,block,flag_state=None):
        if not any(i.mnemonic.startswith('fcmov') or i.mnemonic=='stmxcsr' for i in block.instructions):
            return original_block(lifter,block,flag_state)
        statements=[];span=[]
        def flush():
            nonlocal flag_state,span
            if span:
                part=copy(block);part.instructions=span
                emitted,flag_state=original_block(lifter,part,flag_state)
                statements.extend(emitted);span=[]
        for instruction in block.instructions:
            if not instruction.mnemonic.startswith('fcmov') and instruction.mnemonic!='stmxcsr':
                span.append(instruction);continue
            flush();condition=None;ops=instruction.operands
            if instruction.mnemonic=='stmxcsr':
                statements.extend(lift_instruction(lifter,instruction));continue
            jcc=CONDITIONS.get(instruction.mnemonic)
            index=register_index(ops[1]) if len(ops)==2 else None
            if jcc and flag_state and index is not None and register_index(ops[0])==0:
                condition=module._make_condition(jcc,*flag_state)
            if condition is None:
                statements.append(guard(instruction,'unqualified x87 conditional move flags/operands'))
            else:
                statements.append(f'if ({condition[0]}) fp_top() = fp_st({index}); /* {instruction.mnemonic}: CPU flags */')
                sites[instruction.address]=dict(address=instruction.address,mnemonic=instruction.mnemonic,status='lowered')
            # FCMOV changes neither CPU flags, x87 condition codes nor TOP.
        flush();return statements,flag_state
    def lift_fpu(lifter,instruction,mnemonic,operands):
        if mnemonic=='fnclex':
            sites[instruction.address]=dict(address=instruction.address,mnemonic=mnemonic,status='lowered')
            # g_fp_cc contributes the status bits to FNSTSW; TOP is separate.
            # Clear B/ES/SF/exception bits while retaining C0/C1/C2/C3. The
            # existing native model does not deliver unmasked x87 exceptions.
            return [f'g_fp_cc &= 0x4700u; /* {mnemonic}: clear exception status, retain conditions/TOP/CW */']
        statements=original_fpu(lifter,instruction,mnemonic,operands)
        if any('/* FPU:' in statement for statement in statements):
            return [guard(instruction,'unqualified x87 instruction '+mnemonic)]
        return statements
    def lift_instruction(lifter,instruction):
        if instruction.mnemonic=='stmxcsr':
            ops=instruction.operands
            if len(ops)!=1 or ops[0].type!='mem' or ops[0].mem_size!=4:
                return [guard(instruction,'unqualified SSE control query operand')]
            sites[instruction.address]=dict(address=instruction.address,mnemonic='stmxcsr',status='lowered')
            address=module._fmt_mem(ops[0])
            return ['{ extern uint32_t recomp_mxcsr_read(void); '+
                    f'MEM32({address}) = recomp_mxcsr_read(); }} /* stmxcsr: read native SSE control */']
        if instruction.mnemonic=='ldmxcsr':
            return [guard(instruction,'unqualified SSE control write')]
        return original_instruction(lifter,instruction)
    module.lift_basic_block=lift_block;module.Lifter._lift_fpu=lift_fpu
    module.Lifter.lift_instruction=lift_instruction
    translator=sys.modules.get('tools.recomp.translator')
    if translator is not None:translator.lift_basic_block=lift_block
    module._xboxllr_x87_extension=True

def main():
    donor=Path(sys.argv[1]).resolve();report=Path(sys.argv[2]).resolve()
    sys.path.insert(0,str(donor))
    from tools.recomp import lifter
    if not Path(lifter.__file__).resolve().is_relative_to(donor):
        raise ValueError('CPU generator module resolved outside selected donor')
    install(lifter);sys.argv=['tools.recomp',*sys.argv[3:]]
    try:runpy.run_module('tools.recomp',run_name='__main__')
    finally:
        values=sorted(sites.values(),key=lambda row:row['address'])
        report.write_text(json.dumps(dict(schema='native-x87-lowering.v1',
            lowered=sum(row['status']=='lowered' for row in values),
            guarded=sum(row['status']=='guarded' for row in values),sites=values),indent=2)+'\n')

if __name__=='__main__':main()
