# xemu pure NV2A pixel-shader generator

Source: https://github.com/xemu-project/xemu
Revision: `482088490c2d5983ed5e1eafbd09906157c1eca0`.

`psh.c`, `psh.h`, `psh_regs.h`, `common.c`, and `common.h` derive from
`hw/xbox/nv2a/pgraph/{glsl/,}*` at that revision. Upstream copyright and license
headers are retained. The pixel translator is GPL version 2 or 3, including
the Cxbx/Xeon attribution in its header. Supporting GLSL files are LGPL version
2 or later. See the repository's `LICENSES/GPL-2.0.txt` and
`LICENSES/LGPL-2.0.txt`; these files are not relicensed as PolyForm.

Refresh with `py -3 tools/import_xemu_psh.py <pinned-xemu-checkout>` from the
repository root. The script reads Git objects at the required commit, extracts
the pure generator and records all mechanical modifications:

- Remove machine register and uniform collectors; the real owned-packet adapter
  is `src/host/nv2a_xemu_psh.cpp` plus the OpenGL uniform uploader.
- Retain the actual combiner, texture and final shader generation algorithms.
- Replace three temporary GLib strings with bounded stack formatting. The
  separate `mstring.h` supplies fully implemented portable string operations.
- Remove fake hemisphere-map bodies and fabricated output for BRDF,
  DOT_RFLCT_SPEC_CONST and incomplete DOT_ZW. The generator rejects those modes
  before allocation. Unreachable unsupported branches abort rather than report
  success or generate placeholder values.
- Restrict this entry to NONE/PROJECT2D texture modes, D24, no W buffer,
  convolution, signed textures or shadow maps. The host adapter additionally
  checks registers, resource formats, finite geometry, and all supplied inputs.
  The retained Gaussian helper explicitly rejects quincunx; the incomplete
  floating-point-depth output is replaced with an explicit unsupported error.
- Release the upstream temporary `clip` string after source construction.

No QEMU machine, device, IRQ, memory-map, or DMA shim is incorporated. Other
upstream algorithms retained for source provenance are not enabled by these
admission rules. Increasing coverage requires a real packet contract and tests.

The adapter pairs the donor fragment shader with a host vertex shader over our
existing postclip triangle records. Thus this is borrowed shader execution,
not a transplant of xemu's complete GPU or proof of hardware pixel accuracy.


## Separate HLE renderer imports

`vsh-prog.c`, `vsh-prog.h`, `vsh_regs.h`, `swizzle.c` and `swizzle.h` come
from the same pinned revision. `tools/import_xemu_hle.py` removes the QEMU
header dependencies, retains the actual programmable vertex emitter and texture
layout algorithms, and replaces the unreachable unfinished persistent-constant
write branch with an explicit error. The adapter rejects those programs before
generation. The vertex sources carry GPL-2.0-or-3.0 notices and the swizzle
sources carry LGPL-2.0-or-later notices.

`hle_psh.c` derives from the placeholder-free imported `psh.c` through that
script, with a separate entry and coverage policy. It uses the complete donor
PROJECT2D/PROJECT3D/CUBEMAP/PASSTHRU/CLIPPLANE implementations and integer depth
texture comparisons. The HLE renderer replaces the donor software-style
fragment depth reconstruction with native interpolation and derivatives. Its
pipeline and admission rules are independent of the LLE host adapter.
