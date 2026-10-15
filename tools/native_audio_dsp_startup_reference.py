#!/usr/bin/env python3
"""Unchanged GP/EP setup against maintained MMIO and live owned source RAM.

Offline only. Kernel memory services and the settings caller are explicit
inputs. No original driver return, device read, bootstrap or completion is
substituted. The optional enclosing-call check advances guest time through the
original stall service and executes both real DSPs with the sequencer stopped.
The active extension runs the original SE start/stop helper over the same
live allocations and immutable physical EP lookup. Codec streams stay stopped;
this does not qualify complete original audio output or active reset.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP
from native_ep_startup_reference import OriginalStartup
from native_ep_reference import IMAGE_SHA256

# Retained physical immutable high-Y image, recorded in the private accepted
# audio-ep-y-lookup-20261014 manifest. The image itself is never redistributed.
EP_Y_LOOKUP_SHA256 = '9f2cb3fdbb541a80f4b2b49de094949c88b4edcd4305d3c764ff0de2a852a414'


def clock_result(reply):
    fields = reply.split()
    assert len(fields) == 9 and fields[0] == 'TIME', reply
    keys = ('ticks','gp_phase','ep_phase','gp_cycles','ep_cycles','gp_pc','ep_pc','events')
    return dict(zip(keys, map(int,fields[1:])))


def active_result(reply):
    fields = reply.split()
    assert len(fields) == 9 and fields[0] == 'ACTIVE', reply
    keys = ('ticks','gp_frames','ep_events','gp_dma_nodes','ep_dma_nodes','pcm_words','gp_outstanding','ep_work_pending')
    return dict(zip(keys,map(int,fields[1:])))


class Startup(OriginalStartup):
    transport = None
    stall = None

    def _instruction(self, cpu, address, size, unused):
        if address == 0x80000097 and self.stall is not None:
            stack = cpu.reg_read(UC_X86_REG_ESP)
            ret, us = struct.unpack('<II', cpu.mem_read(stack,8))
            self.stall(us)
            self.kernel_calls.append(dict(ordinal=151, service='stall', args=[us], result=0))
            cpu.reg_write(UC_X86_REG_EAX,0)
            cpu.reg_write(UC_X86_REG_ESP,stack+8)
            cpu.reg_write(UC_X86_REG_EIP,ret)
            return
        super()._instruction(cpu,address,size,unused)

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
        # Actual title owners need only DWORD alignment. Vary both bit2 and
        # bit3 so the original request mask is exercised without a fixed
        #16-byte-aligned fixture hiding the reached low-nibble5 request.
        self.owner, self.settings = 0x30000000+(0,4,12)[self.format_selector], 0x30003000
        self.cpu.mem_write(self.owner, bytes(0x100))
        self.cpu.mem_write(self.settings, bytes(0x100))
        self.put(self.settings+8, self.format_selector)
        self.put(self.owner+4, self.settings)
        # The enclosing caller retires the old controls/list pointers before
        # resource allocation, even on its initially empty owner.
        self.call(0x17c2ad,ecx=self.owner)
        assert self.call(0x17c031, ecx=self.owner) == 0


def check(args):
    if args.execute_active:
        args.execute_stall = True
        assert args.ep_y_lookup is not None, 'active DSPs require the verified immutable EP image'
        lookup = args.ep_y_lookup.read_bytes()
        assert len(lookup) == 8192 and hashlib.sha256(lookup).hexdigest() == EP_Y_LOOKUP_SHA256
    else:
        assert args.ep_y_lookup is None, 'EP image is only consumed by the active-clock extension'
    output = args.output.resolve()
    assert 'build' in output.parts and not output.exists()
    output.mkdir(parents=True)
    blob = args.xbe.read_bytes()
    assert hashlib.sha256(blob).hexdigest() == IMAGE_SHA256
    identity = dict(xbe_sha256=IMAGE_SHA256,
                    observer_sha256=hashlib.sha256(args.observer.read_bytes()).hexdigest(),
                    driver_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    guest_time_advanced=args.execute_stall, dsp_execution=args.execute_stall,
                    active_clock_requested=args.execute_active,
                    ep_y_lookup_sha256=EP_Y_LOOKUP_SHA256 if args.execute_active else None)
    reports = []
    for life in range(3):
        original = Startup(blob, life, life)
        rows, ram_events = [], []
        report = dict(lifetime=life, status='started', transactions=rows, ram_events=ram_events)
        reports.append(report)
        def save():
            (output/'startup.aot-private.json').write_text(json.dumps(dict(identity=identity,lifetimes=reports), indent=2)+'\n', newline='\n')
        with (output/f'process-{life}.aot-private.log').open('w') as log:
            observer_args = [str(args.observer.resolve())]
            if args.execute_active:observer_args += ['--active',str(args.ep_y_lookup.resolve())]
            process = subprocess.Popen(observer_args, stdin=subprocess.PIPE,
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
                def verify_bootstrap():
                    report['bootstrap_state'] = command('STATE')
                    state = report['bootstrap_state'].split()
                    assert len(state) == 7 and state[0] == 'STATE'
                    assert list(map(int,state[1:5])) == [1,1,1,1], 'both actual CPUs must be ready after exactly one bootstrap'
                    for kind, slot in (('GP',0x17dfb8),('EP',0x17dfd8)):
                        reply = command('P '+kind); assert reply.startswith('P ')
                        actual = bytes.fromhex(reply[2:]); assert len(actual) == 8192
                        source = bytes(original.cpu.mem_read(original.get(slot),8192))
                        expected = b''.join(struct.pack('<I',v&0xffffff) for v, in struct.iter_unpack('<I', source))
                        assert actual == expected, 'actual CPU bootstrap must match the current original source pages'
                        report[kind.lower()+'_bootstrap_sha256'] = hashlib.sha256(actual).hexdigest()
                def stall(us):
                    assert us == 10000 and 'dsp_clock' not in report
                    verify_bootstrap()
                    reply = command('T 11250')
                    state = clock_result(reply)
                    assert state['ticks'] == 11250 and state['gp_phase'] == 3 and state['ep_phase'] == 2
                    assert 0 < state['gp_cycles'] < 1333125 and 1333125 <= state['ep_cycles'] <= 1333128
                    assert state['ep_pc'] in (0x31,0x32) and state['events'] == 0
                    report['dsp_clock'] = state
                    count = len(ram_events)
                    assert command('T 11250') == reply and len(ram_events) == count
                if args.execute_stall:original.stall = stall
                original.prepare()
                report['original_owner'] = original.owner
                entries = [(0x17bf66,(),original.owner),(0x17b79f,(),0)]
                entries += [(0x17be82,(),original.owner)] if args.execute_stall else [(0x17b870,(),original.owner),(0x17b999,(),original.owner)]
                for entry, argv, ecx in entries:
                    report['current_original_entry'] = entry; save()
                    original.call(entry, argv, ecx)
                report['device_state'] = command('STATE')
                if not args.execute_stall:verify_bootstrap()
                else:assert 'dsp_clock' in report
                if args.execute_active:
                    start = active_result(command('ACTIVE'))
                    assert start['ticks'] == 11250 and not start['gp_frames'] and not start['ep_events']
                    assert not start['pcm_words']
                    report['active_start'] = start
                    report['current_original_entry'] = 0x17bc9e; save()
                    original.call(0x17bc9e,(1,),original.owner)
                    assert rows[-1]['operation'] == 'W' and rows[-1]['address'] == 0xfe802000 and rows[-1]['value'] == 15
                    # The absolute clock's first event is a partial interval;
                    # advance across eight EP boundaries, not eight fabricated
                    # complete periods beginning at this CPU helper's return.
                    target = (start['ticks']//6000+8)*6000
                    report['active_target_ticks'] = target; save()
                    run_reply = command(f'T {target}')
                    run = clock_result(run_reply)
                    progress = active_result(command('ACTIVE'))
                    report['active_run'] = dict(clock=run,devices=progress); save()
                    assert run['ticks'] == target and run['ep_phase'] == 2 and run['events'] == 8
                    assert progress['gp_frames'] == target//750-start['ticks']//750
                    assert progress['ep_events'] == 8 and not progress['pcm_words']
                    # Stop admission through the original SE helper, then
                    # retain every allocation while actual admitted work runs.
                    # Device disposal afterward is not an original DSP reset.
                    original.call(0x17bc9e,(0,),original.owner)
                    assert rows[-1]['operation'] == 'W' and rows[-1]['address'] == 0xfe802000 and rows[-1]['value'] == 7
                    drain_reply = command(f'T {target+6000}')
                    drained = clock_result(drain_reply)
                    final = active_result(command('ACTIVE'))
                    report['active_drain'] = dict(clock=drained,devices=final); save()
                    assert drained['ticks'] == target+6000 and drained['gp_phase'] == 3 and drained['ep_phase'] == 2
                    assert drained['ep_pc'] in (0x31,0x32), 'original EP must return to its real event poll before fixture disposal'
                    assert drained['events'] == 8 and final['gp_frames'] == progress['gp_frames'] and final['ep_events'] == 8
                    assert drained['gp_cycles'] > report['dsp_clock']['gp_cycles']
                    assert final['gp_dma_nodes'] > 0 and final['ep_dma_nodes'] > 0
                    assert not final['pcm_words']
                    assert not final['gp_outstanding'] and not final['ep_work_pending']
                    count = len(ram_events)
                    assert command(f'T {target+6000}') == drain_reply and len(ram_events) == count
                report['source_identity'] = original.source_identity()
                report['status'] = 'original-startup-active-clock-returned' if args.execute_active else 'original-startup-returned'
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
    detail = ('; original SE start/stop, eight shared-clock EP events and actual GP/EP DMA; codec output and reset remain separate'
              if args.execute_active else '; original10000us wait runs GP to yield and EP into polling without frame events'
              if args.execute_stall else '; execution/cadence still separate')
    print('PASS three unchanged original GP/EP setup returns and actual owned bootstrap images'+detail)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','observer','output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--execute-stall',action='store_true',help='Execute the unchanged enclosing setup and its real10000us wait')
    parser.add_argument('--execute-active',action='store_true',help='Also execute original SE start/stop and eight active EP events; implies --execute-stall')
    parser.add_argument('--ep-y-lookup',type=Path,help='Required verified private EP Y0800 image for --execute-active')
    check(parser.parse_args())
