#!/usr/bin/env python3
"""Prepare a finite, input-only GP bit9 discrimination case entirely offline.

This supplies authored data and diagnostic instructions, never a hardware
runner, original firmware or an admitted interpretation of bit9. Physical SG
encoding and bootstrap must be separately established before any experiment.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

TOKEN=0x5a34c1
NODE=0x200
TARGET=0x400
COUNT=4
MAX_PHYSICAL=0x600000
DIRECT=(0xaa241100,0xbb302200,0xcc313300,0xdd000087)
SCATTER=(0xee244400,0x99305500,0x88316600,0x77000087)
POLL_ATTEMPTS=16


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def classify_snapshot(words):
    """Describe matching bytes/banks without converting a match to semantics."""
    if len(words)!=12:raise ValueError('expected four sampled X/Y/P words each')
    matches=[]
    signatures={'owned_page6_low24':tuple(v&0xffffff for v in DIRECT),
                'owned_page7_low24':tuple(v&0xffffff for v in SCATTER)}
    for bank,at in (('X',0),('Y',4),('P',8)):
        for name,value in signatures.items():
            if tuple(words[at:at+4])==value:matches.append(dict(bank=bank,bytes_match=name))
    return matches


def authored_program(physical,bit9):
    if type(physical) is not int or physical<=0 or physical>MAX_PHYSICAL or physical&16383:
        raise ValueError('dual owned-address discrimination requires a low prepared physical base')
    if type(bit9) is not bool:raise ValueError('choose only exact clear/set comparison')
    code=[];branches=[]
    def emit(*words):code.extend(words)
    def constant(value,target):emit(0x44f400,value,0x447000,target)
    def capture(address,target):emit(0x44f000,address,0x447000,target)
    def expect(address,value,target):
        capture(address,target)
        emit(0x56f400,value,0x200045)
        branches.append(len(code));emit(0x0d1042,0) # relative BNE failure
    def configure(address,value,target):
        emit(0x08f480+(address-0xffffc0),value)
        expect(address,value,target)
    # Results are authored by this DSP program; the CPU never supplies TOKEN.
    constant(0,0x10a);constant(0,0x10b)
    expect(0xffffc5,0,0x100) # stale EOL is an error, never cleared to repair it
    expect(0xffffd6,32,0x101) # explicit offline starting context, not silicon default
    node=(0x4000|NODE,0x59e0|(0x200 if bit9 else 0),COUNT,TARGET,physical+0x6000)
    for i,value in enumerate(node):constant(value,NODE+i)
    for i in range(COUNT):constant(0x6b1000+i,TARGET+i)
    configure(0xffffd7,0,0x102)
    configure(0xffffd5,NODE,0x103)
    configure(0xffffd4,NODE,0x104)
    start=len(code);emit(0x08f496,1)
    capture(0xffffd6,0x105) # passive status; command1 is not a stored value1
    polls=[]
    for _ in range(POLL_ATTEMPTS):
        emit(0,0,0,0) # finite spacing, no claimed hardware latency/rate
        at=len(code);emit(0x0cc507,0) # relative BRCLR C5 bit7 to next attempt
        capture(0xffffd6,0x106);capture(0xffffd4,0x107);capture(0xffffc5,0x108)
        capture(NODE+1,0x109);constant(TOKEN,0x10a);emit(0x87)
        code[at+1]=len(code)-at
        polls.append(at)
    constant(0xbad001,0x10b);emit(0x87)
    failure=len(code);emit(0x87)
    for at in branches:code[at+1]=failure-at
    assert len(code)<TARGET, 'alternate P target must be outside executing instructions'
    metadata=dict(words=len(code),failure_pc=failure,start_pc=start,poll_pcs=polls,
        maximum_eol_poll_reads=POLL_ATTEMPTS,maximum_poststart_eol_reads=POLL_ATTEMPTS+1,
        bit9=bit9,descriptor=list(node),
        result_words=list(range(0x100,0x10c)),target_words=list(range(TARGET,TARGET+COUNT)))
    return struct.pack('<4096I',*(code+[0x87]*(4096-len(code)))),metadata


def prepare(owned,physical,bit9):
    if len(owned)!=36864:raise ValueError('expected exact frozen11315 nine-page owner')
    words=struct.unpack('<9216I',owned)
    if words[8192:8196]!=(0x47504f31,physical,0x399c000,0x47504f31^physical^0x399c000):
        raise ValueError('unrecognized offline source-bound owned preparation fixture')
    for i in range(2048):
        expected=physical+((i+4)*4096 if i<4 else 7*4096)
        if words[2*i:2*i+2]!=(expected,0):raise ValueError('owned SGE mismatch')
    image,metadata=authored_program(physical,bit9)
    ram=bytearray(owned)
    # Actual SG scratch address and direct physical alternative have different
    # owned sources. These are hypotheses to distinguish, not bit9 meanings.
    ram[0x4000:0x6000]=image[:8192]
    struct.pack_into('<4I',ram,0x6000,*DIRECT)
    struct.pack_into('<4I',ram,0x7000,*SCATTER)
    return bytes(ram),image,metadata


def check(root,output,observer):
    root,output,observer=root.resolve(),output.resolve(),observer.resolve()
    if 'build' not in output.parts:raise ValueError('observations must stay private')
    frozen=root/'build/blinx/audio-gp-owned-20261011/qualified-preparation.aot-private.json'
    if sha(frozen)!='132d3dcfc1a93efcbd0fc0e3a498aaf9c1754619a3f6c26fd611a41683843554':
        raise ValueError('unreviewed frozen ownership proof')
    owned_artifacts={a['path']:a['sha256'] for a in json.loads(frozen.read_text())['artifacts']}
    audit=root/'build/blinx/audio-ep-startup-context-audit-20261010/original-startup-observations.aot-private.json'
    if sha(audit)!='307d0ff0f29e29e2b8ef6c7d554852a4267c9433bb5959fcbc772102b959c731':
        raise ValueError('unreviewed original descriptor evidence')
    evidence=json.loads(audit.read_text())
    builders=[x for x in evidence if 'descriptor' in x]
    assert len(builders)==24
    assert {(x['kind'],x['bit9']) for x in builders}=={('GP',0),('GP',1),('EP',1)}
    assert {x['target'] for x in builders if x['bit9']}=={0x200,0x1880,0x2c00}
    output.mkdir(parents=True,exist_ok=True)
    # Reuse one independently captured actual x86 preparation. Its address is
    # already16KiB aligned; the earlier4KiB allocator request is NOT physical
    # SG calibration. A future experiment requires the corrected root contract.
    cases=[]
    for life,physical in enumerate((0x180000,)):
        source=root/f'build/blinx/audio-gp-owned-20261011/final/owned-ram-{physical:x}.bin'
        if sha(source)!=owned_artifacts[str(source)]:raise ValueError('changed frozen owner capture')
        for bit9 in (False,True):
            ram,image,metadata=prepare(source.read_bytes(),physical,bit9)
            name=f'life{life}-bit{int(bit9)}'
            ram_path=output/(name+'.ram.aot-private.bin');ram_path.write_bytes(ram)
            image_path=output/(name+'.program.aot-private.bin');image_path.write_bytes(image)
            modes=('ordinary','bad-pointer','stale-eol','starved','no-work') if life==0 and not bit9 else ('ordinary',)
            for mode in modes:
                capture=output/f'{name}-{mode}.capture.aot-private.bin'
                run=subprocess.run([str(observer),str(image_path),str(ram_path),hex(physical),
                    str(int(bit9)),mode,str(capture)],capture_output=True,text=True,timeout=10)
                (output/f'{name}-{mode}.log').write_text(run.stdout+run.stderr,newline='\n')
                assert run.returncode==0 and not run.stderr,run.stdout+run.stderr
                data=json.loads(run.stdout)
                observed=struct.unpack('<12I',capture.read_bytes())
                expected=tuple(v&0xffffff for v in SCATTER) if mode=='ordinary' and not bit9 else tuple((0x100400 if mode=='stale-eol' else 0x6b1000)+i for i in range(4))
                # Independent exact final X and unchanged Y targets; no reading
                # the producer's metadata as an expected-value oracle.
                assert observed[:4]==expected and observed[4:8]==tuple(0x200400+i for i in range(4)) and observed[8:]==(0x87,)*4
                classification=classify_snapshot(observed)
                assert classification==([dict(bank='X',bytes_match='owned_page7_low24')] if mode=='ordinary' and not bit9 else [])
                cases.append(dict(lifetime=life,physical=physical,bit9=bit9,mode=mode,
                    program=metadata,source_owner_sha256=sha(source),observation=data,
                    expected_x=list(expected),matching_bytes_only=classification,captured_sha256=sha(capture)))
    # The real root preparation returned this high address. It cannot satisfy
    # the dual-source discrimination gate; no truncation/masking/retry allowed.
    for bad in (0x03e8a000,0x03e88000,0x291000,0x602000,0,True):
        try:authored_program(bad,False)
        except ValueError:pass
        else:raise AssertionError('unsafe discrimination address accepted')
    unknown=dict(physical_sg_encoding='unresolved: root single store request/readback mismatch',
        bootstrap='unqualified',bit9='unqualified',counter_config='unqualified',
        high_hardware_owner_fails_dual_source_gate=True)
    result=dict(issue='beads-xbox.1.113.16',scope='offline diagnostic preparation only',
        frozen_owner=dict(path=str(frozen),sha256=sha(frozen)),
        original_builder_evidence=dict(path=str(audit),sha256=sha(audit)),cases=cases,
        possible_source_bytes=dict(ordinary_scatter=list(SCATTER),direct_physical=list(DIRECT)),
        unknown=unknown,hardware_or_network_operations=0)
    (output/'bit9-preparation.aot-private.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(expected_contexts=len(cases),results=str(output/'bit9-preparation.aot-private.json'))))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--observer',type=Path,required=True)
    args=parser.parse_args();check(args.root,args.output,args.observer)


if __name__=='__main__':main()
