#!/usr/bin/env python3
"""Recover Sofdec entry roots from this XBE's own section/pointer tables.

Phobos's Blinx/OutRun discovery fix identifies the PSGSFD_I/_P section bases
as indirect entry roots. Derive those addresses anew; never reuse PAL seeds.
The original title decoder remains responsible for frames, ADX and timing.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from aot_inventory import parse_xbe


def recover(blob, functions):
    image=parse_xbe(blob)
    starts={int(f['start'],16) for f in functions}
    sections=image['sections']
    roots=[]
    for section in sections:
        if section['name'] not in ('PSGSFD_I','PSGSFD_P'):continue
        if not section['executable_candidate'] or not section['raw_size']:
            raise ValueError('Sofdec section lacks executable input bytes')
        references=[]
        for table in sections:
            if table['name'] not in ('.rdata','.data'):continue
            raw=blob[table['raw_offset']:table['raw_offset']+table['raw_size']]
            for offset in range(0,len(raw)-3,4):
                if struct.unpack_from('<I',raw,offset)[0]==section['address']:
                    references.append(f"0x{table['address']+offset:08X}")
        if not references:raise ValueError('No title pointer table refers to '+section['name'])
        roots.append(dict(start=f"0x{section['address']:08X}",section=section['name'],
            references=references,already_discovered=section['address'] in starts,
            note='Title-owned Sofdec section entry, recovered from this image; no imported revision address'))
    if len(roots)!=2:raise ValueError('Expected both Sofdec decoder section roots')
    return image,roots


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe',type=Path,required=True)
    parser.add_argument('--discovery',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if 'build' not in args.output.resolve().parts:raise ValueError('Title-derived seed output must stay under build')
    image,roots=recover(args.xbe.read_bytes(),json.loads(args.discovery.read_text()))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(roots,indent=2)+'\n')
    manifest=dict(xbe_sha256=image['sha256'],seed_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
        source='Phobos bcccf8c config/seeds/4D530013.json and 53450036.json, algorithmic reference',
        policy='Original compiled Sofdec/ADX decoder; no XMV substitution, skipped frames or synthetic audio completion',roots=roots)
    args.output.with_suffix('.contract.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest,indent=2))
