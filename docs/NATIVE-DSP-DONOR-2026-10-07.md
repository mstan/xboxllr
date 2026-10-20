# Blinx effects: validated donor route, integration pending

Owner: `beads-xbox.1.62`. Actual native Blinx now reaches
`IDirectSound_DownloadEffectsImage` after reading all 15188 bytes of
`media/dsp.bin`. It has created and initialized three real 65536-byte stereo
PCM16 buffers at 44100 Hz. No Play, audible game output or attract result yet.

## Reusable core

[mborgerson/dsp56300](https://github.com/mborgerson/dsp56300) at
`c60aeedb6a6014850204c71e53bc0cf5b3104222` is MIT-licensed and provides a Rust
Cranelift JIT with a C static-library interface. Its current README identifies
xemu as a user and describes silicon differential validation. This is a
different project from the GPL `dsp56300/dsp56300` synthesizer core. The local
reference clone is `../refs/dsp56300-mborgerson-review-20261007` from the source
repository's parent directory. Rust 1.98.1 built its locked dependencies and
`dsp56300-emu-ffi` successfully on Windows/MSVC. The disassembler also built.

No xemu implementation was incorporated into this checkpoint. Its peripheral
register facts and the [XboxDev DSP interface description](https://xboxdevwiki.net/DSP)
were behavioral references for an independent private probe. Those references
contain incomplete/uncertain DMA behavior; passing this probe does not establish
an exhaustive hardware model. Source and dependency licensing must accompany
any eventual vendoring. The guarded core is now linked into the private title
pilot; the original whole-audio selection remains available.

The existing three recomp donors do not supply a usable complete DSP engine:
their APU paths include passthrough and command-acknowledgement shortcuts.
Cxbx's inspected effects-image function also explicitly describes incomplete
handling of encrypted code. Those paths were not adopted.

## Original-image evidence

`build/blinx/audit_dsp_image.py` is the private revision-specific investigation.
It executes the owner's original XDK key-generation/decode routines using an
isolated Unicorn reference installation under `build/reference-python`.
An independently expressed decoder produced identical key tables and every
program byte for both owned images. Unicorn is a reference tool here, not a
new dependency of a shipping implementation. Decoded assets remain private.

The reached `dsp.bin` contains three effects, 1535 program words and 1712
state words. Its programs are 828, 4844 and 464 bytes; its scratch allocation
descriptor requests 413696 bytes. The additional image `dsstdfx.bin` contains
nine effects; it was decoded as a second format example, not exercised by the
title or the whole-graph execution probe.

The decoded first program loads its assigned state address, executes real
filter/delay code and falls through into the following effects. The combined
image ends with its actual return. Treating each effect as an independent
function would therefore misrepresent this graph's execution contract.

The private C probe in `build/dsp-probe/` loads the real combined program and
state into the MIT core, supplies a synthetic impulse, and calls the graph.
It uses real X/Y/program/mix storage and a bounded circular scratch-DMA model.
Transfers walk actual seven-word descriptors and copy actual sample bytes;
offset writeback and end-of-list status follow completed work. Unknown memory,
registers, descriptor forms and bounds stop. It does not clear arbitrary
doorbells or report completion without doing the transfer.

Observed in `build/blinx/dsp-audit/probe-full-graph.log`:

- First complete graph invocation returned normally with a balanced DSP stack:
  15396 DSP cycles, 49 DMA nodes, 475 changed state words, about 52.3 ms including
  initial JIT compilation.
- 1500 subsequent blocks processed 48000 samples in about **37.3 ms**. Across
  the initial and repeated calls, 73549 DMA nodes executed. Delay/filter state
  persisted; 14931 scratch bytes remained nonzero after the impulse tail.

This is an isolated effects workload, not full audio-system throughput or game
FPS. It excludes voice mixing, resampling, API synchronization and host output.
The private JSON reports retain asset hashes, per-program hashes and original
decoder comparison evidence. `probe.c` and its build definition remain an
investigation prototype, not production-quality input validation.

## Prepared source freshness (2026-10-19)

Dashboard boot047 linked an older prepared donor/library that still rejected
SEN, despite the maintained resident-stack implementation already being present
in the framework. An existing archive and the upstream revision alone did not
identify the current maintained patch set.

Preparation now records its schema and both generator hashes in `donor.json`.
Full-machine and native-title DSP configuration verify those identities and
the actual prepared source hashes, including patched files, before selecting
the external library. Source/manifest/generator changes trigger configuration
again. Old preparation fails with a request for a fresh private donor; it is
not silently relabeled or modified.

This source check does not attest how an independently supplied archive was
compiled. Keep the locked Cargo command, target ABI, manifest hash and resulting
library hash with each build's evidence. Rebuild into a separate target directory
when changing the prepared source, preserving older candidate identities.

Run the source check independently with:

```powershell
cmake -DSOURCE=<prepared-directory> -P cmake/ValidateDspSource.cmake
```

The six authored preparation checks cover current input, obsolete metadata,
changed generator, changed source, missing source and unreviewed revision.
The actual old dashboard donor rejects; the fresh eight-file SEN preparation
passes. This does not by itself establish a completed dashboard boot.

## Historical integrated checkpoint (2026-10-07)

The implementation is now `audio_dsp.c` plus the build-selected
`audio_xaudio2.cpp` transport. `native_dsp_donor.py` preserves the pinned MIT
license and records source/guard/dependency hashes. Unsupported SA/SC/DM/SEN/MS
modes, RESET, DEBUG/DEBUGcc and WAIT stop; executable tests exercise these guards.
Cargo metadata includes the MPL-2.0 build tool cbindgen and non-GPL runtime
license alternatives. This does not clear the GPL-dependent CPU pilot for release.

Image decoding, descriptor relocation, program/state/Y/scratch ownership and
real DMA are shared services. Revision bindings derive the decoder seed from
qualified original helpers; shared code contains no title key or address.
XAudio2 mixes/resamples voices into 32 bins. The processor seeds six direct
speaker outputs from their bins, runs the actual graph, then sends six-channel
output to the native stereo master. Source chunks own their bytes until real
completion callbacks. Roughly 40 ms of queued PCM plus a 32-frame (0.667 ms)
carry introduces documented transport latency. Arbitrary quantum remainders
and silent-input effect tails are retained. The real-time processor takes no
owner lock. First-use DSP JIT compilation still occurs on that thread and can
stall about 50 ms; glitch-free playback is not yet established.

Download returns an actual owned descriptor after installing this processing
path. Replacement stops the native engine at a processing boundary, swaps the
graph and releases old storage; it does not change backends or replay a floor.
Original routines establish optional descriptor output and verbatim storage
of effect locations, including Blinx's observed `00653470/00000100`; a null
location sets both to `ffffffff`. Consuming parameter services remain unqualified.
Arbitrary concurrent guest writes through effect pointers are also unqualified.
The GP output shell, host resampling/downmix and DMA timing need whole-audio
hardware comparison; this is a bounded floor, not complete audio equivalence.

Validation:

- **29 CTests pass**, including actual DSP output, malformed images, linked
  unsupported-mode guards and real native PCM/cursor/lifetime tests.
- Real Blinx program bytes match the private original-x86 decoder oracle.
  1501 blocks execute **73549 DMA nodes**, producing 90 nonzero impulse samples
  and 191988 nonzero tail samples. The warm 1500 blocks (48000 samples) took
  **39.017 ms**, excluding voice mix/output and cold compilation. Real XAudio2
  looping playback executed 315 blocks in 250 ms including cold compilation.
- Four API replacements during playback preserve the input image, accept
  nullable outputs/locations, retain stable guest allocation totals and release
  all storage. The retained DirectSound backend passes its two audio tests.
- Actual fresh/repeated NTSC Blinx now creates **32 pixel definitions** and
  stops at a **640x480 render-target texture, format `0x12`, usage `1`, type `3`**
  after 20536004/20535912 entries. No title Play, frame, attract or cadence yet.

Evidence is in `build/dsp-all-contract-tests.log`, `build/dsound-regression.log`,
`build/blinx/dsp-audit/{runtime-graph,effects-abi}.log`, and
`build/blinx/dsp-validation.aot-private.json`. The next family is the reached
render-target texture path. Broader audio routing/ADPCM/streams remain pending;
JSRF remains deferred until Blinx succeeds.

## Original integration plan and retained limits

1. Prepare a pinned donor build with license/dependency records and explicit
   rejection of unimplemented modes. Upstream `docs/LIMITATIONS.md` includes
   SA/SC/DM arithmetic modes, stack extension, RESET/DEBUG behavior and timing
   gaps. Warnings must not silently qualify reached unsupported work.
2. Promote validated image decoding, descriptor ownership and the bounded DMA
   contract into shared, independently authored services. Keep XBE helper
   qualification and image-specific facts in generated bindings. Preserve the
   original reference selection.
3. Connect installed effect state and processing to real voice/mix-bin/output
   work before allowing production effects success. Establish descriptor
   lifetime, data updates, routing and completion contracts; do not substitute
   a successful image upload for a functioning effects path.
4. Rerun actual Blinx after the coherent family, then expand only reached
   formats/routing/stream work. JSRF remains deferred.

The private DMA prototype covers the reached 24-bit circular scratch transfers.
It keeps the engine armed until an explicit stop and signals EOL after copies;
this is a bounded scheduling choice, not verified silicon timing. FIFO output,
other transfer formats/strides, broader interrupts and mode changes are not
qualified. A graph returning in the probe proves useful execution, not audio
equivalence with hardware. Further integration must retain these visible gaps.


## Full-size native effects graphs (2026-10-20)

Tracking: beads-xbox.1.142. JSRF's reached standard image contains 20 effects,
3512 program words and3358 initial X words. Its original SDK4134 seed/decode
routines match every independently decoded key and program byte. Images and
observations remain private. This is not the earlier Blinx nine-effect example.

The native adapter previously reserved the first1024 P words and incorrectly
used X:c00 as an allocation limit. Large graphs now use P:0, with a native
JSR/STOP harness above the graph and one in-bank decoder lookahead word. Existing
small graphs keep P:400. Relocated guest descriptors identify the selected real
program storage. Both hardware banks remain4096 words; no unmapped memory guard
is widened. Initial X data can include temporary/output buffers above X:c00.

An authored4093-word program reaches the maintained P limit, performs a real
write, returns through the harness and preserves the last X word and storage
canaries. The next code size is rejected. The real JSRF graph passes1501 blocks,
141094 DMA nodes,112 impulse samples and213240 tail samples;1500 warm blocks took
139.994ms in this host run. The same test on Blinx passes1501 blocks/73549 DMA
nodes, with its original program placement unchanged. Both graphs match original
decoder bytes in actual DSP memory. The two shared DSP contract/guard tests pass
in2.55seconds. These are isolated graph observations, not game FPS or hardware
sample/timing equivalence.

Actual native JSRF installs this graph and creates four PCM16 stereo44100Hz
buffers, then reaches SetGammaRamp after4784 entries. No complete title frame or
attract acceptance is established. The larger graph remains real DSP execution;
effects were not disabled or replaced by an upload-success stub.
