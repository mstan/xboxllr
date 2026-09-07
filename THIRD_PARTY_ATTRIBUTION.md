# Third-party attribution

Third-party material keeps the terms of its authors. The owner's PolyForm grant
does not override these licenses. This inventory records the directly fetched
dependencies and the EEPROM port; transitive and incorporated-source provenance
review remains open. [LICENSE-SCOPE.md](LICENSE-SCOPE.md) explains the unresolved
GPL/PolyForm combined-executable conflict.

## Unicorn Engine / QEMU

[Unicorn Engine](https://github.com/unicorn-engine/unicorn), version `2.1.1`,
is fetched by CMake and statically linked as the CPU floor. Preserve its authors'
notices, `COPYING`, component licenses, and corresponding source when applicable.
A copy of its top-level GPL v2 text is in [LICENSES/GPL-2.0.txt](LICENSES/GPL-2.0.txt).
The QEMU and bundled-library tree includes additional notices and licenses;
this top-level copy is not a substitute for those.

`cmake/UnicornPreciseFault.cmake` derives hash-checked build-tree copies of the
pinned QEMU TLB, translation and MSR helper sources for precise memory-fault
restoration, instruction-clock/TSC repair, and code coherence through RAM aliases. The generated
copies retain their upstream license headers. The bridge in
`src/cpu/unicorn_aot_bridge.c` uses this pinned target's private structures;
upgrading Unicorn requires requalifying these integration changes.

## nv2a_vsh_cpu

[nv2a_vsh_cpu](https://github.com/xemu-project/nv2a_vsh_cpu), commit
`863fc0f50aa4027af113718d2a63e45ac3be7a42`, is fetched by CMake as the NV2A VSH
interpreter. Its upstream license is The Unlicense, copied verbatim into
[LICENSES/nv2a_vsh_cpu-Unlicense.txt](LICENSES/nv2a_vsh_cpu-Unlicense.txt).

## SDL

[SDL](https://github.com/libsdl-org/SDL), tag `release-3.2.0`, is fetched by
CMake for the host window, input and presentation layers.
Copyright (C) 1997-2025 Sam Lantinga and contributors; the pinned top-level
zlib-style notice is in [LICENSES/SDL-zlib.txt](LICENSES/SDL-zlib.txt).
Retain the applicable bundled-component notices when packaging SDL.

## xemu EEPROM generator

[tools/create_eeprom.py](tools/create_eeprom.py) is a modified Python port of
xemu's EEPROM-generation logic, with SHA-1/RC4 support following the referenced
implementation. Source reference: [xemu commit 482088490c2d5983ed5e1eafbd09906157c1eca0](https://github.com/xemu-project/xemu/tree/482088490c2d5983ed5e1eafbd09906157c1eca0),
`hw/xbox/eeprom_generation.c`, `hw/xbox/eeprom_generation.h`,
`util/sha1.c`, and `util/rc4.c`.

Copyright (c) 2020 Mike Davis. Copyright (c) 2024 Ryan Wendland.
The referenced files grant GNU Library/Lesser GPL version 2 or, at the
recipient's option, any later version. The port and its modifications retain
those terms; [LICENSES/LGPL-2.0.txt](LICENSES/LGPL-2.0.txt) supplies the version 2
text. This is a standalone source tool, not code relabeled as PolyForm.
Changes include Python standard-library operation, local file creation/reuse
safeguards, integrity validation and a locally administered virtual MAC address.

## Other xemu references

The project documents xemu-derived hardware behavior and source references in
its design and implementation comments. Those comments are not an assertion
that all referenced source was copied or that all implementation is original.
Any incorporated code must retain its upstream terms and notices. The remaining
file-level provenance review is part of `beads-xbox.1.21`.
