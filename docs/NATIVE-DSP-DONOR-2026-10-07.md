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
any eventual vendoring; the core is not yet linked into the title executable.

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

## Next integration steps and retained limits

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
