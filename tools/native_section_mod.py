"""Generate a private, static data-section mod without changing the input XBE.

The runtime independently admits the original image, section descriptors,
payload digests, code exclusion and relocated ownership. A counter reservation
must have been reviewed for this exact title; zero padding alone is no proof.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
from xbe_image import Xbe


def cbytes(data):
    return "\n".join("    " + ",".join(f"0x{x:02x}" for x in data[i:i+16]) + ","
                     for i in range(0, len(data), 16))


def section_registry(data: bytes, replacements: list[tuple[str, bytes, int]], mod_id: str):
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_.-]{0,127}", mod_id):
        raise ValueError("invalid section mod registry ID")
    image = Xbe(data)
    if not 1 <= len(replacements) <= 16 or len(data) > 256*1024*1024:
        raise ValueError("unsupported section mod count or source size")
    align = lambda x: (x + 4095) & -4096
    end = align(image.base + image.size)
    if end > 0x4000000:
        raise ValueError("source exceeds native title image range")
    seen, cells, records, payloads = set(), set(), [], []
    for name, payload, counter_offset in replacements:
        section = image.named(name)
        if name in seen or section.flags & 4 or not payload or len(payload) > 16*1024*1024:
            raise ValueError("duplicate, executable or invalid section replacement")
        if (counter_offset & 1 or counter_offset < image.table_end or
                counter_offset + 4 > image.headers or counter_offset < 0 or
                any(data[counter_offset:counter_offset+4])):
            raise ValueError("invalid reviewed header counter reservation")
        new_cells = {image.base+counter_offset, image.base+counter_offset+2}
        if cells & new_cells or any(new_cells.intersection((s.head, s.tail)) for s in image.sections):
            raise ValueError("section counter reservation already owned")
        seen.add(name);cells.update(new_cells)
        size = align(len(payload))
        if end+size > 0x4000000 or sum(len(p) for p in payloads)+len(payload) > 16*1024*1024:
            raise ValueError("section mod exceeds owned image range")
        records.append(dict(name=name,index=image.sections.index(section),
                            counter_offset=counter_offset,original_descriptor=data[section.header:section.header+56].hex(),
                            bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest(),
                            address=end,virtual_bytes=size))
        payloads.append(payload);end+=size
    source_sha = hashlib.sha256(data).hexdigest()
    lines = ['/* Private title-derived data. Do not redistribute. */', '#include "section_mods.h"', '']
    for i, payload in enumerate(payloads):
        lines += [f'static const unsigned char section_payload_{i}[]={{', cbytes(payload), '};']
    lines += ['static const struct PilotSectionMod section_mods[]={']
    for i, record in enumerate(records):
        lines += [f'  {{{record["index"]}u,{record["counter_offset"]}u,',
                  '   {'+','.join('0x'+record['original_descriptor'][j:j+2] for j in range(0,112,2))+'},',
                  '   {'+','.join('0x'+record['sha256'][j:j+2] for j in range(0,64,2))+'},',
                  f'   section_payload_{i},{record["bytes"]}u}},']
    lines += ['};', 'static const struct PilotSectionModPlan plan={',
              f'  PILOT_SECTION_MOD_ABI,{len(records)}u,{json.dumps(mod_id)},{json.dumps(source_sha)},section_mods',
              '};', 'const struct PilotSectionModPlan *pilot_title_section_mods(void) {return &plan;}', '']
    source = '\n'.join(lines)
    receipt = dict(schema='xboxllr.native-section-mod.v1',id=mod_id,private_guest_output=True,
                   source_xbe_sha256=source_sha,source_image_unchanged=True,
                   sections=records,registry_sha256=hashlib.sha256(source.encode()).hexdigest(),
                   validation='Generated data and layout; actual native title navigation remains unverified')
    return source, receipt


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe',type=Path,required=True)
    parser.add_argument('--section',nargs=3,action='append',required=True,metavar=('NAME','PAYLOAD','COUNTER_OFFSET'))
    parser.add_argument('--id',required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--manifest',type=Path,required=True)
    args=parser.parse_args()
    inputs=[args.xbe.resolve(),*(Path(p).resolve() for _,p,_ in args.section)]
    destinations=[args.output.resolve(),args.manifest.resolve()]
    if len(set(destinations))!=2 or any(p.exists() or p in inputs for p in destinations):
        parser.error('choose distinct new outputs separate from inputs')
    source,receipt=section_registry(args.xbe.read_bytes(),
        [(name,Path(payload).read_bytes(),int(counter,0)) for name,payload,counter in args.section],args.id)
    for path,data in ((args.output,source),(args.manifest,json.dumps(receipt,indent=2)+'\n')):
        path.parent.mkdir(parents=True,exist_ok=True)
        with path.open('x',encoding='utf-8',newline='\n') as out:out.write(data)
    print(f'Generated {len(receipt["sections"])} private section data mod(s); original XBE unchanged')


if __name__=='__main__':main()
