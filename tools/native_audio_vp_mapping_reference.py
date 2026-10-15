#!/usr/bin/env python3
"""Execute original VP setup up to the shared native admission boundary.

Offline only. Prepared original caller allocations are explicit inputs. This
checks stopped address transport; it neither implements unnamed active table
consumers nor converts the stopped physical method observation into a pass.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
from native_audio_initialization_reference import Initializer, pin
from native_ep_reference import IMAGE_SHA256
import hashlib


class ExpectedBoundary(Exception):
    pass


def check(xbe,observer,physical,output):
    assert 'build' in output.resolve().parts and not output.exists()
    output.mkdir(parents=True)
    blob=xbe.read_bytes();assert hashlib.sha256(blob).hexdigest()==IMAGE_SHA256
    physical_data=json.loads(physical.read_text())
    assert physical_data['status']=='stopped'
    assert physical_data['device_writes_may_have_been_sent']==8
    assert physical_data['storage_operations']==0 and physical_data['automatic_retry'] is False
    before=physical_data['before']
    assert all(before[str(offset)]==value for offset,value in [(0x3fffc,0),(0x5fffc,1),
        (0x2000,7),(0x1100,0x7138f),(0x1000,0),(0x1004,0xd8)])
    guards=[row for row in physical_data['rows'] if row['phase']=='mapping-guard']
    assert len(guards)==24 and all(row['value']==row['expected'] for row in guards)
    observed=[]
    for row in physical_data['rows']:
        if row['phase']!='install-owned-mapping':continue
        payload=bytes.fromhex(row['payload'])
        assert payload[:2]==b'\xc7\x05' and payload[10]==0xa1 and payload[-1]==0xc3
        address,value=struct.unpack_from('<II',payload,2)
        assert struct.unpack_from('<I',payload,11)[0]==address
        assert row['value']==row['expected']==value and int(row['reply'][4:],16)==value
        observed.append(dict(address=address,value=value))
    assert [row['address'] for row in observed]==[0xfe802030,0xfe802044,0xfe802038,0xfe80203c]
    last=physical_data['rows'][-1]
    assert last['phase']=='method-pair' and last['value']==0 and last['expected']!=0
    pa=physical_data['owned_physical']
    assert bytes.fromhex(last['payload'])==(b'\xc7\x05'+struct.pack('<II',0xfe820804,0x7ff)+
        b'\xc7\x05'+struct.pack('<II',0xfe820808,pa+0xe000)+b'\xa1'+struct.pack('<I',0x80000000+pa+0x3ff8)+b'\xc3')
    assert last['expected']==pa+0xe000 and int(last['reply'][4:],16)==0
    assert 'unexpected write/readback' in physical_data['error']
    lives=[]
    for lifetime in range(3):
        original=Initializer(blob,lifetime,lifetime)
        original.produce_startup()
        mappings=[]
        for offset,variable in [(0x202c,0x17df28),(0x2038,0x17df68),(0x203c,0x17df78),
                                (0x2030,0x17df48),(0x2034,0x17df58),(0x115c,0x17df38)]:
            virtual=original.get(variable);owned=original.owned(virtual)
            assert owned['virtual']==virtual and original.get(variable+4)==owned['physical']
            if offset in (0x2038,0x203c):assert owned['requested']==0x2000 and not(owned['physical']&0x3fff)
            mappings.append(dict(offset=offset,virtual=virtual,physical=owned['physical'],bytes=owned['requested']))
        rows=[];boundary=None
        with (output/f'observer-{lifetime}.aot-private.log').open('w') as log:
            with subprocess.Popen([str(observer.resolve()),'--transport'],stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,stderr=log,text=True,bufsize=1) as process:
                assert process.stdout.readline().strip()=='READY'
                def transport(operation,address,size,value):
                    process.stdin.write(f'{operation} {address:x} {size} {value:x}\n');process.stdin.flush()
                    reply=process.stdout.readline().strip()
                    row=dict(operation=operation,address=address,size=size,value=value,reply=reply);rows.append(row)
                    if reply.startswith('FAIL '):
                        assert operation=='W' and address==0xfe8202a4 and size==4 and value==0xfff
                        assert reply=='FAIL original transaction outside admitted semantics: fe8202a4'
                        raise ExpectedBoundary()
                    assert re.fullmatch(r'OK [0-9a-fA-F]{8}',reply),reply
                    row['observed']=int(reply[3:],16);return row['observed']
                original.transport=transport
                original.call(0x17bc43,(0,),original.owner)
                original.call(0x17bf66,ecx=original.owner)
                begin=len(rows)
                try:original.call(0x17b79f)
                except ExpectedBoundary:boundary=rows[-1]
                else:raise AssertionError('unknown original method unexpectedly admitted')
                original.transport=None
                process.stdin.close();assert process.wait(timeout=10)==2
        prefix=[row for row in rows[begin:] if row['operation']=='W' and 'observed' in row]
        assert len(prefix)==6
        for row,expected in zip(prefix,mappings):
            assert row['address']==0xfe800000+expected['offset']
            assert row['value']==row['observed']==expected['physical']
        table=original.get(0x17df28)
        # Original setup gives each voice a self link; list termination is a
        # separate method effect, not a blanket FFFF fill of the voice table.
        assert all(original.get(table+voice*0x80+0x7c)==voice for voice in range(256))
        original.destroy_startup()
        lives.append(dict(lifetime=lifetime,mappings=mappings,transactions=rows,guard=boundary,
            original_voice_links=256,original_destructors_release_all_allocations=True,
            original_setup_returned=False,source_identity=original.source_identity()))
    result=dict(status='original-vp-mapping-prefix-observed',lifetimes=lives,
        physical_mapping_readbacks=observed,physical_method_result='unexpected immediate zero; no later operation, active semantics unresolved',
        pins=[pin(xbe),pin(observer),pin(physical),pin(Path(__file__)),pin(Path(__file__).with_name('native_audio_initialization_reference.py'))],
        limits='Prepared caller/kernel allocation context. Six original mapping writes cross maintained native admission; first unsupported method 2A4 remains a real stop. Physical writes were made with FE halted, SE stopped and DSPs held; original prefix stores the same class of addresses before any methods while FE is free. No active table effects, method timing, whole startup or output qualification. No hardware operation in this tool.')
    path=output/'original-vp-mapping.aot-private.json'
    path.write_text(json.dumps(result,indent=2)+'\n')
    print(f'PASS: 18 original mapping writes, 768 initialized links, three ownership lifetimes; unchanged method boundary FE8202A4; {path}')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','observer','physical','output'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();check(args.xbe,args.observer,args.physical,args.output)
