# dr_wav

Unmodified `dr_wav.h` and `LICENSE` from David Reid / mackron's
[dr_libs](https://github.com/mackron/dr_libs/tree/dfe8377631000664666519fdb83da193fd8037f4),
revision `dfe8377631000664666519fdb83da193fd8037f4` (header v0.14.6).
We use the offered **MIT No Attribution (MIT-0)** option and retain credit and
the complete license anyway.

SHA-256 of upstream bytes:

- `dr_wav.h`: `03e70c1a2d9787cd7ed3e966c075bea7bac6373f759db9cd7ca9ccdfc4ec4493`
- `LICENSE`: `dd1c647e6f767f8ff4b2dfae0fed314726600a01e0cf1ef556afddd5fa96ff15`

`src/machine/apu_adpcm.c` is our separate adapter, using the donor's public
memory-read API. It presents each 36-byte/channel Xbox block as standard IMA,
normalizes the signed header index under the pinned emulator-reference policy,
and takes 64 frames. The donor library is unchanged. No GPL decoder body is
part of this adapter. Existing private test-only donor references and broader
project distribution restrictions remain separately documented.
