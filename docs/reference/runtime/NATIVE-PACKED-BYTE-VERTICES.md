# Packed-byte vertex inputs

Owner: beads-xbox.1.146. The shared native shader declaration supports XDK
`PBYTE4` (`0x44`), corresponding to four NV2A `UB_OGL` components. Its four bytes
map in their stored order to four unsigned normalized values, each divided by
255. It differs from `D3DCOLOR`, whose existing input path swizzles BGRA.

The mapping follows the retained NV2A contract and agrees with the
[xemu vertex array implementation](https://github.com/xemu-project/xemu/blob/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/nv2a/pgraph/gl/vertex.c)
and the explicitly documented JSRF case in
[Cxbx-Reloaded's vertex conversion](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/D3D8/XbVertexBuffer.cpp).
The independently written mapping uses the native D3D11
`R8G8B8A8_UNORM` vertex input and the shared shader translator. It adds no
title-specific address or draw replacement.

The existing real-GPU shader test now also supplies independently specified
byte edges 0, 1, 127, 128, 254 and 255 with distinct channels. Raw shader output
checks establish size, normalization, component order and alpha, alongside the
existing typed multistream/paired-instruction and LLE output comparisons. The
three combined shader, clip-depth and window-snap tests pass in 1.07 seconds.
Actual native JSRF advances past font initialization and further real content
loading to the separate listener-orientation audio boundary. Smaller packed-byte
widths remain guarded until their native layouts are qualified.
