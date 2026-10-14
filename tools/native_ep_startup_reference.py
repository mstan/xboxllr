#!/usr/bin/env python3
"""Compose unchanged original GP/EP driver setup and owned mapped DSP input.

The caller and bounded Xbox kernel RAM floor are authored. Driver and firmware
bytes are unchanged. Unsupported reset/core/timer/frame transitions are recorded
or remain guarded, never treated as an original boot or output qualification.
"""
import argparse
import json
import struct
import subprocess
from pathlib import Path

from unicorn import UC_HOOK_MEM_READ
from native_ep_directory_reference import OriginalProducer, sha
from native_ep_reference import IMAGE_SHA256


class OriginalStartup(OriginalProducer):
    def __init__(self, blob, lifetime, format_selector):
        self.reads = []
        self.format_selector = format_selector
        super().__init__(blob, lifetime)
        self.cpu.mem_map(0xfe810000, 0x50000)
        self.cpu.hook_add(UC_HOOK_MEM_READ, self._read)

    def _read(self, cpu, access, address, size, value, unused):
        if 0xfe800000 <= address < 0xfe860000:
            assert size == 4
            if address == 0xfe820010:
                # Existing synchronous maintained FE capacity, not hardware
                # timing, queued-method completion or a guessed DSP timer.
                self.put(address, 0x80)
            self.reads.append(dict(address=address, size=size, value=self.get(address)))

    def _write(self, cpu, access, address, size, value, unused):
        if 0x20000000 <= address < 0x21400000:
            self.owned(address, size)
        if 0xfe800000 <= address < 0xfe860000:
            assert size == 4 and not (address & 3)
            self.mmio.append(dict(address=address, size=size, value=value))

    def produce_startup(self):
        for initializer in (0x177784, 0x17778f, 0x17b14e, 0x17b159):
            self.call(initializer)
        self.owner, self.settings = 0x30000000, 0x30003000
        self.cpu.mem_write(self.owner, bytes(0x100))
        self.cpu.mem_write(self.settings, bytes(0x100))
        # Borrowed authored caller configuration. Its encoding is a real input
        # to unchanged setup; it is not an original settings-object constructor.
        self.put(self.settings+8, self.format_selector)
        self.put(self.owner+4, self.settings)
        assert self.call(0x17c031, ecx=self.owner) == 0
        self.call(0x17bdf3, ecx=self.owner)
        self.call(0x17b870, ecx=self.owner)
        self.call(0x17b999, ecx=self.owner)
        gp_owner, ep_owner = self.get(self.owner+0xc), self.get(self.owner+0x10)
        # Original private heap may place objects inside its own contiguous
        # page; do not mistake the object for a direct ExAllocatePool return.
        self.owned(gp_owner, 0x3c)
        self.owned(ep_owner, 0xc)
        assert self.get(ep_owner+4) == gp_owner
        gp_scratch = self.get(0x17dfb8)
        ep_scratch = self.get(0x17dfd8)
        gp_bytes, ep_bytes = self.get(0x17dfc0), self.get(0x17dfe0)
        self.owned(gp_scratch, gp_bytes, 'contiguous')
        self.owned(ep_scratch, ep_bytes, 'contiguous')
        assert bytes(self.cpu.mem_read(gp_scratch, 0x5cc)) == self.source(0x17d7e8, 0x5cc)
        ep = bytes(self.cpu.mem_read(ep_scratch, ep_bytes))
        assert ep[:0x320] == self.source(0x31be0e0, 0x320)
        assert ep[0x600:0xbfc] == self.source(0x31be400, 0x5fc)
        directory = list(struct.unpack_from('<36I', ep, 0x400))
        undefined = [16, 17, 18, 19, 24, 26, 28, 29, 30, 31, 32, 33]
        # These bytes come from the actual producer's caller stack, including
        # undefined slots; do not manufacture zero-initialized directory entries.
        context_pointer = self.get(ep_owner+8)
        context = list(struct.unpack('<6I', self.cpu.mem_read(context_pointer, 24)))
        gp_context = list(struct.unpack('<6I', self.cpu.mem_read(gp_scratch+0x800, 24)))
        assert gp_context == [0]*6
        expected_context = list(struct.unpack('<6I', self.source(0x31c5200, 24)))
        if self.format_selector != 2:
            expected_context[1:4] = [0, 0, 1 if self.format_selector == 1 else 2]
        assert context == expected_context
        registers = {m['address']: m['value'] for m in self.mmio}
        assert registers[0xfe83fffc] == 3 and registers[0xfe85fffc] == 3
        assert registers[0xfe802040] == self.get(0x17dfcc)
        assert registers[0xfe802048] == self.get(0x17dfec)
        assert registers[0xfe8020dc] == 19
        assert registers[0xfe802044] == self.get(0x17df8c)
        assert registers[0xfe80204c] == self.get(0x17df9c)
        return dict(format_selector=self.format_selector,
            caller_configuration='Borrowed authored original-encoding settings input; settings object construction is outside scope.',
            gp_owner=gp_owner, ep_owner=ep_owner,
            gp_scratch_virtual=gp_scratch, gp_scratch_bytes=gp_bytes,
            ep_scratch_virtual=ep_scratch, ep_scratch_bytes=ep_bytes,
            directory=directory, directory_undefined_slots=undefined,
            ep_control_context=context, gp_driver_context=gp_context,
            original_constant_context=list(struct.unpack('<6I', self.source(0x31c5200, 24))),
            gp_ep_shared_physical=self.get(0x17dfac),
            final_registers=registers, calls=self.calls, reads=self.reads,
            kernel_calls=self.kernel_calls, mmio=self.mmio)

    def destroy_startup(self):
        # The authored settings input was borrowed, never an allocated original
        # refcount owner. Remove that caller loan; original destructors own and
        # release every real driver pool/page/heap allocation unchanged.
        assert self.get(self.settings+8) == self.format_selector
        self.put(self.owner+4, 0)
        self.call(0x17bec1, ecx=self.owner)
        assert not any(a['live'] for a in self.allocations), self.allocations
        assert self.get(self.owner+0xc) == 0 and self.get(self.owner+0x10) == 0


def check(image, output, observer=None, ep_event_observer=None, ep_y_lookup=None, ep_sustained_input=None):
    image, output = image.resolve(), output.resolve()
    assert 'build' in output.parts, 'original evidence must stay under private build/'
    blob = image.read_bytes()
    assert sha(blob) == IMAGE_SHA256, 'unreviewed original image'
    if ep_sustained_input:
        assert ep_event_observer and ep_y_lookup, 'sustained execution requires the original observer and private lookup image'
        assert ep_sustained_input.stat().st_size==32*6*256*4
    output.mkdir(parents=True, exist_ok=True)
    records = []
    for lifetime, selector in enumerate((0, 1, 2)):
        producer = OriginalStartup(blob, lifetime, selector)
        result = producer.produce_startup()
        ram = output/f'original-startup-ram-{lifetime}.aot-private.bin'
        ram.write_bytes(producer.physical_ram())
        ownership = output/f'original-startup-ownership-{lifetime}.aot-private.bin'
        ownership.write_bytes(b''.join(struct.pack('<II', a['physical'], a['extent'])
                                      for a in producer.allocations if a['live']))
        registers = output/f'original-startup-registers-{lifetime}.aot-private.bin'
        registers.write_bytes(b''.join(struct.pack('<II', int(k), v)
            for k, v in sorted(result['final_registers'].items())))
        result.update(lifetime=lifetime, ram_sha256=sha(ram.read_bytes()),
            ownership_sha256=sha(ownership.read_bytes()),
            registers_sha256=sha(registers.read_bytes()),
            owned_allocations=[dict(a) for a in producer.allocations])
        if observer:
            runs=[]
            for mode in ('staged', 'gp', 'ep', 'revoked', 'transport'):
                command=[str(observer.resolve()), str(ram), str(registers),
                    str(ownership), mode, hex(result['gp_ep_shared_physical'])]
                if mode=='transport':
                    capture=output/f'original-shared-payload-{lifetime}.aot-private.bin'
                    command.append(str(capture))
                run = subprocess.run(command,
                    capture_output=True, text=True, timeout=30)
                log = output/f'original-startup-observer-{lifetime}-{mode}.aot-private.log'
                log.write_text(run.stdout+run.stderr, newline='\n')
                expected_exit=2 if mode in ('gp','ep') else 0
                assert run.returncode==expected_exit, run.stdout+run.stderr
                observation=json.loads(run.stdout)
                assert observation['mode']==mode
                if expected_exit:
                    assert observation['b2_guard'] and not observation['event_admitted']
                    assert 'B2=FFFFFF guard:' in run.stderr
                else:
                    assert not run.stderr
                if mode=='transport':
                    payload=capture.read_bytes()
                    expected=b''.join(struct.pack('<I', (packet*2017+channel*313+sample*19-9000)&0xffffff)
                        for channel in range(6) for packet in range(8) for sample in range(32))
                    assert payload==expected and len(payload)==6144
                    result['exact_shared_payload_sha256']=sha(payload)
                runs.append(dict(mode=mode, exit=run.returncode, expected_result=True,
                    observation=observation, log_sha256=sha(log.read_bytes())))
            result['observer_runs']=runs
        if ep_event_observer:
            runs=[]
            for mode in (('sustained',) if ep_sustained_input else ('startup','first-event','revoked')):
                command=[str(ep_event_observer.resolve()),str(ram),str(registers),str(ownership),mode]
                if ep_y_lookup:command.append(str(ep_y_lookup.resolve()))
                if mode=='sustained':
                    prefix=output/f'original-ep-sustained-{lifetime}'
                    command.extend((str(ep_sustained_input.resolve()),str(prefix)))
                run=subprocess.run(command,
                    capture_output=True,text=True,timeout=180 if mode=='sustained' else 30)
                log=output/f'original-ep-event-{lifetime}-{mode}.aot-private.log'
                log.write_text(run.stdout+run.stderr,newline='\n')
                assert run.returncode==(2 if mode=='first-event' and selector==2 and not ep_y_lookup else 0),run.stdout+run.stderr
                observation=json.loads(run.stdout)
                if mode=='first-event':
                    completed=observation['completed_descriptors']
                    assert completed[0]==[0x41a2,0x5be0,result['directory'][21],0xbc0,result['directory'][20]*4]
                    block={0:(201,0x70d4),1:(188,0x6de4),2:(754,0xc00)}[selector]
                    assert completed[1]==[0x41a2,0x5be0,block[0],0x2b00,block[1]]
                    assert completed[2:7]==[[0x41a2,0x5be0,256,i*256,0x10000+i*0x800] for i in range(5)]
                    tail=([[0x41a2,0x5be2,78,0x63d,0x7458]] if selector==2 else [])
                    tail.append([0x41a2,0x400603,0x1001,0x400 if selector==2 else 0x500])
                    if selector==2:tail.append([0x41a2,0x5be0,2146,0x2b00,0x17c8])
                    if ep_y_lookup and selector==2:
                        assert completed[7:10]==tail and len(completed)==36
                        assert completed[-3:]==[[0x41a2,0x4622,1498,0x401f],
                            [0x41a2,0x4622,220,0x1f],[0x41a2,0x5be2,128,0xb40,0xa268]]
                    else:assert completed[7:]==tail
                    assert observation['clock_events']==1 and observation['dma_nodes']==(36 if ep_y_lookup and selector==2 else 10 if selector==2 else 8)
                    assert observation['all_input_words_verified'] and observation['directory_layout_preserved']
                    assert observation['all_output_words_verified'] and observation['output_nodes']==(18 if ep_y_lookup and selector==2 else 2 if selector==2 else 1)
                    # Shared input starts zero. The high-Y image permits
                    # nonzero intermediate codec state; this checks transfers,
                    # not independent numerical codec correctness.
                    if ep_y_lookup and selector==2:
                        assert not observation['RAM_unchanged'] and observation['writes']==22
                        expected=[0x41a2,0x5be2,128,0xb40,0xa268]
                    else:
                        assert observation['RAM_unchanged'] and observation['writes']==(2 if selector==2 else 1)
                        expected=tail[-1]
                    assert observation['descriptor']==expected
                    if selector==2 and not ep_y_lookup:
                        assert observation['stage']=='original-first-event-next-boundary'
                        assert 'unmapped DSP read at 000fc9' in run.stderr
                        observation['next_boundary']='original PC031E reads Y:0FC9 without a supplied board lookup image'
                    else:
                        assert observation['stage']=='original-first-event-returned'
                        assert observation['original_buffer_toggles']==1
                        observation['next_boundary']='first event returned to real polling; connected clock/reset/processing remains open'
                if mode=='sustained':
                    assert observation['stage']=='original-sustained-events-returned'
                    assert observation['clock_events']==observation['original_buffer_toggles']==32
                    assert observation['dma_nodes']==(1198 if selector==2 else 230)
                    assert observation['output_nodes']==(555 if selector==2 else 32)
                    assert observation['directory_layout_preserved'] and observation['all_input_words_verified'] and observation['all_output_words_verified']
                    assert observation['fifo_bytes']==[32768,34580 if selector==2 else 0]
                    result['sustained_fifo_sha256']=[]
                    for fifo,count in enumerate(observation['fifo_bytes']):
                        capture=Path(str(prefix)+f'-fifo{fifo}.aot-private.bin')
                        assert capture.stat().st_size==count
                        result['sustained_fifo_sha256'].append(sha(capture.read_bytes()))
                runs.append(dict(mode=mode,exit=run.returncode,observation=observation,log_sha256=sha(log.read_bytes())))
            result['ep_event_runs']=runs
        producer.destroy_startup()
        result['original_destructors_release_all_allocations'] = True
        result['executed_original_instructions'] = producer.source_identity()
        records.append(result)
    data = dict(issue='beads-xbox.1.113.13', image_sha256=IMAGE_SHA256,
        tool_sha256=sha(Path(__file__).read_bytes()), lifetimes=records,
        original_boot_qualified=False, original_event_qualified=False,
        original_output_qualified=False,
        scope='Unchanged original resource allocation, GP setup17B870, EP setup17B999 and destructors; original mapped GP descriptor producer9E and helper126 feed the actual three-page EP scratch alias. Original EP dispatcher180 selector7 and276/2E1 reach their unknown bit9 guard.',
        limits='Authored offline caller/kernel RAM/settings input and diagnostic MIX payloads. Transport helper invocation is separate from original boot execution, which pauses before B2 without resuming a yield. FE capacity uses the existing synchronous maintained floor; reset/core-control writes are observations, not hardware/runtime admission. B0-B3, coherent C5/CA/D5 startup, original wake event and bit9 remain guarded. No title, playback or Xbox operation.')
    if ep_event_observer:
        data.update(issue='beads-xbox.1.113.9',ep_event_observer_sha256=sha(ep_event_observer.read_bytes()),
            scope='Unchanged driver tables and mapped EP startup execute through the sole maintained CPU. Selectors0/1 complete seven inputs and one interleaved stereo FIFO0 output then naturally return to their real poll. Selector2 completes ten nodes including78-word linear output, stereo FIFO0 and a2146-word P reload, then guards original Y:0FC9 at PC031E.',
            limits='Explicit offline caller/kernel allocation/settings and observed empty-list clock profile. Reset/core MMIO is recorded, not dispatched. Original shared inputs and outputs are zero; distinct nonzero output transport is separately physically observed. Delayed acknowledgement is queued independently of C4; exact clock/ACK/busy timing remains open. Selector2 high Y-memory semantics are unresolved. No completed title/output/reset or nonzero processing qualification. The older transport observer, if selected, retains its separately recorded historical B2 scope.')
        if ep_y_lookup:
            data.update(ep_y_lookup_sha256=sha(ep_y_lookup.read_bytes()),
                scope='Fresh original driver lifetimes with caller-supplied board Y0800..0FFF image in its separate immutable CPU lookup owner. All three selectors complete their first event and return naturally: selectors0/1 after8 transfers; selector2 after36 verified transfers/18 output nodes, including the physically observed FIFO1 zero-source route.',
                limits='Private board image remains external data, never committed or fabricated. No high-Y write, DMA/host alias or missing-image fallback. Original shared inputs are zero; selector2 produces nonzero intermediate/output state. Transfer bytes and original first-event execution are checked; independent numerical codec correctness and sustained events are not qualified. Reset/core MMIO is recorded, not dispatched; caller clock and exact ACK/busy timing remain open. Connected title/FE/SECTL/output remain required.')
        if ep_sustained_input:
            data.update(ep_sustained_input_sha256=sha(ep_sustained_input.read_bytes()),
                scope='All three unchanged original EP configurations complete32 consecutive events, with actual alternating input windows, output ring wrap, exact transfer bytes, idle polling preservation and original driver destruction. Private FIFO captures retain all output bytes for independent numerical checks.',
                limits='Authored signal loan, offline kernel/settings and explicit setup clock. This report establishes sustained execution and transport, not independent waveform correctness by itself. Original reset/core MMIO, initial SECTL, precise clock/ACK/busy timing, active FE and connected title output remain required. Private high-Y image is caller-supplied.')
    report=output/'original-ep-startup-reference.aot-private.json'
    report.write_text(json.dumps(data, indent=2)+'\n', newline='\n')
    print(f'PASS: {len(records)} original driver startup lifetimes; {report}')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--observer', type=Path)
    parser.add_argument('--ep-event-observer',type=Path)
    parser.add_argument('--ep-y-lookup',type=Path,help='private 2048-word Y0800..0FFF board image for the EP CPU owner')
    parser.add_argument('--ep-sustained-input',type=Path,help='private32-event,6-channel,256-sample planar DWORD signal loan')
    args=parser.parse_args()
    check(args.xbe, args.output, args.observer,args.ep_event_observer,args.ep_y_lookup,args.ep_sustained_input)
