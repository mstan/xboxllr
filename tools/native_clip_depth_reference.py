#!/usr/bin/env python3
"""Original draw / independent affine depth across the right clip plane.

The authored triangle has varying W and a screen-linear depth field independent
of Y. Its clipped right edge must retain that depth field. A homogeneous edge
parameter cannot directly interpolate already divided window depth.
"""
import argparse
import json
import os
from pathlib import Path
import struct

from native_vertex_fetch_reference import Reference,OBJECT,STREAMS,FRAME,XBE_SHA


def run(args):
    ref=Reference(args);failures=[];compared=0
    try:
        ref.resource(OBJECT+0x1000,STREAMS);ref.set_stream(0,OBJECT+0x1000,20)
        ref.bind(1,0x40,0,16)
        for state,value in ((57,0x201),(59,0),(64,1),(67,0x1010101),(147,0)):
            ref.call('D3DDevice_SetRenderStateNotInline',[state,value])
        # Shader MOV exports window-space XYZ/W and the packed diffuse color.
        vertices=((8,8,100,1),(88,8,740,4),(8,56,100,2))
        ref.cpu.mem_write(STREAMS,b''.join(struct.pack('<4fI',*v,0xffffffff) for v in vertices))
        ref.cpu.mem_write(FRAME+0x4000,struct.pack('<4096I',*([0xffffff5a]*4096)))
        ref.draw(0,5,3);ref.check()
        color=struct.unpack('<4096I',ref.cpu.mem_read(FRAME,16384))
        depth=struct.unpack('<4096I',ref.cpu.mem_read(FRAME+0x4000,16384))
        # In the interior, clipping cannot change z=100+8*(x-8).
        # Each pixel center yields an integer ideal Z. One integer step covers
        # binary32 interpolation/floor error; the incorrect clipped Z differs
        # by hundreds of steps. Keep well away from snapped/clipped edges.
        for y in range(12,28):
            for x in range(12,60):
                if (x+.5-8)/80+(y+.5-8)/48>=.9:continue
                index=y*64+x;want=100+8*(x+.5-8);got=depth[index]>>8
                if abs(got-want)>1 or depth[index]&255!=0x5a or color[index]!=0xffffffff:
                    failures.append([x,y,got,want,hex(color[index]),depth[index]&255])
                compared+=1
        return dict(xbe_sha256=XBE_SHA,vertices=vertices,compared_interior_pixels=compared,
            depth_bound_steps=1,failure_count=len(failures),first_failures=failures[:12],
            homogeneous_clip_depth_qualified=not failures,
            expectation='Independent window Z plane and exact RGB/stencil; no production clipper/after-image expectation')
    finally:ref.close()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','binding','library','dependencies','output'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();handles=[os.add_dll_directory(str(args.dependencies.resolve()))]
    result=run(args);args.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
    raise SystemExit(0 if result['homogeneous_clip_depth_qualified'] else 1)
