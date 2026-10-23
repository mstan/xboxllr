# xbox-boot-re portable core

Authored portable C code imported from
[`bennyborn/xbox-boot-re`](https://github.com/bennyborn/xbox-boot-re), commit
`12a3396aa77f72c8d2fd94c0988c7e11539b47e7`, under the adjacent MIT license.
Only `core/src`, `core/include/xboot.h` and `LICENSE` are imported. No files
from the donor's Microsoft-owned `data/` directory are included.

This is a reconstruction of the startup animation, not firmware execution.
The donor qualifies its choreography and generated command stream against
its modified 4627 BIOS. Its legacy voice synthesis is an approximation. xboxllr's host
and owned 4983 data admission have separate qualification recorded in
`docs/NATIVE-BOOT-INTRO.md`; donor claims do not qualify our presentation.

The host ports the original-look shaders and render order described in the
same donor's `hosts/web/src/renderer.js`, `shaders.js` and `textures.js`.
Those authored files are also MIT; their attribution is retained in the host.

The original donor remains pinned above. xboxllr adds an authored portable
external-voice interface in `xboot.h`, `core.c`, `core_internal.h` and `Audio.c`
(API version 3). It emits ordered original-derived sequencer/library events,
post-call registers and source identity without running the donor's approximate
voice renderer. Actual hardware-active state and the library's explicit idle
acknowledgement remain separate. A hardware end alone never permits a guessed
restart at the next beat. Native/legacy ownership cannot be mixed until sound
reinitialization. Original sequencing and the native fixed-point GP arithmetic
are retained. The host defaults to the separate shared native VP adapter and
source-derived actual idle/notifier owner, without a runtime legacy fallback.
An explicit developer-only CMake option retains the old renderer for comparison.
The native signal model has documented software-reference limits; human sound
acceptance and independent silicon qualification are separate claims.
`boot_audio_sequence_test.cpp` uses authored tracks and no original asset bytes
to check ordering, per-channel idle/retrigger ownership and invalid feedback.

All mesh, sample, sequence, DSP-program and table bytes must be extracted
locally from an admitted complete kernel supplied by the user. The public
extractor stores only bounds, relocation rules and integrity fingerprints.
