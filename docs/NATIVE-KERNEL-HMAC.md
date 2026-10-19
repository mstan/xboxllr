# Native XcHMAC service

`pilots/native-title/kernel_hmac.h` implements the data transform for kernel
ordinal 340. The companion `.inc` supplies its native guest ABI adapter.
Existing `save_crypto.inc` remains the owner of XcSHAInit/Update/Final; this
service reuses that portable SHA implementation and does not change the XIP
length-prefixed block-digest checks.

The nxdk primary header declares seven NTAPI arguments: key and length, two
input spans and lengths, and a 20-byte output. Independent inspection of the
owner's captured kernel default implementation at `80033e9c` establishes:

- Consume at most the first 64 key bytes, zero-pad, then XOR ipad/opad.
- Hash ipad followed by the first and second input spans.
- Hash opad followed by the 20-byte inner digest.
- Return with 28 argument bytes consumed. No guest return value is specified.

The long-key rule deliberately differs from standard RFC 2104 preprocessing:
the original clamps long keys instead of hashing them. No console key bytes
are embedded, recorded, or printed. All inputs are consumed before the digest
copy, allowing output overlap. Local key-derived temporaries are erased through
volatile byte writes. Invalid guest spans fail before any output or guest stack
change; page ownership remains enforced by the native runtime's memory owner.

An additional offline execution check used the separately qualified flat kernel
(`827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c`),
whose export resolves to `800341b0`. An authored 80-byte key and two public input
spans produce the same clamped-key digest and consume the expected stack bytes.
This check uses no console key material.

The authored standalone test uses RFC 2202 public vectors, every split of one
two-span message, an independent Python-HMAC oracle for the clamped-key case,
overlap and empty-input cases, and guest ABI/output-boundary assertions. It
checks that unknown ordinals decline, invalid spans stop without publication,
and the VOID service preserves guest integer values and flags.

This does not implement RC4, DES, RSA, custom crypto-vector replacement, warm
console key state, or unspecified invalid-pointer behavior. Those imports and
policies retain their existing guards. The dashboard's actual invocation and
all other missing services require native boot evidence separately.
