# Native Xbox startup intro

The standalone Windows prototype renders the original-look eight-second
Xbox startup animation natively, including the blob, chamber, logo and
synthesized sound. It is **an animation reconstruction, not firmware boot**.
It does not execute MCPX, 2BL or an original kernel, initialize a console,
or transfer firmware-created machine state into a native game. It has no
access to an HDD, EEPROM, native game saves or title memory.

The shared Windows launcher can run this separate presentation stage before
creating the native title process. Dashboard defaults on; games default off.
Settings has an independent startup toggle and optional complete-kernel
choice, which stay editable after the save-console profile is locked. The
first-launch game setup does not gain another requirement. Published packages
need rebuilding to include this fast follow; no installed owner copy is
modified by the integration work.

Missing or unsupported startup data gives a concise Settings status and the
title launches directly. Completion or an explicit skip proceeds normally;
an intro failure or 25-second helper timeout is logged and falls through to
the title after helper cleanup. There is no fake black delay or downloaded
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
It requires a user-owned, already unpacked complete kernel image. A short
kernel dump sufficient for save signing lacks the startup INIT data and
is rejected. Other firmware versions and direct flash/MCPX extraction are
not implemented. A complete kernel may be unpacked locally with an
appropriate open-source BIOS tool; do not distribute that input.

The private pack is 72,224 bytes, with 11 ranges totaling 71,779 bytes:

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

Nine ranges are byte-identical to the donor's published 4627 data
fingerprints at relocated addresses. The descriptor/track table matches
after explicit relocation. The sequence's consumed descriptors, events
and track bytecode also match after data-pointer relocation. The unused
function-dispatch table at sequence offsets 4..0x20f is zeroed; the portable
core never executes or reads it. This omits irrelevant original code
addresses rather than claiming that 4983 code matches 4627 code.

Each range has an integrity fingerprint. The host admits the exact complete
pack SHA-256 `6c6df777c291a447a2de2693e3edfc4a69e0c4151548c8e058b512669a09233f`
before calling the core. It rejects altered, truncated and unknown packs;
the reconstruction's internal parser is not treated as a general untrusted
mesh/sequence loader. User input files are only read. Extraction refuses
an existing output directory and never overwrites a console profile.

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
- `data/boot/helper.log` and `last-run.json`: bounded helper output and status.
- `data/boot/{temp,appdata,localappdata}/`: process-local writable paths.

An explicitly selected complete kernel is validated before its startup data
is used. With that field empty, a prepared private pack may be reused, or a
compatible console kernel selected above may prepare it. A short or unknown
signing dump never blocks gameplay. No save profile, EEPROM, HDD identity or
Partition1 content is written by the startup module. The helper is ended
before the title's process and input recorder begin; diagnostic input replay
continues to reproduce the title directly.

`native_title_launcher.build_launcher` also builds the shared helper on
Windows, under `<build>/boot/`. The release stager now requires
`--boot-intro <helper.exe>` (or the `boot_intro=` API argument). It packages
`native/XboxStartup.exe`, its MIT notice, and a complete buildable copy of
the startup host/core/extractor source under `source/native-startup/`.
It never copies a pack or kernel. Archive admission rejects `.pak` files
and an `XBPK` payload even when renamed. The Dashboard title declares
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

The integration regression batch passes 60 focused tests, including compiled
launcher protocol roundtrip, actual isolated helper execution with a newly
extracted private pack, preservation of persistent progress/signing identity,
timeout termination/kill cleanup, and all three package-default fixture cases.
The helper's actual Windows import closure is complete (four files including
the executable). These are framework/fixture results; owner-facing handoffs
of the three rebuilt games are still pending review.
