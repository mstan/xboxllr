# Virtual HDD security

The default is a **locked, independently provisioned virtual drive**. The owner
also authorizes an explicit unlocked-drive configuration. This is a narrow HDD
policy choice; other missing device behavior still stops execution.

A softmod starts through stock firmware before its exploit runs and still needs
the HDD locked at startup. Rocky5's [Chimp source](https://github.com/Rocky5/Chimp261812/blob/master/Source%20Files/Edited%20bin/usr/bin/chimp)
therefore prevents unlocking the master drive in softmod mode. Modified firmware
can handle an unlocked disk; for example [Cromwell](https://github.com/XboxDev/cromwell/blob/master/drivers/ide/BootIde.c)
only performs authentication when IDENTIFY reports a locked drive. Being unlocked
is a real drive state. Accepting an arbitrary password on a locked drive is a
different behavior and is not implemented here.

## Locked startup

Provision once, after choosing the EEPROM used by the virtual console:

```powershell
py -3 tools/provision_hdd_security.py --eeprom ..\machine\eeprom.bin --output ..\machine\hdd-security.key
```

Keep the file and pass `--hdd-security-file ..\machine\hdd-security.key` alongside
the existing `--hdd` and `--eeprom` arguments. `--hdd-security locked` is optional
because it is the default. Missing credentials stop startup with instructions.
The replay harness accepts the flag through
`-ExtraXlleArgs @('--hdd-security-file','..\machine\hdd-security.key')`.

The tool authenticates the EEPROM before decrypting its HDD key. It derives the
user password using standard HMAC-SHA1 over the virtual drive's model and serial,
in that order, and zero-pads the result to 32 bytes. This follows Cromwell's
documented protocol; no Cromwell implementation code was imported. It uses the
existing separately attributed LGPL EEPROM helper.

The identity is `ST310211A` / `XLLE000000000000000`, with ATA string byte order
and trailing spaces handled by IDENTIFY. It identifies the virtual device, not
the owner's physical HDD. The raw image contains readable sectors; it does not
carry the physical drive's password storage. A random master credential is
provisioned separately for this virtual drive. The private file stores exactly
64 bytes: user password then master password. Both are checked byte for byte.

The tool creates `.key` files exclusively, never overwrites them, never prints
credential values, and never modifies the EEPROM or HDD image. If the virtual
console's EEPROM changes, explicitly provision a **new** private file and select
it at launch. This does not recover or unlock any physical drive.

## Explicit unlocked startup

Pass `--hdd-security unlocked` with `--hdd`. Do not supply a security file in this
mode. The device starts with security disabled and unlocked; IDENTIFY word 128
is `0x0001`. Reads and writes are permitted. Stock-firmware boot is not promised
by this setting: it does not change the guest's boot checks or error handling.

The locked mode reports `0x0007` at power-on and `0x0003` after successful
authentication. Unlocking does not disable security: a hardware reset relocks it.
Every run logs the selected policy and final security bits, without credentials.
Copy-on-write remains the default in either mode.

## Implemented contract and limits

The model follows [ATA-5, T13/1321D revision 3](https://www.seagate.com/support/disc/manuals/ata/d1153r17.pdf),
section 6.11, table 9, and sections 8.32/8.34:

- SECURITY UNLOCK consumes the complete 512-byte parameter sector before
  comparing the selected 32-byte user/master credential. Wrong passwords abort.
- Five mismatches while locked exhaust the counter. Further unlock/erase-unit
  commands abort until hardware reset. Failures while unlocked do not decrement
  it. Master unlock works at high security and is rejected at maximum security.
  The CLI provisions high security; maximum is covered by controller tests.
- Locked reads, writes, DMA, verify and flush commands abort before media or
  DMA-memory access. IDENTIFY remains readable.
- FREEZE LOCK aborts when locked and freezes security changes when unlocked,
  including disabled-security mode. Repeated freeze retains the frozen state.
- Software reset preserves security state and cancels partial transfers.
  The controller's hardware-reset entry point relocks enabled security, clears
  freeze and restores the retry counter. Fresh attachment applies power-on state.
- IDENTIFY words 82, 85 and 128 report supported/enabled/current security state.
  Hardware command rejection completes with ABRT and an interrupt subject to
  nIEN. Missing modeled behavior remains an explicit emulator fault.

This is the boot authentication subset, **not the entire ATA security feature
set**. Guest SET PASSWORD, DISABLE PASSWORD and ERASE operations remain
unimplemented when the current security state would allow them. UNLOCK without
provisioned credentials, including in disabled mode, also explicitly stops.
Forbidden operations in locked/frozen/expired states receive the specified
hardware rejection. No unsupported command returns fabricated success.

Hardware-reset semantics are tested at the controller interface; a complete
motherboard reset/power lifecycle is not qualified. Physical drive timing,
manufacturer-specific defaults and live-console differential capture remain
outside this checkpoint.

## Validation, 2026-10-02

Behavioral tests cover both credential selectors, a mismatch in each of the
32 password bytes, retry exhaustion, frozen states, partial parameter sectors,
software/hardware reset, ATAPI rejection, nIEN, all implemented locked media
command families, and copy-on-write. Six provisioning/CLI tests use synthetic
EEPROMs and an independent .NET HMAC reference vector. Full Release CTest:
**61/61 pass**.

The owner's existing local EEPROM was authenticated and a private credential
file provisioned before execution. A retail-kernel run reached one billion
instructions without fault, performed 5,665 sector reads and one copy-on-write
sector write, and ended with word 128 `0x0003`. The old missing-security stop at
17,940,376 instructions is cleared. The unlocked configuration also completed a
bounded 30-million-instruction run with word 128 `0x0001`; this alone does not
establish its eventual guest boot outcome. Logs and credentials remain under
`build/aot-20261002/`. Neither run is a Blinx boot attempt or a speed measurement.

A subsequent 12-billion-instruction run with `clock-accept.txt` reached the
dashboard menu, verified visually. Its final PPM SHA-256 is
`36e87a97a1883e44115db2be6d03996baccd6eae2b8499cb1d52446d04b09dfe`, exactly
matching the existing replay reference. It completed without fault, read 42,360
sectors, made two copy-on-write sector updates, and ended with security `0x0003`.
The command uses the same inputs/options as the README, plus
`--test-input-script tests/scripts/clock-accept.txt`,
`--test-input-capture-prefix build/aot-20261002/security-dashboard`,
`--max-insns 12000000000` and
`--frame-dump build/aot-20261002/security-dashboard.ppm`, without interactive
presentation. The credential file for this validation is
`build/aot-20261002/hdd-security.key`. The private log is `security-dashboard.log`
in the same directory. This restores the demonstrated dashboard route with
password checking; it does not qualify every ATA command or title launch.
