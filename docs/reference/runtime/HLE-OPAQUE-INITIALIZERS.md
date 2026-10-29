# HLE opaque initializer consumer policy

The stock dashboard reaches a draw at 9,362,225,883 instructions whose 09F8
receipt differs from the earlier boot draw profile. Envytools labels 09F8
UNK09F8; its Kelvin method test admits low values 0..4 and 15 while leaving
raster meaning opaque. The maintained host renderer has no 09F8 consumer;
the consulted xemu method dispatch/GL shader also has none.

References: envytools
[`nv20_3d.xml`](https://github.com/envytools/envytools/blob/f102b82381f3f11cee113d16374c87091db039d9/rnndb/graph/nv20_3d.xml)
and `MthdKelvinUnk3f0` in
[`pgraph_class_kelvin.cc`](https://github.com/envytools/envytools/blob/f102b82381f3f11cee113d16374c87091db039d9/hwtest/pgraph_class_kelvin.cc).
The inspected xemu revision is `482088490c2d5983ed5e1eafbd09906157c1eca0`.

HLE explicitly omits these unknown raster effects for absent or setter-valid
receipts. It preserves raw known/value ownership and logs the first actual
omitted receipt. It does not invent a reset/LOAD default or claim silicon
raster equivalence. Invalid setter encodings remain rejected. Strict LLE
retains its known-value 4 draw requirement.

The initializer-family audit leaves actual context-write, clipping, two-side,
single-sample storage/MSAA-mask and active texture color-key checks intact.
Edge/cache/shadow/bump/eye/opaque 1D84 receipts have no additional blanket gate;
mode consumers and their existing unsupported-feature guards still apply.

Authored queued draws through the fake renderer verify identical published
guest target bytes for all admitted encodings and absence in HLE, raw
receipt retention, invalid encoding before draw/reference side effects, and
strict negatives. Reopen the omission when a supported renderer gains a 09F8
consumer, physical evidence identifies required visual effects, or a title
exposes a discrepancy. Context-byte projection and exact raster behavior stay
deferred; this is not new hardware qualification.
