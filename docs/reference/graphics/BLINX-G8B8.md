# Reached G8B8 texture support

Campaign QA reaches `D3DFMT_G8B8` (`0x28`) while loading Forge of Hours,
UI Round 8 Stage 1. The previous native texture admission guard stopped there.
This is a campaign extension to the bounded attract qualification, tracked by
`beads-xbox.1.132`.

The guest texture retains its original swizzled two-byte texels, format header,
mip offsets, lock pitches and shared surface-view storage. The binding appends
the format's capability byte (`0x11`) read from the original XDK
`Get2DSurfaceDesc` table; existing binding identities and CPU bodies must match.
Only sampled, power-of-two 2D storage is admitted for this format.

The shared native adapter stores expanded BGRA texels and keeps the guest
allocation format separately from the donor storage format. A byte pair `(B,G)`
samples unsigned `(B,G,B,G)`, including alpha `G`. Both prepared D3D11 backends
use the existing ordinary color texture path. The existing upload comparison
checks the raw pair against its expanded shadow; unchanged input returns before
conversion or GPU upload. Changed input expands in the existing locked buffer,
preserving Morton texel order for the donor's ordinary unswizzle. There is no
new shader variant, frame readback or allocation in this conversion loop.

Reference facts were checked in local primary sources:

- xemu `482088490c2d5983ed5e1eafbd09906157c1eca0`:
  `hw/xbox/nv2a/nv2a_regs.h`, `pgraph/texture.c`, and the GL/Vulkan
  `pgraph/*/constants.h` `SZ_G8B8` entries specify format `0x28`, two-byte
  swizzled storage and component selection `(R,G,R,G)` from the byte pair.
- Cxbx-Reloaded `585c49a50af1255ab155099e06f24505f9c5a800`:
  `src/core/hle/D3D8/XbConvert.cpp` independently repeats the low byte into blue
  and red and the high byte into green and alpha.

Focused validation passes:

- PHOBOS GPU G8B8, existing backend and CURRENT/TEMP checks: 3/3, 2.39 seconds.
- PHOBOS guest graphics ABI/resource checks: 1/1, 1.85 seconds. These include
  original capability-backed descriptors, two-byte locks/mip offsets, alias
  ownership, changed CPU data and final guest allocation reclamation.
- SPOON G8B8 and CURRENT/TEMP checks: 2/2, 4.80 seconds.

The dedicated GPU check covers unsigned byte endpoints 0/127/128/255, actual
sampling/alpha, rectangular Morton storage, all mip levels through 2x1/1x1,
changed uploads, unchanged source buffers, rejected usage/dimensions/other
formats, and three native device lifetimes per backend. Actual Round 8 title
run `campaign-019` subsequently creates/samples the reached format and clears
all three normal stages through original Results/save and boss unlock, after
separate original callback and combiner bump fixes. This does not assert
new signed-format, linear-format or real-hardware precision qualification.
