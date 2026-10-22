# Native signing-input preparation

Tracking: `beads-xbox.1.187.5`. Signing inputs retain the `XLLEKEY1` format and
the native runtime's original title/console identity contract.

Normal preparation uses the standard-library HMAC derivation already checked
against the original kernel. It does not execute Xbox code or import Unicorn:

```powershell
python tools/native_save_keys.py --xbe local/owned/default.xbe --kernel local/owned/kernel.bin --eeprom local/owned/eeprom.bin --output build/save-keys.aot-private.bin
```

The tool still needs the reviewed owned kernel as the source of private
certificate material. Its SHA256 must be
`827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c`.
No kernel body, certificate root or console disk key is supplied by the tool.
The matching owned XBE and an authenticated 256-byte EEPROM are also required.
The EEPROM can be an existing profile or a fresh locally generated identity;
never replace an existing profile to prepare another build or title.

The native runtime consumes the same 360 bytes: format magic, XBE/EEPROM
identities, console disk material and 17 per-title results. An existing output
or receipt is refused. Invalid kernel identity, incomplete certificate tables,
unauthenticated EEPROM and a missing/zero root are rejected before writing.
Receipts contain identities and counts; key values are not logged.

For offline qualification, append `--verify-original`. That explicitly invokes
the bounded original-kernel oracle and requires the separately installed
Unicorn reference dependency. It compares all 17 results before producing any
output. This optional research path is separate from normal setup and native
gameplay. Native title execution does not gain an interpreter dependency.

Qualification on October 21, 2026:

- The normal producer matches the existing original-derived 360-byte artifacts
  for Blinx, Dashboard and JSRF, byte for byte. Source input files are unchanged.
- Optional original execution separately matches 17 results for each title,
  for 51 comparisons. No key values or owned executable/kernel bodies enter Git.
- Eight ROM-free checks cover stdlib-only preparation, wire identity, optional
  oracle dependency, fingerprint/certificate/EEPROM rejection and preservation
  of existing keyfiles and receipts.

This removes the normal setup interpreter dependency. It does not yet provide
game-only first-run setup: an owned kernel or an already prepared console/title
profile remains necessary. Shared profile/bootstrap and archive staging remain
separate release work. Existing native saves and console identities are retained.
