# License scope and distribution

Owner decision, October 22, 2026: license and release this project under the
GNU GPL. Owner-original contributions are **GPL-3.0-or-later**, with the full
GPLv3 text in [LICENSE](LICENSE) and the copyright/grant in [NOTICE](NOTICE).
This supersedes the former PolyForm Noncommercial and no-GPL shipping policy
for the current source. Earlier grants are not revoked. This change concerns
xboxllr, BlinxRecomp and JSRFRecomp; it does not relicense unrelated projects.

## Third-party components

Third-party material retains its own terms and notices. Our grant covers only
rights held by the owner, not every file's authorship or original game content.
The Phobos renderer remains under its GPLv3 terms, with the MIT upstream notice
and component notices preserved. MIT, BSD, LGPL and other components retain
their respective grants. See [the exact inventory](THIRD_PARTY_ATTRIBUTION.md).
New native CPU preparations use the reviewed MIT helper and an authored bridge;
older private preparations retain their original fork notices.

The former policy-driven requirement to replace the GPL renderer is removed.
GPL distribution still requires the applicable notices and corresponding source
for the actual build, including incorporated modified dependencies and build
scripts. A root license change alone is not a complete source-package audit.
Native title gameplay does not link Unicorn; the separate legacy full-system
route does. Any future legacy binary needs its own component/version review.

## Current release boundary

The October 22 release is a **source/tooling preview**, not a downloadable
precompiled game. Title-native executables currently statically include code
translated from the owner's original XBE. That input's rights are not changed
by our GPL grant, and the repository policy keeps original/generated title code
private. Such executables are not included in these releases. Supplying only
our wrappers would not provide all the source for those combined executables.

Release source archives contain the framework, title metadata/mods, build tools
and notices. They exclude original games/firmware/Dashboard files, generated
title bodies, kernel/EEPROM/signing material, disk images, native saves and
profiles. A recursive title source archive includes its exact framework pin.
Build locally using your own admitted title input and the documented toolchain.
The source-generation/build process is documented in [Development](docs/DEVELOPMENT.md).

Standalone ROM-free binaries may be released separately after their exact
corresponding-source and dependency package is verified. No original-game rights
or blanket clearance for an arbitrary combined binary are implied here.

Artwork/screenshots identify the supported titles and have separate rights
notices. They are not covered by a blanket GPL software grant. The standalone
EEPROM source tool keeps its LGPL notice; generated console identities and keys
remain private data. Read the [setup guide](docs/SETUP.md) before sharing saves.

References: [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html),
[GNU on generated output](https://www.gnu.org/licenses/gpl-faq.en.html#GPLOutput),
[GNU on corresponding source](https://www.gnu.org/licenses/gpl-faq.en.html#SourceAndBinaryOnDifferentSites).
