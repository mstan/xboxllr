#!/usr/bin/env python3
"""Prepare pinned ADPCM HLE plus a separate APU-decoder reference test input.

All imported implementation remains in ignored private build output.
"""
import argparse,hashlib,json,subprocess
from pathlib import Path

PIN='bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0'
GIT='C:/Program Files/Git/cmd/git.exe'

def prepare(donor,output):
    def read(name):return subprocess.run([GIT,'-C',str(donor),'show',PIN+':'+name],check=True,capture_output=True).stdout
    output.mkdir(parents=True,exist_ok=True)
    sources={}
    for name in ('src/hle/xbox_adpcm.c','src/hle/xbox_adpcm.h','src/apu/apu_state.h',
                 'src/apu/apu_vp.c','src/apu/apu_regs.h','LICENSE','LICENSE.upstream-MIT','LICENSES/LGPL-2.1.txt'):
        data=read(name);target=output/Path(name).name;target.write_bytes(data)
        sources[name]=hashlib.sha256(data).hexdigest()
    apu=(output/'apu_state.h').read_text()
    start=apu.index('static inline int adpcm_decode_block(');end=apu.index('\n}',start)+2
    notice=apu[:apu.index('*/')+2]
    reference=notice+'\n#include <stdint.h>\n#include <stddef.h>\n'+apu[start:end]+'''
/* Test-only entry: the VP consumes only the first 64 frames per block. */
int pilot_adpcm_lle_reference(int16_t *out,const uint8_t *in,size_t bytes,int channels) {
    return adpcm_decode_block(out,in,bytes,channels);
}
'''
    (output/'adpcm_lle_reference.c').write_text(reference,newline='\n')
    vp=(output/'apu_vp.c').read_text();regs=(output/'apu_regs.h').read_text()
    assert 'block_position = cbo % ADPCM_SAMPLES_PER_BLOCK' in vp
    assert '#define ADPCM_SAMPLES_PER_BLOCK 64' in regs
    (output/'NOTICE').write_text(
        'Private GPL-dependent Xbox ADPCM research build; not an intended non-GPL shipping artifact.\n'
        'Direct donor: https://github.com/phobos665/xboxrecomp/tree/'+PIN+'\n'
        'HLE codec retains its NoRain211/doaxbv-re provenance and GPL-3.0 notice.\n'
        'APU reference retains xemu copyright/LGPL-2-or-later header and is linked only into tests.\n'
        'adpcm_lle_reference.c extracts the complete original decoder, unchanged, plus a test entry.\n'
        'The VP block-consumption rule is 64 samples/channel; the decoder also computes an unused 65th.\n',newline='\n')
    result=dict(repository='https://github.com/phobos665/xboxrecomp',revision=PIN,
                transitive_hle='https://github.com/NoRain211/doaxbv-re',
                scope='private research; GPL HLE, separate LGPL APU reference; no intended non-GPL shipping import',
                sources=sources,prepared={p.name:hashlib.sha256(p.read_bytes()).hexdigest()
                                         for p in output.iterdir() if p.is_file() and p.name!='donor.json'})
    (output/'donor.json').write_text(json.dumps(result,indent=2)+'\n',newline='\n')
    return dict(revision=PIN,source_files=len(sources))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--donor',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();print(json.dumps(prepare(a.donor,a.output)))
