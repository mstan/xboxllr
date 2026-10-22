# xbox-boot-re portable core

Authored portable C code imported unchanged from
[`bennyborn/xbox-boot-re`](https://github.com/bennyborn/xbox-boot-re), commit
`12a3396aa77f72c8d2fd94c0988c7e11539b47e7`, under the adjacent MIT license.
Only `core/src`, `core/include/xboot.h` and `LICENSE` are imported. No files
from the donor's Microsoft-owned `data/` directory are included.

This is a reconstruction of the startup animation, not firmware execution.
The donor qualifies its choreography and generated command stream against
its modified 4627 BIOS. Voice synthesis is an approximation. xboxllr's host
and owned 4983 data admission have separate qualification recorded in
`docs/NATIVE-BOOT-INTRO.md`; donor claims do not qualify our presentation.

The host ports the original-look shaders and render order described in the
same donor's `hosts/web/src/renderer.js`, `shaders.js` and `textures.js`.
Those authored files are also MIT; their attribution is retained in the host.

All mesh, sample, sequence, DSP-program and table bytes must be extracted
locally from an admitted complete kernel supplied by the user. The public
extractor stores only bounds, relocation rules and integrity fingerprints.
