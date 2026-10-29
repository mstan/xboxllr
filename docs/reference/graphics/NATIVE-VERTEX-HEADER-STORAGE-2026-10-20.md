# Native vertex header storage

Tracking: `beads-xbox.1.181`.

Fresh JSRF New Game followed by original mission handovers `3003c -> 20028`
repeatedly stopped while releasing a vertex buffer. A live 101824-byte vertex
owner retained one reference, but its 12-byte guest header page had been freed
through the original user-VM service. The resource release then read unmapped
memory. There was no corresponding graphics-owner collection.

Bounded allocation traces and a private debugger watch on the original loader's
buffer field establish address reuse: the loader retained a raw user-VM pointer,
that allocation was freed, and the native graphics bridge reused its address
for a standalone 4-KiB vertex header reservation. Later original loader cleanup
released that address. Changing graphics reference counts or tolerating an
unmapped resource read would conceal the ownership mismatch.

The original NTSC-U JSRF image's classic CreateVertexBuffer boundary is at
`0x199720`, length 86, SHA256
`cd9ebd987ea5d6d7e106c313516dfbde602a47dd5722ecc598290e60221c8f6b`.
It allocates a zeroed 12-byte fixed heap object, separately from contiguous
vertex data. It does not reserve an independent user-VM page for that header.

Native vertex headers now use the maintained small-object pool already used
by native texture and index headers. All failure and final-collection paths
return the header to that allocator. Physical vertex storage, external headers,
references, binding retention and waiter ownership retain their existing
contracts. The HLE pool does not reproduce the original heap's numerical
addresses; it preserves opaque header ownership without placing driver objects
in recycled raw user-VM reservations.

Validation:

- The dedicated `pilot_graphics_vertex_header_storage` fixture passes across
  three device lifetimes. It frees and reuses actual raw user VM around a live
  vertex buffer, checks CPU/GPU bytes, retains the buffer through a binding
  after its final public release, and verifies final header/data cleanup and
  the original memory budget. It uses the common allocator so SDK profiles
  without CreateVertexBuffer2 can exercise this contract too.
- Private `mission-qa-022` starts a fresh New Game, loads `3003c`, and revisits
  `20028` without the former unmapped read. The same process then traverses
  `20032 -> 3003c -> 20028`; the last two locations respond to movement, and
  the final revisit reaches ordinary gameplay state 15.
- Deferred-state relevance pixels still pass. Broader ABI/capacity/scheduler
  fixtures report explicit skips for missing boundaries in this older SDK
  profile; those skips are not passes.

The handovers bypass mission prerequisites. These results qualify the reached
allocation/lifetime failure and repeated area loading, not campaign completion.
Original code, generated code, debugger records and captures remain private.
