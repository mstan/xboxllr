#!/usr/bin/env python3
"""Run unchanged original AC97 startup helpers against the maintained ACI.

Offline only. Original code/data and hardware receipts remain private. The
prepared memory/IRQL floor is inherited from the existing GP/EP source fixture;
KeStallExecutionProcessor advances its explicit guest clock, never a CAS read.
Interrupt registration and the enclosing initializer are checked by the native
title, not substituted inside this prepared helper proof.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from native_ep_startup_reference import OriginalStartup
from native_ep_reference import IMAGE_SHA256
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP


def pin(path):
    path = path.resolve()
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


class CodecStartup(OriginalStartup):
    transport = None
    ticks = 0
    stalls = 0

    def __init__(self, blob, lifetime):
        super().__init__(blob, lifetime, lifetime)
        self.cpu.mem_map(0xfec00000, 0x1000)

    def _read(self, cpu, access, address, size, value, unused):
        if self.transport and 0xfec00000 <= address < 0xfec01000:
            result = self.transport('R', address, size, 0)
            cpu.mem_write(address, result.to_bytes(size, 'little'))
        super()._read(cpu, access, address, size, value, unused)

    def _write(self, cpu, access, address, size, value, unused):
        if self.transport and 0xfec00000 <= address < 0xfec01000:
            self.transport('W', address, size, value)
        super()._write(cpu, access, address, size, value, unused)

    def _instruction(self, cpu, address, size, unused):
        if self.transport and address == 0x80000097:
            sp = cpu.reg_read(UC_X86_REG_ESP)
            ret, us = struct.unpack('<II', cpu.mem_read(sp, 8))
            assert us == 20
            self.ticks += (us*1125000+999999)//1000000
            self.stalls += 1
            self.transport('T', self.ticks, 0, 0)
            cpu.reg_write(UC_X86_REG_EAX, 0)
            cpu.reg_write(UC_X86_REG_ESP, sp+8)
            cpu.reg_write(UC_X86_REG_EIP, ret)
            return
        super()._instruction(cpu, address, size, unused)


def physical(folder):
    context = json.loads((folder/'context.aot-private.json').read_text())
    batch = json.loads((folder/'stopped-batch.aot-private.json').read_text())
    ordering = json.loads((folder/'cas-order-observation.aot-private.json').read_text())
    prepared = json.loads((folder/'cas-order-prepared.aot-private.json').read_text())
    assert context['status'] == 'observed' and batch['status'] == 'stopped-startup-observed'
    assert ordering['status'] == 'cas-order-observed'
    values = {r['address']:r['value'] for r in context['rows']}
    assert values[0xfec0012c] == 2 and values[0xfec00130] == 0x300100
    assert values[0xfec0007c] == 0x574d and values[0xfec0007e] == 0x4c09
    assert all(values[0xfec00000+off] == 0 for off in (0,2,0x18,0x26,0x28,0x2a,0x2c))
    assert batch['device_writes_may_have_been_sent'] == 12
    assert [r['value'] for r in batch['rows'] if r['phase'] == 'reset-descriptor-base'] == [0,0]
    code = (folder/'cas-order.aot-private.bin').read_bytes()
    assert hashlib.sha256(code).hexdigest() == prepared['sha256']
    assert bytes.fromhex(ordering['rows'][0]['payload']) == code
    assert ordering['rows'][0]['value'] == 0xac971001
    assert ordering['first_cas_busy'] and ordering['original_stalls'] == 1
    assert ordering['device_writes_may_have_been_sent'] == 3
    for data in (context,batch,ordering):
        assert data['storage_operations'] == 0 and data['automatic_retry'] is False
        for row in data['rows']:
            assert re.fullmatch(r'RET [0-9a-fA-F]{1,8}', row['reply'])
            assert int(row['reply'][4:],16) == row['value']
    return [pin(p) for p in sorted(folder.iterdir()) if p.is_file() and
            p.suffix in ('.c','.py','.json','.bin')]


def check(xbe, observer, evidence, output):
    assert 'build' in output.resolve().parts and not output.exists()
    output.mkdir(parents=True)
    blob = xbe.read_bytes()
    assert hashlib.sha256(blob).hexdigest() == IMAGE_SHA256
    pins = physical(evidence)
    lives = []
    for lifetime in range(3):
        original = CodecStartup(blob, lifetime)
        original.produce_startup()
        rows = []
        with (output/f'observer-{lifetime}.aot-private.log').open('w') as log:
            with subprocess.Popen([str(observer.resolve()),'--transport'], stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE, stderr=log, text=True, bufsize=1) as process:
                assert process.stdout.readline().strip() == 'READY'
                def transport(operation, address, size, value):
                    process.stdin.write(f'{operation} {address:x} {size} {value:x}\n');process.stdin.flush()
                    reply = process.stdout.readline().strip()
                    assert re.fullmatch(r'OK [0-9a-fA-F]{8}', reply), reply
                    result = int(reply[3:],16)
                    rows.append(dict(operation=operation,address=address,size=size,value=value,result=result))
                    return result
                original.transport = transport
                assert original.call(0x17c3bc) == 1
                assert original.call(0x17c43d) == 0
                assert original.call(0x17c420,(1,0)) == 1
                assert original.call(0x17c420,(12,0x0808)) == 1
                assert original.stalls > 0
                rings = []
                for index, offset in enumerate((0x110,0x170)):
                    assert original.call(0x17c581,(index,0),ecx=original.owner+0x14) == 0
                    stream = original.get(original.owner+0x14+index*4)
                    assert original.call(0x17c794,(0,0,0),ecx=stream) == 0
                    ring = original.get(stream+0x18)
                    pa = original.get(stream+0x1c)
                    assert original.owned(ring,0x100)['physical'] == pa
                    assert transport('R',0xfec00000+offset,4,0) == pa
                    target = original.get(0x17df0c)
                    original.call(0x17c5e9,(target,0x2000),ecx=stream)
                    assert bytes(original.cpu.mem_read(ring,8)) == struct.pack('<II',target,0xc0001000)
                    assert transport('R',0xfec00000+offset+5,1,0) == 0
                    assert transport('R',0xfec00000+offset+6,2,0) == 1
                    assert transport('R',0xfec00000+offset+8,2,0) == 0
                    if index:
                        for value in (0x02000000,0x02000002):
                            original.call(0x17c514,(value,),ecx=stream)
                            assert transport('R',0xfec0017c,4,0) == value
                    rings.append(dict(index=index,virtual=ring,physical=pa,buffer=target,
                                      descriptor=struct.pack('<II',target,0xc0001000).hex()))
                original.call(0x17cae2,ecx=original.owner+0x14)
                assert not original.get(original.owner+0x14) and not original.get(original.owner+0x18)
                original.ticks += 24
                transport('T',original.ticks,0,0)
                # Acquire and retain the semaphore: the actual original wait
                # must exhaust its1000 stalls and return failure, never unlock
                # it from polling or fabricate a successful codec write.
                assert transport('R',0xfec00134,1,0) == 0
                before = original.stalls
                assert original.call(0x17c38f) == 0 and original.stalls-before == 1000
                assert transport('R',0xfec0007c,2,0) == 0x574d
                original.transport = None
                process.stdin.close();assert process.wait(timeout=10) == 0
        original.destroy_startup()
        lives.append(dict(lifetime=lifetime,rings=rings,transactions=rows,calls=original.calls,
            source_identity=original.source_identity(),stalls=original.stalls,
            original_codec_helpers_returned=True,original_held_semaphore_timeout=True,
            original_destructors_release_all_allocations=True))
    result = dict(status='original-aci-startup-helpers-observed',lifetimes=lives,
        pins=pins+[pin(xbe),pin(observer),pin(Path(__file__))],
        limits='Prepared memory/IRQL caller and explicit initialized-kernel codec profile. Original codec/stream helpers are unchanged; stalls service an authored guest clock. No replacement IRQ-registration return, full cold boot, active SPDIF processing, precise AC-link bus phase or connected PCM-output claim.')
    report = output/'original-aci-startup.aot-private.json'
    report.write_text(json.dumps(result,indent=2)+'\n')
    print(f'PASS three original codec helper groups, six owned rings, asynchronous waits and three original semaphore timeouts; {report}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','observer','evidence','output'):parser.add_argument('--'+name,type=Path,required=True)
    args = parser.parse_args();check(args.xbe,args.observer,args.evidence,args.output)
