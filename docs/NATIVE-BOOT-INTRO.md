# Native Xbox startup intro

The standalone Windows prototype renders the original-look eight-second
Xbox startup animation natively, including the blob, chamber, logo and
synthesized sound. It is **an animation reconstruction, not firmware boot**.
It does not execute MCPX, 2BL or an original kernel, initialize a console,
or transfer firmware-created machine state into a native game. It has no
access to an HDD, EEPROM, native game saves or title memory.

The shared Windows launcher can run this separate presentation stage before
creating the native title process, retaining its final original after-image
until that owned title acknowledges its first successful presentation.
Dashboard defaults on; games default off.
Settings has an independent startup toggle and optional complete-kernel
choice, which stay editable after the save-console profile is locked. The
first-launch game setup does not gain another requirement. Published packages
need rebuilding to include this fast follow; no installed owner copy is
modified by the integration work.

Missing or unsupported startup data gives a concise Settings status and the
title launches directly. Completion or an explicit skip proceeds normally;
an intro failure or 25-second animation/setup timeout is logged and falls through
to the title after helper cleanup. Final-image handoff has a separate bounded
55-second title-present wait and a 60-second helper owner/exit budget. There is no fake black delay or downloaded
original asset. No proprietary boot assets are distributed.

## Source and asset admission

The portable core is the MIT authored reconstruction from
[`bennyborn/xbox-boot-re`](https://github.com/bennyborn/xbox-boot-re/tree/12a3396aa77f72c8d2fd94c0988c7e11539b47e7).
Its license and unmodified core are retained under
`src/third_party/xbox-boot-re/`. The D3D11 host adapts the same project's
original-look WebGPU shaders and ordered render contract. It preserves
the two light-depth views, veil ping-pong, logo render-to-texture, 1280x480
scene and the two-to-one horizontal final resolve. The final 640x480
picture is scaled uniformly and letterboxed when the window is wider.
There is no video playback, browser, per-game patch or interpreter involved.

The extractor currently admits one complete 4983 kernel: 710,720 bytes,
SHA-256 `be30b43eecc31d46459bb2b0fb57d117dbc7bd8b56a23b369c6eaed48c16d86d`.
This exact input is a **modified firmware profile with an embedded Xecuter2
after-image**, not a stock retail 4983 image. It requires a user-owned, already
unpacked complete kernel image. A short
kernel dump sufficient for save signing lacks the startup INIT data and
is rejected. Other firmware versions and direct flash/MCPX extraction are
not implemented. A complete kernel may be unpacked locally with an
appropriate open-source BIOS tool; do not distribute that input.

The required v2 private pack is 73,120 bytes, with 14 ranges totaling 72,559 bytes:

| Data | Bytes |
| --- | ---: |
| DSP program | 6,292 |
| Arc-sine factors | 196 |
| Three PCM sample ranges | 32,311 |
| Audio sequence | 1,784 |
| Camera path | 588 |
| Logo meshes | 13,256 |
| Chamber scene | 16,692 |
| Envelopes | 560 |
| Descriptor/track pointers | 100 |
| Embedded firmware after-image RLE | 628 |
| Original final quad and gray conversion constants | 152 |

Nine ranges are byte-identical to the donor's published 4627 data
fingerprints at relocated addresses. The descriptor/track table matches
after explicit relocation. The sequence's consumed descriptors, events
and track bytecode also match after data-pointer relocation. The unused
function-dispatch table at sequence offsets 4..0x20f is zeroed; the portable
core never executes or reads it. This omits irrelevant original code
addresses rather than claiming that 4983 code matches 4627 code.

Each range has an integrity fingerprint. The host admits the exact complete
pack SHA-256 `e97b240aa8f0d20ce70d8c9ee2e4f3a150ca75808ba01c412c487335a7807dca`
before calling the core. It rejects altered, truncated and unknown packs;
the reconstruction's internal parser is not treated as a general untrusted
mesh/sequence loader. User input files are only read. Extraction refuses
an existing output directory and never overwrites a console profile.

The previous 72,224-byte pack omitted the final after-image and is rejected by
the current helper. Its private cache is preserved; Settings explicitly asks
the user to re-select the same compatible complete kernel to regenerate data.
There is no save-state or original-input migration.

The packaged launcher explicitly supplies the **selected original title XBE's
header logo** as a separate after-image source. The admitted 690-byte stock
Microsoft RLE has SHA-256
`7b6b049abaf7ebef37be861d1f5a868984e076c7d39347ac81e812d087ab7660`.
Existing owned Dashboard and Blinx XBE header extents contain this exact data.
The reader bounds the header's address/length and reads only that extent.
Unknown logo data fails the optional stage rather than silently changing a
firmware variant. Without `--title-logo`, standalone helper runs deliberately
retain the admitted firmware's embedded Xecuter2 image. Both the kernel profile
and selected after-image source are recorded separately in the run receipt.
No original logo bytes are shipped with the extractor or package.

Audio needs all of the samples, sequence, envelopes, pointers and DSP data
listed above. It is not silently substituted with silence. The core
reconstructs sequencing and the GP stage, while **voice synthesis is an
approximation**, as the donor documents. The full eight-second stereo
48 kHz stream is generated before playback; missing output devices report
failure. Normal completion lets the audio finish; skip resets and closes
the output immediately. Unattended runs require `--headless --mute`.

## Build and private smoke test

Use a non-C: build/cache/temp directory. For example, in PowerShell:

```powershell
$bootTask = 'G:\Temp\Codex\xboxllr\native-boot'
New-Item -ItemType Directory -Force "$bootTask\temp", "$bootTask\appdata", "$bootTask\localappdata" | Out-Null
$env:TEMP = "$bootTask\temp"; $env:TMP = $env:TEMP
$env:APPDATA = "$bootTask\appdata"; $env:LOCALAPPDATA = "$bootTask\localappdata"
python -B tools/native_boot_assets.py --kernel G:\OwnedInputs\complete-kernel.img --output "$bootTask\assets"
cmake -S pilots/native-boot -B "$bootTask\build" -A x64
cmake --build "$bootTask\build" --config Release --parallel 2
& "$bootTask\build\Release\xboxllr_boot_intro.exe" --pack "$bootTask\assets\xboot.pak" --headless --mute --validate --capture-dir "$bootTask\captures"
```

`--capture-dir` must be new. It retains start/middle/logo/final GPU images
and final presentation at 16:9, 21:9 and 32:9. `--audio-file <new.wav>` saves
the complete locally synthesized sound without playing it. All those
artifacts contain original-derived content and stay private.

`--core-only` runs all 480 frame descriptions and sound without creating a
graphics device. `--realtime` paces a headless run to its eight-second
timeline. `--skip-at-ms 1000` tests an explicit skipped result. The foreground
window accepts Escape, Enter, Space or controller A/B/Start/Back to skip.
Held launch buttons and keyboard auto-repeat do not immediately skip it;
a fresh press is required after this stage begins.
No input is injected into any other application. A completed result requires
the core's real end boundary; skip is reported separately. Run budget and
allocation sizes are bounded. Every process owns and releases its resources.
`--volume 0..100` scales only the generated stream; it does not change system
audio settings. The launcher passes the owner's selected volume. Automated
handoffs use hidden/muted uncapped rendering to keep test loops short.

## Shared launcher and package handoff

`tools/native_boot_launcher.py` owns only these private application paths:

- `data/boot-choice.txt`: intro enable/kernel choice, separate from console setup.
- `data/boot/assets/<pack-hash>/`: verified extracted pack and source fingerprints.
- `data/boot/title-logo/<logo-hash>.rle`: private selected-original-XBE logo.
- `data/boot/helper.log` and `last-run.json`: bounded helper output and status.
- `data/boot/{temp,appdata,localappdata}/`: process-local writable paths.

An explicitly selected complete kernel is validated before its startup data
is used. With that field empty, a prepared private pack may be reused, or a
compatible console kernel selected above may prepare it. A short or unknown
signing dump never blocks gameplay. No save profile, EEPROM, HDD identity or
Partition1 content is written by the startup module. On normal visible launch,
three random exclusive per-launch events connect only the owning launcher,
its boot helper and its native title. The helper signals readiness only after
the final after-image; the native backend prepares its first swap before
showing its window without activating it, and acknowledges only an actual
`S_OK` swap. An occluded result cannot retire the boot window. The final image
stays alive until that acknowledgement, skip, owned-title failure, cancellation
or bounded timeout. No desktop/window discovery, broadcast, unrelated process
termination or arbitrary fixed hold is used. The helper also observes its
launcher's process handle and exits if that owner dies. Without startup IPC,
the existing native window behavior is unchanged. Automated runs and diagnostic
input replay continue to reproduce the title directly.

`native_title_launcher.build_launcher` also builds the shared helper on
Windows, under `<build>/boot/`. The release stager now requires
`--boot-intro <helper.exe>` (or the `boot_intro=` API argument). It packages
`native/XboxStartup.exe`, its MIT notice, and a complete buildable copy of
the startup host/core/extractor source under `source/native-startup/`.
It never copies a pack or kernel. Archive admission rejects `.pak` files
and an `XBPK` payload even when renamed. Loose `.rle` files and either admitted
original logo fingerprint are also rejected when renamed. The Dashboard title declares
`launcher.startup_intro = true`; other titles default off without game-name
checks in the shared runtime. Saved settings override the initial default.

The core is C11 and independent of Win32, graphics and audio devices. The
first live presentation host is Windows D3D11 only and is excluded entirely
on other platforms. MSVC's strict scalar C math adapts the donor's three
clang builtin spellings without changing the imported core. There is no
host assembly or optional instruction-set requirement.

## Qualification and remaining burndown, 2026-10-22

| Item | State | Evidence or remaining work |
| --- | --- | --- |
| Complete owned 4983 extraction, including sound | Passed | Exact input/11 range/pack fingerprints; original unchanged; no data imported into Git |
| Native core end-to-end | Passed | 480 frames, 433,105 described draws; actual `XB_DONE` at 8.00001 s; state/pack/audio guard bytes intact |
| D3D11 original-look render smoke | Passed | All 480 frames, 429,422 submitted draws (known NaN placements skipped), zero D3D11 debug errors; representative start/mid/logo images inspected |
| Final logo | Passed against donor artifact | GPU RGB output is byte-identical to the donor's published `docs/images/logo.png`; this is not independent real-hardware qualification |
| Full audio synthesis | Passed | 384,000 stereo frames, exactly 8 s, peak 10,506, RMS 1,620.64; nonzero full WAV retained privately |
| Wide presentation | Passed | Actual 16:9, 21:9, 32:9 GPU images retain the 4:3 content and black margins without stretching |
| Rejection, explicit skip and teardown | Passed smoke | Short/unknown kernels, changed pack, invalid pointers and existing output rejected; distinct bounded skip; new run succeeds afterward |
| Portability | Compile checked | All 13 core translation units cross-compile to AArch64 ELF with warnings as errors; Windows-only host imports stay guarded |
| Full sequence accuracy | Open | Donor choreography and consumed-data equivalence support this port; middle-frame presentation and approximate voice output still need independent comparison or a quick owner review |
| Foreground audio device/skip feel | Open | Background runs are muted; generated duration and content are measured, no speaker test claimed |
| Shared settings and startup lifecycle | Passed fixtures | Built launcher protocol roundtrip with locked console unchanged; end/skip/error/timeout route, private extraction and native-title spawn order checked |
| Source-complete packaging | Passed fixtures | All three title default cases; helper/MIT source included; ordinary and renamed original packs rejected |
| Actual three-title handoff | Open | Rebuild owner packages after review; launch Dashboard/Blinx/JSRF after completion/skip with their existing identity/storage unchanged |
| Additional kernels/direct flash inputs | Deferred | Add explicit supported profiles and integrity tests when needed; never scan arbitrary firmware and guess |
| Actual firmware execution/full state handoff | Separate deferred route | This presentation component does not implement or imply it |

The local one-core smoke generated core frames plus sound in about 0.42 s.
The debug D3D11 run with readbacks completed its eight seconds of content in
about 2.7 s, plus 0.8 s setup. These show real-time headroom in this setup,
not a performance guarantee or a benchmark under isolated machine load. A
muted real-time run completed all 480 frames in 8.01 s. An explicit GPU skip
at 1,000 ms reported `skipped` after 59 frames at 0.984 s; the next full run
completed normally. Nine focused tests also passed with the private input,
including rejection before audio/device setup when headless mute is omitted.

Tracking: central Beads `beads-xbox.1.190`.

## Authentic native presentation continuation, 2026-10-23

The owner's current requirement is the authentic original visible sequence,
timing and sound, running natively. Nonpresentation security, key and hardware
initialization may be skipped. Full-machine LLE and firmware-created state
handoff are not prerequisites for that presentation requirement. The current
helper remains an unqualified presentation port with the limitations below;
renaming it would not satisfy the requirement.

The retained donor is MIT authored source derived from its documented original
animation routines. It is not a licensed copy of Microsoft source. Its
sequencer and translated GP arithmetic have distinct original-code evidence;
its VP voice-to-sample behavior uses an authored 16-tap resampler and an
interpretation of envelopes/loop boundaries. The present port does not turn
that interpretation into an independently qualified original sound model.
See the pinned [core description](https://github.com/bennyborn/xbox-boot-re/blob/12a3396aa77f72c8d2fd94c0988c7e11539b47e7/docs/core.md)
and [audio evidence and limits](https://github.com/bennyborn/xbox-boot-re/blob/12a3396aa77f72c8d2fd94c0988c7e11539b47e7/docs/decomp/Audio.md).

A concrete host timing correction is now authored. Visible/realtime runs feed
measured monotonic millisecond ticks into the original-derived timing core,
retiring missed presentation deadlines after a stall. Previously they advanced
by frame number at an assumed 60 Hz while sound played in real time, which could
stretch the scene behind its sound. Audio device and controller initialization
now finish before the common timeline starts. Offline captures retain the
deterministic 480-picture schedule. The report states `monotonic-ms` or
`deterministic-60hz`; neither mode claims sound-model qualification. The new
Windows helper builds and its portable `native_boot_timeline` fixture passes.
Hidden/muted D3D11 validation completed the deterministic 480-picture sequence
with all nine capture files byte-identical to the prior build. The measured
timeline completed at 8.015 seconds with 434 presented pictures over 8.023 host
seconds; missed deadlines no longer extend the sequence. Explicit skip remained
distinct at 1.000 host second. The generated PCM fingerprint is unchanged, the
pack is unchanged and no graphics errors occurred. These observations do not
exercise a foreground audio device or compare the sound to an independent
original reference. The clock fixture accompanies the packaged startup source.

Remaining presentation work is concrete:

1. Compare the private owned-4983 run at representative blob/chamber/flash/logo
   times and the full sound stream with an independently established original
   reference. The existing final-logo match is insufficient for the full scene.
2. Isolate VP differences at original voice commands, per-voice samples, mix bins
   and GP output. Use a reusable native voice bridge with explicit ownership and
   portable behavior where a measured difference requires it; do not replace
   one unqualified approximation with another or substitute a recording.
3. Resolve renderer differences, including the current omitted NaN-constant
   draws, against the original command/raster behavior. No enhancement rendering
   or guessed visual change belongs in this qualification.
4. Validate normal completion and explicit skip into Dashboard with original
   identity/progress unchanged, normal owner audio and no unintended new input.

The original after-image path is now authored from bounded owned 4983 routines:
RLE decoder `0x8006ab87..0x8006ac63`, gray conversion at `0x8006ab40`, quad
source at `0x8007a028`, and one final draw at `0x8006ac92..0x8006b0b4` after
animation teardown. The original submit accepts a pointer/length; there is no
fade or fixed hold in this stage. Its full `0x274`-byte embedded bitmap is 628
bytes and decodes exactly 1,700 pixels, with no uninitialized tail. It is
visibly Xecuter2, which is why the source profile is named explicitly. The
actual selected-title Microsoft logo also decodes exactly 1,700 pixels.
The authored portable decoder validates flags, short/long runs, extent and
complete output; gray conversion is the original rounded 204/15 scale. The
private quad supplies the 100x17 rectangle at 270,400 in 640x480 space. The native
host copies the opaque image once after `XB_DONE`, preserving its black texels.
Independent original-raster and full-sequence comparison remain open.

Two focused native fixtures pass: monotonic timing and bounded after-image
decoding. Hidden/muted embedded and explicit title-logo D3D11 runs complete
with zero debug errors; the stock-logo run ends at 8.012 core seconds/8.025 host
seconds and shows the actual Microsoft wordmark. Real Win32 event tests retain
the helper until an owned simulated title signals presentation, and clean up
on title exit 7 or missing-present timeout without terminating the title. These
tests qualify the IPC lifecycle. A subsequent coordinated Dashboard native/helper
rebuild and actual first-present probe also passed: the retained helper received
`title-presented` and exited0, followed by a bounded five-second muted Dashboard
run with private state and unchanged original identity source. The final helper
fingerprint is `0bb9844327c72294c4fc42d46d72e66ccbdc27d1acf82937ed65738cd4b205b4`.
This used a hidden/muted helper; foreground transition and normal audio review
remain open. Earlier deterministic capture/PCM results
above predate the added after-image. Audio synthesis is unchanged and still
explicitly approximate.

There are reusable VP components already in `src/machine/apu_rate.cpp`,
`apu_envelope.cpp`, `apu_filter.cpp` and `apu_voice_quantum.cpp`, with device
clock/DMA ownership behind `audio_device_lle.h`. They provide a bounded path to
an independent native comparison. Their original-title qualifications do not
automatically cover this boot sequence: the current device transport starts at
the initialized kernel-audio handoff, and the nominal signal policy is explicit.
A startup adapter must supply the original sequence's actual voice/register
events, private sample memory, cursor/loop/end ownership and idle feedback,
then compare voice, mix-bin and GP output. Reusing the donor's already-rendered
voice output as that adapter's source would not independently test the disputed
VP behavior. Static/native lifting of original animation CPU routines is also
possible, but does not by itself resolve the separate voice hardware model.

Dashboard's initial toggle remains ON and every game defaults OFF. The source
test also exercises an unknown game with no startup declaration. No original
asset, sample, shader bytecode, kernel or capture is added to source or releases.

The integration regression batch passes 60 focused tests, including compiled
launcher protocol roundtrip, actual isolated helper execution with a newly
extracted private pack, preservation of persistent progress/signing identity,
timeout termination/kill cleanup, and all three package-default fixture cases.
The helper's actual Windows import closure is complete (four files including
the executable). These are framework/fixture results; owner-facing handoffs
of the three rebuilt games are still pending review.
