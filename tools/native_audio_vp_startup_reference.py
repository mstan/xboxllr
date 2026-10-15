#!/usr/bin/env python3
"""Run the original VP initializer through maintained stopped APU methods.

Offline only. Prepared original caller allocations and initialized-kernel state
remain explicit inputs. Physical evidence supplies field mapping and ownership;
pending FE work, active table consumers and complete audio startup stay open.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from native_audio_initialization_reference import Initializer, pin
from native_ep_reference import IMAGE_SHA256


def physical_evidence(folder):
    maps=json.loads((folder/'observation.aot-private.json').read_text())
    follow=json.loads((folder/'followup-observation.aot-private.json').read_text())
    assert maps['status']=='methods-observed' and maps['device_writes_may_have_been_sent']==24
    assert follow['status']=='followup-observed' and follow['device_writes_may_have_been_sent']==16
    assert follow['output_table']==0x56551002
    expected=[(0x2a4,0x207c),(0x2b0,0x2080),(0x2a0,0x2078),(0x2a8,0x2088),(0x2ac,0x2084)]
    assert [(r['method'],r['register']) for r in maps['field_map']]==expected
    prepared=json.loads((folder/'prepared.aot-private.json').read_text())
    prepared_follow=json.loads((folder/'followup-prepared.aot-private.json').read_text())
    assert len(prepared['cases'])==9 and len(prepared_follow['cases'])==7
    for name,item in prepared['payloads'].items():
        assert pin(folder/(name+'.aot-private.bin'))['sha256']==item['sha256']
    assert pin(folder/'method-followup.aot-private.bin')['sha256']==prepared_follow['payload_sha256']
    for data in (maps,follow):
        assert data['storage_operations']==0 and data['automatic_retry'] is False
        assert data['device_writes_may_have_been_sent']==sum(r['writes'] for r in data['rows'])
        for row in data['rows']:
            assert int(row['reply'][4:],16)==row['value']
            if row['expected'] is not None:
                assert row['value'] in (row['expected'] if isinstance(row['expected'],list) else [row['expected']])
        final={}
        for row in data['rows']:
            if row['phase']!='final-guard':continue
            payload=bytes.fromhex(row['payload']);assert len(payload)==6 and payload[0]==0xa1 and payload[-1]==0xc3
            final[struct.unpack_from('<I',payload,1)[0]-0xfe800000]=row['value']
        assert final=={0x3fffc:0,0x5fffc:1,0x2000:7,0x1100:0x7138f,0x1000:0,0x1004:0xd8}
    assert sum(r['phase']=='field-zero' for r in maps['rows'])==5
    assert sum(r['phase']=='field-restore' for r in maps['rows'])==5
    def wrapped(name,a,b):
        return b'\x68'+struct.pack('<I',b)+b'\x68'+struct.pack('<I',a)+b'\xe8\x04\0\0\0\x83\xc4\x08\xc3'+(folder/(name+'.aot-private.bin')).read_bytes()
    for row,(method,register) in zip([r for r in maps['rows'] if r['phase']=='field-zero'],expected):
        assert bytes.fromhex(row['payload'])==wrapped('method-fields',method,0)
        assert row['value']==0x56520000|(1<<((register-0x2078)//4))
    for row,(method,_) in zip([r for r in maps['rows'] if r['phase']=='field-restore'],expected):
        assert bytes.fromhex(row['payload'])==wrapped('method-fields',method,0xfff) and row['value']==0x56520000
    for phase,stage,result in [('hold-method-pair',1,0x56530001),('release-held-pair',2,0x56530002)]:
        row=next(r for r in maps['rows'] if r['phase']==phase)
        assert bytes.fromhex(row['payload'])==wrapped('method-gate',maps['owned_physical'],stage) and row['value']==result
    for phase,stage,result in [('all-owned-table-bytes',0,0x56550000),('submix-change',1,0x56550001),
                               ('submix-restore',2,0x56550002),('output-table-route',3,0x56551002)]:
        row=next(r for r in follow['rows'] if r['phase']==phase)
        assert bytes.fromhex(row['payload'])==wrapped('method-followup',follow['owned_physical'],stage) and row['value']==result
    files=[p for p in folder.iterdir() if p.is_file() and p.suffix in ('.c','.py','.json','.bin')]
    return expected,[pin(p) for p in sorted(files)]


def check(xbe,observer,physical,output):
    assert 'build' in output.resolve().parts and not output.exists()
    output.mkdir(parents=True)
    blob=xbe.read_bytes();assert hashlib.sha256(blob).hexdigest()==IMAGE_SHA256
    fields,pins=physical_evidence(physical)
    lives=[]
    for lifetime in range(3):
        original=Initializer(blob,lifetime,lifetime);original.produce_startup()
        before=original.physical_ram();ram=output/f'input-{lifetime}.aot-private.bin';ram.write_bytes(before)
        owners=output/f'owners-{lifetime}.aot-private.bin'
        owners.write_bytes(b''.join(struct.pack('<II',a['physical'],a['extent']) for a in original.allocations if a['live']))
        result_ram=output/f'output-{lifetime}.aot-private.bin';rows=[]
        with (output/f'observer-{lifetime}.aot-private.log').open('w') as log:
            with subprocess.Popen([str(observer.resolve()),'--transport-ram',str(ram.resolve()),str(owners.resolve()),str(result_ram.resolve())],
                    stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,text=True,bufsize=1) as process:
                assert process.stdout.readline().strip()=='READY'
                def transport(operation,address,size,value):
                    process.stdin.write(f'{operation} {address:x} {size} {value:x}\n');process.stdin.flush()
                    reply=process.stdout.readline().strip();assert re.fullmatch(r'OK [0-9a-fA-F]{8}',reply),reply
                    result=int(reply[3:],16)
                    rows.append(dict(operation=operation,address=address,size=size,value=value,result=result,
                        result_kind='submitted-method-argument' if operation=='W' and address>=0xfe820000 else 'maintained-register-read'))
                    return result
                original.transport=transport
                original.call(0x17bc43,(0,),original.owner)
                original.call(0x17bf66,ecx=original.owner)
                assert transport('R',0xfe801510,4,0)==0
                begin=len(rows);original.call(0x17b79f)
                vp_rows=rows[begin:]
                writes=[r for r in vp_rows if r['operation']=='W'];assert len(writes)==14
                assert [r['address'] for r in writes[6:]]==[0xfe820000+m for m in [0x2a4,0x2b0,0x2a0,0x2a8,0x2ac,0x804,0x808,0x2c0]]
                for _,register in fields:assert transport('R',0xfe800000+register,4,0)==0xfff
                assert transport('R',0xfe8020ac,4,0)==0x09070806
                assert transport('R',0xfe801300,4,0)==0x2c0
                assert transport('R',0xfe820010,4,0)==0x80
                original.transport=None;process.stdin.close();assert process.wait(timeout=10)==0
        expected=bytearray(before);table=original.get(0x17df4c);target=original.get(0x17dfac)
        table_owner=original.owned(original.get(0x17df48));assert table_owner['physical']==table and table_owner['requested']==0x4000
        struct.pack_into('<I',expected,table+0x7ff*8,target)
        actual=result_ram.read_bytes();assert actual==expected,'only the actual input SGE pointer may change in the device RAM view'
        voice_table=original.get(0x17df28)
        assert all(original.get(voice_table+voice*128+124)==voice for voice in range(256))
        original.destroy_startup()
        lives.append(dict(lifetime=lifetime,transactions=rows,vp_writes=14,original_vp_returned=True,
            input_table=table,input_slot=0x7ff,input_target=target,exact_device_ram=True,original_voice_self_links=256,
            original_destructors_release_all_allocations=True,source_identity=original.source_identity()))
        pins += [pin(ram),pin(owners),pin(result_ram)]
    result=dict(status='original-vp-startup-return-observed',lifetimes=lives,
        physical_field_map=fields,physical_output_sge_table='GPFADDR',physical_input_control='control1 holds; control0 releases',
        pins=pins+[pin(xbe),pin(observer),pin(Path(__file__)),pin(Path(__file__).with_name('native_audio_initialization_reference.py'))],
        limits='Prepared original GP/EP caller objects and initialized-kernel context. Unchanged VP initializer returns using the shared native admission. Device RAM is an owned physical view: the only device write is the actual SGE pointer; original CPU voice self-links are separately checked. No whole startup, active field consumer, held-method runtime queue, DSP cadence or connected output qualification. No hardware access by this tool.')
    report=output/'original-vp-startup.aot-private.json';report.write_text(json.dumps(result,indent=2)+'\n')
    print(f'PASS: three full original VP initializer returns,42 writes,3 exact owned SGE publications,768 original voice self-links; {report}')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','observer','physical','output'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();check(args.xbe,args.observer,args.physical,args.output)
