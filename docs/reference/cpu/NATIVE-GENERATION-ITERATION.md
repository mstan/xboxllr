# Preserving unchanged native compilation work

Tracking: beads-xbox.1.154. Regeneration used to rewrite timestamps on every
generated CPU unit, even when its final bytes matched the previous generation.
A two-instruction correction therefore recompiled all 153 JSRF units.

Before regeneration, the driver snapshots the actual hashes and file times of
existing generated C/header files. After instruction hardening, partitioning and
all selected bindings finish, it restores prior times only for final files with
identical bytes. Changed and new files keep their current times; deleted files
stay deleted. Other metadata and external files are not touched. The generation
report records the number of unchanged compiler inputs retained.

Image, body, admission and build receipts are still checked. Changed headers or
compiler arguments still invalidate the normal build dependencies. The result
preserves compilation work for identical inputs; it does not preserve an old
binary across changed source or flags.

The filesystem fixture verifies unchanged source/header retention, changed
header contents/times, new files, deletion, unrelated metadata and an initially
absent output directory.

The actual JSRF correction retained 164 identical compiler-input timestamps and
compiled only the two changed units in 3.17 seconds. A subsequent unchanged
regeneration retained all inputs, rebuilt in 3.93 seconds without compiling a
CPU unit, and preserved the executable SHA256 exactly. Regeneration itself took
48.46 and 61.42 seconds under the observed machine load. These are local
iteration measurements, not a performance guarantee. Private logs retain the
ordinary preparation/configuration/build receipts and both executable hashes.
