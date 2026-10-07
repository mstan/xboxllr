# Third-party attribution

Third-party material keeps the terms of its authors. The owner's PolyForm grant
does not override these licenses. This inventory records the directly fetched
dependencies and the EEPROM port; transitive and incorporated-source provenance
review remains open. [LICENSE-SCOPE.md](LICENSE-SCOPE.md) explains the unresolved
GPL/PolyForm combined-executable conflict.

## Private native-title reference pilot

`tools/native_pilot.py` invokes Phobos xboxrecomp at
`63f18f0334fbd8c00313c4bb565accf00627a4fd` from an external checkout. Its
generated code copies `templates/runtime/recomp_types.h` from that GPL-3.0
fork into ignored private output. The header is modified there to use
thread-local diagnostic state; missing generated bodies become terminal
unsupported faults. The resulting executable is reference-only and excluded
from the normal product build. No donor kernel/GPU acknowledgement runtime is
linked by this pilot. See [pilot scope](pilots/native-title/README.md).

Phobos kernel bridge signatures/host lock mapping and Cxbx-Reloaded `585c49a`
Xbox type/EEPROM definitions were consulted for the launch adapter's contracts.
These references do not clear the Phobos header or the combined reference
executable for non-GPL distribution. Preserve the external donor's LICENSE,
NOTICE and per-file notices with its source; no generated game source belongs
in this repository.

The native pilot dispatcher independently implements the observed API boundary
using host waitable timers and an owned callback queue. Microsoft's NT timer,
DPC and wait documentation plus Cxbx's Xbox layouts/ordinals supplied contract
references. The MIT sp00nznet `1409a7d` timer bridge was inspected for its worker
architecture and known gaps; no donor timer service bodies were imported.
Links, exercised behavior and remaining qualification limits are recorded in
the pilot README. Its overall reference-only distribution status is unchanged.

The native VM adapter uses host NT services for page backing and independently
tracks guest reservation ownership. nxdk prototypes/flags, Cxbx address-range
constants and Microsoft NT memory documentation supplied ABI references.
sp00nznet `1409a7d` memory/file bridges were inspected for useful mechanisms and
limitations. No donor memory/file service bodies were incorporated by this
checkpoint; private CPU-header licensing remains unchanged.

The installed-media adapter uses actual host NT file services. MIT sp00nznet
`1409a7d` `kernel_file.c` and `kernel_path.c` informed device-path/handle design;
nxdk supplied Xbox structure/signature references and Microsoft documented the
host I/O contract. No donor file-service bodies or drive-authentication success
responses are incorporated. The private byte-hash-bound wrapper selects the
owner-authorized installed-media contract or the original generated routine.
Original title code stays in ignored private output; the Phobos CPU helper
continues to make that pilot a GPL-dependent reference artifact.

The native HDD/namespace checkpoint also consulted all three donor file
backends: sp00nznet `1409a7d`, Phobos `63f18f0`, and the pinned NoRain review
checkout. sp00nznet's 16 KiB logical-volume correction and actual link/path
mapping informed the design; host NT services implement create/write/flush,
metadata and volume operations. Donor fixed-capacity fallbacks and ignored
flush errors are excluded. nxdk and Microsoft supply ABI references. Existing
libfatx was checked as an external GPL reference for partition/header facts;
no libfatx or GPL filesystem implementation was imported. Local LLE disk header
inspection corroborates geometry only, not API equivalence.

`pilots/native-title/sections.inc` owns a page-residency ledger over the verified
XBE and uses host NT allocation/decommit services. nxdk declares the API;
Cxbx `585c49a` `EmuKrnlXe.cpp` was consulted for section/boundary count and shared
page lifetime behavior. No Cxbx loader body is incorporated. sp00nznet's
all-resident reference-count-only routines were inspected and not adopted.
The private Phobos CPU helper and earlier distribution limitations remain.

The utility-drive service is independently implemented with host NT file APIs
and a native ownership manifest. Its caller boundary was reviewed from the
owner's original XBE; generated original bodies remain ignored private output.
Microsoft directory/file-information documentation supplies host API contracts.
nxdk's LaunchDataPage declaration and Cxbx's null cold-launch initialization
were consulted as ABI/initial-state facts; no GPL cache/launch service body is
incorporated. The existing GPL-dependent CPU-helper restriction is unchanged.

The native read-through file cache independently allocates real buffers and
uses the existing host NT read path. nxdk supplies its page-count signatures;
Cxbx `EmuKrnlFs.cpp` supplies default/maximum count facts. Main sp00nznet and
Phobos's size-only bridges were inspected but not incorporated. Microsoft's
NtReadFile documentation supplies synchronous cursor and EOF contracts. This
adds no GPL service body and does not change the private CPU-helper restriction.

`tools/native_d3d_donor.py` prepares the Windows D3D backend from MIT sp00nznet
`1409a7d7801d3e931fb1074be6104209ddd9a33e`: seven `src/d3d/*.c` files,
their six headers, and `src/platform/xbox_winnt.h`. Original LICENSE/NOTICE and
per-file notices remain with the ignored build-tree copies. The manifest records
source/prepared hashes and excluded methods. Changes remove unqualified COM
bodies, make failures explicit and clean up partial device creation. No APU,
kernel, NV2A device model or LGPL `nv2a_regs.h` is imported. The guest adapter
uses original XBE ABI inspection and Cxbx resource-layout/constant/front-buffer
facts as external references; no Cxbx/Phobos graphics service body is imported.
The already existing Phobos CPU helper still makes the combined executable a
private GPL-dependent reference artifact, not a cleared shipping product.

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

`cmake/UnicornX87.cmake` also derives the pinned FPU helpers and declarations.
`src/cpu/x87_status.inc` adapts exception merging from the portable SoftFloat
path in xemu's `target/i386/tcg/fpu_helper.c` at commit
`482088490c2d5983ed5e1eafbd09906157c1eca0`. Copyright (c) 2003 Fabrice Bellard;
LGPL 2.1 or later, with the text in [LICENSES/LGPL-2.1.txt](LICENSES/LGPL-2.1.txt).
Modifications include per-operation status/C1 handling and staging floating
store conversion state until the actual memory operation succeeds. Arithmetic
uses the existing extended-precision SoftFloat implementation. xemu's separate
host-FPU path is not incorporated.

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

The experimental `--nv2a-renderer xemu` path now directly incorporates the
production pixel-shader generator at the same pinned xemu commit. Files and
mechanical changes are listed in
[src/third_party/xemu/README.md](src/third_party/xemu/README.md). The pixel
translator retains GPL version 2-or-3 terms and its espes, Jannik Vogel,
Matt Borgerson, Cxbx and Xeon attribution; supporting files retain their LGPL
version 2-or-later notices. The integration supplies actual draw state and
uniforms, without importing a QEMU machine shim. Existing combined-executable
distribution questions in LICENSE-SCOPE remain unresolved.

The project documents xemu-derived hardware behavior and source references in
its design and implementation comments. Those comments are not an assertion
that all referenced source was copied or that all implementation is original.
Any incorporated code must retain its upstream terms and notices. The remaining
file-level provenance review is part of `beads-xbox.1.21`.
