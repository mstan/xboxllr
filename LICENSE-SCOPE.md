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

## Windows releases and original-title inputs

The October 22 preview includes precompiled Windows native-title applications
and separate source/tooling downloads. GPL permits binary distribution subject
to its license and corresponding-source requirements; it does not require
players to compile the application. The owner's decision removes the former
policy restriction on distributing the selected GPL renderer.

The project grant does not license the original Microsoft or Sega software.
Native title executables include mechanically translated original instructions;
we claim no copyright in those instructions. Players must supply their own
supported game or Dashboard files for data/assets. Original XBEs, disc images,
firmware dumps, HDD images, EEPROM, keys and saves are not bundled.

Source downloads include project source, modifications, build/generation tools
and the exact framework pins. A dependency-source supplement preserves prepared
renderer/runtime/launcher source and notices used by the Windows builds.
Original-title function bodies are generated from the user's supplied input;
they are not included as original-derived source downloads. See
[Development](docs/DEVELOPMENT.md) for preparation and build instructions.

This describes the distribution model, not a claim that a disclaimer grants
third-party permission or settles all questions about translated-code copyright.
The [original rights notice](RIGHTS-NOTICE.md) reserves the original owners'
rights without attempting to remove obligations for incorporated GPL code.

Artwork/screenshots identify the supported titles and retain separate rights
notices. The standalone EEPROM tool retains its LGPL terms. No legacy
Unicorn/full-system executable is included in these native-title downloads.
Read the [setup guide](docs/SETUP.md) before importing or sharing saves.
