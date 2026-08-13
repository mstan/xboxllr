# PROGRESS — pick up here

> Session-pickup document for ANY agent (Claude or Codex) with zero prior context.
> Read this first, then STATUS.md (newest entry first), then docs/ROADMAP.md + docs/DESIGN.md.
> STATUS.md stays the single source of "what is true"; this file is the operational
> quick-start + the precise cursor of where the boot currently stops.

## What this project is

**xboxlle** — an LLE-first, dynamic-engine (NOT static-recomp) faithful original-Xbox
emulator. CPU floor = Unicorn (embedded QEMU x86 core, 2.1.1, via FetchContent). Devices
harvested from xemu, silicon-corrected against a live console. GPL-2.

- Framework repo (git, branch `master`): `F:\Projects\xboxlle\xboxlle`
- Workspace parent (NOT a repo): `F:\Projects\xboxlle\` — holds `refs/` (xemu, nxdk,
  XboxBiosTool, fatx clones) and `machine/` (git-ignored copyrighted dumps: flash, decrypted
  2BL/kernel, eeprom.bin, 8GB FATX xbox-hdd.img with C:\xboxdash.xbe = XBMC softmod).
- Phase: **Arc 1, A4** (boot device → dashboard). A2e complete. See docs/ROADMAP.md.
- Private upstream: `https://github.com/mstan/xboxlle` (`origin`; branch `master`).

## Current operating note (2026-08-10)

Matthew's active goal is now narrower and performance-bound: get the stock Microsoft
dashboard into an SDL window that he can navigate and validate at **at least 10 presented fps**,
with no visible rendering errors. Audio is secondary; "slow or absent" is acceptable before
visual/input correctness. Keep host usage nominal: long emulator probes must use a hard
`--max-host-memory-mb` cap, and avoid running expensive probes while the user needs the machine.
Subagents are allowed when they reduce total spend or wall time; prefer Luna for bounded
read-only/low-complexity work, Terra for difficult implementation or audits, and reserve Sol
for orchestration/adversarial review or truly stuck work. Opt-in accelerated/HLE-style render
paths are acceptable only when the software LLE fallback remains available and the expected
faithfulness loss is nominal-to-none. Do not contact real Xbox hardware or push `xboxlle`
commits without explicit current authorization.

## Current optimization cursor (2026-08-13)

Active Beads issue: `beads-xbox.1.13` (`Performance: reach interactive-navigable stock
dashboard (>=10 fps sustained)`). Current retained code on `origin/master` includes the direct
vertex-fetch optimization plus disabled perf-timer guards for the default no-accounting path.
The latest rejected experiment was direct PFIFO command-word fetch via `mem_direct_`; it was
exact but slower on the 1B smoke and was reverted.

Correctness status: the exact OpenGL/resident baseline for the stock dashboard 12B replay is
`build/resident-resolve-bgra-v1-12b.report.json`. It exits cleanly with empty stderr and final
PPM SHA-256 `876b58c2d0e6c63fec917c73dcc43a7d09983baf1da2b5b2176fa73a1b44da6d`.
The 1B smoke golden remains
`a6087ec5178c7619d8136de2aa159dde7161d56f9e4c3b899b7165935d0353d8`.

Speed status: the retained 12B exact baseline is still far from the goal: final FPS `2.552`,
final-four wall `52.636s`, CPU `52.486s`, generic raster `19.297s`, resident resolve `2.087s`,
resident ensure `1.393s`. Final-four inclusive accounting shows `nv2a_pushbuffer_ns=38.132s`,
`raster_ns=19.303s`, so pushbuffer time outside raster is about `18.829s`; CPU outside
pushbuffer is about `14.354s`. A perfect generic-raster/resolve/ensure fix alone would not
reach 10 fps, so the next pass must account for and reduce the broader pushbuffer/CPU cost too.

Latest rejected experiment: gated `XLLE_Q_RECIP_HOIST=1` was exact at 1B and 12B, but slower
against the retained baseline: final FPS `2.552 -> 2.448`, final-four wall `+3.80%`,
CPU `+3.26%`, generic raster `+4.07%`, resolve `+23.24%`. Source was reverted.

One-time Fable critique result: stop treating scalar micro-tweaks as the main path. Before the
next architecture bet, establish clearer accounting/error bars and target a larger exact bucket:
tile/row threading, SIMD, and family-keyed combiner specialization for the hot
`03aa4000` blended generic families. GPU fixed-function OpenGL/Vulkan is not exact for these
packets on this host; exact compute remains plausible only if it operates on coherent resident
color storage with old-color reads and no full CPU snapshot/readback.

Continuation finding: `perf-nv2a-split` now breaks out method dispatch, vertex submission, and
vertex fetch/transform. The exact 12B diagnostic `build/nv2a-split-v1-12b.report.json` shows the
late dashboard is not only fragment-bound: final-four instrumented counters were
`dispatch_method=43.072s`, `submit_vertex=41.342s`, and `vertex_transform=19.180s` alongside
`raster_generic=21.696s`. Whole-run final diagnostics show 455,812 vertex batches,
128,040,827 transformed vertices, and 261,153,958 attribute fetches. A conservative exact
`XLLE_VERTEX_TRANSFORM_CACHE=1` prototype was rejected: it stayed hash-exact at 12B but hit only
1,849 times versus 453,270 misses and regressed against the retained baseline. Do not revive that
range-hash cache shape without a stronger repeated-batch key.

Retained optimization: NV2A vertex attribute fetch now uses the direct unified-memory pointer
when available, after the same DMA-object start/end validation, and falls back to the existing
`mem_read_` callback otherwise. Validation: all `build-release/test*.exe` passed; 1B stock smoke
`build/direct-vertex-fetch-default-smoke-1b.report.json` was exact/empty stderr; 12B stock replay
`build/direct-vertex-fetch-default-v1-12b.report.json` was exact/empty stderr with final SHA
`876b58c2...`. Against the same `perf-nv2a-split` instrumented baseline, final-four wall improved
`61.490s -> 57.376s` (`-6.69%`), CPU `-6.70%`, dispatch `-7.84%`, submit `-8.05%`, and vertex
transform `-14.33%`. The absolute FPS reported with heavier accounting is still only `2.376`, so
the >=10 fps goal remains open.

Retained default-path cleanup: disabled perf-accounting timers are now guarded at the hottest
CPU, host-poll, NV2A dispatch/pushbuffer, submit/transform, and raster timing scopes, so normal
interactive and `-NoPerfAccounting` replays do not construct no-op `ScopedAdd` timers. Validation:
all `build-release/test*.exe` passed; no-accounting 1B smoke
`build/disabled-timer-guards-noperf-smoke-1b.report.json` was exact/empty stderr and improved
final-four wall against the clean direct-fetch no-accounting smoke by `25.617s -> 23.549s`
(`-8.07%`), final FPS `2.673 -> 2.777`; full no-accounting 12B
`build/disabled-timer-guards-noperf-v1-12b.report.json` was exact/empty stderr with final SHA
`876b58c2...` and final FPS `2.482`; perf-accounting 1B smoke
`build/disabled-timer-guards-perf-smoke-1b.report.json` remained exact/empty stderr and still
emitted parseable CPU/host/NV2A split counters. Rejected adjacent experiment:
`build/direct-pfifo-fetch-smoke-1b.report.json` was exact but regressed final FPS
`2.845 -> 2.819` and final-four wall by `+3.81%`, so source was reverted.

## Arc 1 burndown

- **Done:** A0 foundations; A2 live-kernel boot, phase-1 device init, FATX mount, and
  dashboard XBE mapping.
- **Partial / validation debt:** A1's real-hardware probe channel is live, but deterministic
  xemu-TCG cosim remains deferred. A3 uses Unicorn's dynamic translator, but the planned
  page-versioning proof is not complete.
- **In progress:** A4. Storage loads the XBE; PFIFO semaphore DMA and timed APU voice
  completion clear the early AV waits; PGRAPH software notifications plus flip/vblank
  completion finish the boot animation; `xbmc.exe` initializes NVNet and transmits through
  its descriptor engine; guest-programmed scanout can be dumped/presented; and Kelvin
  color/depth/stencil clears, indexed vertex DMA/fetch/assembly, and programmable vertex
  shaders plus the exercised fixed-function transform subset now execute. The first
  indexed and inline vertices now traverse the same programmable/fixed transforms. The
  landed triangle profiles write the dashboard's pitch-Z16 depth prepass, dominant
  untextured swizzled radial-combiner pass, and both witnessed indexed 256x256 textured
  A8R8G8B8/Z24S8 passes, the indexed 1280x480 pitch-textured and
  pitch-projected passes, the pitch single-texture fan and two-cube
  alpha-blend and dynamic three-texture passes, plus the inline path's pitch
  A8R8G8B8 projected-texture additive-blend pass and linear
  1280x480-to-640x480 dashboard blits. PGRAPH parameter shadows now carry the
  retail flip queue's framebuffer address into PCRTC, so a 500M capture shows
  the first guest-rendered green boot-orb glow through the full LLE scanout path. The next
  41+30-batch tex7/C61 fan variants, a 32-batch shader-63 strip variant, and a live
  keyboard-to-XID bridge are also landed. A frame series now reaches a recognizable Xbox X logo
  at 4.5B, but 4.75–6.0B is black and the 6.4B endpoint is near-black; the guest has not
  scheduled endpoint-2 polls. An opt-in disconnected-link replay completes the retail PHY
  timeout path and launches `evoxdash`. Its dominant witnessed immediate-mode A4R4G4B4 quad
  family now renders the real main-menu labels into the alternating 720x480 buffers through
  the retail PFIFO/PGRAPH/unified-RAM/PCRTC path. The paired I8/palette row quad also executes
  exactly. Its format-6 RS8/RS13 direct A8R8G8B8 sibling produces the visible default My
  Programs selection bar. The independently witnessed RS16/RS17 64x128 I8+palette family now
  adds the subtle dark menu-column panel/border around that list. IQRS3/IQRS7's 512x128
  I8+palette artwork now adds coherent upper-left **mediacenter home** logo/art. IQRS5/IQRS10's
  512x512 direct A8R8G8B8 image now reveals the full controller background/main panel, leaving
  earlier counters intact. IQRS4/IQRS7's 1024x1024 direct A8R8G8B8 image completes the blue/teal
  controller-home composition, with earlier counters still intact. This is visual
  panel/default-row/artwork/background visibility, not
  input-driven selection or navigation.
- **Test-only input architecture milestone:** OHCI has a parameterized synthetic three-port
  embedded-hub profile with zero identity fields, while the production/default replay remains
  the unchanged direct-XID model. Focused unit flow proves separate hub/child address and
  configuration, hub class/status, port-1 change/reset/enable and EP1 reporting, empty ports
  2/3, and downstream XID vendor plus periodic D-pad/A/B TD delivery. Isolated Debug and
  Release `test_ohci`, `test_ohci_synthetic_hub`, `test_xid_input`, and `xlle` fixture checks
  passed. This neither proves retail topology nor navigation: exact hardware descriptors and
  enumeration evidence remain blocked on `beads-xbox.1.5.8.1`.
- **Synthetic navigation proof:** commits `f2d9d7e` + `6712f74` carry a no-input 11.5B control
  (hub `1/1`, child XID `2/1`, 408 endpoint-2 / 409 aggregate reports; baseline PPM SHA-256
  `D936A993E5441E8D424E3406AE844BE2FA41A179C5F50C239B444009C6382C33`) and an explicitly
  test-only Down input run. It presses at `10981668393` / endpoint-2 320, releases at
  `11011004028` / 325, and completes at `11040335577` / 330. The post/final PPM SHA-256 is
  `DBCFAD3AE1A8BF40A87C9E8C086E81ED1E9A9C7CA062D5A7B3747FEFE1F0B0F4`; final scanout FNV64 is
  `4c34c2704eb61e54`. The highlight moves **My Programs** -> **My Files** and remains stable to
  final, but broad panel/controller redraw makes the frames non-pixel-perfect. This demonstrates
  one visible synthetic D-pad selection transition only: real embedded-hub evidence is still blocked on
  `beads-xbox.1.5.8.1`, and submenu/A activation remains unproven.
  A follow-on 13B Down-then-A replay (commit `ed399fe`, milestone 46) now presses A after the
  Down selection: XID stays configured and the guest keeps polling, but every post-A frame is
  completely black through 13B (hash `73feb194b7694383`, byte-identical), so A activation is
  guest-driven but its rendered target is not yet understood. The same session identifies the
  cloned HDD's third-party dashboard as XBMC (`C:\evoxdash.xbe`), with the stock Microsoft
  `C:\xboxdash.xbe` intact alongside it. The stock-dashboard variant image
  (`machine\xbox-hdd-stockdash.img`, milestone 47) has now booted: black-frame rendering is
  not unique to XBMC — the stock dashboard also boots fault-free through the
  production direct-XID path (no synthetic hub) and stalls on its own incomplete black frame
  (whether the two black frames share a cause is NOT yet established)
  (garbage rows over black at 6.5B/11.5B, hash `b6f0b57a4922cd33` stable from 9.5B). PGRAPH is
  doing heavy real work, so leading suspects are the stalled NV2A flip
  (`write-increments=663 stalls=663`, `PCRTC_START` pinned at `03978000`) and undecoded
  immediate vertices (`methods=53427 vertices=0`).
- **Immediate burn-down path:** root-cause the NV2A flip-stall / undecoded-immediate-vertices
  suspects behind the stock-dashboard black frame (milestone 47), continue the passive exact
  visual census, validate the dashboard frame against a real-Xbox reference, and in parallel
  observe real XID endpoint polling behind the Controller S embedded hub before proving basic
  D-pad/A/B menu navigation.
- **Not started:** A5 end-to-end retail-dashboard navigation and Arc 2 game work.

## Where the boot stands RIGHT NOW

The full chain works: 2BL (SMC handshake → SHA-1 → RC4 → LZX) → kernel entry 0x80010000 →
HAL/memory/executive init → phase-1 device init (interrupt-driven SMBus, EEPROM at 0x54) →
**FATX HDD mounted over our IDE controller** (13,121 sectors DMA-read, 419 IRQ-14
completions) → **dashboard XBE mapped** (`LOAD symbols: xbmc.exe` + 16 section notifications
via kernel debug service int 0x2D).

**Current frontier:** the AV/display worker now finishes, `xbmc.exe` creates a fourth KTHREAD,
and dashboard user code executes through NIC initialization and its first TX kick. The
dependency chain peeled this session is:

1. spun on APU VP `NV1BA0_PIO_FREE` → fixed (returns 0x80, xemu semantics)
2. spun on PFIFO idle → fixed (RUNOUT/CACHE1 status read EMPTY)
3. blocked on interrupt-driven SMBus to the TV encoder → fixed (CX25871 slave at 0x45)
4. spun polling VGA input-status-1 (PRMCIO 0x3DA) retrace bit → fixed (sub-frame phase)
5. spun at 0x800570d4–0x800570dc waiting for RAM 0x03fb3000 to advance → fixed: the former
   DMA_GET=DMA_PUT sink discarded the kernel's NV097 semaphore releases. PFIFO now parses and
   records the stream, resolves RAMHT/DMA objects in PRAMIN, and writes semaphore values to
   unified guest RAM. Verified: 158,966 methods, 14 releases (5..0x1f), old loop exited.
6. the 0x800637d3–0x800637da APU voice-idle wait → **fixed**. VOICE_ON/OFF now maintain
   guest voice state/lists, the 187.5 Hz APU frame emits SE2FE_IDLE_VOICE, the FE trap and
   IEN/ISTS lifecycle drive IRQ 5, and FECTL release consumes the decoded trap. The retail
   kernel executes its real completion routine before freeing voice 64.
7. the two-buffer throttle at 0x80052f71 → **fixed**. Nonzero NV097 NOPs now raise PGRAPH
   software-method notifications; the kernel schedules the flip, vblank writes
   NV_PGRAPH_INCREMENT, and PFIFO resumes the deferred semaphore. The counters advance only
   through the guest's real ISR path.
8. ACI descriptor completion was implemented and tested, but proved unrelated to the
   throttle. It remains as functional headless PCM output with routed IRQ 6.
9. after 485 completed flips the animation exits; SMC fan readback 0x10 and a standard
   disconnected-bus OHCI floor carry the dashboard into user code. A 6.1B run is fault-free
   at EIP 0x0011f09e with four KTHREADs and OHCI0 operational.
10. dashboard D3D rebuilds PFIFO with PUT temporarily ahead of GET → **fixed**. CACHE1
    pusher enable/mode/channel gates are honored, GET/PUT translate through the selected DMA
    object, and consumed GET is mirrored into USER and PFIFO readback.
11. the first NIC access at 0xFEF00190 → **fixed through initialization**. NVNet now has a
    strict register aperture, PHY/MDIO/link lifecycle, ring-pointer reset, W1C status, and
    IRQ4 aggregation. A 6.35B run completes 13 MDIO reads + one write and stops loudly on
    the first enabled TX descriptor kick at EIP 0x002d74a5.
12. the enabled TX kick → **fixed** with descriptor DMA, multi-fragment assembly, guest
    writeback, ring wrap, IDLE, TX status/IRQ, and an explicit headless packet sink. The
    dashboard's first kick finds an empty ring (zero VALID descriptors), then a 6.4B run
    reaches its budget fault-free in `KiIdleLoop`.
13. first framebuffer readout → **fixed through scanout/presentation**. PCRTC_START, PRMCIO
    CRTC pitch/depth, and PRAMDAC dimensions/format resolve a validated unified-memory
    surface to RGBA; `--frame-dump` writes PPM and `--window` presents it through SDL3. At
    6.4B the guest selects 720x480x32 at `0x03c8c000`, pitch 3072. The frame is black because
    the recorded NV097 clear/draw stream still has no raster side effects. This is the
    historical first-readout checkpoint; milestone 34 supersedes it with visible boot output.
14. first raster operation → **fixed through Kelvin surface clears**. RAMHT object/class
    binding gates PGRAPH execution; color/zeta DMA contexts, pitch surfaces, masked color,
    Z16, and Z24S8 clear semantics write validated unified-memory rectangles. At 6.4B, 852
    color and 1,968 zeta clears execute fault-free; the final scanout contains the guest's
    uniform RGB(1,0,1) clear. Geometry remains the next boundary.
15. dominant indexed geometry front-end → **fixed through fetch and assembly**. Vertex DMA
    A/B, all 16 array slots, six Xbox formats, `BEGIN_END`, `ARRAY_ELEMENT16/32`, and
    `DRAW_ARRAYS` now validate and decode guest memory. At 6.4B, 436,456 batches submit
    48,554,942 vertices, perform 99,763,024 float-attribute fetches, and assemble 45,997,592
    triangles across triangle/list/strip/fan modes. Both indexed transform modes now execute;
    raster writes are the next boundary, and 485 `INLINE_ARRAY` batches remain recorder-only.
16. programmable vertex transform → **fixed through Cheops execution**. The 136-slot
    program store, 192 float4 constants, load/start pointers, viewport aliases, and execution
    mode feed the pinned NV2A CPU interpreter. At 6.4B, 292,866 programmable batches execute
    35,477,464 vertices with deterministic oPos hash `18b38c709acba547`.
17. fixed-function vertex transform → **fixed for the full exercised subset**. Matrix,
    material, lighting, light-mask, texgen, and texture-matrix state are tracked. All 143,590
    fixed batches use skinning off, no active lights/texgen/texture matrices, and one of two
    implemented lit/unlit color paths. The full 48,554,942-vertex position hash is
    `e7d742624c1f1793`.
18. first triangle raster path → **fixed for the untextured pitch-Z16 depth prepass**.
    Triangle lists/strips/fans use fixed-coordinate top-left coverage, culling, polygon
    offset, clip rejection, and LEQUAL D16 writes through zeta DMA. At 6.4B, 142,842
    batches rasterize 5,243,066 front-facing triangles and write 139,247,115 depth samples
    with hash `9424b7ed8661f321`. Programmable screen postprocessing corrects the combined
    48,554,942-vertex position hash to `b1bdfde660324e2e`; color draws remain the frontier.
19. dominant untextured color profile → **fixed through swizzled A8R8G8B8/Z24S8
    rasterization**. Morton-addressed color/zeta surfaces, perspective-correct smooth
    varyings, the exact radial three-stage combiner, alpha/blend, LEQUAL, and stencil-
    preserving Z24 writes execute. At 6.4B, 155,372 batches cover 4,107,334,135 fragments
    and commit 727,241,984 paired writes with hash `b57945aef12a9289`. Its last target is
    `0x02f8f00c`, not final scanout `0x03c8c000`; textured draws are the visible-frame
    frontier.
20. next high-volume textured color profile → **fixed through two witnessed texture
    fetches and its eight-stage combiner**. Texture DMA A/B, offsets, palettes, borders,
    shadow compare, and final factors are retained in the draw census. Shader `0x10c60`
    disables stage 0, cube-remaps stages 1/2 into one 64x64 or 128x128 swizzled
    X8R8G8B8 image, samples them bilinearly with clamp-to-edge, and forces stage 3 to zero
    through shadow-NEVER. With the interleaved projected sibling also executing, all 2,394
    witnessed batches rasterize 30,198 triangles, perform 1,434,732 samples, and commit
    717,366 paired color/Z24 writes with hash `537c8e788aaf4e83`. This supersedes the
    standalone checkpoint: the sibling's earlier depth writes correctly reject later
    fragments, and the shared combiner now clamps its weighted intermediate exactly.
21. lower-volume projected textured profile → **fixed through three bilinear samples and
    the seven-stage dot-product combiner**. Shader `0x10c61` PROJECT2D-divides stage 0 by
    interpolated W and repeat-samples a 128x128 swizzled A8R8G8B8 image; stages 1/2 retain
    the cube-remapped shared X8 image and stage 3 remains shadow-NEVER. At 6.4B, 204 batches
    rasterize 10,368 triangles, perform 2,044,719 samples, and commit 681,573 paired writes
    with hash `117e65d4ec81492a`. Earlier depth/radial/transform hashes remain unchanged;
    targets `0x0299f394` and `0x029a2980` are offscreen and final scanout stays the clear.
22. sparse inline-array geometry → **fixed through interleaved decode, transform, and
    primitive assembly**. `INLINE_ARRAY` derives component-aligned slot offsets from the
    active array formats, decodes all six supported Xbox formats without DMA, preserves
    disabled/last attribute values, and submits into the existing programmable/fixed
    transform path. At 6.4B, all 485 batches consume 5,820 words into 1,455 vertices and
    2,910 attribute decodes, bringing totals to 436,941 batches, 48,556,397 vertices, and
    45,998,077 triangles with position hash `2824162d794cc5de`. Existing raster hashes and
    final scanout are unchanged. The inline states expose the next profile: pitch
    A8R8G8B8, one projected texture, source-alpha/ONE additive blending, initially ALWAYS
    depth, and no depth writes.
23. inline pitch/additive color profile → **fixed through projected sampling, alpha,
    blending, and read-only depth**. PROJECT2D-divided coordinates bilinearly sample a
    256x256 swizzled A8R8G8B8 texture with clamped U/V, the one-stage combiner multiplies
    texture RGBA by c0, GEQUAL 1 rejects zero alpha, and SRC_ALPHA/ONE ADD saturates into the
    pitch target. The later dashboard state switches the unused P address mode, supplies an
    inert opaque border, and changes depth from ALWAYS to read-only Z24S8 LESS; both pass and
    reject preserve depth and stencil. At 6.4B, 113 batches rasterize 226 triangles and
    43,083,242 fragments, perform 39,818,347 texture samples, pass alpha 38,224,887 times,
    and commit 15,123,585 color-only writes with hash `6363f356e93c6124`. Prior raster,
    transform, and final scanout hashes remain unchanged; the last inline target
    `0x0387fb78` is still offscreen from final scanout `0x03c8c000`.
24. OHCI TD execution and Xbox Controller S → **fixed through enumeration and endpoint
    I/O**. The scheduler walks periodic, control, and bulk ED lists; transfers bounded
    one/two-page TD buffers; retires condition codes/toggles; writes HCCA done heads; and
    raises the standard completion/root-hub interrupts. OHCI0 port 1 exposes a full-speed
    Controller S with standard USB and XID descriptors, control requests, injectable
    20-byte input reports, and 6-byte rumble output. Focused coverage verifies descriptor,
    address/configuration, done-head, IRQ, exact input bytes, and rumble. A fault-free 6.4B
    retail replay performs four control requests across 10 TDs and configures address 1;
    an 8B replay remains fault-free with the same counts and all prior raster/scanout hashes
    unchanged. This dashboard boot window does not yet poll endpoint 2; host input plumbing
    remains.
25. NVNet receive DMA and external packet transport → **fixed through retail ingress**.
    RX checks the enabled/link/DMA gates, applies the witnessed broadcast/multicast/MAC
    filter, consumes one AVAIL descriptor per frame, bounds and writes the guest buffer,
    stores exact length plus flags `0x0011`, advances/wraps the high-half-sized RX ring,
    restores IDLE, and raises RX IRQ `0x02`. An opt-in nonblocking UDP tunnel carries one
    complete Ethernet frame per datagram and honors guest backpressure at a 1 kHz guest-time
    poll. Focused coverage proves DMA, filtering, drops, wrap, IRQ, and a loopback datagram.
    A UDP-backed 6.4B replay admits a queued 42-byte broadcast ARP frame through the retail
    driver's ring (`RX offer/desc/pkt/bytes=1/1/1/42`), clears the IRQ through the guest,
    reaches budget fault-free, and preserves every prior raster/scanout hash.
26. dominant indexed pitch-textured family → **fixed through linear color/Z24 output**.
    Shader `0x10c60` reuses the validated two cube samples and eight-stage combiner with
    per-state stage-6 RGB weights; its enabled PROJECT3D Y16 shadow stage is output-inert
    because texture 3 is not referenced by the combiner. At 6.4B, 121,296 batches
    rasterize 4,464,588 triangles, perform 833,192,036 samples, and commit 416,596,018
    paired pitch color/Z24 writes with hash `78a9682b85e41f92`. Recorder-only batches fall
    from 136,016 to 14,720. Prior independent hashes and final scanout remain unchanged;
    the shared-destination inline hash re-baselines to `610e9157ec10a027` with its write
    count unchanged.
27. indexed pitch-projected family → **fixed through three-sample linear output**.
    Shader `0x10c61` PROJECT2D-samples stage 0, cube-remaps stages 1/2, applies dynamic
    stage-5 RGB weights, and writes pitch A8R8G8B8/Z24S8. The later opaque-black border
    is inert under REPEAT addressing and is covered explicitly. At 6.4B, 10,336 batches
    rasterize 1,693,723 triangles, perform 695,540,724 samples, and commit 231,846,908
    paired writes with hash `4ebdb2f4475cd80c`. Recorder-only batches fall from 14,720
    to 4,384. Independent raster/transform/scanout hashes remain unchanged; shared pitch
    hashes re-baseline under the newly interleaved depth writes.
28. pitch single-texture fan family → **fixed through border-filtered color-only output**.
    Shader `0x1` PROJECT2D-samples one 1024x1024 swizzled A8R8G8B8 texture with bilinear
    U/V CLAMP_TO_BORDER, copies texture RGB, forces alpha one, and replaces pitch color
    through ONE/ZERO blending while LEQUAL-tests Z24S8 without modifying depth or stencil.
    At 6.4B, all 121 retained batches rasterize 242 triangles and 74,342,400 fragments,
    with every fragment passing, sampling once, and writing color. Hash
    `e734e675e998af83` is deterministic; recorder-only batches fall from 4,384 to 4,263
    while every prior raster, transform, and scanout hash remains unchanged.
29. pitch two-cube alpha-blend family → **fixed through six-stage combiner output**.
    Shader `0x63` cube-remaps stages 0/1 into one 64x64 swizzled image, dots expanded
    RGB, derives alpha as `1-(1-max(dot,0))^2`, and blends constant RGB `0x0D3308`
    through SRC_ALPHA/ONE_MINUS_SRC_ALPHA. GEQUAL-one alpha and writable LEQUAL Z24S8
    preserve rejected fragments and stencil. At 6.4B, all 120 retained batches
    rasterize 11,411 triangles and 30,808 fragments; 18,094 depth passes perform
    36,188 samples and 16,485 alpha passes commit paired writes with hash
    `fbe1b6125ce84364`. Recorder-only batches fall from 4,263 to 4,143 while all
    preceding hashes and counts remain unchanged.
30. inline linear dashboard blits → **fixed through bilinear color-only output**.
    Inline triangle lists PROJECT2D-divide texel-space coordinates, bilinearly sample a
    1280x480 linear A8R8G8B8 source with CLAMP_TO_EDGE, and copy texture RGB plus diffuse
    alpha into a 640x480 pitch target. Culling and depth access are disabled. At 6.4B,
    all 106 retained batches rasterize 106 triangles and perform 32,563,200 samples and
    color writes with hash `a6fa8d7f361f0383`. Recorder-only batches fall from 4,143 to
    4,037 and retained states from 962 to 961 while every prior raster, transform, and
    scanout hash remains unchanged.
31. dynamic three-texture pitch family → **fixed through factor-varying blended output**.
    Shader `0x63` cube-remaps stages 0/1 into one 64x64 image while programmed stage 2
    remains shader-inert. The expanded-RGB dot derives alpha; dynamic stage-3 c0 RGB
    modulates that alpha and adds to fixed tint `0x0D3308` before SRC_ALPHA blending and
    writable LEQUAL Z24S8. At 6.4B, the semantic profile covers 2,104 batches across 101
    retained states, rasterizes 199,617 triangles and 512,754 fragments, and turns 6,400
    depth passes into 12,800 samples plus 6,400 paired writes with hash
    `a9f6884dc0abadf3`. Recorder-only volume falls from 4,037 to 1,933 batches and 961 to
    860 states. Independent hashes and final scanout remain stable; the later shared-target
    inline-additive hash rebaselines to `763144f45d489441`.
32. disabled-state inline linear-blit variants → **fixed through semantic state matching**.
    With only dither enabled on A8R8G8B8, programmed combiner constants, alpha/blend/depth/
    cull functions, and polygon offsets are inert. The profile now keys on output-affecting
    geometry, surface, texture, combiner, mask, shade, control, and clip state while focused
    coverage proves captured `C4A0FF60` factors and SRC_ALPHA blend programming do not alter
    output. At 6.4B, 62 additional batches across two states raise the combined profile to
    168 triangles and 51,609,600 samples/writes with hash `3f1f5a430082ada4`.
    Recorder-only volume falls from 1,933 to 1,871 batches and 860 to 858 states; every
    independent raster, transform, downstream inline-additive, and scanout hash is stable.
33. front-culled tex5 pitch triangle lists → **fixed through projected and diffuse output**.
    One exact raster envelope covers four retained 42-batch states. The projected signature
    PROJECT2D-divides stage-0 coordinates and border-bilinearly samples the witnessed
    16x128/256x256 swizzled A8R8G8B8 images; its programmed cube stage is output-inert and
    unfetched. The shader-off sibling copies diffuse RGBA. Both cull FRONT winding, replace
    pitch color, and perform writable LEQUAL Z24 while preserving stencil. At 6.4B, 168
    batches rasterize 11,706 surviving triangles and 34,627,699 fragments; 10,316,807 depth
    passes commit paired writes, 5,653,620 projected samples occur, and the profile hash is
    `c1bde764442a20dd`. Recorder-only volume falls from 1,871 to 1,703 batches and 858 to
    854 states. Earlier independent hashes plus transform/scanout remain stable; later
    shared-target inline-blit and inline-additive outputs rebaseline to
    `f055814e10fd11ef` and `979e32b12359dba4`.

34. boot-animation scanout → **fixed through the retail flip/resolve path**. Kelvin
    method `0x1D8C` now shadows parameter A at PGRAPH `0x1A88` (and method
    `0x1D90` shadows parameter B at `0x186C`), so the kernel's trapped-NOP
    `0x304` handler queues framebuffer `0x032A4000` instead of zero. The inline
    resolve accepts output-inert PROJECT2D P-axis, border-color, and shadow-depth
    retained variants. At 500M, 29 full 640x480 resolves perform 8,908,800
    samples/writes, PCRTC remains on `0x032A4000`, scanout hash is
    `bee38a32a69372a1`, and the captured frame visibly contains the early green
    boot-orb glow. This remains ROM/kernel/PFIFO/PGRAPH/RAM/PCRTC LLE with no
    D3D interception or host framebuffer substitution.
35. late-replay iteration and interactive input → **improved, with visibility/input
    blockers measured rather than hidden**. `--reduced-retire-diag` removes only exact
    retired-PC/thread attribution from the unconditional code hook and is 7.3% faster at
    500M with exact output/counter parity; full diagnostics remain the default. A narrow
    tex7/C61 fan profile executes 41 formerly recorded batches at 6.4B, producing
    1,951,345 samples/color writes with hash `d53095d432805a3b`. The selected 720x480
    scanout is nevertheless still uniformly black. `--interactive` maps arrows, Z/X,
    Enter, and Backspace into the Controller-S state and presents live scanout at a
    throttled 30 Hz. Enumeration reaches address/configuration 1, but the guest leaves all
    OHCI schedules empty and performs no endpoint-2 transfer. The plumbing is ready;
    dashboard visibility, guest input polling, and menu navigation remain open.
36. retained late-draw variants and passive USB binding evidence → **improved without
    widening guest-visible behavior**. Exact border/factor gates execute a 32-batch
    shader-63 strip state and a 30-batch C61 fan state through already-proven semantics.
    Recorder-only volume falls from 678 states/1,345 batches to 676/1,283, while the final
    scanout remains one-color black hash `73feb194b7694383`. A four-entry EP0 trace proves
    the guest reads 8 device-descriptor bytes, sets address 1, requests 80 configuration
    bytes but receives the complete 32-byte descriptor through an error path, then sets
    configuration 1. It never requests the XID descriptor or publishes endpoint-2 work.
    The descriptor matches local xemu/Cxbx references; exact short-packet condition-code
    confirmation and hardware/reference comparison precede any semantic change.
37. visual cadence, replay speed, and pitch-three tie observability → **measured and bounded**.
    The normal-RAM Unicorn memory hook is lazy unless a diagnostic client requests it; on the
    same binary at 500M it is about 33.5% faster with an identical final PPM. The new
    `--frame-series <path-prefix> <instruction-interval>` interface enforces an interval of at
    least 1,000,000 instructions and records actual sample counts in filenames. It shows a
    recognizable Xbox X logo at 4.5B, black output from 4.75B through 6.0B, then a 6.25B mode
    transition with noisy lower scanlines and a near-black 720x480 endpoint at `0x03c8c000`.
    This is still not dashboard/menu visibility. The pitch-three sampler has a deliberately
    narrow deterministic continuation for exact `|X|==|Y|>|Z|`: select X only for that retained
    profile, while pitch-multi and all-axis ties keep legacy behavior. The 6.4B replay is
    fault-free with tie direction0/direction1 = 5/0 and zero/rescale counters = 0/0; this is
    not asserted as a hardware-proven general tie priority.
38. offline dashboard lifecycle and first complete draw packet → **captured, next semantic
    gap exact**. Link-up reaches the stock XONLINE/XNET 90,000-tick wait. Experimental
    `--net-disconnected` follows the retail driver's bounded 6,000 x 500 us autonegotiation
    and 1,000 x 500 us link polls; by 11.5B it launches `evoxdash` with eight kernel threads.
    The late loop alternates `0x03df4000`/`0x03c8c000` scanout and performs 125 full-screen
    clears/flips while remaining near-black. The bounded clear-to-draw trace proves the first
    subsequent class-`0x97` packet is BEGIN mode 8, then four vertices using generic 2F
    texture coordinates (`0x18c8/0x18cc`), generic 4UB diffuse (`0x194c`), and legacy 4F
    position commits (`0x1518` through `0x1524`), followed by END. This explains the 6,000
    new BEGIN/END writes with zero modeled vertex submissions. The witnessed front end now
    executes 232,662 immediate writes as 49,500 vertices in 3,000 mode-8 batches
    (12,375 quads / 24,750 triangles), with an early guard that leaves mode 8 recorder-only.
    A guarded 11.5B replay is fault-free and produces a byte-identical endpoint PPM; all
    established raster counters/hashes remain unchanged. Post-VSH/texture evidence for the
    new 27 recorder states is the next boundary; the menu and navigation are still not visible.
39. first recognizable dashboard menu frame → **rendered through the retail LLE path**.
    One exact output-state profile executes the witnessed immediate mode-8 A4R4G4B4 text
    family: programmable VSH output feeds independent quads split as `(1,2,0)` and `(2,3,0)`,
    a swizzled 256x256 atlas is bilinearly sampled, and the captured combiner, alpha test, and
    source-alpha blend write the alternating `0x03df4000`/`0x03c8c000` targets. An 11.5B
    disconnected-link replay executes 875 batches / 15,500 triangles / 1,731,000 fragments
    and samples / 558,625 alpha passes and color writes with zero profile rejects. The final
    720x480 scanout has RGBA FNV64 `a77da2bfc5887ee8` and PPM SHA-256
    `4198470101D5DCD66867C392A4B85DDF26C99C0C930E3CB1DCAE3B6E438BE535`; visual inspection
    shows My Programs, My Files, My Pictures, My Music, My Videos, Weather, and Settings.
    This proves first menu visibility without HLE rendering or host framebuffer substitution,
    not complete graphics or interaction: other mode-8 families remain recorder-only and no
    endpoint-2 XID poll/menu-state transition has been observed.
40. paired indexed dashboard quad family → **executed exactly, with no selected-scanout
    difference**. The exact RS0/RS3 pixel state resolves a swizzled 128x32 I8 image and its
    adjacent 256-entry A8R8G8B8 palette through DMA A, palette-expands before bilinear
    filtering, applies texture alpha kill, and feeds the retained combiner/blend into either
    alternating pitch target. The 11.5B replay executes 875/875 candidates with zero rejects:
    1,750 triangles and 4,567,500 fragments/samples/writes, I8 hash `070a0f9ca1d32663`.
    Recorder-only work drops to 687 states / 2,314 batches while every published A4 text
    counter/hash remains exact. The final scanout and PPM are byte-identical to milestone 39,
    so the former selected-row-highlight interpretation is not claimed. Separately, retained
    OHCI terminal-TD evidence proves the direct-XID configuration short read used `R=0`,
    returned 32/80 bytes with CC9, and halted its ED. Do not weaken that correct OHCI rule:
    the device-tree fidelity gap is the missing Controller S embedded three-port hub ahead of
    the XID child; exact hub descriptor bytes require the warned real-hardware probe workflow.

41. RS8/RS13 format-6 immediate dashboard family → **default selected row is visible**.
    The exact four-vertex direct-color sibling of RS0/RS3 samples a swizzled 128x32
    little-endian A8R8G8B8 texture through DMA A, applies the captured alpha-kill,
    combiner, and source-alpha blend state, and writes both alternating pitch targets.
    Palette state stays passive for format 6; only indexed I8 admission requires it. A
    Release 11.5B disconnected reduced-retire replay completes fault-free in 243.144 s
    with empty `build/probe-mode8-a8-rs813-executed-11.5b.stderr.log`; its stdout companion
    records 125/125 A8 candidates, reject `00` / raster reject `0`, 250 triangles, and
    652,500 fragments/alpha passes/color writes/samples, hash `61d45aeeec495fe5`.
    Recorder-only work is 685 states / 2,189 batches. I8 remains
    875/1,750/4,567,500 with hash `070a0f9ca1d32663`; A4 remains
    875/15,500/1,731,000/558,625, though its output hash becomes
    `1cd0461e8bed58b3` because new destination contents participate in later blending.
    The final scanout is FNV64 `52613982b9d8fdd9` and
    `build/frame-mode8-a8-rs813-executed-11.5b.ppm` hashes SHA-256
    `1A2FEAA99EB423F710B390D412ED520B014E397ECB359CDD64E96852C5C53817`.
    Visual inspection and exact comparison against the prior accepted frame show a coherent
    180x29 bar at x=270,y=158 behind My Programs: 4,725 pixels differ, exactly within
    `180x29+270+158`. This proves default selected-row visibility only—not controller input,
    navigation, or a guest-owned state transition. Next: RS16/RS17 menu-column evidence or
    the Controller-S embedded-hub / endpoint-2 topology needed to prove input.

42. RS16/RS17 indexed menu-column family → **panel visibility is rendered exactly**.
    The exact four-immediate-vertex state samples a 64x128 swizzled I8 texture through DMA A,
    expands its adjacent little-endian A8R8G8B8 palette before bilinear filtering, then applies
    the captured alpha-kill, combiner, and source-alpha blend state to either alternating pitch
    target. The full retained-state IQRS diagnostic landed in `1f6e7a7`; it distinguishes this
    family from a captured zero-final-factor lookalike, which stays recorder-only. The Release
    11.5B disconnected reduced-retire replay finishes fault-free in 234.460 s with empty
    `build/probe-mode8-menu-i8-rs1617-executed-11.5b.stderr.log`; its stdout companion records
    124/124 candidates, reject `00` / raster reject `0`, 124 batches, 248 triangles, and
    4,776,480 fragments/alpha passes/color writes/samples, hash `ecbecc3b80917983`.
    Recorder-only work is 683 states / 2,065 batches. Earlier profile counts are unchanged;
    destination interactions rebase their hashes to A4 `b719e98aa60676c3`, row-I8
    `27545230ed7deda3`, and A8 `21933973698fca35`. The final scanout FNV64 is
    `5ddd492f7bb48be9`; `build/frame-mode8-menu-i8-rs1617-executed-11.5b.ppm` has SHA-256
    `7125D8EA84C6FD24A4A250094416F0EC6C2DCCFD1CE6882B4DF095877CDFA4B0`, with matching PNG.
    Exact comparison against the selected-row frame finds 32,398 changed pixels in
    `180x214+270+158`; visual inspection shows a coherent subtle dark menu-column panel/border
    around the list while retaining the blue selected row. This is visual fidelity only, not
    input, navigation, or a guest-owned selection change.

43. IQRS3/IQRS7 upper-left artwork family → **mediacenter-home composition is visible**.
    The exact four-immediate-vertex pair resolves a 512x128 swizzled I8 texture and its
    immediately preceding little-endian A8R8G8B8 palette through DMA A, palette-expands before
    bilinear filtering, and applies the shared alpha-kill, combiner, and source-alpha blend
    state to both alternating pitch targets. The fault-free Release 11.5B disconnected
    reduced-retire replay completes in 237.629 s with empty
    `build/probe-mode8-artwork-i8-executed-11.5b.stderr.log`; its stdout companion records
    125/125 candidates, reject `00` / raster reject `0`, 250 triangles, 4,915,500
    fragments/samples, 2,474,875 alpha passes/color writes, and hash `54c2429a3605cc5f`.
    Recorder-only work is 681 states / 1,940 batches. Earlier telemetry/hashes remain unchanged:
    A4 `b719e98aa60676c3`, row-I8 `27545230ed7deda3`, A8 `21933973698fca35`, and menu-I8
    `ecbecc3b80917983`. The final scanout FNV64 is `825f3c20466439fb`; the PPM
    `build/frame-mode8-artwork-i8-executed-11.5b.ppm` hashes SHA-256
    `3CB536A60F20DDAA1D1FD592A7B4443E761C81C35BEDF7032929073CFD43FC08`, with matching PNG.
    Exact comparison against the prior panel frame finds 9,287 changed pixels in
    `338x102+45+39`; visual inspection shows coherent mediacenter-home upper-left logo/art
    while retaining the selection bar and panel. The +3.169 s (about 1.35%) single-run replay
    difference is subject to noise; both runs remain in the roughly four-minute band. This is
    not an input, navigation, or guest-owned selection claim.

44. IQRS5/IQRS10 main-panel A8R8G8B8 family → **controller home background is visible**.
    The exact four-immediate-vertex pair samples a 512x512 swizzled little-endian A8R8G8B8
    texture through DMA A. Palette state is passive; the exact alpha-kill, combiner, nonzero
    final factors, and source-alpha blend state write the alternating pitch targets, while the
    zero-final-factor IQRS16 lookalike remains recorder-only. The fault-free Release 11.5B
    disconnected reduced-retire replay completes in 241.388 s with empty
    `build/probe-mode8-main-panel-a8-executed-11.5b.stderr.log`; its stdout companion records
    124/124 candidates with zero reject masks, 124 batches, 248 triangles, 33,529,600
    fragments/samples, 13,417,544 alpha passes/color writes, last `03f37018`, and hash
    `74b351eb7eee0da3`. Earlier profile counters remain unchanged. The final scanout FNV64 is
    `84f622fae4176b8d`; the PPM SHA-256 is
    `64FB065345EAA9F46B495B7C03E6F1DDFC252DFE87F528E176EDC53FC4B02DD2`, and its PNG SHA-256 is
    `474537626177E7BD5E7A1F626456FFA83DED6BFD1AF6C7363FE3EAF0A04E3879`.
    Exact comparison against the artwork frame finds 100,710 changed pixels in
    `566x391+45+39`; visual inspection reveals the full controller background/main panel while
    retaining text and selection. The +3.759 s (1.58%) single-run change is subject to noise;
    both runs remain in the roughly four-minute band. This does not establish input or
    navigation: the Controller-S embedded-hub topology still blocks guest endpoint polling.

45. IQRS4/IQRS7 full-screen A8R8G8B8 family → **controller-home composition is complete**.
    The exact four-immediate-vertex pair samples a 1024x1024 swizzled little-endian A8R8G8B8
    texture through DMA A, with stale palette state passive. The captured alpha-kill, combiner,
    nonzero final factors, and source-alpha blend write either full-clip pitch target; the
    zero-final-factor IQRS12 lookalike remains recorder-only. The accepted fresh rebuilt Release
    11.5B disconnected reduced-retire replay completes fault-free in 266.058 s with empty
    `build/probe-mode8-fullscreen-a8-executed-11.5b.stderr.log`; its stdout companion records
    124/124 candidates, reject `00` / raster reject `0`, 124 batches, 248 triangles, and
    42,854,400 fragments/alpha passes/color writes/samples, last `03f5bf38`, hash
    `d595bb56f28eb6c3`. Earlier profile counts remain unchanged, though destination interactions
    rebase downstream hashes. The stale-binary 288.861 s run is invalid evidence and is not
    accepted. The valid +24.670 s (+10.22%) delta from the 241.388 s main-panel run is material
    and the next optimization target; it is still one clean, no-concurrent-builds run and thus
    subject to normal single-run noise.

    The final scanout FNV64 is `5994d8280fd524cd`; the PPM
    `build/frame-mode8-fullscreen-a8-executed-11.5b.ppm` has SHA-256
    `D936A993E5441E8D424E3406AE844BE2FA41A179C5F50C239B444009C6382C33`, and its PNG has SHA-256
    `21035B38E17B3C146C1E5E391FDA6BB0CC6956BC033457843B6F43D2300870DB`. The matching diff and
    stdout/stderr logs live in `build/`. Exact comparison against the main-panel milestone finds
    342,914 changed pixels within `565x390+45+39`; visual inspection shows the complete blue/teal
    background/final controller-home composition. This makes no input or navigation claim: the
    Controller-S embedded hub still blocks guest polling.

46. synthetic Down-then-A replay reaches 13B → **stable but black post-selection frame;
    dashboard identified as XBMC**. The same test-only three-port synthetic hub carries a
    scripted Down-then-A sequence through a fresh 13B disconnected reduced-retire replay
    (commit `ed399fe`); all six focused tests pass (`test_xid_down_a_script`, `test_xid_script`,
    `test_ohci`, `test_ohci_synthetic_hub`, `test_xid_input`, `test_machine_config`). The run is
    fault-free in 264.3 s with an empty `build/probe-synthetic-hub-down-a-rerun-13b.stderr.log`;
    final OHCI state is frames=9421, eds/tds/retired/done=4238/1301/722/691, xid-endpoint2=664,
    hub-addr/config/port1=1/1/00000003. Endpoint-2 counts trace Down PRESS/RELEASE at 320/325
    (matching the Down-only 11.5B proof above, hash `4c34c2704eb61e54` at count 330 and still at
    count 408), then A PRESS/RELEASE at 408/413 (`analog[0]=0xff`). Post-A hash
    `73feb194b7694383` is byte-identical from count 418 through the 13B endpoint (count 664);
    the four post-A PPMs share SHA-256
    `0C9B6D850BF9A1B0D04FB310A2C15CEF78E123AD126CC6D8FD0DDF7CEE198AA1`. Visual inspection shows
    the pre-A frame is the dashboard main menu with My Files highlighted and every post-A frame
    is completely black: a real, XID-configured, guest-scheduled state transition to a
    not-yet-understood blank view. Separately, the cloned HDD's third-party dashboard is
    identified as XBMC, installed at `C:\evoxdash.xbe` (confirmed by XBE strings and
    `C:\XboxMediaCenter.xml`/`C:\XBMC\`); the stock Microsoft `C:\xboxdash.xbe` (1,961,984 bytes)
    is intact alongside it. A stock-dashboard variant image
    (`machine\xbox-hdd-stockdash.img`, `/evoxdash.xbe` swapped for stock bytes via pyfatx) is
    built and its first boot is milestone 47. This remains test-only synthetic-hub evidence,
    not retail topology: `beads-xbox.1.5.8.1` still blocks real Controller-S hub
    descriptors/enumeration.

47. stock-dashboard variant boot → **runs fault-free through the production direct-XID path,
    but stalls on an incomplete black frame**. `machine\xbox-hdd-stockdash.img` is a byte copy
    of `xbox-hdd.img` with `C:/evoxdash.xbe` unlinked and rewritten with the stock
    `C:/xboxdash.xbe` bytes (1,961,984 bytes) via pyfatx in WSL, read-back verified; `xboxdash.xbe`
    itself and the original image are untouched. An 11.5B disconnected reduced-retire replay
    (`--frame-series build\frame-stockdash-boot 500000000`, `--frame-dump
    build\frame-stockdash-boot-final-11.5b.ppm`) exits 0 in 4m26s with an empty
    `build\probe-stockdash-boot-11.5b.stderr.log`; OHCI shows the production direct-XID topology
    (no synthetic hub), `EP0 addr=1 GET_DESCRIPTOR(config) len=0x80` outcome=error cc=9, matching
    the short-read behavior established elsewhere. The stock dashboard genuinely runs: scanout
    reconfigures to 640x480x32 pitch 2560 at `PCRTC_START 0x03978000` (versus XBMC's 720x480).
    Frame-series hashes change at 6.5B (`0545e67084c18ffb`) and 9.5B (`b6f0b57a4922cd33`), then
    hold stable through the 11.5B final frame (`b6f0b57a4922cd33`). Human inspection: 6.5B is
    black with garbage rows at the bottom, 11.5B final is mostly black with garbage rows at the
    top — not a recognizable dashboard. PGRAPH does heavy real work (vertex batches=449,050,
    submitted=99,134,424, triangles=62,857,508; pitch-textured batches=121,296,
    304,256,110 fragments passing/writing; transform fixed/programmable=147,299/301,751), so
    this is not a PGRAPH-idle stall. Two leading suspects: `NV2A flip: write-increments=663
    stalls=663 read-increments=662 notifications=2109 surface=21100000 waiting=1` with
    `PCRTC_START` pinned at `03978000`, and `NV2A immediate vertices: methods=53427 vertices=0`
    (53,427 immediate-mode writes decoding zero vertices). This supersedes the prior "in
    progress" phrasing: the stock dashboard is fault-free end to end, but the exact NV2A
    blocker for the black frame remains open.

## How to build & run

```sh
export PATH=/c/msys64/mingw64/bin:$PATH        # MSYS2 mingw64 toolchain
cd /f/Projects/xboxlle/xboxlle
# First configure only (CMake 4 needs the policy flag for old deps):
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j
ctest --test-dir build                         # 10/10 (ring, jsonl, nv2a, apu, aci, smc, ohci, xid_input, nvnet, spike)

cd build
./xlle.exe --boot ../../machine/flash-dump-ff000000.bin \
           --run2bl ../../machine/bios-extract/bldr.bin \
           --hdd ../../machine/xbox-hdd.img \
           --eeprom ../../machine/eeprom.bin \
           --max-insns 500000000
```

External Ethernet is opt-in. Add
`--net-udp-bind <local-port> --net-udp-peer <host> <peer-port>` to carry one complete
Ethernet frame per UDP datagram. This is a raw layer-2 tunnel, not NAT or an IP shim; pair
it with another tunnel endpoint or a bridge that preserves Ethernet frames.

For faster long replays, add `--reduced-retire-diag`; it preserves guest behavior and
fault/raster diagnostics while omitting exact retired-PC/thread attribution. The run
prints rich diagnostics:
KTHREAD scan + every-waiter wait-block/stack walk, per-thread instruction attribution,
non-idle PC ring, last-64 I/O + MMIO rings, IDE/NV2A/IRQ counters, code bytes around final
EIP, and decoded kernel debug services (`[dbg] DbgPrint:`, `[dbg] LOAD symbols:`).

For visual chronology, add `--frame-series <path-prefix> <instruction-interval>`. The interval
must be an integer of at least 1,000,000 instructions; each PPM filename uses the actual guest
instruction count at which it was sampled.

For an offline-path experiment, add `--net-disconnected`. This changes the modeled PHY link
state after device initialization and before guest execution, so it behaves as a pre-boot
link transition rather than a verified cold-reset cable-out state; it is mutually exclusive
with the UDP Ethernet peer. For a bounded decoded-command witness, add
`--pfifo-clear-trace <32-bit-color-address>`. It arms at the first successful matching clear,
then selects the first subsequent Kelvin BEGIN, retains the 32 methods ending there, and
records through its matching END under a 4,096-record cap. Every entry includes object class
and parser-time GET/PUT. It does not capture instruction time, semaphore/notification
acknowledgement, or flip completion.

## The bring-up loop (how work proceeds)

Run → the machine fault-stops loudly on the first unmodeled register/port/command (or a
thread visibly spins) → characterize (against the live console when possible, else xemu as
boot witness) → model faithfully → rerun. Never a silent default; every xemu-default-0 or
stand-in is documented in the header of the device that carries it.

Deliberate documented stand-ins currently in place (fine for boot, replaced later in A4):
- NV2A PFIFO is a synchronous recorder with semaphore DMA execution; most recorded methods
  still have no CP/raster side effects. Guest-programmed framebuffer selection, host
  readout/presentation, Kelvin pitch-surface clears, and indexed vertex fetch/primitive
  assembly plus programmable and exercised fixed-function transforms work. The witnessed
  pitch-Z16 depth-only, untextured swizzled radial-color, and both witnessed indexed
  shader-`0x10c60`/`0x10c61` textured triangle profiles rasterize. Inline arrays execute
  through transform/assembly and their pitch/additive color profile writes guest color.
- APU has the load-bearing voice-list, VOICE_OFF, timed idle-voice FE trap, notifier, and IRQ
  lifecycle, but still no DSP/audio synthesis. ACI advances real PCM descriptors and IRQs
  headlessly but produces no host audio.
- OHCI0 currently attaches a deterministic Controller S XID directly on root port 1.
  Periodic/control/bulk ED scheduling, TD buffer transfer/retirement, HCCA done writeback,
  standard enumeration, XID descriptors, injectable input reports, and rumble output work.
  This flattened topology is not hardware-faithful: a real Controller S first exposes an
  embedded three-port hub, with XID on downstream port 1 and memory units on ports 2/3.
  `--interactive` supplies live keyboard state, but the retail guest never reaches an
  endpoint-2 poll in the flattened model; OHCI1 remains empty.
- NVNet implements the register/PHY/MDIO/link/IRQ floor plus TX and RX descriptor engines.
  An opt-in nonblocking UDP layer-2 tunnel provides external packet transport; without it,
  transmitted packets are explicitly counted and discarded. Higher-level NAT/bridging is
  intentionally outside the device model.
- PIT tick cadence approximated from retired instructions (1 IPC assumption).

## Hardware oracle (live console)

Xbox v1.1, Conexant encoder, softmod XBMC. `xlle-agent v0.2` on `192.168.4.81:4380`.
Driver: `python tools/xbox_agent.py --host 192.168.4.81 {ping|info|read|exec <file.bin>}`.
- EXEC runs a small int(void) payload, returns `RET <eax hex>`. Payload generators from this
  session: `pci_probe.py` (PCI config reads) and `smc_read.py` (SMBus reads) in the session
  scratchpad — regenerate as needed; both validated against known registers.
- STRICTLY one serialized command at a time; NEVER parallel-subagent the console.
- Agent blocks kernel-space reads; MMIO + low RAM fine. Ask the maintainer before assuming
  the console is powered on.

## Key kernel addresses (this image, retail 4983)

- KPCR 0x8003da3c; PrcbData.CurrentThread at +0x28 (phys 0x0003da64, polled by diagnostics).
- Idle thread KTHREAD 0x8003d8f0; phase-1 0x80001bf8 (aka 0xd0008bf8); AV worker 0x80001a98.
- Idle loop 0x8001e3ce; clock ISR 0x80035630; IDT base 0x8003dcc0.
- HAL SMBus interrupt-driven wait ~0x800176e9-0x8001783a; KeStallExecutionProcessor loop
  0x80056ca8-0x80056cb9; resolved AV fence spin 0x800570dc; resolved APU completion spin
  0x800637d3-0x800637da; resolved flip throttle caller 0x80052f71 (counters
  0x80061998/0x80061fe0). First observed dashboard user EIPs include 0x0046671d and
  0x0011f09e.
- KTHREAD offsets (nxdk xboxkrnl.h): State +0x2c, Priority +0x32, KernelStack +0x24,
  WaitBlockList +0x58; KWAIT_BLOCK: Thread +0x08, Object +0x0c, Next +0x10.
  States: 1=Ready 2=Running 4=Terminated 5=Waiting.
- Kernel VAs identity-map 0x80000000→phys 0 (+ 4MB large pages); thread stacks live at
  0xD0000000 (NOT identity — use `CpuFloor::translate()`; uc_mem_read/write are physical).

## Hard rules

- Faithful LLE floor + opt-in HLE. Dynamic engine. No static recomp. No HLE-kernel. No
  stubs. Fault-stop loudly on anything unimplemented (documented xemu-default-0 exceptions
  only). Real hardware is the oracle; xemu is a reference/witness, not truth.
- Most complete option always wins over the faster/surgical shortcut.
- Always-on ring buffers for observability; never arm-then-run or pause/step to observe.
- Copyrighted bytes stay in `machine/` (workspace parent), never committed.
- `.gitignore` gotcha: the rule must stay `/machine/` (root-anchored) or `src/machine/`
  silently vanishes from git.

## Next steps (priority order)

1. Continue the passive mode-8 evidence census and admit only the remaining exact witnessed
   visual families needed for dashboard fidelity. Preserve the bounded selector that produced
   the first recognizable menu frame, and compare output with a real-Xbox reference rather
   than generalizing from xemu's host renderer.
2. The live keyboard-to-XID mapping and OHCI protocol are landed. The configuration read is
   now directly proven as `R=0`, 32/80 bytes, CC9, with a halted ED; preserve those OHCI
   semantics. Capture the real Controller S embedded-hub descriptors/topology on the v1.1
   console, then model root → hub → downstream-port-1 XID and prove a real endpoint-2 report
   causes a D-pad/A/B menu-state change. Do not synthesize a guest poll.
3. Exercise the landed NVNet UDP tunnel against a useful peer/bridge as navigation requires;
   RX descriptor DMA is proven through the retail driver.
4. Longer term (A4 proper): extend the landed recorder into the required CP/method subset and
   software rasterizer (docs/ROADMAP.md).
5. Keep validating modeled values against the live console; reconfirm xemu-sourced defaults.

## Session hygiene

- Update STATUS.md (newest entry first) each milestone; commit per milestone with the
  existing message style (`git log --oneline`).
- Temp payloads/scripts go in the session scratchpad, not the repo.
