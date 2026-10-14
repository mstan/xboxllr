#!/usr/bin/env python3
"""Actual original GP effects publication, paused at its real acknowledgement wait."""
import argparse, hashlib, json, struct, subprocess
from pathlib import Path
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import *
from native_dsp_control_reference import OriginalControl
from native_ep_reference import IMAGE_SHA256
ASSET_SHA="319a8f34f42e8198cccf3fd5e6b517073fd2b440deb3478b55db3ea1e81bca37"
def sha(b): return hashlib.sha256(b).hexdigest()
class Publication(OriginalControl):
    def __init__(self, blob):
        super().__init__(blob,0,0)
        self.pause_enabled=True
        self.wait=False
        self.pin_pages={};self.pin_ranges={};self.pin_events=[]
        self.cpu.hook_add(UC_HOOK_CODE,self.pause)
    def allocate(self,kind,requested,alignment=0x1000,protection=4):
        # Exact reached large-pool request: actual original ordinal15 evidence
        # establishes page-aligned base/extent; retain authored physical floor.
        if kind=='pool' and requested==413696:
            self.pool_next=(self.pool_next+4095)&~4095
        base=super().allocate(kind,requested,alignment,protection)
        if kind=='pool' and requested==413696:
            a=self.owned(base,requested);assert not(base&4095) and not(a['physical']&4095) and a['extent']==413696
        return base
    def _instruction(self,cpu,address,size,unused):
        if address==0x80000000+175:
            sp=cpu.reg_read(UC_X86_REG_ESP)
            start,length,unlock=[self.get(sp+4+i*4) for i in range(3)]
            assert length and start+length<=0x100000000 and unlock in (0,1),"Invalid page lock range/boolean"
            allocation=self.owned(start,length)
            key=(start,length);pages=list(range(start&~4095,(start+length+4095)&~4095,4096))
            assert pages and allocation['physical']%4096==allocation['virtual']%4096
            if unlock:assert self.pin_ranges.get(key,0)>0 and all(self.pin_pages.get(x,0)>0 for x in pages),"Unmatched page unlock"
            else:assert all(self.pin_pages.get(x,0)<0xffffffff for x in pages),"Page pin overflow"
            delta=-1 if unlock else 1
            self.pin_ranges[key]=self.pin_ranges.get(key,0)+delta
            for page in pages:self.pin_pages[page]=self.pin_pages.get(page,0)+delta
            self.pin_events.append({'start':start,'length':length,'unlock':unlock,'pages':pages,
                'physical_start':allocation['physical']+start-allocation['virtual']})
            self.kernel_calls.append({'ordinal':175,'service':'validated_owned_page_pin_floor','args':[start,length,unlock],'result':0})
            cpu.reg_write(UC_X86_REG_EAX,0);cpu.reg_write(UC_X86_REG_ESP,sp+16);cpu.reg_write(UC_X86_REG_EIP,self.get(sp));return
        if address in (0x80000000+17,0x80000000+171):
            sp=cpu.reg_read(UC_X86_REG_ESP);base=self.get(sp+4);allocation=self.owned(base)
            assert not any(count and allocation['virtual']<=start<allocation['virtual']+allocation['extent'] for (start,length),count in self.pin_ranges.items()),"Free of pinned original allocation"
        super()._instruction(cpu,address,size,unused)
    def pause(self,cpu,address,size,unused):
        if self.pause_enabled and address==0x177a3a:
            self.wait=True;cpu.emu_stop()
    def publish(self, asset):
        startup=self.produce_control()
        self.asset_base=0x31000000
        self.cpu.mem_map(self.asset_base,0x10000)
        self.cpu.mem_write(self.asset_base,asset)
        self.put(self.stack,self.end)
        for i,v in enumerate((self.asset_base,len(asset),0)):
            self.put(self.stack+4+i*4,v)
        self.put(self.stack+16,0x76543210)
        self.saved={r:0x12340000+i for i,r in enumerate((UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI))}
        for r,v in self.saved.items():self.cpu.reg_write(r,v)
        self.cpu.reg_write(UC_X86_REG_ECX,startup['gp_owner'])
        self.cpu.reg_write(UC_X86_REG_ESP,self.stack)
        self.cpu.emu_start(0x177ab4,self.end,count=1000000)
        assert self.wait and self.cpu.reg_read(UC_X86_REG_EIP)==0x177a3a,"Original publication did not reach acknowledgement wait"
        scratch=self.get(0x17dfb8)
        assert self.get(scratch+0x810)==3
        assert bytes(self.cpu.mem_read(self.asset_base,len(asset)))==asset
        return startup

def physical(p,linear):
    table=p.get(0x17dfc8);a=p.owned(table,8);assert a
    maximum=p.get(0xfe8020d4)
    assert linear//4096<=maximum
    slot=table+linear//4096*8;p.owned(slot,8)
    page=p.get(slot);assert not(page&4095)
    result=page+(linear&4095)
    assert any(x['live'] and x['physical']<=result< x['physical']+x['extent'] for x in p.allocations)
    return result

def continue_publication(p,data,asset,output,checker):
    initial=p.physical_ram();live=[dict(x) for x in p.allocations if x['live']]
    frozen={x['virtual']:bytes(p.cpu.mem_read(x['virtual'],x['extent'])) for x in live}
    capture=output/'continued-ram.aot-private.bin'
    command=[str(checker.resolve()),str((output/'publication-ram.aot-private.bin').resolve()),
        str((output/'publication-registers.aot-private.bin').resolve()),str((output/'publication-ownership.aot-private.bin').resolve()),
        str(data['voice_physical']),str(data['source_sge_physical']),str(data['notifier_physical']),'frames',str(capture.resolve())]
    r=subprocess.run(command,capture_output=True,text=True,timeout=30)
    (output/'continuation.log').write_text(r.stdout+r.stderr)
    assert not r.returncode and not r.stderr,r.stdout+r.stderr
    observation=json.loads(r.stdout)
    if observation['status']!='original_gp_publication_frame_continued':return {'continuation':observation,'original_acknowledged':False}
    after=capture.read_bytes();assert len(after)==len(initial)==p.RAM_BYTES
    writes=list(struct.iter_unpack('<II',Path(str(capture)+'.writes.aot-private.bin').read_bytes()))
    # Closed original output areas plus original allocated graph scratch; only
    # actually changed bytes within actual recorded device writes are returned.
    pool=next(x for x in live if x['kind']=='pool' and x['requested']==413696)
    allowed=[(data['voice_physical']+71*128,128),(physical(p,0x800),24),
        (physical(p,0xb000),2560),(pool['physical'],pool['extent'])]
    allowed += [(physical(p,0x8000+i*0x800),128) for i in range(6)]
    for address,length in writes:
        assert length and any(b<=address and address+length<=b+n for b,n in allowed),'Write outside closed original output areas'
        assert any(x['physical']<=address and address+length<=x['physical']+x['extent'] for x in live),'Write without original live owner'
    assert live==[dict(x) for x in p.allocations if x['live']],'Original owner drift'
    assert all(bytes(p.cpu.mem_read(x['virtual'],x['extent']))==frozen[x['virtual']] for x in live),'Original CPU allocation changed while paused'
    changed=[]
    for index,(before,value) in enumerate(zip(initial,after)):
        if before!=value:
            assert any(a<=index<a+n for a,n in writes),'Changed byte not produced by observed device write'
            matches=[x for x in live if x['physical']<=index<x['physical']+x['extent']]
            assert len(matches)==1,'Ambiguous or stale physical owner'
            changed.append((matches[0]['virtual']+index-matches[0]['physical'],value))
    assert bytes(p.cpu.mem_read(p.asset_base,len(asset)))==asset
    # Validation finishes before any returned writes; never copy stack or asset.
    for address,value in changed:p.cpu.mem_write(address,bytes([value]))
    original_context=list(struct.unpack_from('<6I',initial,physical(p,0x800)))
    resulting_context=list(struct.unpack_from('<6I',after,physical(p,0x800)))
    assert original_context[4]==3 and resulting_context==original_context[:4]+[0]+original_context[5:]
    assert p.get(p.get(0x17dfb8)+0x810)==0
    p.pause_enabled=False
    p.cpu.emu_start(0x177a3a,p.end,count=100000)
    assert p.cpu.reg_read(UC_X86_REG_EIP)==p.end and p.cpu.reg_read(UC_X86_REG_ESP)==p.stack+16
    assert p.get(p.stack+16)==0x76543210
    assert all(p.cpu.reg_read(reg)==value for reg,value in p.saved.items())
    assert p.cpu.reg_read(UC_X86_REG_EAX)==0 and bytes(p.cpu.mem_read(p.asset_base,len(asset)))==asset
    return {'continuation':observation,'original_acknowledged':True,'original_return':0,
        'returned_changed_bytes':len(changed),'recorded_device_writes':writes,'closed_allowed_extents':allowed,
        'borrowed_asset_unchanged':True,'original_stack_and_nonvolatiles_preserved':True,
        'limits':'One uploaded graph frame with authored PCM/voice/kernel allocation floor; EP, parameters and full shell not qualified'}

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--xbe',type=Path,required=True);ap.add_argument('--asset',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--checker',type=Path);a=ap.parse_args()
    blob=a.xbe.read_bytes();asset=a.asset.read_bytes()
    assert sha(blob)==IMAGE_SHA256 and len(asset)==15188 and sha(asset)==ASSET_SHA
    assert not a.output.exists();a.output.mkdir(parents=True)
    p=Publication(blob)
    report={'scope':'Original producer only; no firmware event or acknowledgement fabricated','inputs':{str(a.xbe):sha(blob),str(a.asset):sha(asset),str(Path(__file__)):sha(Path(__file__).read_bytes())}}
    try:
        data=p.publish(asset)
        ram=p.physical_ram();(a.output/'publication-ram.aot-private.bin').write_bytes(ram)
        (a.output/'publication-ownership.aot-private.bin').write_bytes(b''.join(struct.pack('<II',x['physical'],x['extent']) for x in p.allocations if x['live']))
        regs={m['address']:m['value'] for m in p.mmio}
        (a.output/'publication-registers.aot-private.bin').write_bytes(b''.join(struct.pack('<II',x,v) for x,v in sorted(regs.items())))
        report.update(pin_events=p.pin_events,status='paused_at_original_acknowledgement_wait',pc=hex(p.cpu.reg_read(UC_X86_REG_EIP)),startup=data,context=[p.get(p.get(0x17dfb8)+0x800+i*4) for i in range(6)],allocations=p.allocations,original_acknowledged=False)
        if a.checker:
            report.update(continue_publication(p,data,asset,a.output,a.checker))
            if report['original_acknowledged']:report['status']='original_gp_publication_acknowledged'
        report['original_executed_source_identity']=p.source_identity()
    except Exception as e:
        sp=p.cpu.reg_read(UC_X86_REG_ESP)
        report.update(status='blocked_original_producer',pc=hex(p.cpu.reg_read(UC_X86_REG_EIP)),guard=str(e),original_acknowledged=False,
            blocked_stack={'return_pc':hex(p.get(sp)), 'first_three_args':[hex(p.get(sp+4+i*4)) for i in range(3)]},
            live_allocations=[x for x in p.allocations if x['live']],
            borrowed_asset_unchanged=bytes(p.cpu.mem_read(p.asset_base,len(asset)))==asset,
            last_kernel_calls=p.kernel_calls[-6:], wait_reached=p.wait)
        report['next_requirement']='Investigate exact recorded original producer/continuation guard; no substituted acknowledgement'
    (a.output/'producer-report.aot-private.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
