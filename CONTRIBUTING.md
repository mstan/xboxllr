# Contributing to XboxLLR

XboxLLR is an early beta. Help with missing Xbox functionality, game bugs,
performance, portability and documentation is welcome.

## Report a problem

[Open an issue](https://github.com/mstan/xboxllr/issues/new) with your release or
commit, operating system and hardware, selected settings, reproduction steps,
and expected versus actual behavior. File game-specific problems in the
[Blinx](https://github.com/mstan/BlinxRecomp/issues) or
[JSRF](https://github.com/mstan/JSRFRecomp/issues) repository when available.

For a crash, attach the launcher's shareable diagnostic ZIP. See
[diagnostic instructions](docs/TESTER-DIAGNOSTICS.md). Never upload game files,
firmware, keys, EEPROMs, saves or `PRIVATE-DO-NOT-SHARE` diagnostic folders.

## Prepare a change

Start with [developer setup](docs/DEVELOPMENT.md) and the
[architecture guide](docs/ARCHITECTURE.md). The developer guide includes checks
that need no original Xbox files. Native game builds additionally need your own
supported game input and the title's pinned framework revision.

Shared CPU, graphics, audio, input, kernel and storage improvements belong here.
Title identity, revision metadata and game-specific mods belong in the title
repository. Another game may need missing functionality implemented before it
runs; please send reusable engine fixes upstream.

## Send a pull request

- Explain the problem, resulting behavior and how you checked it. Link an issue
  if one exists; an issue is not required for a small fix.
- Run the checks relevant to your change and `git diff --check`. For runtime
  changes, include meaningful gameplay evidence. Performance claims need matched
  before/after measurements; preserve other games' behavior and native saves.
- Keep supported platforms working. Guard host-specific assembly or SIMD and
  retain a portable fallback.
- Run `python tools/check_repository_hygiene.py` before committing and its
  `--history` mode before publishing source. Keep original game data, generated
  original code, private profiles and secrets out of commits.
- Preserve third-party attribution and licenses. See
  [license scope](LICENSE-SCOPE.md) and [component attribution](THIRD_PARTY_ATTRIBUTION.md).

Keep changes focused so they are practical to review and test. Describe any
remaining limitation plainly; a successful build alone does not establish that
a game works.
