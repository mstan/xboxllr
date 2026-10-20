#!/usr/bin/env python3
"""Re-lift a reviewed function extent in the private native-title pilot.

Recovery metadata belongs to the input image, not a title-specific runtime
replacement. Keep original bytes and donor lowering; never create a success
body for the unresolved target. Full preparation replays the same extents.
Incremental indirect entries declare their symbol in the dispatch unit,
preserving the common header and unrelated original translation units.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from aot_inventory import parse_xbe
from native_pilot import DONOR_PIN


def load_plan(path,blob,image,functions):
    """Validate a source-only batch before invoking the compiler or publishing."""
    plan=json.loads(Path(path).read_text(encoding='utf-8'))
    if (not isinstance(plan,dict) or set(plan)!={'schema','sha256','extents'} or
        plan['schema']!='xboxllr.native-function-recovery.v1' or plan['sha256']!=image['sha256'] or
        not isinstance(plan['extents'],list) or not plan['extents']):
        raise ValueError('invalid native recovery plan/image')
    seen=set()
    for record in plan['extents']:
        required={'start','original_end','end','sha256','reason'}
        if (not isinstance(record,dict) or not required.issubset(record) or
            set(record)-required-{'new_entry','terminal','contained_entries'} or
            any(type(record[k]) is not int or not 0<=record[k]<=0xffffffff for k in ('start','original_end','end')) or
            not isinstance(record['sha256'],str) or not re.fullmatch('[0-9a-f]{64}',record['sha256']) or
            not isinstance(record['reason'],str) or not record['reason'].strip() or
            type(record.get('new_entry',False)) is not bool or record['start'] in seen):
            raise ValueError('invalid native recovery extent')
        if record.get('new_entry') and record['original_end']!=record['start']:
            raise ValueError('new native entry must have an empty previous extent')
        seen.add(record['start'])
        # Match the extent compiler's canonical record for explicit defaults.
        if not record.get('new_entry'):record.pop('new_entry',None)
        if record.get('terminal')=='return':record.pop('terminal')
        if record.get('contained_entries')==[]:record.pop('contained_entries')
    apply_extents(blob,image,functions,plan['extents'])
    return plan


def apply_extents(blob, image, functions, records):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    functions=json.loads(json.dumps(functions))
    for record in records:
        start,end=record['start'],record['end']
        matches=[f for f in functions if int(f['start'],16)==start]
        new_entry=record.get('new_entry',False)
        if not matches and new_entry:
            section=next((s for s in image['sections'] if s['flags']&4 and s['address']<=start<end<=s['address']+s['raw_size']),None)
            if not section:raise ValueError('entry outside executable file-backed section')
            f=dict(start=f'0x{start:08X}',end=f'0x{start:08X}',size=0,name=f'sub_{start:08X}',
                   section=section['name'],confidence=1.0,detection_method='observed_indirect',
                   num_instructions=0,has_prologue=False,calls_to=[],called_by=[])
            functions.append(f);matches=[f]
        if len(matches)!=1: raise ValueError('extent needs one existing function')
        f=matches[0]
        if int(f['end'],16) not in (record['original_end'],end): raise ValueError('original extent changed')
        if end<=record['original_end']: raise ValueError('recovery must extend the body')
        overlaps=[g for g in functions if g is not f and
                  int(g['start'],16)<end and int(g['end'],16)>record['original_end']]
        allowed={g['start']:g for g in record.get('contained_entries',[])}
        if len(allowed)!=len(record.get('contained_entries',[])):
            raise ValueError('duplicate contained entry')
        for g in overlaps:
            low,high=int(g['start'],16),int(g['end'],16)
            bound=allowed.pop(low,None)
            if not bound or not start<=low<high<=end or bound['end']!=high:
                raise ValueError('extension overlaps another discovered body')
            source=next((s for s in image['sections'] if s['flags']&4 and
                s['address']<=low<high<=s['address']+s['raw_size']),None)
            if not source:raise ValueError('contained entry outside executable bytes')
            offset=source['raw_offset']+low-source['address']
            if hashlib.sha256(blob[offset:offset+high-low]).hexdigest()!=bound['sha256']:
                raise ValueError('contained entry bytes changed')
            # Keep the independently callable entry and its dispatch/body.
            # Only the parent's reviewed extent expands to include shared tails.
        if allowed:raise ValueError('contained entry does not match overlapping discovery')
        section=next((s for s in image['sections'] if s['flags']&4 and s['address']<=start<end<=s['address']+s['raw_size']),None)
        if not section: raise ValueError('extent outside executable file-backed section')
        offset=section['raw_offset']+start-section['address'];code=blob[offset:offset+end-start]
        if hashlib.sha256(code).hexdigest()!=record['sha256']: raise ValueError('recovered bytes changed')
        instructions=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(code,start))
        terminal=record.get('terminal','return')
        if terminal not in ('return','tail-jump'):
            raise ValueError('unknown reviewed terminal kind')
        mnemonic='ret' if terminal=='return' else 'jmp'
        if (not instructions or sum(i.size for i in instructions)!=end-start or
                instructions[-1].mnemonic!=mnemonic):
            ending='a return' if terminal=='return' else 'an unconditional tail jump'
            raise ValueError('reviewed extension must decode completely through '+ending)
        f.update(end=f'0x{end:08X}',size=end-start,num_instructions=len(instructions))
    return sorted(functions,key=lambda f:int(f['start'],16))


def retire_missing_guard(sources, symbol, address, missing_count):
    """Retire exactly the hardened direct-target definition being recovered.

    An indirect callback may have no old guard. A direct call already has a
    one-line definition which the normal multiline compiler-body matcher does
    not recognize. Reject other/duplicate definitions rather than leaving two
    native owners or silently replacing an unreviewed implementation.
    Returns proposed source edits; the caller publishes only after validation.
    """
    definition = re.compile(r'^\s*void\s+' + re.escape(symbol) +
                            r'\s*\(\s*void\s*\)\s*\{', re.M)
    canonical = re.compile(r'^void ' + re.escape(symbol) +
        r'\(void\) \{ RECOMP_UNIMPL\("undiscovered direct target", 0x' +
        f'{address:08X}' + r'u\); \}(?:\r?\n|$)', re.M)
    owners = [(path, code) for path, code in sources if definition.search(code)]
    if not owners:
        return {}, missing_count
    if len(owners) != 1:
        raise ValueError('duplicate missing-target definitions before recovery')
    path, code = owners[0]
    matches = list(canonical.finditer(code))
    if len(matches) != 1 or len(definition.findall(code)) != 1:
        raise ValueError('unreviewed missing-target definition before recovery')
    if type(missing_count) is not int or missing_count < 1:
        raise ValueError('missing-target inventory disagrees with recovered guard')
    return {path: canonical.sub('', code, count=1)}, missing_count - 1


def recover(args):
    output=args.output.resolve();donor=args.donor.resolve();xbe=args.xbe.resolve()
    if 'build' not in output.parts: raise ValueError('output must remain under private build/')
    git='C:/Program Files/Git/cmd/git.exe' if sys.platform=='win32' else 'git'
    if subprocess.check_output([git,'-C',str(donor),'rev-parse','HEAD'],text=True).strip()!=DONOR_PIN:
        raise ValueError('unreviewed CPU donor')
    subprocess.run([git,'-C',str(donor),'diff','--exit-code','HEAD','--','tools','templates/runtime'],check=True,stdout=subprocess.DEVNULL)
    blob=xbe.read_bytes();image=parse_xbe(blob)
    manifest_path=output/'generation.aot-private.json';manifest=json.loads(manifest_path.read_text())
    if manifest['sha256']!=image['sha256']: raise ValueError('generated image identity mismatch')
    generated=output/'gen'
    for name,digest in manifest['files'].items():
        if hashlib.sha256((generated/name).read_bytes()).hexdigest()!=digest: raise ValueError(f'changed artifact: {name}')
    record_path=output/'function-extents.aot-private.json'
    records=json.loads(record_path.read_text()) if record_path.exists() else dict(sha256=image['sha256'],extents=[])
    if records['sha256']!=image['sha256']: raise ValueError('extent image identity mismatch')
    if any(r['start']==args.function for r in records['extents']): raise ValueError('extent already recovered')
    functions_path=output/'disasm/functions.json';functions=json.loads(functions_path.read_text())
    function=next((f for f in functions if int(f['start'],16)==args.function),None)
    if args.new_entry:
        if function:raise ValueError('new entry already discovered')
        function=dict(name=f'sub_{args.function:08X}',end=f'0x{args.function:08X}')
    elif not function:raise ValueError('missing function: use --new-entry only for a measured entry point')
    section=next(s for s in image['sections'] if s['address']<=args.function<args.end<=s['address']+s['raw_size'])
    offset=section['raw_offset']+args.function-section['address']
    record=dict(start=args.function,original_end=int(function['end'],16),end=args.end,
                sha256=hashlib.sha256(blob[offset:offset+args.end-args.function]).hexdigest(),reason=args.reason)
    if getattr(args,'terminal','return')!='return':record['terminal']=args.terminal
    if getattr(args,'contained_entry',[]):
        record['contained_entries']=[]
        for address in args.contained_entry:
            other=next((f for f in functions if int(f['start'],16)==address),None)
            if not other:raise ValueError('contained entry must already be discovered')
            end=int(other['end'],16);at=section['raw_offset']+address-section['address']
            record['contained_entries'].append(dict(start=address,end=end,
                sha256=hashlib.sha256(blob[at:at+end-address]).hexdigest()))
    if args.new_entry:record['new_entry']=True
    records['extents'].append(record)
    updated=apply_extents(blob,image,functions,[record])
    work=output/'extent-recovery';work.mkdir(exist_ok=True)
    proposed=work/f'functions-{args.function:08x}.json';proposed.write_text(json.dumps(updated,indent=2)+'\n')
    command=[sys.executable,'-m','tools.recomp',str(xbe),'--function',hex(args.function),'--functions',str(proposed),
        '--disasm-dir',str(output/'disasm'),'--func-id-dir',str(output/'classification'),'--abi-dir',str(output/'abi'),
        '-o',str(work),'--trace-all-entries']
    result=subprocess.run(command,cwd=donor,capture_output=True,text=True,check=True)
    (work/f'lift-{args.function:08x}.log').write_text(result.stderr)
    symbol=function['name'];pattern=re.compile(r'^void '+re.escape(symbol)+r'\(void\)\n\{\n.*?^\}\n',re.M|re.S)
    bodies=pattern.findall(result.stdout)
    if len(bodies)!=1 or 'RECOMP_UNIMPL' in bodies[0] or 'recomp_stub_missing' in bodies[0]:
        raise ValueError('recovered lowering incomplete')
    if manifest.get('thread_checkpoints'):
        from native_thread_checkpoints import instrument
        bodies[0], unused = instrument(bodies[0])
    sources=[(p,p.read_text()) for p in generated.glob('recomp_*.c')]
    matches=[(p,c) for p,c in sources if pattern.search(c)]
    retired={};remaining_missing=manifest.get('missing_targets',0)
    if args.new_entry:
        if matches:raise ValueError('new entry already has generated code')
        retired,remaining_missing=retire_missing_guard(sources,symbol,args.function,remaining_missing)
        path=generated/f'recomp_entry_{args.function:08x}.c'
        if path.exists():raise ValueError('new entry output already exists')
        code='#define RECOMP_GENERATED_CODE\n#include "recomp_funcs.h"\n#include <math.h>\n'+bodies[0]
        dispatch_path=generated/'recomp_dispatch.c';dispatch=dispatch_path.read_text(encoding='utf-8')
        # This measured indirect root is referenced by the dispatch unit.
        # Keep its declaration there, so recovering one callback does not
        # invalidate every original CPU translation unit's common header.
        # Full preparation still replays the source-bound extent normally.
        include='#include "recomp_funcs.h"\n'
        if dispatch.count(include)!=1:raise ValueError('unreviewed dispatch header boundary')
        dispatch=dispatch.replace(include,include+'\nvoid '+symbol+'(void);\n',1)
        rows=list(re.finditer(r'^    \{ 0x([0-9A-Fa-f]{8})u, \(recomp_func_t\)sub_[0-9A-Fa-f]{8} \},$',dispatch,re.M))
        if not rows or any(int(row[1],16)==args.function for row in rows):raise ValueError('unreviewed or duplicate dispatch table')
        position=next((row.start() for row in rows if int(row[1],16)>args.function),rows[-1].end()+1)
        dispatch=dispatch[:position]+f'    {{ 0x{args.function:08X}u, (recomp_func_t){symbol} }},\n'+dispatch[position:]
        dispatch=re.sub(r'Maps \d+ Xbox VAs',f'Maps {len(rows)+1} Xbox VAs',dispatch,count=1)
        from native_dispatch import compact, refresh_inventory_count
        # A sorted insertion shifts the table's upper bound and compact range.
        # Derive the count from the C array so recovered high-address callbacks
        # cannot silently fall beyond an old numeric binary-search bound.
        dispatch, compact_record = compact(refresh_inventory_count(dispatch))
    else:
        if len(matches)!=1: raise ValueError('expected one original generated body')
        path,code=matches[0];code,count=pattern.subn(lambda m:bodies[0],code)
        if count!=1: raise ValueError('ambiguous replacement')
    old_banner=re.compile(r'^ \* Original: 0x'+f'{args.function:08X}'+r' - [^\n]+$',re.M)
    new_banner=old_banner.findall(result.stdout)
    if len(new_banner)!=1: raise ValueError('missing recovered source extent')
    code=old_banner.sub(lambda m:new_banner[0],code)
    # All validation and lowering succeed before changing authoritative output.
    (work/f'recovered-{args.function:08x}.c').write_text(result.stdout,newline='\n')
    path.write_text(code,newline='\n');functions_path.write_text(json.dumps(updated,indent=2)+'\n',newline='\n')
    for retired_path,retained_source in retired.items():
        retired_path.write_text(retained_source,encoding='utf-8',newline='\n')
    if retired:manifest['missing_targets']=remaining_missing
    if args.new_entry:
        dispatch_path.write_text(dispatch,encoding='utf-8',newline='\n')
    record_path.write_text(json.dumps(records,indent=2)+'\n',newline='\n')
    manifest['function_extents']=records['extents']
    if args.new_entry:
        manifest['compact_dispatch']=compact_record
    if manifest.get('thread_checkpoints'):
        from native_thread_checkpoints import apply
        manifest['thread_checkpoints']=apply(generated)
    if manifest.get('poll_waits'):
        from native_poll_waits import apply as bind_poll_waits
        manifest['poll_waits']=bind_poll_waits(blob,image,generated,manifest['poll_waits']['contract'])
    manifest['files']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(generated.iterdir()) if p.is_file()}
    manifest_path.write_text(json.dumps(manifest,indent=2)+'\n',newline='\n')
    return record


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('donor','xbe','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--function',type=lambda x:int(x,0),required=True)
    parser.add_argument('--end',type=lambda x:int(x,0),required=True)
    parser.add_argument('--reason',required=True)
    parser.add_argument('--new-entry',action='store_true',help='Recover an observed undiscovered function with a reviewed non-overlapping extent')
    parser.add_argument('--terminal',choices=('return','tail-jump'),default='return',
                        help='Reviewed final instruction; tail-jump admits source-bound wrappers without extending through unrelated bodies')
    parser.add_argument('--contained-entry',type=lambda x:int(x,0),action='append',default=[],
                        help='Preserve an existing source-bound shared tail inside the reviewed expanded body')
    print(json.dumps(recover(parser.parse_args())))
