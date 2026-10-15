#!/usr/bin/env python3
"""Unchanged GP/EP setup against maintained MMIO and live owned source RAM.

Offline only. Kernel memory services and the settings caller are explicit
inputs. No original driver return, device read, bootstrap or completion is
substituted. This transport does not yet advance DSP execution or guest time.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from unicorn.x86_const import UC_X86_REG_EIP
from native_ep_startup_reference import OriginalStartup
from native_ep_reference import IMAGE_SHA256


class Startup(OriginalStartup):
    transport = None

    def _read(self, cpu, access, address, size, value, unused):
        if 0xfe800000 <= address < 0xfe860000:
            assert size == 4
            assert self.transport is not None
            observed = self.transport('R', address, size, 0)
            self.put(address, observed)
            self.reads.append(dict(address=address, size=size, value=observed))

    def _write(self, cpu, access, address, size, value, unused):
        super()._write(cpu, access, address, size, value, unused)
        if 0xfe800000 <= address < 0xfe860000:
            assert self.transport is not None
            self.mmio[-1]['pc'] = cpu.reg_read(UC_X86_REG_EIP)
            self.transport('W', address, size, value)

    def physical_owner(self, address, size):
        assert size and size <= 4096
        owners = [a for a in self.allocations if a['live'] and
                  a['physical'] <= address and address+size <= a['physical']+a['extent']]
        assert len(owners) == 1, f'physical access lacks one live owner: {address:x}+{size:x}'
        return owners[0]['virtual']+address-owners[0]['physical']

    def prepare(self):
        for entry in (0x177784, 0x17778f, 0x17b14e, 0x17b159):
            self.call(entry)
        self.owner, self.settings = 0x30000000, 0x30003000
        self.cpu.mem_write(self.owner, bytes(0x100))
        self.cpu.mem_write(self.settings, bytes(0x100))
        self.put(self.settings+8, self.format_selector)
        self.put(self.owner+4, self.settings)
        assert self.call(0x17c031, ecx=self.owner) == 0


def check(args):
    output = args.output.resolve()
    assert 'build' in output.parts and not output.exists()
    output.mkdir(parents=True)
    blob = args.xbe.read_bytes()
    assert hashlib.sha256(blob).hexdigest() == IMAGE_SHA256
    identity = dict(xbe_sha256=IMAGE_SHA256,
                    observer_sha256=hashlib.sha256(args.observer.read_bytes()).hexdigest(),
                    driver_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    guest_time_advanced=False, dsp_execution=False)
    reports = []
    for life in range(3):
        original = Startup(blob, life, life)
        rows, ram_events = [], []
        report = dict(lifetime=life, status='started', transactions=rows, ram_events=ram_events)
        reports.append(report)
        def save():
            (output/'startup.aot-private.json').write_text(json.dumps(dict(identity=identity,lifetimes=reports), indent=2)+'\n', newline='\n')
        with (output/f'process-{life}.aot-private.log').open('w') as log:
            process = subprocess.Popen([str(args.observer.resolve())], stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=log, text=True, bufsize=1)
            try:
                assert process.stdout.readline().strip() == 'READY'
                def command(line):
                    process.stdin.write(line+'\n'); process.stdin.flush()
                    while True:
                        reply = process.stdout.readline().strip()
                        parts = reply.split()
                        if parts and parts[0] in ('MEMR', 'MEMW'):
                            address, size = int(parts[1], 16), int(parts[2])
                            virtual = original.physical_owner(address, size)
                            if parts[0] == 'MEMR':
                                assert len(parts) == 3
                                data = bytes(original.cpu.mem_read(virtual, size))
                                answer = 'DATA '+data.hex()
                            else:
                                assert len(parts) == 4
                                data = bytes.fromhex(parts[3]); assert len(data) == size
                                original.cpu.mem_write(virtual, data); answer = 'DONE'
                            ram_events.append(dict(operation=parts[0], physical=address, virtual=virtual,
                                                   size=size, sha256=hashlib.sha256(data).hexdigest()))
                            process.stdin.write(answer+'\n'); process.stdin.flush()
                        else:
                            return reply
                def transport(op, address, size, value):
                    row = dict(operation=op, address=address, size=size, value=value,
                               pc=original.cpu.reg_read(UC_X86_REG_EIP))
                    rows.append(row)
                    reply = command(f'{op} {address:x} {size} {value:x}')
                    row['reply'] = reply
                    if op == 'W':
                        assert reply == 'OK', reply
                        return None
                    assert reply.startswith('OK ') and len(reply) == 11, reply
                    return int(reply[3:], 16)
                original.transport = transport
                original.prepare()
                for entry, argv, ecx in ((0x17bc43,(0,),original.owner),
                                         (0x17bf66,(),original.owner),(0x17b79f,(),0),
                                         (0x17b870,(),original.owner),(0x17b999,(),original.owner)):
                    report['current_original_entry'] = entry; save()
                    original.call(entry, argv, ecx)
                report['device_state'] = command('STATE')
                state = report['device_state'].split()
                assert len(state) == 7 and state[0] == 'STATE'
                assert list(map(int,state[1:5])) == [1,1,1,1], 'both actual CPUs must be ready after exactly one bootstrap'
                for kind, slot in (('GP',0x17dfb8),('EP',0x17dfd8)):
                    reply = command('P '+kind); assert reply.startswith('P ')
                    actual = bytes.fromhex(reply[2:]); assert len(actual) == 8192
                    source = bytes(original.cpu.mem_read(original.get(slot),8192))
                    expected = b''.join(struct.pack('<I',v&0xffffff) for v, in struct.iter_unpack('<I', source))
                    assert actual == expected, 'actual CPU bootstrap must match the current original source pages'
                    report[kind.lower()+'_bootstrap_sha256'] = hashlib.sha256(actual).hexdigest()
                report['source_identity'] = original.source_identity()
                report['status'] = 'original-startup-returned'
            except BaseException as error:
                report.update(status='stopped', error=repr(error), source_identity=original.source_identity()); save(); raise
            finally:
                process.stdin.close()
                report['observer_exit'] = process.wait(timeout=10)
                save()
        assert report['observer_exit'] == 0
        # Device owners have been retired before original caller resources are
        # destroyed. This is not an original active stop/reset qualification.
        original.transport = lambda op,address,size,value: (_ for _ in ()).throw(AssertionError('unexpected cleanup MMIO'))
        original.destroy_startup()
        report['original_allocations_freed'] = not any(a['live'] for a in original.allocations)
        save()
    print('PASS three unchanged original GP/EP setup returns and actual owned bootstrap images; execution/cadence still separate')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','observer','output'):
        parser.add_argument('--'+name, type=Path, required=True)
    check(parser.parse_args())
