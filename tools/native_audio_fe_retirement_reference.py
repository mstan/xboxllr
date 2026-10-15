#!/usr/bin/env python3
"""Execute the original active FE shutdown helper with explicit synthetic replies.

This establishes its actual submission/consumer/timeout contract, not hardware
completion or an emulator implementation. Driver callees and bytes are unchanged.
Only the imported microsecond stall has an authored kernel-service floor.
"""
import argparse
import hashlib
import json
from pathlib import Path

from unicorn import UC_HOOK_MEM_READ
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP
from native_ep_directory_reference import OriginalProducer
from native_ep_reference import IMAGE_SHA256


class Retirement(OriginalProducer):
    def __init__(self, blob, case):
        super().__init__(blob, 0)
        self.case = case
        self.cpu.mem_map(0xfe820000, 0x1000)
        self.notify = self.allocate('contiguous', 16) if case['notifier'] else 0
        self.put(0x17df38, self.notify)
        self.put(0xfe801100, case['fe'])
        self.put(0xfe802000, case['se'])
        assert self.get(0x189fb8) == 0x80000097, 'original imported KeStallExecutionProcessor ordinal151'
        self.events, self.polls, self.waits, self.pio_reads = [], [], [], 0
        self.cpu.hook_add(UC_HOOK_MEM_READ, self._read)

    def _instruction(self, cpu, address, size, unused):
        if address != 0x80000097:
            return super()._instruction(cpu, address, size, unused)
        stack = cpu.reg_read(UC_X86_REG_ESP)
        microseconds = self.get(stack+4)
        assert microseconds == (1 if self.notify else 0x29b)
        self.waits.append(microseconds)
        self.events.append(dict(kind='authored_kernel_stall', requested_microseconds=microseconds))
        # This explicit kernel floor does not produce elapsed time or advance
        # hardware. Synthetic notifier delivery is separately labelled below.
        cpu.reg_write(UC_X86_REG_EAX, 0)
        cpu.reg_write(UC_X86_REG_ESP, stack+8)
        cpu.reg_write(UC_X86_REG_EIP, self.get(stack))

    def _read(self, cpu, access, address, size, value, unused):
        if 0xfe800000 <= address < 0xfe830000:
            assert size == 4 and address in (0xfe801100, 0xfe802000, 0xfe820010)
            if address == 0xfe820010:
                # Authored empty/full-space sequence; neither latency nor an
                # actual device queue is claimed. Original code masks low2bits.
                self.put(address, (0, 3, 7)[min(self.pio_reads, 2)])
                self.pio_reads += 1
            self.events.append(dict(kind='mmio_read', address=address, value=self.get(address)))
        elif self.notify and self.notify <= address < self.notify+4096:
            assert address == self.notify+15 and size == 1, 'original consumer must use status only'
            assert cpu.reg_read(UC_X86_REG_EIP) == 0x179e17
            number = len(self.polls)+1
            threshold, status = self.case['delivery_poll'], self.case['delivery_status']
            if threshold is not None and number == threshold:
                self.cpu.mem_write(address, bytes([status]))
                self.events.append(dict(kind='synthetic_device_reply', poll=number, status=status))
            observed = bytes(cpu.mem_read(address, 1))[0]
            self.polls.append(observed)
            self.events.append(dict(kind='original_status_read', poll=number, status=observed))

    def _write(self, cpu, access, address, size, value, unused):
        if 0xfe800000 <= address < 0xfe830000:
            assert size == 4 and (address, value) in ((0xfe82010c, 0), (0xfe82010c, 1), (0xfe820104, 2))
            self.events.append(dict(kind='mmio_write', address=address, value=value))
        elif getattr(self, 'notify', 0) and self.notify <= address < self.notify+4096:
            assert address == self.notify+15 and size == 1 and value == 0x80
            self.events.append(dict(kind='original_status_write', status=value))
        else:
            super()._write(cpu, access, address, size, value, unused)


def check(xbe, output):
    blob = xbe.read_bytes()
    assert hashlib.sha256(blob).hexdigest() == IMAGE_SHA256
    output = output.resolve()
    assert 'build' in output.parts and not output.exists(), 'new private evidence directory required'
    output.mkdir(parents=True)
    common = dict(mode=1, fe=0x7130f, se=15, notifier=True, delivery_poll=None, delivery_status=0x80)
    cases = [
        dict(common, name='delayed_completion', delivery_poll=3, delivery_status=1),
        dict(common, name='first_poll_completion', delivery_poll=1, delivery_status=1),
        dict(common, name='pending_timeout'),
        dict(common, name='zero_status_timeout', delivery_poll=1, delivery_status=0),
        dict(common, name='other_status_timeout', delivery_poll=1, delivery_status=2),
        dict(common, name='no_notifier', notifier=False),
        dict(common, name='already_held', fe=0x7138f),
        dict(common, name='setup_stopped', se=7),
        dict(common, name='release', mode=0),
    ]
    reports = []
    for case in cases:
        original = Retirement(blob, case)
        result = original.call(0x17c1b1, (case['mode'],))
        writes = [(e['address'], e['value']) for e in original.events if e['kind']=='mmio_write']
        status_writes = [e for e in original.events if e['kind']=='original_status_write']
        active = bool(case['mode'] and not(case['fe'] & 0xe0) and (case['se'] & 0x18))
        completed = active and case['notifier'] and case['delivery_status']==1 and case['delivery_poll'] is not None
        assert result == int(active), 'return value reports active branch, NOT successful notification'
        if active:
            assert original.pio_reads == 3
            assert writes == [(0xfe82010c,0)]+([(0xfe820104,2)] if case['notifier'] else [])
            if case['notifier']:
                # The original countdown tests the old value after decrement.
                # There are668 polls/stalls on expiry, not667.
                count = case['delivery_poll'] if completed else 0x29c
                assert len(original.polls) == count and original.waits == [1]*(count-int(completed))
                assert len(status_writes) == 2
                assert original.polls[-1] == (case['delivery_status'] if case['delivery_poll'] else 0x80)
                interesting = [e['kind'] for e in original.events if e['kind'] in ('mmio_write','original_status_write')]
                assert interesting == ['mmio_write','original_status_write','mmio_write','original_status_write']
            else:
                assert original.waits == [0x29b] and not original.polls and not status_writes
        else:
            assert not original.waits and not original.polls and not status_writes
            assert writes == ([(0xfe82010c,1)] if not case['mode'] else [])
            assert original.pio_reads == (3 if not case['mode'] else 0)
        if original.notify:
            expected = bytearray(b'\xa5'*4096)
            if active: expected[15] = 0x80
            assert bytes(original.cpu.mem_read(original.notify,4096)) == bytes(expected)
        identity = original.source_identity()
        report = dict(case=case, original_return=result, actual_consumer_saw_one=bool(1 in original.polls),
                      synthetic_completion_case=bool(completed), poll_count=len(original.polls),
                      stall_calls=len(original.waits), requested_stall_microseconds=sum(original.waits),
                      final_status=bytes(original.cpu.mem_read(original.notify+15,1))[0] if original.notify else None,
                      original_calls=original.calls, events=original.events, source_identity=identity,
                      original_allocations_retained=True)
        assert report['actual_consumer_saw_one'] == bool(completed)
        path=output/(case['name']+'.aot-private.json')
        path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        reports.append(dict(name=case['name'], original_return=result, consumer_completed=bool(completed),
                            polls=len(original.polls),stalls=len(original.waits),sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    summary=dict(status='original-caller-contract-under-synthetic-device-replies',image_sha256=IMAGE_SHA256,
        cases=reports,hardware_operations=0,emulator_semantics_qualified=False,
        conclusion='Return1 and final pending80 do not prove completion; retain actual status1 consumption before reset',
        limits='Authored DISPATCH_LEVEL and kernel stall floor; no real elapsed time, FE/DMA ordering, hardware notification payload, or connected title retirement')
    (output/'summary.aot-private.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    print('PASS nine unchanged original FE shutdown/release paths; notification, timeout and cleanup distinguished')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    check(args.xbe,args.output)
