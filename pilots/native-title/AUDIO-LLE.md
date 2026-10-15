# Bounded original audio contract

Tracked by `beads-xbox.1.113`. Audio is the active independent offline family;
the original-scheduler/GPU startup experiments are not prerequisites. The family
remains **open**, and the overall burndown is **6/9 qualified**. Earlier dated
observations retain their historical counts. Hardware access has resumed after
the dashboard migration. Physical EP input/output and delayed acknowledgement
now admit the original input, linear output and interleaved stereo FIFO0 routes.
All three original configurations now sustain32 consecutive events with silence
and distinct-channel signals. Independent PCM arithmetic and AC-3 decoding
pass representative numerical checks. This uses the privately captured EP high-Y
image and physically matched FIFO1 source401F route. Without that image,
selector2 retains the ten-transfer Y:0FC9 stop. The earlier malformed-descriptor diagnosis was wrong:
400603/1001 is an intentional packed stereo FIFO descriptor. See the bounded
closing checklist for the current evidence and timing/reset requirements.

The native title continues to select its real XAudio2/DSP implementation.
The audio LLE floor still needs GP/EP startup, active FE ordering and whole-driver
integration before it is a qualified selectable title backend. Processing scope
follows the reached voice census in the bounded closing checklist. Existing
audible HLE output does not remove that requirement.

## DSP host-control source audit - 2026-10-14

Tracked in `.113.9`. The connected title's next stop is the first GP FF00
write. Review of the retained unchanged original setup identifies this finite
group before implementing or probing it. All addresses below are DWORD CPU
MMIO addresses; the observed original write value is not a register definition.

| CPU address | Original instruction | Written value / ordering | Still required |
| --- | --- | --- | --- |
| FE83FF00 / FE83FF04 | 17B883 / 17B889 | Both0, before GP reset1 | Effect of the zero writes and relation to held GP state |
| FE83FF10 | 17B953 | `(owner & ~8) \| 1`, after GP reset1 | Which bits affect hardware; the authored owner30000000 explains the prepared value30000001 and is not a hardware constant |
| FE83FF14 | 17B958 | FF, before GP reset3 | Write effects and CPU status/readback semantics |
| FE85FF00 / FE85FF04 | 17B9F0 / 17B9F6 | Both0, after EP reset1 | Effect of the zero writes and relation to held EP state |
| FE85FF5C | 17BAF3 | 0, before EP FF10/FF14 and reset3 | Effect of the original zero write |
| FE85FF10 / FE85FF14 | 17BAFA / 17BB05 | 1 / FFFFFFFF, before EP reset3 | Control/status behavior and the supported write masks |

Pinned xemu4820884 `hw/xbox/mcpx/apu/dsp/gp_ep.c` routes these offsets through
generic register storage; only FFFC has an explicit reset transition there.
That implementation supplies no independent explanation of these control
effects. In particular, do not admit the group by copying its generic storage
fallback or by expecting every register to read back the written value.
The retained physical EP configuration observation already distinguishes CPU
FF14=100 from DSP C5=0. Neither view can be substituted for the other.

The existing maintained APU owns the CPU aperture and binds reset callbacks
to the sole GP/EP CPUs. Extend that ownership when semantics are established;
do not add a second register file or DSP execution owner to the title adapter.
Batch the remaining host controls with the reached FIFO/setup dependencies,
then connect shared-clock execution before another title run. Active execution
and FE ordering remain separate required behavior, not waived by held-state
register evidence. Source evidence is retained in
`build/blinx/audio-reset-startup-20261014/original-control.aot-private.txt`
and `tools/native_ep_startup_reference.py`.

This was source review only while the owner was gaming: no builds, tests,
title launches or hardware operations. No new qualification pass or deferral
is claimed; the overall count remains6/9.

The subsequent light physical batch supplied32 stable read-only observations
and the original zero-write readbacks. EP FF10=1 read1, but a following0 still
read1; the strict batch stopped and remote dashboard/SMC recovery completed.
A separate original-order prefix with both cores held observed GP
FF10=30000001 reading1, GP FF14=FF retaining its pre-store0 and EP
FF14=FFFFFFFF reading0 after an initial100. The last result contradicted the planned retained
status, stopping that batch before final context reads. Neither stopped batch
is a whole-prefix qualification pass.

After a second remote recovery, an isolated one-store comparison read EP
FF14=100 immediately before FFFFFFFF, then0 twice afterward. All nine surrounding
control words remained exact across21 reads; CPU GP/EP FF10 stayed0, both
cores held and setup clocks stopped. This establishes the observed CPU status
bit8 clear without intervening reset/control writes. Other bits and active
effects remain open. The probe is responsive, with EP FF14 now0 and no pending
recovery or allocation ownership. Tiny payload checks execute183 cases across
the four fixed programs; they verify access/return mechanics, not register
semantics or maintained LLE behavior.

These observations reject generic last-written-value storage and supply the
next CPU control/status comparisons. Earlier clock-event evidence already
observed CPU FF10 changing1to0 when a setup event arrived, but a general alias
with DSP C4/C5 is still unproven. Connect the supported CPU requests/status
effects to the existing APU/GP/EP event ownership, preserving the distinct
status views, then qualify the whole original startup/shared-clock group.
Private evidence and recovery receipts are in the probe worktree under
`results/dsp-host-controls-20261014/`; sanitized details are in its
`docs/DSP-HOST-CONTROLS.md`. No production behavior changed and6/9 remains.

## Original codec initialization connected - 2026-10-14

Tracked in `.113.29`. Physical vendor574D/4C09 and ordinary zero register
readbacks match [Wolfson WM9709, pp9/13](https://consolemods.org/wiki/images/7/7e/WM9709.pdf).
The maintained ACI now represents that fixed48kHz DAC, replacing the generic
SigmaTel mixer/rate state. Ready GLOB_CNT2/GLOB_STA00300100 are an explicit
post-kernel handoff, not cold-reset defaults. Original PCM/SPDIF reset/base/
zero-index setup and stopped format02000000/02000002 use the existing owner.
Active SPDIF, link resets and active/nonzero CIV rewrites remain guarded.

The physical direct-CAS follow-up exposed asynchronous completion after a mixer
write. Remote recovery succeeded; a bounded experiment then observed completion
after one original20us wait. The shared guest clock now services pending commands
at the nominal next48kHz link boundary. CAS reads do not advance time or force write
completion. A synchronous NAM read returns its response and releases CAS; its
unmodeled CPU bus stall and exact link phase are documented in AUDIO-D10.

Three original codec helper groups create six owned rings and destroy all their
allocations; three real1000-wait semaphore failure branches also return. No
enclosing IRQ-registration result is fabricated by that source fixture. All42
targeted checks pass. The changed muted title run crosses the enclosing original
codec setup/IRQ registration and stops at GP FE83FF00=0 after105.5s. It captures
zero PCM/stamps. Required next work remains DSP host controls/shared execution,
active VP/FE behavior and connected output. Family count stays6/9;29 retained
question IDs contain28 deferred scopes. HLE processing is unchanged.

## Complete original VP initializer - 2026-10-14

The original stopped VP initializer now returns through shared native/source
admission, including its fiveFFF fields, packed submix configuration and real
owned input-SGE publication. Physical differential observations establish the
nonsequential field mapping and distinguish VPSGE input from GPF output.
Native integration checks the actual table writes across three allocation/free
lifetimes. All38 targeted checks pass; three unchanged original initializer
executions contribute42 writes,three exact input publications and768 voice
self-links. One changed title run crosses FE8202A4 and reaches AC97 read
FEC0012C after64.625s, with zero PCM. HLE processing is unchanged.

The earlier zero readback inherited FE1510=1. Physical held/release observations
show that clearing this original startup control permits pending methods to
publish even with SE stopped. Runtime held queues and active voice/DSP fence
ordering remain guarded under A04; they are not supplied by immediate fake
completion. Non-null field consumers and active method access also remain
guarded. See the [current evidence and limits](../../docs/BLINX-QUALIFICATION-CLOSURE.md#complete-stopped-vp-initializer-and-ac97-boundary---2026-10-14).

## Original VP mappings and method boundary - 2026-10-14

The original VP prefix now routes six mappings through the same admission
function used by the native device. Unnamed2038/203C retain stopped, aligned
physical addresses; their active consumers remain guarded. Original allocation
and instruction evidence spans three lifetimes,18 mapping writes and768 voice
self-links. The existing SSL table mapping is connected. All37 targeted checks
pass, and a changed45.828s title run reaches the first unknown method2A4=FFF
with zero PCM. HLE processing is unchanged.

Physical mapping readbacks match, but the input-SGE method pair returned
immediate zero instead of the expected pointer. No later probe operation or
restoration write followed. That result cannot establish synchronous completion
or settle method timing/semantics. The owner-authorized dashboard SMC restart
and remote probe return restored the stopped controls and replaced mappings;
no manual console action remains pending. See the
[evidence and remaining required work](../../docs/BLINX-QUALIFICATION-CLOSURE.md#vp-mapping-transport-and-unresolved-method-ordering---2026-10-14).

## Stopped initialization and VP mapping boundary - 2026-10-14

The actual original stopped initializer now executes against the maintained
canonical register owner. Its23 configuration fields and three FE control
values have independent physical readback. The pre-video71F8F capture and
original kernel/control helpers establish the explicit warm FE handoff.
Configuration admission is limited to stopped/halted startup and original
values; active access/effects, unknown encodings and FE completion remain
guarded. No second register store or fabricated processing owner is introduced.

All36 targeted checks and three live original helper/initializer lifetimes pass.
The final connected title crosses FETFORCE0 and stops at FE802038=02F80000 with
zero PCM, after57.453s. The next batch includes the actual VP setup17B79F plus
the remaining DSP host controls; the retained65-write inventory now includes
that previously omitted VP routine. See the
[source, physical and title evidence](../../docs/BLINX-QUALIFICATION-CLOSURE.md#stopped-initialization-batch-and-connected-vp-mapping-boundary---2026-10-14).
This closes stopped transport, not the whole audio family. HLE remains unchanged.

## Physical XGSCNT control - 2026-10-14

The original initializer's stopped zero write and sequencer mode1 now control
a retained sample counter on the sole APU owner. Physical checks establish zero,
run, stop and resume; modes2/3 retain the counter with FE halted but their active
processing remains guarded. Warm handoff readback requires the original zero
initialization rather than borrowing absolute machine uptime. Reads cannot
advance time; nonzero/running/partial writes and invalid clocks reject.

All34 targeted checks pass, including shared-clock lifetimes and compiled
ISR/DPC reads. The physical/reference checker binds nine matching volatile
writes to retained original control execution. Exact edge phase is AUDIO-D09;
start/stop/reset behavior is implemented, not deferred. See the
[closing evidence](../../docs/BLINX-QUALIFICATION-CLOSURE.md#physical-sample-counter-gateresetresume---2026-10-14).
The earlier title stop remains FETFORCE0; no new title/playback run is claimed.

## Original reset/bootstrap transport - 2026-10-14

The initialized-kernel profile now supplies observed GP0/EP1 and routes exact
DWORD reset0/1/3 (EP1/3) to the existing sole DSP CPU owners. Release fetches two
actual guest SG pages into the existing P bank; repeated release/read does not
reload or execute. Failed complete fetches publish no partial P image. Existing
counter/control/pending-work reset guards remain. Unsupported NMI/abort and
overlapping transactions fail before device effects. Host bank access while
held is not admitted by the native adapter. Exact hardware boot latency and
warm resets after active processing remain separate qualification questions.

`native_audio_reset_reference.py` executes three original driver setup and
destruction lifetimes, records reset1/3 plus the later host-control requests,
and captures all six original bootstrap images at their actual release writes.
The MMIO observer matches12,288 resulting P words exactly. The original caller
and allocation floor remain authored; passive register storage is not claimed
as active device execution. Retained successful physical GP-marker and EP
startup captures separately verify release3 and2,048 loaded P words each.
Pinned xemu's reset-bit and mapped-bootstrap behavior is an additional reference,
not the source of the physical results. No external implementation is copied.

Six component checks and25 native/compiled-MMIO checks pass. The new component
exercises six CPU lives/twelve boots, actual instruction markers, noncontiguous
SG pages, retained banks, failed-fetch atomicity and guarded history. Native
tests traverse real pinned-memory DMA and verify released owners remain retained
when unqualified shared-clock execution is requested. The changed title passes
initial reset, then stops at FE801500=0 after52.313s, before connected bootstrap
release or PCM output. Next is the grouped FE/host-control startup contract,
then shared-clock execution and active FE completion. Evidence:
`build/blinx/audio-reset-startup-20261014/`. HLE unchanged;6/9 and27 deferrals.

## Startup FE control and connected GP-reset boundary - 2026-10-14

`initialize_kernel_audio_handoff` establishes the pinned initialized-kernel
SECTL/IEN contract and retained physical idle FE control1 in one fresh APU.
It grants only startup control read/write access; the captured idle-fence
contract remains separate. Binding unused held DSP owners preserves access.
Processing enable, traps, VP/host-bank writes and DSP transport revoke it;
restoring register values cannot erase history. Partial/crossing operations,
notification address/data and active control remain guarded. Reads and control
release perform no RAM publication, IRQ, voice completion or DSP work.

The source check binds the retained held-window payload result and identity,
executes nine original17BC0C RMW calls across three allocation/destruction
lifetimes, and executes all three complete17BF66 initializers. A distinct
kernel control seed with high bits set survives init/retirement: only bit0
comes from those RMW calls; the other zero bits come from the physical idle
observation. This does not establish general cold-reset or active FE behavior.

The23 native integration checks pass. The direct FE suite retains96 idle
publications and covers120 overlapping transactions plus startup history and
notification rejection. Failed underlying device reads preserve the caller
value; a private candidate restoring the old assignment fails that check.
One connected title run now reaches WRITE FE83FFFC=0 after65.5s (exit2),
with zero captured PCM bytes/stamps. Next is GP/EP reset/bootstrap and active
FE ordering. Full FECTL/device startup, connected output and lifecycle/movie
timing remain open. HLE processing and6/9 families/27 deferrals are unchanged.
Evidence: `build/blinx/audio-fe-startup-20261014/`.

## Initialized kernel SECTL and interrupt mask - 2026-10-14

The native LLE owner begins after the pinned4983 kernel's initialized idle
handoff. Actual original800654DC writes SECTL0 then7 and IEN=AFA; original
80067442 idle retirement leaves7/AFA with global interrupts disabled and
GP/EP reset registers0/1. Four incoming SECTL seeds execute to natural returns
with353 distinct original instruction identities. The final fields agree with
the retained physical probe-entry capture before video/network initialization.

The maintained `native_audio_handoff_reference.py` also executes original
17BC9E/17BBD5 controls and the complete17BF66 initializer over three genuine
driver allocation/destruction lifetimes. Title initialization replaces IEN
with D8. The handoff is explicitly the initialized-kernel profile; warm apps
can change that mask. Kernel IRQL/empty software ownership and passive MMIO
storage are authored fixture inputs. Active FE completion, DSP reset timing,
full boot and whole title startup are not supplied by this source check.

`audio_device_lle.cpp` applies7/AFA before publishing its mappings, permitting
the two aligned DWORD reads through the existing sole register owner. General
cold-reset state is unchanged. Three device lifetimes and22 focused integration
checks pass, including passive reads, source-derived RMW values, unsupported
width and existing reset/unknown-access guards. HLE processing is unchanged.
Private evidence: `build/blinx/audio-startup-context-20261014/`.

The connected title crosses both former initial reads. The final changed run
exits2 after53.047s at DWORD READ FE801510, retaining the strict FE control
guard. Its log and executable identity are in `title/attempt3/`; the current
binary is retained at `title/bin/`. One preparation-only failure exposed a
configure-only HDD path in the new IO library and was corrected before these
audio observations. Runs use the retained isolated persistent test disk and
private EEPROM/config/storage. No connected PCM or draw is claimed. Next:
the subsequent FE-control checkpoint above, then DSP startup and active
ordering;6/9 and27 unchanged.

## Sustained original EP and numerical output - 2026-10-14

Six fresh driver lifetimes cover32 events per configuration for silence and
distinct431/653/887/1103/1327/83Hz signals. Every event returns naturally to
the poll with acknowledged status and no pending work. Selectors0/1 complete230
transfers; selector2 completes1198. Input windows alternate by the original
P:01C6 write to P:0116; all other directory fields remain exact. Output rings
wrap, repeated idle/time/read calls create no work, and actual original driver
destructors release all allocations. The signal and setup-clock loans are
explicit; original GP production and codec/title consumption remain separate.

`qualify_native_ep_output.py` independently evaluates the original PCM matrix:
all65536 compared samples match exactly across both PCM settings and signals.
It extracts five complete AC-3 frames per signal from the IEC61937 transport,
checks burst padding/spacing, and uses FFmpeg with CRC checking and fatal decode
errors. Both streams decode as640kbps48kHz5.1(side),7680 frames each. Distinct
tones retain channel assignment and gain within1% on the five full-band
channels; representative LFE gain uses10%. Other-tone leakage is below2%, and
decoded silence is below2^-13 full scale. Phase is recorded without claiming
hardware latency or exact LFE filter behavior. The last incomplete burst is
reported separately and excluded from decoding.

Five independent negative controls reject wrong channels, substituted silence,
padding corruption and a payload CRC error. Eighteen retained startup/event/
revocation cases still pass with actual destructors, including the absent-image
guard. No production/HLE change, hardware operation or playback occurred.
Evidence: `build/blinx/audio-ep-sustained-20261014/`. Required next work is initial
SECTL/startup, shared-clock/reset and active FE ordering, then connected title
output/stop/reuse/movie timing. Count6/9 and27 precision deferrals remain.

## Physical FIFO1 source and first-event completion - 2026-10-14

Ordinary FIFO1 control4622/source001F copies24 distinct samples. Source401F
produces zeros despite distinct X data at the low address; source441F also
produces zeros, rejecting literal-value packing. The exact original1498-sample
source401F transfer is physically matched, including its2996-byte CUR advance,
untouched alternate destination/FIFO0, source/canaries, allocation and bootstrap.
All four hardware owners are retired through separately authorized recovery.

The maintained DMA uses its existing packing/FIFO/failure owners for ordinary
FIFO1 X output and the measured401F zero route. The broader flagged source map
remains unknown and guarded. Three physical programs match on policies1/2/4;
wrong-output controls reject, and ring-wrap/partial-write/bank-edge/adjacent-mode
checks preserve ownership. No HLE change or synthetic completion is introduced.

With the private Y image, all three original settings complete their first
event and return naturally to the real poll. Selectors0/1 complete8 transfers;
selector2 completes36, with18 output nodes and22 RAM writes. Its final nodes
are1498 zero FIFO1 samples,220 ordinary FIFO1 samples and128 linear output
words. SP0, acknowledged frame, C4=1, no pending work and later unchanged
polling are checked. Sustained events, independent codec numerics, shared
clock/reset/SECTL, active FE and connected title output remain required.
Evidence: `build/blinx/audio-ep-fifo-source-20261014/`; count6/9 remains.

## Physical EP high-Y lookup - 2026-10-14

Physical Y:0FC0..0FCF reads are stable and distinct from known low-Y RAM;
the original indexed Y:0FC9 read agrees. A separate fresh observation captures
Y:0100..0FFF into owned internal outputs, preserving the bootstrap and source
allocation. The zero0100..07FF interval remains guarded. Populated0800..0FFF
is supplied privately as a2048-word image, copied once before EP execution
into a separate immutable CPU lookup owner. No writable RAM, host or DMA
alias is added, and no board bytes are tracked. Physical write behavior and
cross-console image identity are not established.

The actual small physical program matches all99 fields/data and full banks;
wrong-image, missing-image, write and bank checks reject as intended. Eight
retained APU regressions, both physical stereo programs and eighteen fresh
original setup/event/revocation outcomes pass. Probe validation covers four
methods/six synthetic actual-DSP cases. Both physical allocations are retired
through separate authorized remote recovery with fresh PING/INFO.

With the image, unchanged selector2 completes33 transfers/15 output nodes/
18 RAM writes and produces nonzero intermediate state from zero shared input.
It stops at four-word FIFO1 descriptor `41A2,4622,5DA,401F`. Original helper029A
deliberately combines N4=1 and R0=1F into source401F; its DMA interpretation
remains unresolved and guarded. This is original execution and exact transfer
evidence, not numerical codec/output acceptance. Exact timing, reset, SECTL,
active FE and connected title output remain required. Evidence:
`build/blinx/audio-ep-y-lookup-20261014/`; count6/9, precision27, HLE unchanged.

## Physical EP interleaved FIFO - 2026-10-14

Original helper2B1 constructs a four-word FIFO descriptor. Control400603 selects
PCM16 output, planar channel stride256 and interleave; count1001 represents256
frames and two channels. Physical400403/400603 pairs both transferred all512
samples exactly, advanced the existing SG-mapped FIFO0 cursor by1024 bytes and
preserved source/canaries, alternate destination,8704 immutable owner words
and2048 P words. The maintained CPU matches both programs under DMA policies
1/2/4 through the existing DMA, packing and FIFO owners. Other interleave
shapes remain guarded; byte correctness does not establish busy/ACK timing.

Fresh original selectors0/1 return to PC0031/0032/SP0 after seven inputs and
one FIFO output. Selector2 also performs78-word linear output and then reloads
2146 P words, reaching original `MOVE Y:(R0+N0),A` at031E with R0=0FC2/N0=7.
Y:0FC9 is outside the admitted EP Y bank and remains guarded. The first-event
fixtures have zero shared input/output; nonzero original processing and the
connected title route remain required. Exact physical transfer patterns,
ring/failure/adjacent-mode checks and fresh original lifetimes are retained in
`build/blinx/audio-ep-fifo-20261014/`. HLE is unchanged; count6/9 remains.

## Physical EP configuration and original tail - 2026-10-14

The hardware batch's five matched write/readback pairs establish the reached
empty configuration: C5 acknowledgement, CA0, D7=0 and D5/D4=4000. The model
now supports only that CA profile and exact EP empty-list seed, and uses the
observed initial D6=0 instead of an unsupported STOPPED default. Configuration
during active DMA still rejects. No GP CA support or EP wake is inferred.

All34 physical fields and2048 loaded P words match on the maintained EP CPU,
using initial D5=4000 as explicit captured input. A wrong CA expectation fails.
The unchanged original02F3 tail also executes to RET through an authored
caller with exact register state and preserved RAM/banks. Full original entry
still yields at02F3/SP2; this helper call does not replace a real wake.
The existing30 EP packets,30 GP frames and retained guards pass. Evidence:
`build/blinx/audio-ep-config-20261014/`. HLE processing is unchanged.

## Original EP first yield - 2026-10-14

The production EP owner now admits the exact initial C5=FFF clear only while
executing its bootstrap with no DMA history, pending DMA or events. C5 uses
the shared write-one-to-clear contract, independently represented for GP and
EP in pinned [xemu peripheral semantics](https://github.com/xemu-project/xemu/blob/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/mcpx/apu/dsp/dsp.c).
The original prefix then executes its own C4=1 and yields at PC02F3/SP2. There
is no synthetic resume: CA=0, D5/D4=4000 and D7=0 are after that yield and have
not executed. The broad FFF clear in a nonempty or warm context remains unsupported.

This is bounded empty-state execution, not a physical reset/event measurement.
The detailed original checker still verifies all workspace/directory P effects
and unchanged X/Y/GP/source RAM. External FFF acknowledgement and clearing a
real DMA EOL through this bootstrap exception are rejected. The30 EP packets,
30 GP frames and counter/descriptor guards pass. Fast HLE has no processing
change. Evidence: `build/blinx/audio-ep-first-yield-20261014/`.

Fresh hardware context after remote launch is stable but differs from the old
probe baseline: the new dashboard leaves EP scratch/FIFO tables and IEN00D8.
No inherited pointers were dereferenced and no qualification device writes
were made. The next required work is the original EP wake/CA/D5 context,
reset/cadence and threshold, followed by active FE and connected output.

## Original defaults through composed voice processing - 2026-10-12

`tools/native_audio_default_reference.py` executes the complete original settings
and derived voice constructors, ordinary configuration helpers and live headroom
setter into maintained source DMA, rate conversion, amplitude and ordered sends.
PCM16 stereo and Xbox ADPCM mono at44100/48000 each pass three fresh device lives.
Original headroom600 produces independently expected gain10^(-384/1280); the real
headroom0 setter restores unity scaling. A complete public frequency call proves
creation44100 versus effective48000 and original frequency0 restoration.

Eight persistent headroom/filter rejection cases preserve RAM/cursor state;
twelve deliberately wrong gain expectations reject the output. Original Stop0
retains its actual deferred work node. Real original cancellation, inactive trap,
unlink and caller-owned detach/rebind retire it before the new source is used.
Independent negative PCM/ADPCM interiors reject old source/history; actual finite
drain publishes DONE before the original completion consumer returns. Existing
tone amplitude/RMS tolerances0.001/0.0003 and finite SRC count tolerance1 frame
remain unchanged. No production APU correction was required.

Root verifies103 unique artifacts and51 whole original function extents, then
replays the portable tool once:12 lives,8 guards and12 wrong-gain controls pass
in0.391s. Private proof:
`build/blinx/audio-default-composition-20261012/root-portable/qualified-root.aot-private.json`,
SHA256 `8f5053884eb59c6d931f0f9014375248418bb2768c16e9d5669169653c412a48`.

Assigned voice/source/notifier storage, constructor-zeroed RAM, coherent clock
and synchronous PIO capacity are explicit component preconditions. Full original
allocation/configuration emission, ISR/DPC scheduling, GP/EP/AC97 startup and title
output remain separate closing work. This establishes the ordinary bypass/phase0
processing contract; the final title census must identify any additional reached
mode. No new hardware, title/GPU run, audible output or HLE processing change.

## Executed original PCM positions - 2026-10-11

`tools/native_audio_position_reference.py` executes public176AB8 and its complete
original helper chain against actual maintained source CBO/MMIO. All168 cases
pass for PCM8/16 mono/stereo, rate conversion, full/partial loops and optional
output pointers. Playing read positions reflect source fetch/prefetch; the
full-ring write position is `(read + 32 * alignment) % capacity`. No output
frame counter substitutes for CBO. The stopped branch uses authored saved
fields and does not qualify the original Stop or finite-completion lifecycle.
Private generation proof `reference-cursor-contract-20261011/qualified-original-cursor.aot-private.json`
has SHA256 `5789775b03823bd94138180fc9b2d847126c6af17728eec44fc5abe96dbfddbf`.
The [shared clock/audio composition](../../docs/BLINX-MOVIE-STALL-AUDIT-2026-10-08.md#original-positions-and-composed-clockaudio---2026-10-11)
also passes host-delay invariance; full movie integration remains open.

## Whole native audio under the original CPU owner - 2026-10-11

The selected21 audio bindings now retain actual original address, extent and
SHA-256 as well as their real stack cleanup. The reviewed image's original
DSOUND code is preloaded; registration verifies resident bytes before sealing
CPU admission. Generated native/original wrapper bodies and the selected binding
record remain unchanged. Four additional original vtable entries stop before
inner Release or ECX scalar destruction can access shortened native objects;
whole selected public Release retains their native lifetime owner.

A shared original RTL critical section serializes audio, graphics and installed
media before native audio/renderer/VM locks. Actual original pool15/23/17 and
owned VM services resume the same original thread through the existing CALL/RET
protocol. Native completion callbacks retain host-owned chunk/PCM lifetimes;
this route does not invent original DSP readiness, interrupts or completion.

Three muted original CPU lifetimes perform36 real whole API calls: device/buffer
creation, full/wrapped Lock/Unlock, exact host PCM readback, settings,
detach/rebind and buffer/device Release. Pool/thread/handle cleanup, original
flags/x87/XMM, integer registers and stack cleanup pass. Three more contended
lifetimes prove audio/graphics exclusion and host-lock release before original
wake. Effects composition, compiled IRQ/DPC audio entry, recursive host locks
and inner object destruction remain explicitly guarded. No Play is used.

The combined generation passes **39/39 checks in8.90s**, zero skips, with105
frozen identities in
`build/blinx/audio-pool-component-20261011/qualified-final.aot-private.json`, SHA-256
`a83d4e224cb600a93ece499c007a2cdc7a65b678d7a7c447a91ea0a47f1cdbbb`.
This qualifies the original-scheduler/native-audio connection, not the whole
original GP/EP/control/cadence/codec/title-output gate. Those required behaviors
remain open. The family count remains **4/9**, with the same26 precision deferrals.
No physical Xbox operation or repeated audible/attract test occurred.

## Observed idle FE fence through original helper - 2026-10-11

The retained physical token observations now have an executable maintained
counterpart. A fresh device can import only the observed probe context:
FECTL=`00071F8F`, SECTL=`7`, FE control=`1`, GP reset=`0`. This is explicit
captured state, not a cold-reset default. Other MMIO writes, DSP writes or
CPU-owner bindings revoke this admission; a later register restoration or
snapshot import cannot erase intervening work. Busy-to-idle ordering remains
unqualified.

Exact DWORD control reads/writes and owned physical FEMEMADDR/FEMEMDATA stores
now operate in that context. FEMEMDATA enqueues one token. A separate serialized
device service performs the actual four-byte little-endian RAM write; CPU
polls and control release perform no publication. Service works with control
still `1`, matching the held-window observation, and after release to `0`.
Address/data reads, partial/crossing accesses, other control values, duplicate
pending data, pending rebinds, outstanding work and failed/reentrant DMA remain
guarded. No IRQ or voice completion is fabricated.

`native_fe_fence_reference.py` executes original helper `177DF0`, both original
control calls `17BC0C` and their critical-section callees over real captured
kernel mappings, actual kernel allocation/physical translation and device RAM
DMA. Across 12 contexts, nine fences complete at three independently scheduled
instruction-service intervals; three withheld-service cases retain the CPU
sentinel when the real helper returns. This establishes that its return alone
is insufficient evidence. Subsequent explicit service publishes the token
before that allocation is freed. Sequence wraparound, original stack/register
state, neighboring RAM, control restoration and allocator statistics pass.
There are 12 helper calls and 36 original control calls, with no replaced callee
or CPU token store. These service intervals are nominal policy, not silicon
latency measurements.

The focused APU batch passes **10/10 in 0.14s**. The new fixture covers 96 actual
idle publications over six lifetimes and 90 captured-context overlap guards,
plus cold-state, ownership/history, pending-work, RAM failure and reentry cases.
The original active-audio DPC audit still rejects at `17BC1D`, before a fence
store or completion consumer. This bounded slice closes `.115.1`; parent `.115`,
active ordering, cold initialization and full audio remain open at **4/9**.
The 26 safe precision deferrals are unchanged.

Private evidence is `build/blinx/audio-fe-idle-20261011/qualified.aot-private.json`,
SHA-256 `dcf4b3bafe96f5e03c771aef281a93f37f8f62972ec5f3c8b5f1fcc36caf7943`.
It binds 22 retained source/dependency files, actual binaries, original outcomes
and the failed test generations. Current APU reference DLL SHA-256 is
`148df223b8c49a4835e3b0e6581c483d8bf858e4750079585a0583e59bc14172`.
No Xbox request, attract/audio playback, native HLE implementation change or
whole-title LLE qualification occurred.

## GP FIFO transport - 2026-10-09

The maintained DMA engine now transfers real bytes through GP output FIFOs
0..3 and input FIFOs 0..1, using the separate FIFO scatter/gather table and
each ring's own BASE/END/CUR. PCM16 truncation and both PCM24 byte layouts
are implemented. Scratch and FIFO share packing and physical-page transport;
FIFO transfers leave the scratch-only descriptor fields unchanged.

[Published DSP research](https://xboxdevwiki.net/DSP) supplies the ring and
sample-lane contract. Pinned xemu `dsp/gp_ep.c` and `apu_regs.h` corroborate the
register/SGE layout; no implementation body was copied. The original NTSC
`0x17b870` routine independently shows GPFADDR/GPFMAXSGE setup, FIFO0 bounds
`0..0x2000`, and initialization of output cursors from their bases. That routine
was inspected, **not qualified as a complete executable owner**: its reset and
bootstrap accesses remain outside this transport. Phobos's audited APU README
describes its GP effects bypass/EP passthrough, so those paths were not adopted.

Validation includes **108 transport cases**, **12 guards**, and **16 device
lifetimes / 2,064 repeated transfers**. Tests compare all physical RAM, including
untouched scratch storage, noncontiguous page crossings, exclusive-end wrapping,
multiple ring laps, distinct input/output cursors and real completion status.
The reused MIT DSP core executes six authored instruction-program cases that perform
actual scratch/FIFO output and input through these peripherals. Their expected
sample bytes come from independent published examples, not native output.

Scope remains noninterleaved unit-stride transfers, at most 4,096 words, and
low-24-bit rings with 256-byte aligned bounds. Odd PCM16 counts, other formats,
unknown control bits and invalid ring state stop explicitly. A later page fault
can leave bytes already physically written; it earns neither a completed node
nor CUR advancement. Invalid-state silicon behavior and precise DMA latency
are not claimed. Original GP boot/framework, interleaved output, EP/AC97 and
whole-driver audio remain open. This is a new runnable floor component, not
qualification of the native effects shell or whole-title LLE audio.

Final validation: maintained APU **8/8 in 0.22s**, complete native/contract suite
**56/56 in 35.70s**, plus all prior original configuration/source/control and
30 IRQ cases pass. Reference DLL SHA-256:
`090cedf7bcaac1e5f9ff6d751ce0520781799aebd3ebc34bbf70cdd6b09e7f55`.
Logs and private fixture output use `build/blinx/audio-fifo-*`.
The final rebuilt native title remains exact SHA `76ff2a4e...ff12f730`, so the
preceding **190.578s / 18-capture** Blinx regression is retained without a
duplicate run. Stable game pin unchanged; audio stays open and the family
count stays **4/9**. DSP bit 9 `.114` and the FE fence `.115` stay deferred.

## Original IRQ5, unlink and deferred-work queue - 2026-10-09

`native_audio_interrupt_reference.py` connects the maintained APU to the
maintained dual PIC and reuses the display family's captured-kernel mappings,
interrupt entry and port transport. Original kernel allocation, interrupt
initialization/connection and actual Blinx ISR `0x17856e` execute unchanged.
The ISR handles the actual inactive-voice trap, invokes original unlink worker
`0x178d7c`, publishes its software status and queues original deferred work.
The fixture does not call that worker or manufacture its completion record.

**30 cases / 66 ISR deliveries / 30 original unlinks** pass: mono/stereo,
three fresh device lifetimes for each of combined notification/trap,
notification-before-frame, device-IRQ masking, original disconnect/reconnect
and command-stop cases. Interrupted registers/stack/flags and IRQL are restored;
acknowledging the reasserted trap causes no duplicate unlink. A second original
queue request is rejected while the real DPC is pending. Original cancellation
removes it once, and original allocation statistics return to baseline.

This intentionally validates delivery with the interrupted caller at
DISPATCH_LEVEL. **Deferred completion remains pending**, with its original
status/list state checked before teardown. Cancelling the DPC for fixture
teardown is not successful execution of that deferred work. Original derived
completion is still independently exercised by the earlier control fixture.

The `--barrier-audit` run lowers IRQL through the original kernel, enters the
real DPC `0x17838e` and reaches fence helper `0x177df0`. It stops at the strict
`0xfe801510` read, before the completion consumer executes. The helper toggles
that register's bit 0, writes FEMEMADDR/FEMEMDATA and polls actual RAM. Pinned
xemu describes its immediate FEMEMDATA write as provisional; Phobos, sp00nznet
and NoRain's audited APU cores share that assumption. They are not independent
proof of the fence's ordering or control ownership.

The maintained APU's older unconditional FEMEMDATA guest write is now removed.
The control/address/data writes fail before mutation, and the strict reference
continues to reject the control read. No guessed memory completion is admitted.
Root negative cases verify caller RAM and interrupt state remain unchanged.
The precise unresolved question is **`beads-xbox.1.115`**, deferred alongside
other silicon questions. Independent GP/FIFO and graphics work can continue;
do not repeatedly rerun this guarded DPC or the parked title startup.

### First owned-RAM FE observation - 2026-10-10

Under the owner's standing benign-write authorization, the running v0.4.0
agent executed one reviewed, compiled x86 payload. It allocated a fresh
4096-byte uncached page through the actual kernel, checked physical translation
and alignment, initialized a sentinel, and reproduced the original helper's
SET_CONTROL_BIT0(1), FEMEMADDR, FEMEMDATA, SET_CONTROL_BIT0(0) sequence. The
CPU did not write the token to RAM. The device token appeared on the fifth
post-release uncached poll. Control bit0 was restored to its observed original
value and actual readback `1` was verified.

The payload made exactly five MMIO stores, all to FE801510, FE801324 or FE801334.
The allocated page remains owned until restart, including on failure, so a
late device write cannot target reused agent/kernel memory. It was separate
from the EXEC code buffer. No voice, GP/DSP, GPIO, storage, flash, EEPROM,
boot configuration, upload or launch operation occurred. This observation
required no audio playback or emulator title run. Raw records stay private on
the G: recovery drive.

All **50 offline probe tests** pass. Eight new tests execute the actual
compiled freestanding payload against independent PE/API/device models,
checking exact allocation flags/arguments, bounded export reads, physical
alias checks, authorized store addresses/order, callee state, delayed/absent
device writes, page retention, changed controls and no host retry. The
payload was 888 bytes, SHA-256
`84d65df3bfbe6b3d0e457ea8886c75b7255001c6c4c3fe265b19246972edb8e4`.
The original helper was separately inspected: it sets bit0 from its Boolean
argument; its first call sets `1` and its second sets `0`.

This qualifies an observed basic transfer only in the current application's
context. Five polls are not a measured hardware delay. There was no observer
between the data store and control release, so this run does not establish
whether publication waits for that release. Nor does it establish which
queued voice/notification/DSP work must precede publication. Keep `.115` and
the maintained FE guard open until those admission/ordering requirements have
independent evidence and a working implementation. Exact source/limits are in
[`xboxlle-probe`'s FE-FENCE.md](https://github.com/mstan/xboxlle-probe/blob/feat/av-handshake-probe-20261009/docs/FE-FENCE.md).

The distinct held-window follow-up also succeeded. It made the same five FE
stores to a separate newly owned page, but sampled RAM up to 1000 times
between FEMEMDATA and this payload's control release. Actual control reads
were `1` before and after that window. **The token was already visible before
this payload cleared bit0**; the first subsequent post-release poll also saw
it. The original control was again restored and verified. Two pages, **8192
bytes total**, now remain owned until restart.

This positive observation rejects an absolute model that always defers basic
idle token publication until this control release. The control/RAM reads are
sequential; they do not atomically exclude other kernel writers or establish
busy FE/notification ordering. Do not replace the current guard with that
release-gating hypothesis. The maintained implementation still needs explicit
admission for known completed work plus evidence for any outstanding work.

All **53 offline tests** pass, including actual execution of both compiled
variants, pre-release and post-release publication, changed controls around
the window, finite timeout, failed restore and strict host variant decoding.
The held-window payload was 968 bytes, SHA-256
`ec86e06791f593803dc226b8496ec47ef47c20f3ad120bb144bd10c20ad5e062`.
The first payload/source remains committed independently for reproduction;
raw records and compiler/source hashes are private. No audio playback,
title run, FE retry or storage operation occurred. `.115` remains open.

### Direct device FE fence guard - 2026-10-11

The strict reference wrapper already rejects unqualified FE fence transactions,
but direct `McpxApu` reads previously returned register-file defaults and writes
overlapping byte lanes could bypass the exact-offset write guard. The maintained
device now rejects any read or write touching the control/address/data DWORDs
at1510/1324/1334 before changing registers, RAM or IRQ state. Failure is sticky;
the API's zero return on a rejected read is not qualified readback.

The direct-component regression fails against the previous device code. Nine
focused APU checks pass in0.15s with the corrected device, covering96 guarded
transactions, unchanged surrounding register bytes and caller memory, no DMA
or IRQ publication, and suppression of later voice completion after failure.
Adjacent nonoverlapping accesses preserve ordinary transport. Existing source,
rate, SSL, VP, DMA, FIFO and completion checks remain passing. The changed APU
library and exact source/test/failure generations are retained privately under
`build/blinx/audio-fe-overlap-20261011`.

This closes a guard bypass, not the fence's missing publication/ordering model.
Original DPC/consumer integration and `.115` remain open. The existing original
barrier observation retains its prior binary identity; it was not rerun. No
default HLE audio processing, title/attract/audio playback or hardware operation
changes, and the qualified family count remains4/9.

Reproduction (owned private inputs):

```powershell
py -3 tools/native_audio_interrupt_reference.py --xbe <owned-default.xbe> --snapshot build/blinx/io-boot-18m.ram.bin --library build/visibility-lle/libxlle_apu_reference.dll --output build/blinx/audio-irq-original.aot-private.json
```

Add `--barrier-audit` with a separate output to reproduce the expected nonzero
guard result. This is a bounded IRQ/queue qualification, not whole audio-device
initialization, working deferred completion, timing fidelity or LLE title audio.
The audio family remains open and the count remains **4/9**.

Final validation: maintained APU **7/7 in 0.28s**, complete native/contract
suite **56/56 in 35.71s**, the 30 new original IRQ cases, and all prior original
configuration/source/natural-completion/command-stop fixtures pass. The guarded
DPC reproducer still stops before the completion consumer. Reference DLL SHA-256:
`00f4382e6be49b83c9fe2b6bb0dffbc3b2b83368d31c3f647722797d671a2266`.
Evidence: `build/blinx/audio-irq-{original,barrier-guard,configuration-original,source-original,natural-original,stop-original}.aot-private.json`
and `audio-irq-{machine-build,root-tests,full-build,full-tests}.log`.
The fully rebuilt native title retains SHA `76ff2a4e...ff12f730`; its exact
**190.578s / 18-capture** regression from the preceding batch remains valid.
No duplicate title run or new LLE-title claim is made. Stable game pin unchanged.

## Finite-buffer processing and original completion - 2026-10-09

The current hash-bound native audio bindings expose DirectSound buffers and
effects, with no stream-object API. Original nonspatial initialization uses
`DATA_TYPE=buffer`; movie audio also rewrites real buffer storage. This makes
finite-buffer completion a more immediate integration step than SSL breadth.
SSL remains required wherever reached; it has not been waived for the console
or later titles.

`McpxApu::process_2d_voice` now connects real SGE source consumption, persistent
rate conversion, bounded envelopes and final 2D sends to static retirement.
The admitted original ordinary defaults are filter bypass and format headroom
7. Unknown filter/headroom, spatial, linked, multipass, SSL and phase-inversion
modes fail explicitly before source consumption. Pause/lock/inactive voices
remain stationary. Final samples reach the actual 32-bin destination before
ACTIVE clears, the real status byte changes and notification IRQ is raised.
Natural retirement does not invent a `VOICE_OFF` method or pad the destination.

**Timing scope:** this nominal reference retires after the converter's actual
finite output is drained, at a 32-sample processing boundary. Detecting drain
can take a final zero-output call. Pinned xemu retires while fetching the last
source samples; it does not establish our chosen drain/IRQ latency as silicon
behavior. Exact prefetch, interpolation and completion phase remain explicit
unqualified timing questions, deferred behind offline work. This batch proves
finite data/ownership and observable eventual completion, not sample-exact IRQ
timing, downstream GP drain, all-voice scheduling or complete title audio.

Validation connects independent original and native observations:

- Root `apu_completion`: **72 PCM/ADPCM mono/stereo finite cases**, 1..129
  source frames at half/unity/double rate. Exact nominal frame counts, final
  CBO, state/status/IRQ ordering, untouched unused output, no repeated
  completion, looping/pause/lock and unsupported-stage guards pass.
- Original control fixture `--kernel ... --natural`: **eight complete original
  initialization/start cases** consume 128 real samples and retire without a
  command-stop. The actual inactive-frame trap precedes original unlink worker
  `0x178d7c`; original derived consumer `0x17a34b` reads and resets the
  software completion record published by that worker. Stack/callee-saved
  registers and real guest/device list ownership are checked. No callee or
  result is substituted. Full ISR/DPC dispatch remains to be connected, and
  notifier payload accesses remain guarded to the qualified status byte.
- Native `pilot_audio_completion_lle`: **six actual XAudio2 captures**, mono/
  stereo at 24/48/96 kHz with 1,057 finite source frames and a distinctive
  final 32-frame tail. Integrated per-channel output agrees within three peak
  sample-frames plus 0.2% of LLE integrated output. Maximum measured area
  difference was 0.386 normalized sample-frames. Both outputs retain the tail;
  native status stops, unused bins stay silent, and a subsequent capture
  contains no repeated audio. Different finite-edge interpolation is permitted;
  this is not a waveform-exact comparison or host-time completion simulation.

Both maintained-machine and native builds succeed. Maintained APU tests:
**7/7 in 0.27s**; complete native/contract suite: **56/56 in 37.06s**. Earlier
original configuration/source/command-stop/settings fixtures also pass.
Reference DLL SHA-256:
`faed837429f0f553dedcb843d5a21dcb45ac1aca5e47f7f22c1b6d32b9a3c5d5`.
Evidence: `build/blinx/audio-completion-{configuration,source,stop,control,detach}-original.aot-private.json`
and `audio-completion-{machine-build,root-tests,full-build,full-tests}.log`.
The rebuilt native title SHA-256 is
`76ff2a4e836246508dac2992a77972aa552cfb8ff8ceb87950196530ff12f730`.
Fresh native regression `audio-completion-floor-attract` ran **190.578s**;
all **18 captures** were inspected: movie/title progression, demo gameplay,
Start interruption/loading, title return and demo reentry. Both scheduled
Start press/release pairs were delivered, with no logged STOP or audio fault.
The harness terminated the still-live process at its time limit; return code 1
is not a crash. This protects native playback and is not whole-title LLE audio,
cadence or endurance qualification. The stable private game pin stays unchanged.
The full family remains open: **4/9**. No new donor bodies or licenses are added.

## Original static-buffer stop and unlink - 2026-10-09

`McpxApu::voice_off` now implements the bounded ordinary-buffer stop route.
It reads the real voice state, clears ACTIVE, discards owned resampler history,
then writes record A's DONE status and raises the actual notification IRQ.
The voice remains linked until the original driver removes it. Source data,
format/routing, cursor and other voices are retained. Failed state/status DMA
does not publish a successful interrupt; a status-write failure can follow a
real stop and is reported as a terminal partial operation. SSL, linked and
multipass owners still stop at explicit guards instead of assuming record A.

The supported notifier contract is **status only**. The original exercised
reader tests byte 15 for value 1. Bytes 0..14 are preserved by this model and
are **not qualified hardware payload values or hardware preservation claims**.
The original-code fixture rejects any attempted access to those payload bytes.
This restores real static stop work without reinstating the withdrawn constant
envelope-information byte. Timestamp/envelope payload semantics remain open.

With `--kernel <owned kernel-live.bin>`, `native_audio_control_reference.py`
now executes the actual derived `CMcpxBuffer` vtable and **complete original
stop owner `0x179a44`**, including the original kernel ordinal-128 time read
and original command-submission callees. The time routine reads a coherent
fixed shared-clock input; this does not qualify clock generation. Original
`0x179e0e` observes pending/done through the device's actual shared notifier
RAM. A maintained setup frame emits the inactive-voice trap; complete original
unlink worker `0x178d7c` then updates the real list registers/voice memory,
software list and pending-stop flag. No callee, result or notifier is replaced.
The worker is invoked explicitly after verifying the real trap; the intervening
full ISR/DPC dispatch is still an integration requirement.

All **eight mono/stereo/envelope cases** pass, preserving ABI registers and
stack on every complete original call. Root `apu_stop` verifies state-before-
status-before-IRQ ordering, guarded failures, retained caller storage and
**32 stop/rebind lifetimes** without stale resampler output. The native audio
test additionally verifies both cursors freeze after a real Stop. Existing
native detach/rebind tests retain eight output/silence/storage lifetimes and
mono/stereo ADPCM rebinding; original settings detach/rebind remains five cases.
Phobos `bcccf8c` stop/lifecycle behavior was rechecked. Its host-time cursor
does not replace this sample/state evidence; existing native XAudio2 Stop,
FlushSourceBuffers and destruction already provide the matching host operations.

Validation: maintained APU tests **6/6 in 0.23s**, native suite **55/55 in
35.31s**, followed by the expanded native Stop test **1/1 in 0.89s**. All
earlier original configuration/source cases and the eight new stop cases pass.
Both maintained-machine and native builds succeed. Reference DLL SHA-256:
`a09888c9a13fd8915dd7eb3269aabd0127eeb1ba2f8231d2528f0105f675e968`.
The native title executable is unchanged at `67b1af85...05a8e600`, preserving
the previous **190.141s / 18-capture** Blinx regression. No redundant title
run, new whole-title LLE audio qualification or family closure is claimed.
Evidence: `build/blinx/audio-stop-{configuration,source,control,detach}-original.aot-private.json`
and `audio-stop-{machine-build,native-build,full-tests,final-native-build}.log`.

The next integration step was updated by the reachability audit above:
finite-buffer processing/completion precedes SSL breadth. `.88` and `.113`
remain open; **4/9** families remain qualified. Hardware-only questions stay last.

## Current GP memory/DMA batch and deferred question - 2026-10-09

The maintained APU now owns real 24-bit GP X, MIX, Y and P banks shared by
host MMIO and the DSP core. `apu_gp_dma.hpp` performs actual circular scratch
transfers through GPSADDR/GPSMAXSGE and physical 4-KiB SGE pages. The bounded
contract covers noninterleaved unit-stride formats 2 and 6, linked descriptors,
offset writeback, freeze/unfreeze, explicit STOP and end-of-list acknowledgement.
No status read invents completion; unknown registers, formats and modes stop.
Mid-transfer DMA faults can leave actual earlier bytes written but cannot
publish completed nodes or end-of-list. GP reset/bootstrap remains guarded.

The independently authored `apu_dsp_core.c` bridge reuses the already pinned
MIT mborgerson DSP core (`c60aeedb6a6014850204c71e53bc0cf5b3104222`). The new
`pilot_gp_lle_contract` executes an authored program whose real MOVEs start
linked output/input DMA, crossing nonadjacent physical pages in both formats.
The root `apu_gp_dma` test also covers circular wrap, four banks, frozen work,
invalid descriptors/pages, MMIO ownership and 64 lifetimes. This is executable
CPU/device integration, not retail GP firmware or a whole audio backend.

[Published DSP byte examples](https://xboxdevwiki.net/DSP) distinguish format 2
(24-bit sample in the high bytes of a 32-bit container) from format 6 (low
bytes). They exposed a native adapter error: format 2 previously used the
low-byte layout. Both directions are corrected in `audio_dsp.c`; a literal
byte assertion now tests the native adapter independently of round-trip output.
Pinned xemu treats these formats alike and labels the interpretation uncertain;
its implementation body is not imported. The current native original-graph
probe passes **1,501 blocks / 73,549 real DMA nodes**, with real impulse/tail
output and program bytes matched to the original-x86-decoded private image.
That checks retained native execution, not independent whole-graph correctness.

The separate maintained-LLE original-graph probe **does not pass**: its first
descriptor has transfer control `0x004bd0`, including unknown bit 9. The published
reference leaves that bit undocumented; xemu and the current native adapter
ignore it. Their agreement cannot qualify it. The floor rejects the descriptor
before data transfer. This question is **deferred as `beads-xbox.1.114`** behind
available offline work; preserve the reproducer instead of repeatedly rerunning
it or weakening the guard. No console interaction occurred. Existing hardware
evidence is preferred before any separately authorized measurement.

Build/run the root `test_apu_gp_dma`, and the native CTest
`pilot_gp_lle_contract`. To reproduce the open original-graph boundary, run
`pilot_gp_lle_test <owned dsp.bin> <private seed in hex> <original-decoded image>`;
it currently fails explicitly at that descriptor. This diagnostic's existing
six-output graph shell also does not qualify GP bootstrap, frame signalling,
output FIFO or EP. Its prospective exact native comparisons share the CPU core,
so they would not be independent proof of DSP instructions.

Focused APU/source/rate/envelope/GP tests pass **5/5 in 0.25s**; the complete
native/contract suite passes **55/55 in 33.55s**. Original configuration,
source and whole init/start-owner fixtures all pass again. Full maintained
machine and native builds pass. The fresh default-native Blinx regression runs
**190.141s**, with 18 inspected captures showing Demo, interruption/loading,
title return and Demo reentry. Both Start press/release pairs were delivered;
no STOP/audio-stop/fault was logged. The harness terminated the still-running
title at its budget, so this is neither an exit failure nor endurance/cadence
sign-off. No LLE-audio title run is claimed; family count remains **4/9**.

Private evidence: `build/blinx/audio-gp-*-original.aot-private.json`,
`audio-gp-{machine-build,native-build,full-tests,native-original-graph}.log`,
and `audio-gp-floor-attract/{run.aot-private.json,contact.png}`. The explicitly
failing graph probe is preserved in `audio-gp-original-graph.log`.
Reference DLL SHA-256:
`7eb8bd120ef77ffe412d03a0698545f4710342a2fe217df4deb19f73fe9224c1`;
native executable SHA-256:
`67b1af858416c4ed25876eb0002fd373605321212044e4cdba13423505a8e600`.
The stable private Blinx repository pin remains unchanged.

Continue offline with original control/SSL/notifier ownership and remaining
voice scaling/filter processing. GP bootstrap/FIFO/EP remain implementation work; only
the specifically unresolved bit and other silicon-only observations go last.
Do not make the unknown bit a prerequisite for these independent services.

## Executable configuration floor

`src/machine/apu.hpp` now applies selected-voice configuration methods to the
actual guest physical voice table. Routing/format, envelope/filter parameters,
three packed volume words, pitch and buffer base/loop/current/end fields are
no longer merely echoed in the PIO register file. Pitch preserves the voice
list link; 24-bit buffer offsets preserve the neighboring envelope byte.
Configuration alone does not set ACTIVE, advance playback, signal completion
or raise an IRQ. Invalid voice ownership/DMA stops the reference; address
arithmetic cannot wrap a bad voice-table pointer into valid RAM.

`tools/apu_reference.cpp` supplies a strict C transport to this maintained
implementation. It accepts implemented 32-bit configuration/control writes
explicit PCM/ADPCM source pulls, nominal rate conversion, submix headroom writes and final 2D send-stage
processing. These are distinct pipeline boundaries; no missing upstream stage
is silently supplied by the final mixer.
Unsupported methods/widths fail persistently before successful completion or
later RAM modification. The older broader machine register-file defaults are
not admitted through this qualification boundary.

`tools/native_audio_configuration_reference.py` executes the owner's complete
NTSC original routing and pitch helpers without substituted callees. The XBE
is bound to SHA-256
`e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249`.
It verifies real return/stack/callee-saved-register behavior and checks output
against independently expressed field expectations. Those actual output words
then enter the maintained APU and update its guest voice table.

Five routing layouts and eleven pitches give **55 original cases**, including
empty/default-bin filling, reordered bins, signed pitch and saturation. The
original internal pitch helper saturates to **-32767..8191** before producing
the hardware word. This does not yet qualify public SetPitch validation or
audible resampling; native public range handling is a follow-up comparison.
Another **16 buffer-field cases** check masked ownership, and **five negative
cases** reject unknown methods, wrong widths, bad voice IDs, out-of-RAM DMA and
32-bit address wrap. The source RAM and another voice remain intact.

The expanded `test_apu` checks all thirteen full-word configuration methods,
pitch/link separation, buffer/envelope separation, whole-RAM write isolation
and terminal-fault behavior. Its earlier VOICE_OFF/notifier claim is corrected
below; the independently specified inactive-voice IRQ case remains. The private original/model fixture passes;
evidence: `build/blinx/audio-configuration-original.aot-private.json`, reference
DLL SHA-256 `41d8fdcea694dd1107f7f73d909773ab55382a97aff3b7f1816b3d3393e2e3df`.

This is original-helper plus real device-configuration evidence. It does not
execute the enclosing DirectSound lock/command emission path end to end, nor
qualify every declared parameter's downstream signal-processing behavior.

## PCM source consumption and native comparison

The maintained APU now fetches PCM8/16/24/32 mono/stereo through its actual
scatter/gather table and guest RAM. It handles nonadjacent pages, including a
single sample or stereo pair straddling a page boundary. Each consumed frame
advances the guest CBO while preserving its envelope byte. Interior loops wrap
to LBO only after consuming the inclusive EBO sample. Pause and voice-lock
prevent reads/advancement; a later seek or caller data write is visible on the
next pull. Unsupported SSL/multipass/linked processing fails explicitly;
ADPCM is added by the subsequent batch below.
No host clock estimates samples, and no silence is invented to fill a request.

This is the source stage before resampling, envelopes, routing and GP effects.
Its `ended` output hands the real final sample to the pipeline owner; it does
**not** retire the guest voice or publish a notifier. The owner must handle
that event before requesting more source data. Stop/restart state is currently
an input to this stage, not a newly qualified whole control/notification path.

`test_apu_source` checks eight PCM format/channel combinations, splits requests
around 31/32/64-frame boundaries, and verifies the last sample, one-frame buffers,
interior/repeated loops, pause/lock/seek and inactive ownership. Fault cases cover
unsupported formats, out-of-range cursors, descriptor wrap, invalid/misaligned
SGEs and a failed second-channel page read without cursor advancement.

`native_audio_source_reference.py` executes the original region-setting routine
from entry `0x179efd` through its calculations, stopping at its first PIO poll
`0x179f9b`. All reached helper calls execute unchanged, with an explicit IRQL-2
caller precondition; no FIFO capacity or enclosing return is substituted.
The original subtracts one from the converted end, corroborating inclusive EBO.
Its resulting LBO/EBO then drive our real source consumption. **40 cases** cover
PCM8/16 mono/stereo, lengths 1/31/32/33/65 and one-shot/interior loops. Complete
original byte/sample helpers independently round-trip the consumed cursor.
The emitted PCM samples and cursor units match the independently authored input.

`pilot_audio_source_lle` compares that source stage with actual XAudio2 mixer
samples for PCM8/16 mono/stereo at 48 kHz, unity gain and interior loops.
All four cases pass with a **3e-5 normalized-sample tolerance**; actual native
play/write cursors stay in the chosen loop. These are sequential test runs,
never duplicate production execution. Other rates, pitch interpolation and
downstream envelopes/mixing/GP behavior remain separate work.

Validation: source and APU tests **2/2 in 0.03s**; mixed suite **53/53 in 21.69s**.
After removal of the notifier placeholder below, both original fixtures and
the native-source test pass again (the latter in 1.21s). Private evidence:
`build/blinx/audio-source-{original,configuration}.aot-private.json`,
`audio-source-{build,native-build,full-tests}.log`. The title executable hash
still matches the recorded 190.141s regression, so it was not rerun for this
device-stage/test-only addition; no new actual audio-LLE title run is claimed.
Final reference DLL SHA-256:
`c4498668be4949a07821fd0006f480355fab3a5fb50bfa1667d80f4e37147cac`.

## ADPCM source consumption using an independent permissive decoder

The same maintained source stage now fetches complete Xbox ADPCM blocks through
the real SGE/RAM path, decodes them and advances CBO in source frames. Each
36-byte/channel block supplies 64 played frames, including the header predictor.
Mono/stereo, mid-block pulls/seeks, inclusive end, loops across block boundaries,
pause/lock and a fresh guest-data/page-map read on each new pull are covered.
A failed block fetch does not advance CBO or invent output for that block;
already consumed earlier blocks remain correctly reported. This still hands
source end to the unfinished downstream owner rather than publishing completion.

The codec is David Reid's unmodified **dr_wav**, pinned at
`dfe8377631000664666519fdb83da193fd8037f4`, using its offered MIT-0 license.
Our separate `apu_adpcm.c` adapter presents one block through the public IMA WAV
memory API and consumes the first 64 frames. Its signed header-index clamping
and ignored reserved byte follow the pinned emulator reference; unusual-header
behavior is not a new hardware measurement. No GPL decoder body is added to
this maintained implementation. Full upstream source, license, hash and credit
are retained under `src/third_party/dr_wav`; pilot notices accompany test output.

Qualification is bundled with the existing source fixtures:

- **512 blocks** compare maintained dr_wav output with the separately linked
  private Phobos HLE and xemu-derived APU reference. All 256 header-index byte
  values, mono/stereo, saturation, reserved bytes, the unused final high nibble
  and output guards are covered. These two older donor references share
  provenance and are not counted as two independent silicon observations.
- Authored +/-1 ramps independently test channel interleave, cross-page blocks,
  requests around the 64-frame boundary, loops starting/ending inside blocks,
  seek/write/remap visibility and partial DMA failure. PCM cases remain intact.
- **56 original region/unit cases** now include 16 ADPCM cases. Original NTSC
  region arithmetic and complete conversion helpers supply the real LBO/EBO
  and the 64-frame byte-position quantization; no enclosing API/MMIO completion
  is substituted. Exact source samples are checked independently.
- **Six actual native mixer cases** compare PCM8/16 and ADPCM mono/stereo
  interior loops at 48 kHz, unity gain, with the existing 3e-5 normalized-sample
  tolerance. ADPCM uses the existing private HLE decoder into the actual native
  PCM transport, independently of the maintained APU decoder. This comparison
  covers that decode/transport boundary, not an original whole-driver run.

APU/source tests pass **2/2 in 0.02s**, native source comparison in **1.85s**,
and the combined native/contract suite **53/53 in 23.88s**. Evidence:
`build/blinx/audio-adpcm-{decoder-build,source-build,native-build,full-build,full-tests}.log`
and `audio-adpcm-source-original.aot-private.json`. Reference DLL SHA-256:
`34862b33c0a9f44c7d3a5eeed8131becc5da69420b7326f605ae8975dcbac5ec`.
The complete rebuild changed the native executable identity. A fresh
**190.344s** default-native Blinx regression captured 18 frames: actual Demo,
replayed interruption, loading/title return and Demo reentry. Both Start
press/release pairs were delivered; no STOP/audio-stop/fault was logged.
Evidence is `build/blinx/audio-adpcm-floor-attract/{run.aot-private.json,contact.png}`
and `build/blinx/run-audio-adpcm-floor-attract.log`. Executable SHA-256:
`24f32feb2c5bcfea8aa0818b19c674b12096649decc769f20389059ef976a358`.
The harness terminated the still-running process at its budget; this is not
natural-exit, endurance or cadence sign-off. No LLE-audio title run or family
closure is claimed. The stable private Blinx repository pin remains unchanged.

Reproduce the maintained-device checks with the root `test_apu_source` and
`xlle_apu_reference` targets. Run `native_audio_source_reference.py --xbe <owned
NTSC image> --library <reference DLL> --dependencies <DLL dependency directory>
--output <private JSON>` and the pilot's `pilot_adpcm_reference` /
`pilot_audio_source_lle` CTests. These select separate diagnostic reference and
native executions; production does not run duplicate implementations.

## Original format setter and final 2D routing/gain stage

Executing the complete original format setter `0x178805` exposed a gap in the
previous hand-authored source setup: NTSC DirectSound programs ADPCM as
**S24 + ADPCM container** (`0xa...`), while the earlier fixtures used S16 + ADPCM
(`0x9...`). Both decode identically under the pinned emulator model, but the
maintained source previously rejected the actual title form. It now accepts
both. All **56 source cases** first run the original format setter and its
unmodified IRQL helpers with the voice still unallocated and caller IRQL 2;
then original region arithmetic and unit helpers drive the maintained DMA path.
Stack and callee-saved registers are checked for complete helper executions.
No MMIO availability or helper return is substituted.

`McpxApu::mix_2d_samples` now implements the **final 2D send stage** using actual
guest voice-table routing and eight packed attenuation values, plus the 32
submix-headroom method values. Ordered send slots select left/right channels;
repeated destinations accumulate. Attenuation has 1/64-dB units, `0xfff` mutes
a send, and each submix headroom bit halves its contribution. The stage adds
to the existing 32-by-32 planar bins; it does not clear other voices, advance
CBO, change envelopes or publish completion. Spatial voices, unknown routing
phase bits, bad DMA, non-finite inputs and invalid destination shapes fail.

This is a real component of the LLE pipeline, **not a whole voice processor**.
Its input must already have undergone resampling, filters, envelopes and voice
scaling. The new tests compose it with real source DMA at 48 kHz, no filtering
and unity upstream scaling. Those explicit preconditions do not implement or
qualify the missing upstream stages for the title. General/voice headroom,
nonstationary envelopes, spatial processing and stream ownership remain open.

The complete original volume helper `0x178a7b` supplies **84 cases** across
mono/stereo, empty/default/reordered/eight/duplicate routes and volume/mute
boundaries. Its packed words feed maintained RAM methods and the source/send
chain. Independent expectations check all bins and unchanged guest RAM across
the final stage. `test_apu_source` additionally checks multi-voice accumulation,
submix headroom masking, live gain changes, unused frame slots/output guards,
and terminal failures. No original code bytes are committed.

The real native mixer comparison now runs **24 cases**: PCM8/16 and ADPCM,
mono/stereo, each with default, eight-bin, repeated-bin and empty routing. It
retains actual native consumption and interior-loop cursors. The native path
keeps continuous dB arithmetic: tolerance is 3e-5 normalized sample plus 0.2%
of the reference amplitude for 1/64-dB quantization. A hardware-muted send
allows at most 0.000632 times its source amplitude as quiet native output.
This bounded difference is documented instead of forcing HLE to reproduce
LLE internal arithmetic. Upstream silence/noise, arbitrary rates and post-GP
output are not established by these stationary pre-GP comparisons.

Validation: **84 volume/mix + 55 routing/pitch + 16 fields + 5 negative** original
configuration cases; **56 original format/region/source cases**; root APU/source
**2/2 in 0.06s**; actual native comparison **7.11s**; mixed suite **53/53 in 28.32s**.
Both the maintained machine library and private native targets build. Evidence:
`build/blinx/audio-mix-{configuration-original,source-original}.aot-private.json`
and `audio-mix-{source-build,machine-build,native-build,full-build,full-tests}.log`.
Reference DLL SHA-256:
`892a6f410261ac6a541d11532b259bb3c28ebedc2d890fed3c4aa38d74c95317`.
The title executable retains SHA `24f32feb...976a358`, so the immediately prior
190.344s native attract regression is reused; no redundant attract run or new
LLE-audio title qualification is claimed. **4/9** remains unchanged.

Pinned xemu explicitly describes its sinc interpolation as a quality choice
rather than a confirmed silicon algorithm. [libsamplerate 0.2.2](https://github.com/libsndfile/libsamplerate/tree/0.2.2),
BSD-2-Clause, was reviewed as a reusable non-GPL candidate; no library code was
imported in this batch. Keep source-rate progression and interpolation error
as explicit contracts. Consult existing evidence before any new hardware
measurement; if needed, exact silicon interpolation belongs at the back of
the queue and must not block independent processing or control work.

## Persistent nominal source-rate processing

`src/machine/apu_rate.cpp` connects the real PCM/ADPCM source stage to a
persistent converter per voice. The actual signed pitch word determines source
consumption versus the 48-kHz output clock. Source pulls perform the existing
SGE DMA and CBO writes; filter-buffered output is consumed before requesting
more guest input. An initial test exposed premature prefetch that made CBO
depend on request partition. That is corrected: bulk and seven-frame requests
now agree on samples and fetched source frames in the tested cases.

Pause/lock freeze both history and source consumption. A seek, source binding
change or new voice activation discards obsolete history; pitch changes retain
it. A finite buffer is fetched exactly once, then the converter drains and
reports no further frames. It never repeats the final source sample, fills a
request with invented silence or publishes a guest notifier. Required DMA
failure and unsupported SSL/modulated-pitch requests stop. The caller still
owns amplitude envelopes, filtering, GP submission and completion delivery.

The actual converter is unmodified **libsamplerate 0.2.2**, commit
`c96f5e3de9c4488f4e6c97f59f5245f22fda22f7`, with BSD-2-Clause library sources.
`SRC_SINC_FASTEST` follows pinned xemu's reference choice; its acknowledged
uncertainty about the silicon interpolation kernel remains. This establishes
an executable nominal-rate floor and explicit numerical behavior, not a new
hardware golden for interpolation, prefetch latency or pitch-transition shape.
The upstream developer-only GPL assembly-check script is not built or packaged.
The adapter, lifecycle rules and tests are authored here; xemu's wrapper and
starvation-silence workaround are not copied. Attribution and license are
retained beside the build artifacts and in the repository.

Validation is bundled around actual source consumption:

- Root tests verify commanded tone frequency/amplitude/residual, persistent
  chunking, 36 finite PCM/ADPCM cases with untouched output guards, pause/lock,
  seek, pitch changes, explicit failures and 64 independent lifetimes. Finite
  source counts and CBO remain exact even while output is filtered/resampled.
- **12 complete original pitch-helper cases** drive maintained rate conversion
  and independent tone fits, with original byte/frame helpers reading the
  resulting CBO. The earlier 55 configuration, 84 volume/mix and 56
  format/region/source cases remain green. Public SetPitch/SetFrequency range
  handling is not inferred solely from these internal-helper cases.
- **12 actual native comparisons** compose our maintained source, rate and 2D
  send stages, then compare with real XAudio2 capture at nominal source rates
  from 12 kHz to approximately 192 kHz, mono/stereo. Tests fit phase-independent
  tones at the commanded frequency, check amplitude/residual and unused bins,
  and read real native cursors. LLE amplitude error is below 0.001 normalized
  and RMS residual below 0.0003; native amplitude error/difference is below
  0.002 and residual below 0.001. Native integer-Hz rate rounding is explicit.
  These are bounded steady-tone checks, not transient or whole-title A/V proof.

The full machine and native builds pass. Root APU/source/rate **3/3 in 0.11s**;
combined native/contract suite **54/54 in 32.45s**. Evidence:
`build/blinx/audio-rate-{configuration-original,source-original}.aot-private.json`
and `audio-rate-{build,test-build,machine-build,native-build,full-build,full-tests}.log`.
Reference DLL SHA-256:
`b26e54f73adc27dbf343f16377d14e650f0f1e115fdb5d5677ab51fb77daea25`.
The rebuilt native executable SHA-256 is
`c5ff3de946b93d17c4ec83697443244c8cfbb0c931f10b0ce975d0a727009cfa`.
Its fresh default-native regression `audio-rate-floor-attract` ran **190.329s**:
18 inspected captures show demo gameplay, Start interruption/loading, title
return and demo reentry. Both scheduled Start press/release pairs were delivered;
there was no logged STOP or audio fault. The harness terminated the still-live
process at its time limit; its return code is not a crash or natural-exit claim.
This protects the native path, not whole-title LLE audio or endurance/cadence.
The family stays open: **4/9**.

Build root target `test_apu_rate` and run CTest `apu_rate`; the real native
comparison is pilot CTest `pilot_audio_rate_lle`. The original-code fixture is
`native_audio_configuration_reference.py` with the same owned-XBE/reference-DLL
arguments already documented above. No production backend switching or duplicate
execution is introduced.

## Original voice start and bounded envelope processing

`src/machine/apu_envelope.cpp` adds an executable source/envelope quantum. Each
call processes one 32-output-sample interval; the envelope clock is independent
of source pitch. It composes real source DMA, persistent rate conversion,
signed pitch modulation and amplitude scaling. Filtering, voice scaling,
final sends, GP and completion remain separately owned stages.

The authored envelope model supports disabled, delay, ordinary nonzero-rate
attack, hold and sustain. It writes actual counter/phase/level fields while
preserving the neighboring source offsets. Both envelopes are evaluated before
either is changed; unsupported decay/release/forced-release and zero-rate or
overflowing attack stop without silently advancing the other envelope. Hold
can enter decay, but the next processing call stops at that unqualified curve.
Pause/lock/inactive voices consume no source or envelope time.

`VOICE_ON` previously ignored both envelope-start fields. It now initializes
their independent phases/counters before playback. Unknown start states/flags
and nonzero initial CBO are guarded before list mutation. The latter preserves
the unresolved cursor-reset question instead of adopting xemu's FIXME as fact.
No envelope or source operation clears ACTIVE or fabricates a notifier.

Independent evidence and its scope:

- [XboxDevWiki's APU observations](https://xboxdevwiki.net/APU#Envelopes)
  describe the phase/counter interface, sustain updates and pitch modulation.
  Pinned xemu remains a behavioral cross-check. Some transition details and
  curves are explicitly uncertain in these sources; this is a documented
  reference model, not a new silicon trace. No GPL implementation body is
  copied. New console observations, if needed, stay behind offline work.
- `native_audio_control_reference.py` executes **eight complete original NTSC
  init/filter-default/start cases**: `0x178705`, its actual original vtable
  format callee, `0x179283`, and `0x179c4b`, without replacing callees or returns.
  Preconditions are an allocated nonspatial voice, empty original owner list,
  DISPATCH_LEVEL and synchronous method consumption. Real method writes update
  the maintained voice table; original linked-list writes, notifier IN_PROGRESS
  initialization, phase/counters and 128 actual output frames per case are
  checked. Only admitted methods can report free PIO capacity after processing;
  unsupported transactions stop. This does not emulate queue latency or qualify
  full allocation/interrupt/SSL/completion ownership.
- Root `apu_envelope` tests cover all **16 start-phase pairs**, disabled sample
  identity, delay/attack/hold sequences at two source pitches, stable gain and
  modulation spectra, live sustain updates, pause/lock/inactive state and guards.
  They assert real memory/source effects, including preservation of source
  offsets and refusal to invent downstream completion.
- Native `pilot_audio_rate_lle` now has **18 source/rate/envelope/send cases**.
  Real XAudio2 gain/frequency reproduce stable envelope effects within the
  existing spectral/amplitude tolerances. Integer-Hz and hundredth-dB rounding
  are explicit; mute is compared against the native -100 dB floor. This does
  not claim native time-varying envelope API support.

The original-control fixture uses the same owned-XBE/reference-DLL arguments
as the earlier fixtures, writing
`build/blinx/audio-envelope-control-original.aot-private.json`. Root CTest is
`apu_envelope`. Earlier original configuration/source fixtures still pass.
The full machine/native builds pass; root APU/source/rate/envelope **4/4 in
0.15s**, combined native/contract **54/54 in 33.29s**. The reference DLL SHA-256
is `4222dd53882ee9165208665d529b1fdecf99ff3263940a7ce922d0a7ea5d38b6`.
Logs are `build/blinx/audio-envelope-{build,native-build,machine-build,full-build,
full-tests}.log`; the control, source and configuration original fixtures retain
private JSON under the same batch prefix. The rebuilt native executable SHA-256
is `995adc6fd3982ed97115f9996a16dd74299691e6954e0f8905de37f1267c3b35`.
Fresh `audio-envelope-floor-attract` ran **190.156s**; 18 inspected captures
show demo gameplay, Start interruption/loading, title return and demo reentry.
Both scheduled Start press/release pairs arrived; no STOP/audio fault was logged.
The harness terminated the live process at its limit. This is a default-native
regression, not whole-title LLE audio, cadence or endurance qualification.
Family **4/9** stays unchanged: this does not close audio.

## Withdrawn notifier assumption

The audit found the older `post_voice_notifier` wrote a literal `1` into byte
14 and selected SSLA even for streaming voices. Pinned xemu explicitly marks
its envelope-information byte provisional. That is not independent correctness
evidence. That placeholder was removed, initially guarding all VOICE_OFF work.
The subsequent static-stop batch above restores only the independently bounded
status/stop/idle/unlink route. Unknown payload bytes and SSL owners remain
unqualified. The strict reference rejects payload reads and unsupported owners.

This intentionally withdraws the old LLE notifier qualification. The default
native audio/title route is unaffected. The broader LLE machine still stops on
unqualified stream/linked/multipass VOICE_OFF ownership.
Original settings and real native detach/rebind evidence still stand. Before
closing audio, establish the actual notifier byte/record meaning and delivery
through original caller evidence and independent documentation/observations;
do not restore a fixed envelope value. New console observations, if actually
needed, remain at the end of the burndown. Their necessity is not established.

## Read-only GP takeover context - 2026-10-10

Under the owner's standing read-only authorization, two fixed eight-DWORD
batches completed through the unchanged v0.4.0 ARM/EXEC route. Both observed
GP reset `0` at their endpoints and AV `11` unchanged. Scratch/FIFO SG addresses
were already nonzero, with maximum fields 1544/1; no pointed-to memory was read.
The second batch observed IEN=`AFA`, SECTL=`7`, FECTL=`71F8F`, ISTS=`0`. Under
the factual register masks, the global IRQ-enable bit was clear, XCNTMODE OFF
and FE method mode HALTED. This is current application state, not reset-state
qualification or an atomic exclusion of other CPU writers.

No DSP reset/start, SG, program/data, DMA/FIFO, IRQ, playback or storage write
occurred. Seven hardware-free GP context tests pass, executing every fixed
load and proving zero stores, exact closed lists, endpoint checks and no retry.
They follow the 76-test combined probe pass; these seven changed/new GP tests
were run separately, without repeating the unchanged two-million-call clock
test. The underlying data movement and bit9 guard remain unchanged.

Preparation found a necessary boot dependency: xemu's GP bootstrap model loads
8192 scratch bytes into P. A one-page SG setup with direct P writes would not
supply that boot source. The next design must own the full boot/SG/sample
extent, initialize descriptors through the authored DSP after bootstrap, observe
completion before restoring shared pointers and preserve the observed quiet
device conditions. No such takeover has run. See
[the fixed context](https://github.com/mstan/xboxlle-probe/blob/feat/av-handshake-probe-20261009/docs/GP-CONTEXT.md)
and [owned-memory design constraints](https://github.com/mstan/xboxlle-probe/blob/feat/av-handshake-probe-20261009/docs/GP-BIT9-PLAN.md).
`.114` and the broader audio family remain open; **4/9** stays unchanged.

## Owned GP bootstrap prerequisite - 2026-10-10

The fixed probe implements a wholly owned 28KB SG/boot/guard allocation and an
authored six-word GP program that writes X100 and stops. Nine focused offline
tests pass, including real execution on the pinned MIT DSP CPU. No retail
firmware, agent rebuild, audio/FE/setup enable, descriptor DMA or storage write
is involved. Source and the stopped observation are published in probe commits
`0f9a4ae` and `a509e700bdd592da48db9b896f396fde1dfee4c8`; see
[GP-BOOT](https://github.com/mstan/xboxlle-probe/blob/a509e700bdd592da48db9b896f396fde1dfee4c8/docs/GP-BOOT.md).

The first hardware attempt returned `DF000008`, immediately after its first
SG-address store failed readback. It stopped before SG-maximum programming
or GP start. The allocation remains retained and the current SG base is
unknown. The return does not encode the requested address or actual readback;
neither bootstrap behavior nor the mismatch cause is established. No retry,
restoration payload or further hardware request followed. Human screen
inspection is pending under probe AGENTS.md rule8. Once inspected, a distinct
read-only context can characterize the stopped state; do not restart the write
experiment automatically. GP bootstrap/bit9 and full audio remain open.

### Read-only post-failure context - 2026-10-10

The owner subsequently authorized continued reads, including unexpected read
outcomes, while requiring a stop after an unexpected write. A distinct fixed
eight-DWORD context batch completed with zero device writes: GP reset remained
zero and AV remained `11` at both endpoints; the SG maxima remained1544/1.
The scratch SG address differs from its pre-write sample, while the other seven
context words match. This establishes the later register observation, not the
instantaneous value that caused `DF000008`, the requested allocation address,
or the reason verification failed. The failed store has not been retried.

A bounded attempt to identify the candidate retained RAM table then stopped.
The generic read command rejected its CPU alias before access. A fixed
DWORD-load/return payload was validated offline for one intended load, zero
stores, and preserved callee state; its first hardware load timed out without
returning a word. A subsequent distinct health PING also timed out. No SG,
reset/start, DMA, audio, storage, or recovery store followed; possible backing
allocations have not been freed. The agent is unavailable pending the owner's
screen report/restart, while independent graphics work continues.

The SG register alone does not establish a valid CPU mapping or owned storage.
Future RAM probes must establish mapping validity before dereferencing a
candidate alias. The read failure neither qualifies bootstrap nor proves its
cause; GP bootstrap/bit9 and the complete audio family remain open. Raw pointer
values, payloads and replies remain in private recovery records.

The owner reported that the screen still looked normal, then restarted and
relaunched v0.4.0. A fresh eight-DWORD GP context batch completes with zero
device writes, stable reset/AV endpoints, and the same SG maxima. The agent
is available again. No failed RAM load or SG write has been repeated; screen
freeze and the RAM timeout's cause remain unestablished.

## Read-only inherited GP mapping observation - 2026-10-10

A distinct fixed batch uses kernel exports 173/174, first calibrating their
BOOLEAN/physical-address interpretation against the known kernel image base
and a fixed invalid address. All four current inherited scratch-table direct
CPU-alias pages are reported unmapped. Its eight device reads preserve GP
reset, AV phase, SG address and maximum; zero device stores, candidate-RAM loads,
allocations, frees, DSP starts or audio output occur. No physical query follows
an invalid-page result. The earlier failed SG write and RAM load are not retried.

This rejects treating those current aliases as mapped RAM. It establishes
neither the previous context's timeout cause nor SG ownership/encoding, GP boot
or bit9 semantics. Future experiments need independently owned, verified
mappings. The fixed payload and 13 authored x86/host controls are published at
probe commit `5107573`; the combined hardware-free suite passes 105 tests with
one separately retained optional DSP checker skipped in this invocation.
See [the exact query and limits](https://github.com/mstan/xboxlle-probe/blob/5107573/docs/GP-MAPPING.md).
Audio `.113`, bit9 `.114` and the **4/9** family count remain open.

## Remaining work, in order

1. Preserve the now-connected original IRQ/PIC/ISR/unlink and DPC-queue route.
   Completing the deferred consumer through the original fence is `.115` and
   stays behind independent offline work. Unknown notifier payload fields stay
   explicit. SSL descriptor/source ownership remains open, behind the currently
   reached buffer path rather than ahead of it.
2. Complete the remaining voice-processing path around the maintained source,
   rate/envelope quantum and final 2D send stages: remaining envelope curves, voice
   scaling and filtering, then connect the existing GP donor. Validate actual stream
   output and transitions, not only stationary mixer comparisons.
   Existing original byte/sample conversion, detach, decoder and real native
   output tests are reusable evidence, not a completed LLE audio device.
3. Execute the original DirectSound owner over maintained DMA/interrupt/memory
   services; integrate static title selection and validate actual Blinx audio,
   resource reuse and movie-relative timing against documented tolerances.
4. Put only specifically unresolved silicon/timing observations at the end of
   the burndown. Do not request a console merely because the LLE route is slow.

The shared runtime regression for this batch uses the **default native audio**:
52/52 mixed tests, followed by 190.141s of Blinx with demo/interruption/title/
reentry and no stop/audio-stop/fault. See `SCHEDULING-LLE.md` for exact evidence.
That run protects the existing title route; it is not a new audio-LLE title run.

## Nominal ordinary-envelope lifecycle and replaceable signal boundary

Ordinary static 2D voices now have working decay, release and force-release
phases. `VOICE_RELEASE` snapshots the currently stored levels and changes
phase/counters without fetching source or publishing completion. Amplitude
force-release retires through the existing state, notifier status and IRQ owner
before another source fetch; filter force-release supplies zero modulation and
does not complete the voice. Start/stop and source/table/SG rebinding clear the
private release history and retained rate output.

Curve policy is explicitly nominal: `apu_envelope_curve.hpp` isolates an
exponential -60 dB interval, eight-bit floor and bounded zero/sustain endpoint.
Pinned xemu itself marks the exact curve, threshold and release origin/count
details uncertain. Functional phase support does not qualify silicon precision.
The [deferred queue](LLE-DEFERRED-QUESTIONS.md) preserves those questions and
reopening triggers.

The unamplified source/rate quantum now returns envelope control separately,
reserving a filter insertion point before amplitude and final sends. Existing
`source_envelope_quantum` bypass behavior is preserved. Other filter/headroom,
stream/linked/multipass/spatial and GP/bootstrap guards remain explicit. The
strict original-owner reference has not yet admitted the new release method;
its later integration is separate from the maintained device implementation.

The hardware-free APU batch passes **9/9 entries**, including the new lifecycle
fixture and existing source, rate, envelope, stop, completion, GP DMA and FIFO
checks. Representative real PCM quanta establish current-level release, a fade
against an independent unity source/rate clock, ordered retirement, no force-
release source fetch, retrigger/rebind isolation and pause/lock/guard behavior.
Evidence: `build/blinx/services-foundation-v3-20261010.aot-private.json` and
its `apu-tests` log. No waveform playback or console probe was required.

An earlier broad batch ran an old standalone realtime native mixer comparison,
which failed without scenario/sample diagnostics. That executable predates this
generation and does not honor the newer output-mute switch. Its failure is
preserved without inferring a cause or changing source/tolerances. After the
game exited, the fresh executable honored output mute and passed that comparison
alongside the fresh shader follow-up (2/2). Evidence is
`build/blinx/services-foundation-fresh-followup-20261010.aot-private.json`.
The separate muted title regression stopped at the host hidden-window
`DXGI_STATUS_OCCLUDED` guard after 144.062s; it is not a completed title pass.
After the host presentation correction, the single muted default-title
follow-up reaches **190.172s /18 captures**, with all replay rows and no
stop/audio-stop/fault. It covers Demo gameplay, interruption/title return and
next Demo loading. Final evidence is
`build/blinx/services-foundation-qualified-followup-20261010.aot-private.json`.
This validates the bounded shared-runtime regression and closes the nominal
envelope task; it does not execute a whole-title original DirectSound/APU route.
The current 9/9 APU result covers the changed CPU stages, not whole-title audio.
Actual filters/scaling, SSL/GP/EP ownership/output and original DirectSound/title
selection remain active implementation gaps. Existing HLE audio stays on its
maintained output path.

## Raw SSL descriptor ownership - 2026-10-10

`beads-xbox.1.113.2` adds the missing shared-RAM staging foundation for SSL.
`SET_CURRENT_SSL` selects a bounded 64-entry window, and its 128 DWORD methods
write the actual offset/control pairs at `VPSSLADDR`, rather than falling into
generic register storage. Each voice has distinct A/B base/count bindings.
`read_ssl_segment` validates the binding and reads both opaque DWORDs freshly
from the current physical table. CPU rewrites and table remaps are observed
without a descriptor cache. The authored `apu_ssl.hpp` owner isolates the
staging/address policy from later stream decoding and completion.

This implements **raw staging**, not interpretation of the control word or
consumption of stream samples. The existing streaming source/retirement guards
remain; no start/advance, A/B switching, starvation silence, notifier payload,
IRQ, timing or whole-stream output is supplied. The strict original-driver
transport deliberately does not admit these new methods yet. No linked SSL
emitter was identified in the inspected Blinx static audio region, so this
batch is not an original-stream-driver qualification.

The bounded admission uses the published method numbers/field masks and pinned
[xemu register definitions](https://github.com/xemu-project/xemu/blob/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/mcpx/apu/apu_regs.h).
Its [VP reference](https://github.com/xemu-project/xemu/blob/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/mcpx/apu/vp/vp.c)
supplies the staging-address relation, while explicitly marking interpretation
and several stream lifecycle details uncertain. No implementation body was
copied. Admit only the 8,192-entry reservation, aligned 64-entry windows and
at most 16 entries per bank; these conservative limits do not qualify malformed
or broader silicon behavior. Unknown context-DMA selection, partial/misaligned
transactions, bad extents, address overflow and failed memory access stop
explicitly. A failed descriptor read leaves its caller's output unchanged.

The separate, silent APU batch passes **10/10 in 0.23s**. Eight fresh lifetimes
cover four voices and both banks, with 384 actual DWORD stores and 256 fresh
pair reads; delayed cross-owner reads prevent one global binding from passing.
Additional cases exercise the last descriptor, direct CPU rewrite, table
remap, empty bank, fresh device isolation and terminal failures. Full caller
RAM comparisons include voice/cursor/notifier storage and establish that
staging changes only the addressed words. Stream pulls still stop before
samples or completion are invented. Evidence is
`build/blinx/audio-ssl-foundation-20261010/{apu-tests.log,host/Testing/Temporary/LastTest.log}`.
Only its private build was rebuilt; earlier source-bound reference DLLs are
retained. No console probe, audio playback or title/attract run was required.
The native HLE audio implementation has no changes. Audio stays open and the
family count remains **4/9**, with the same deferred-question queue.

## Device-owned ordinary VP to GP quantum - 2026-10-10

`beads-xbox.1.113.3` connects the previously separate voice-processing stages
to actual GP input ownership. `process_2d_quantum` reads the guest's real 2D
list, preflights its complete links/configuration, and processes its ordinary
voices through maintained source DMA, persistent rate conversion, envelopes
and final sends. It writes all 32 bins by 32 sample slots into the existing
GP mix bank at `X:0x1400..0x17ff`. Host aperture reads and descriptor DMA resolve
those same words; no second mix buffer or host voice registry owns the device.

Finite voices are retired **after** that publication. The final DONE/status
stores and level IRQ therefore cannot precede publication of their quantum.
A retired voice left on the real list emits the existing idle trap on the next
admitted call; the original driver remains responsible for unlinking it. This
batch does not replace or newly qualify that original ISR/consumer. Disabled,
halted or trapped sequencing does not admit a quantum; paused/locked voices
fetch no source. Empty/no-contribution quanta publish a zero mix bank without
claiming successful stream consumption or invented starvation samples.

The audit retains the reached original ordinary defaults: NTSC initialization
`0x178705` sets voice HEADROOM=7, and the complete previously qualified filter
helper `0x179283` supplies bypass defaults. Its nondefault path emits the real
mode and coefficient methods; those encodings alone do not establish output.
The current native binding has no filter API. Pinned xemu leaves ParaEQ/cutoff
modulation incomplete and does not consume voice HEADROOM; its modified GPL
LADSPA filter is not independent silicon evidence. DLS2/ParaEQ/spatial and other
headroom values retain their guards and active implementation dependencies;
[AUDIO-D04/D05](LLE-DEFERRED-QUESTIONS.md) retain their precision questions.
Advancing the reached bypass pipeline avoids admitting guessed filter output.

The isolated `apu_mix_word.hpp` policy converts normalized sums to signed
PCM24 with declared saturation and truncation. Existing native audio uses that
same scale, but agreement does not establish MCPX accumulator precision,
clipping or rounding. This boundary can be replaced without changing voice,
memory or completion ownership. Pinned [xemu GP/EP transport](https://github.com/xemu-project/xemu/blob/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/mcpx/apu/dsp/gp_ep.c)
corroborates the planar bank location. No implementation body was copied.

The separate, silent APU batch passes **11/11 in 1.03s**. Three mixed-source
lifetimes run 72 complete quanta with real mono/stereo DMA, persistent rate,
nonunity sustain, repeated-bin gains and submix headroom. Independent constant
and 750-Hz tone fits check the resulting PCM24 words. Known-bit GP descriptors
then transfer those actual words through noncontiguous physical pages. Three
finite-source lifetimes check publication-before-IRQ, real retirement, idle
trap/list ownership, and zero replay after caller-side unlink. Negative cases
cover complete-list preflight, unsupported later voices, physical overflow,
source failure after a real prefix, failed final notifier stores and admission
controls. A late source failure may leave its earlier voice state advanced;
a late retirement failure reports that the bank was published. Both stop
terminally without a completed-quantum sequence or falsely successful retirement.

Evidence: `build/blinx/audio-vp-quantum-20261010/qualified-batch.aot-private.json`
and `apu-tests-final.log`. The earlier first-test failure is preserved: its
fixture attempted illegal initial SUSTAIN, and was corrected to reach sustain
through the actual legal HOLD/DECAY lifecycle. No production guard was relaxed.
No preserved reference DLL, native audio implementation, console state or title
run was changed. This explicit API reserves clock/admission ownership for the
later device scheduler; it does not guess cadence, execute GP bootstrap/graph,
resolve original descriptor bit9, produce EP/AC97 output or qualify full title
audio. Those remaining integration dependencies keep the family open.

## Owned GP CPU and mapped bootstrap - 2026-10-10

`apu_gp_processor.cpp` now owns one actual guarded MIT DSP CPU over the same
APU X/Y/P/MIX banks used by host MMIO and GP DMA. Explicit CPU reset clears
the actual CPU state; it preserves the separately owned memory/peripheral
banks. Only a held CPU admits a fresh mapped bootstrap. The fixed profile
reads 8 KiB through the real GPSADDR/GPSMAXSGE mapping, loads masked 24-bit
words into P0..2047, and invalidates translated instructions. A failed read
does not publish a partially loaded program or claim a successful bootstrap.
The temporary transfer buffer does not become another persistent P owner.

The 2048-word profile is a factual lead from
[pinned xemu's GP bootstrap](https://github.com/xemu-project/xemu/blob/482088490c2d5983ed5e1eafbd09906157c1eca0/hw/xbox/mcpx/apu/dsp/dsp_jit.c).
Mapped execution is distinct from observed MCPX hardware boot. Actual
GPRST/MMIO, DMA-device reset and automatic VP/GP scheduling remain guarded;
this API does not supply those successes or infer physical boot latency.

Executable memory has one serialized CPU owner. Host P writes and actual
input-DMA P publication advance an invalidation epoch; the next run discards
stale translations. A P-target DMA while translated code executes is rejected
before its first executable word/node/status is published. Safe in-run code
reload remains an explicit future integration boundary. External P publication
uses these observed paths; the core retains raw bank pointers. CPU failure,
invalid run budgets and a terminally faulted APU cannot return successful work.
The pinned ABI's cycle budget carries real instruction-cost debt across calls;
returned PC/power/SP come from the CPU, not from requested cycles. STOP requires
an explicit reset before another boot/run, without guessing frame interrupts.

One representative batch executes **30 mapped boots / three reused lifetimes**.
Actual ordinary mono/stereo voice quanta feed MIX; real DSP MOVEs read 96 words
from three planar bins and real peripheral DMA publishes those words across
nonadjacent guest pages. CPU/DMA copies are exact. The independent constant
signal check retains the existing 2e-6 nominal source-rate tolerance; it does
not qualify silicon rounding. Mid-life SGE remap and program/destination changes
are visible freshly. Already translated host and input-DMA P rewrites execute
the new instruction immediate without a CPU reset. Separate guarded cases
cover active-code P publication and the unresolved original descriptor bit9;
both preserve the program and publish no completed DMA node.

`tools/native_gp_bootstrap_reference.py` binds the original NTSC XBE and extracts
the 1484-byte firmware prefix initialized by complete original helper0x17785e.
It loads that unchanged prefix through the new mapped owner with an authored
STOP tail and executes the actual entry. JSR0155 reaches FFFFC5=0FFF; the
maintained interrupt acknowledgement admits only its independently qualified
DMA status bit80 and stops explicitly. That preserved **expected failure is
not original boot qualification**. It pinpoints another reached prerequisite
without inventing frame/interrupt state or replacing original instructions.
Original full scratch/graph storage, GP frame ownership, further firmware
peripherals, bit9, FIFO/EP/AC97 and audible full-title output remain open.

The optional `pilot_gp_processor_contract`/`pilot_gp_processor_guards` targets
use the already prepared guarded DSP library. The product/native fast HLE has
no new per-quantum CPU or validation work. Focused APU checks pass **13/13**;
the optional pilot targets also pass independently. Source/binary/log hashes,
earlier fixture/configuration failures and the original firmware stop are
preserved in `build/blinx/audio-gp-bootstrap-20261010/qualified-batch.aot-private.json`
and `original-gp-bootstrap-guard.aot-private.json`. No reference DLL, playback,
whole-title run or console operation is part of this batch. The new ownership
module and fixtures are authored here; no xemu implementation body is imported.

## Owned GP events, frames and yield - 2026-10-10

The reached `FFFFC5` is an MCPX polled event register. Its identity is not
inherited from Motorola's peripheral map:
[DSP56362UM Appendix D](https://www.nxp.com/docs/en/user-guide/DSP56362UM.pdf)
places HBAR at this address and core interrupt priorities at FFFFFE/FFFFFF.
The unchanged Xbox firmware instead polls bit1 before frame work and bit7
after DMA, then writes2/80 selectively. These reached helpers do not require
invented architectural interrupt-vector delivery. Unknown vendor event bits
remain outside the admitted profile.

The maintained APU now composes its real frame bit2 with the existing actual
DMA EOL bit80. Reads neither post an event nor finish DMA. Selected writes clear
selected flags; acknowledging a frame does not release its input bank. The
exact original0FFF startup clear is admitted only while this CPU executes its
mapped bootstrap initialization before its first actual yield. Other unknown
masks remain guarded. Original D5 start-pointer context and D7 configuration0
are owned separately; nonzero configuration modes do not fabricate work.

`ApuGpProcessor::start_frame` first processes the actual VP quantum. Only a
successful published bank with its real VP sequence can acquire the GP lease
and post bit1. Disabled processing and real idle traps post nothing and leave
the CPU yielded; an empty active list can publish a real zero MIX bank. The
frame owner cannot accept an arbitrary caller-supplied generation or bank.
VP, host MMIO and input-DMA publication cannot overwrite a leased MIX bank;
the VP guard runs before source/cursor work, and input DMA is rejected before
physical input/FIFO cursor work. GP CPU access uses the existing mapped bank.
The publication transaction itself excludes reentrant host/DMA writers. A
retirement callback that fails a processor entry point cannot revive that
failed owner or post a frame when the outer transaction returns. Actual VP
prefix work remains reported; it is not rolled back or labeled GP success.

Actual `FFFFC4=1` requests the pinned core's callback-safe halt. This returns
at the instruction's actual next PC, preserving registers/stack with power0;
it is a vendor yield, distinct from STOP/reset. Only that returned yield
releases the MIX lease. A frame acknowledgement alone does not release it,
and pending unacknowledged events cannot be overwritten by another start.
Resuming clears the actual halt after a real frame publication, without a PC
reset. Positive unused run budget is discarded at yield; negative instruction
debt and the actual elapsed core cycle count survive. Halted runs cannot add
unused budget repeatedly. The reported core cycles are CPU diagnostics, not
an implementation of the unowned MCPX B3 timer or silicon timing evidence.
Removing an owner with outstanding work faults rather than forging a yield.

One representative batch executes **30 real VP/GP/DMA frames over three
lifetimes**, with simultaneous selected frame/EOL events and exact CPU/DMA
copies of real MIX. The independent constant waveform retains the existing
nominal source-rate tolerance. Cases cover lease overlap before bank/cursor
changes, disabled/idle admission, failed retirement, reentrant publication and
CPU failure, abandoned work, STOP versus yield, and preserved instruction debt.
The unchanged original155, D2 and160 helpers independently clear real X/Y,
own STOP/FREEZE/default context, spin without an event, and acknowledge the
frame selectively while actual DMA EOL remains. Authored helper callers are
explicitly separate from complete original firmware boot.

The unchanged whole1484-byte prefix now passes its earlier C5 boundary and
stops at actual **FFFFB2=FFFFFF clock configuration**. No timer result is
substituted. B0..B3 identity/configuration/producer, linear buffer15 transport,
bit9 executable reload, hardware GPRST/bootstrap/cadence and EP/output remain
guarded requirements. Exact silicon latency can be deferred after a real
producer exists; returning a donor's zero B3 value cannot qualify that producer.
This batch does not close the audio family or claim full firmware/output.

The earlier .113.4 reports/binaries remain unchanged at their original source
identities. New evidence is in
`build/blinx/audio-gp-frame-20261010/qualified-batch.aot-private.json` and
`original-gp-frame-reference.aot-private.json`; the original bytes remain
private. Initial fixture failures are preserved: fixed run budgets assumed an
ack instruction had already executed, and the idle fixture omitted the required
FETFORCE1 flag. The corrected fixtures follow actual event consumption and
existing legal trap configuration; production guards were not relaxed. The
default native HLE path gains no DSP work, playback or title runs. No physical
console operations or preserved reference-DLL rebuilds are part of this batch.

## Linear GP scratch and original parameter transport (.113.6)

Buffer15 now moves actual linear scratch bytes through GPSADDR/GPSMAXSGE and
the existing guest SGE callbacks. Word4 supplies the absolute scratch offset;
circular-only base/size words5/6 are ignored. The original DB/EB builders
initialize exactly five words. Fetching seven fields would read another
descriptor or unrelated metadata, and would reject a valid five-word object
at a bank boundary. The maintained owner fetches only the linear object's
fields. Linked transfers, input staging, sample-bank publication, executable
publication and EOL use the existing real transport and ownership rules.

Representative checks use reversed, nonadjacent physical pages, adjacent
five-word descriptors, X/Y/P destinations, PCM16 and both PCM24 lanes. They
cover real page-crossing bytes, ignored circular fields, a descriptor ending
at the X-bank boundary, preserved offsets and real P publication epochs.
A failed input page does not publish its staged bank or node; actual output
prefix bytes may remain after a later page failure. The owner stays failed.
Leased MIX input is rejected before scratch reads, and unknown bit9, linear
offset writeback and in-run executable publication remain guarded.

Unchanged original DB, EB, 0126 and 005C instructions execute under authored
callers. Across three lifetimes, **15 actual DMA transfers** import real
control and parameter words, clear the consumed parameter request, write its
real scratch acknowledgement, and move Y samples to real output bytes.
Original entry1B..1D selects `49E2`, format2 PCM24 MSB packing, after EB creates
format6 PCM24 LSB. PCM16 is a separate format1 contract; `49E2` does not select
PCM16. These helpers consume actual EOL and STOP/FREEZE/START status; polling
reads never advance a counter or manufacture completion. Authored callers
isolate those functions and do not qualify the full original entry or graph.

The remaining B0..B3 setup is explicitly unresolved. The unchanged prefix
writes B2=FFFFFF, B0=1 and B1=1, then reads B3 into profiling snapshots/deltas.
Its threshold branches both reach the same yield. This identifies a software
consumer, but does not establish register identity, enable/reset rules, clock
rate, epoch or wrap. The [primary investigator's issue240](https://github.com/xqemu/xqemu/issues/240)
explicitly leaves timer operation/configuration unknown. The [original trace
in issue151](https://github.com/xqemu/xqemu/issues/151) confirms the reached
control path, but its frequency estimate is not silicon evidence. The official
[DSP56362 manual](https://www.nxp.com/docs/en/user-guide/DSP56362UM.pdf) assigns
different functions/reserved slots to those addresses; its peripheral map
cannot establish MCPX vendor semantics. Pinned xemu returns zero for B3 and
does not implement the three configuration writes. That is not a producer.
The earlier “clock configuration” boundary names a suspected function, not a
qualified register identity. B0..B3 remain guarded without guessed latches,
zero values or read-count progress. Existing actual CPU elapsed diagnostics
and the injectable APU sample clock are preserved; neither is substituted for
this unowned peripheral. Precise phase/rate can be deferred only after its
genuine producer and configuration contract exist.

The linear layout follows the [XboxDevWiki DSP descriptor observations](https://xboxdevwiki.net/DSP),
which also state their limited hardware coverage. Original builder ownership
and original control/sample transfers independently check the reached use.
No GPL implementation body is copied. Private evidence is in
`build/blinx/audio-gp-linear-20261010/qualified-batch.aot-private.json` and
`original-gp-linear-reference.aot-private.json`. The first fixture failure
used format2 while expecting PCM16; its log is retained and the fixture was
corrected without relaxing production behavior. Earlier .113.4/.113.5 reports
remain bound to their original sources. Full boot/graph, in-run P/bit9 reload,
hardware reset/cadence, timer production and EP/output remain open. This adds
no work to the default fast HLE path and closes no broader audio-family gate.

## Distinct EP processor and physical output foundation (.113.7)

The maintained APU now owns separate EP X/Y/P banks, EPSADDR/EPSMAXSGE scratch
transport and EPFADDR/EPFMAXSGE FIFO transport. The fixed EP profile has
3072 X words, 256 Y words and 4096 P words, with no GP MIX alias. EP host
apertures expose only those banks; holes, unsupported widths and EPRST remain
guarded. EP bank storage is lazy, and an explicit `ApuEpProcessor` selects its
own CPU, program-publication epoch and cache owner. The existing GP interface
and its actual VP/MIX lease remain intact. Both processors use one maintained
DMA implementation and the same pinned MIT DSP core, with a fixed hardware
instance selecting banks and physical tables. There is no second persistent
sample graph or direct-speaker path.

The representative authored chain admits a real VP quantum, has actual GP
instructions publish planar samples through physical scratch pages, and has
actual EP instructions consume those pages into its own X bank, form stereo
pairs and publish PCM16 bytes through its real output FIFO. Independent caller
RAM expectations check every output byte and every untouched byte. The chain
covers 30 packets across three APU lifetimes, simultaneous GP/EP owners, an EP
CPU replacement, source/output page remapping, a page-crossing scratch packet
and the original-size output ring's wrap. Nominal source-rate waveform checks
retain the existing tolerance; transport bytes and cursors are exact. Failed
input staging does not publish its bank/node; a later output-page failure may
leave its real prefix while retaining CUR/node ownership and terminal failure.
No EP host access, reset or yield can borrow/release the GP frame lease.

The original driver provides independent ownership evidence. Original
`17B999` configures separate EP output rings of `2000` and `4000` bytes. Original
`17CD84` stages an 800-byte EP prefix and a 1532-byte helper block at P:`180`.
The unchanged entry executes on the actual EP CPU and stops at B2 configuration.
An authored caller executes unchanged original `276` and `2E1`, builds its
five-word linear descriptor in P and stops before sample/node/output publication
because the original control sets unknown bit9. Original `288` selects linear
output, `29A` constructs a four-word noninterleaved FIFO descriptor, and `2B1`
constructs interleaved FIFO output; these observations do not admit their
unknown bit9 semantics. The maintained owner now fetches four FIFO fields,
five linear fields or seven circular fields, according to the actual object.

The [XboxDevWiki DSP observations](https://xboxdevwiki.net/DSP) establish the
distinct bank sizes and shared descriptor structure; the [APU description](https://xboxdevwiki.net/APU)
explains physical scratch communication and the separate audio-codec consumer.
Pinned xemu register locations and original driver accesses identify the EP
tables and apertures. Its GP-sized EP memory map, monitor speaker/silence
replacement and guessed EP cadence are not adopted. No GPL implementation
body is copied. The existing prepared MIT core/library and source samplerate
cache are reused without rebuilding a preserved reference DLL.

This qualifies a bounded physical producer/consumer foundation. It does not
qualify original EP firmware boot/graph/output: dynamic original P/table
loading, B0..B3, bit9 executable reload, hardware EPRST/bootstrap, EP frame
admission/cadence and AC97 consumption remain open. The original helper's
later RUNNING-to-idle wait also needs genuine DMA lifecycle ownership; status
reads never manufacture progress. Supported authored programs explicitly
STOP between transfers and do not replace that original completion contract.
Fine silicon phase/precision questions can remain deferred after each genuine
producer exists. The default fast HLE constructs neither DSP CPU nor EP banks
and gains no processing work. No playback, title run or physical console
operation is part of this batch; the broader audio gate remains open.

Private evidence is retained in
`build/blinx/audio-ep-foundation-20261010/qualified-batch.aot-private.json` and
`original-ep-reference.aot-private.json`. Original bytes stay private. Earlier
.113.4/.113.5/.113.6 evidence remains frozen at its original source identities.
The initial new FIFO-boundary fixture requested one PCM16 word, violating the
existing even-word extent guard. Its failure log and source identities are
retained; the fixture now uses two words without relaxing production behavior.

## Queued DMA completion and dynamic executable ownership (.113.8)

START now owns a real queued GP/EP job and asserts RUNNING before transfer.
One device-work quantum captures and validates a descriptor; the next performs
its actual scratch/FIFO transfer and bank/node publication. Only successful
end-of-list work clears RUNNING and posts EOL. EOL acknowledgement is separate
from idle state. Status reads are passive; there is no read counter, constant
zero completion or helper-specific PC shortcut. Explicit STOP cancels the
unfinished staged job without publishing it, and FREEZE holds real work.
Already completed preceding list nodes and real output prefixes remain visible
on later failure. Failed input staging publishes neither its destination nor
its node, and failures are terminal.

The sole DSP CPU owner supplies serialized actual instruction boundaries. Its
injectable `ApuDmaDispatchPolicy` defaults to one real work quantum per retired
instruction while a job is pending; representative checks also use spacing
two and four. A caller without a CPU supplies explicit bounded device-work
quanta. This is a nominal fair admission/copy policy, not an assertion of
MCPX bus frequency, silicon phase or natural STOPPED bit5. The pinned core's
single-instruction ABI provides the boundary for ordinary effective-address
MOVE as well as MOVEP. Its block escape callback only covers selected opcode
forms, so block execution cannot provide this general boundary. The maintained
wrapper charges the actual consumed cycles against the same core budget,
retains negative debt, and discards only unused positive budget on real yield.
No alternate CPU, scheduler, stack, PC or sample graph is introduced.

Supported input DMA can now publish P after the actual instruction/callback
has returned. Its existing program epoch invalidates the sole core's warm
instruction cache before execution continues at the actual PC. Host executable
publication or nested CPU/device work from a transport callback stays guarded.
CPU reset and owner removal cannot silently discard pending jobs. Failed P
staging preserves the complete bank and epoch and stops before the following
consumer/STOP. These rules do not admit unknown descriptor bit9, original
dynamic directory semantics or full original executable boot.

The representative batch runs unchanged original EP helpers `2D5` (START and
RUNNING observation), `2DA` (real EOL/error check and acknowledgement) and `2D0`
(RUNNING-to-idle wait). An authored caller supplies known-bit jobs. Actual GP
and EP CPUs load changed executable blocks through physical scratch, execute
them using genuine CALL/RET, then publish exact PCM16 consumer bytes through
their own real output FIFOs. Eighteen repeated loads/consumers cover three
lifetimes per hardware instance, dispatch spacing one/two/four, already warm
translated code, page/ring crossings, exact caller RAM/CUR and 4096 passive
reads between transfers. The earlier real VP/GP/scratch/EP waveform chain,
frame lease and original B2/bit9 guards are checked in the coherent batch.

The [DSP observations](https://xboxdevwiki.net/DSP) establish the incomplete
register/descriptor contract and end-of-list event; original helpers establish
the software-required transitions. Pinned xemu's read-count completion is
explicitly uncertain and is not adopted. No GPL implementation body is copied.
Exact silicon work rate/phase and natural STOPPED bit5 can remain isolated
precision questions because genuine transfer/idle/event producers now exist.
B0..B3, original bit9/configuration, full bootstrap/graph, hardware reset,
EP frame admission/cadence and AC97 output remain guarded or open. The default
fast HLE constructs no DSP owner and gains no instruction or DMA-dispatch work.

Private source-bound evidence is in
`build/blinx/audio-dma-lifecycle-20261010/qualified-batch.aot-private.json` and
`original-dma-lifecycle-reference.aot-private.json`. The preceding read-only
audit and all .113.4 through .113.7 proof remain frozen at their original source
identities. Initial build/fixture/debt-boundary failures are retained. This
closes the bounded job/completion/publication dependency; the broader audio
gate and original output qualification remain open. No playback, title run or
physical Xbox operation is part of this batch.

## References and reuse

- Original NTSC private input: full helpers at `0x1789a0` and `0x178c97`;
  original command emitter also uses the selected-voice/routing/format/volume/
  pitch method addresses. Bindings are isolated in the reference fixture.
  Region-prefix and complete unit-conversion bindings are isolated separately
  in `native_audio_source_reference.py`; no original bytes are committed.
- xemu `482088490c2d5983ed5e1eafbd09906157c1eca0`,
  `hw/xbox/mcpx/apu/{apu_regs.h,vp/vp.c}`: register locations, field widths and
  memory-side effects. No new GPL implementation body is copied or adapted.
- Phobos `bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0`,
  `src/hle/dsound_buffer_model.c`: reviewed its frequency/position handling and
  host-time cursor model. Existing native real-output transport is retained;
  that host-time model does not establish the device's sample-consumption floor.

The new configuration handler, strict transport and fixture are authored here.
Existing private-pilot and older dependency restrictions remain documented in
`LICENSE-SCOPE.md` and `THIRD_PARTY_ATTRIBUTION.md`; this is no distribution
clearance for the current combined reference executable.
### Original EP block/directory producer ownership (bounded offline evidence)

Central child `beads-xbox.1.113.10` executes unchanged original resource/heap
allocation, GP scratch-owner and EP staging helpers, including `17CD84` and
`17CBD4`, against explicit offline Xbox kernel RAM services. The authored
`tools/native_ep_directory_reference.py` checks three physical allocation
layouts, six actual original dynamic blocks per layout, both firmware prefixes,
the constant table and all nineteen genuine EP scratch SG entries. Original
destructors release every allocation and restore allocation counters. Failures
at the first, middle and last contiguous allocation return the original
`8007000E` and release their actual partial lifetimes. Original code and firmware
bytes remain unchanged; original contents and captures stay private under
`build/`.

The actual thirty-six-word `P100` directory establishes twenty-four slots.
Twelve retain caller-stack contents until later setup; this evidence neither
zeros them nor admits their consumers. Defined entries include all six dynamic
block pointer/count pairs, the `4000/600` buffer pair, the `1CFE/18` constant
table pair and the `1D16/2000` free workspace pair. The constant table contains
twenty bytes of the original section's mapped zero tail, rather than additional
file bytes.

A private source-bound observer creates exactly one EP core over maintained APU
RAM/banks/DMA. Safe authored callers leave the original static firmware/helpers
intact. Eighty-one real known-bit P transfers include fifty-four complete block
copies across separate caller/failure lifetimes; expected consumer words come
from independently captured original producer RAM. Original dispatcher `180`,
selector eleven, toggles `P116` twice and returns through its actual stack.
Its initialization route genuinely publishes `P19E/P19F` from the free-workspace
pair and clears thirty context words, then stops at the existing EP `C5=FFF`
guard with its actual stack preserved. That guard prevents the following yield
and unidentified `CA=0` write. Selector one executes unchanged `276/2E1`, emits
the genuine `41A2/5BE0/2F2/2C00/C00` descriptor and stops on unsupported bit nine
before dynamic P publication/copy/EOL. Revoked allocation ownership fails before
caller publication and remains terminal.

This qualifies the actual driver producer and supported memory ownership
routes. It does not qualify original dynamic-block execution, unset directory
entries, full initialization/boot, timer configuration, hardware reset/cadence,
AC97 or output. Known-bit transport descriptors are authored; the original
unknown-bit builders remain guarded. The existing maintained APU owner already
implements the supported routes, so this batch adds source-bound qualification
without a new production CPU, scheduler, bootstrap entry or default-HLE cost.

Private proof: `build/blinx/audio-ep-directory-20261010/qualified-batch.aot-private.json`.

## Original EP startup context audit (2026-10-11)

Bounded issue: `beads-xbox.1.113.11`. A private observer executes unchanged
`2EF` and records `C5=FFF`, genuine `C4=1` yield at `PC2F3/SP1`, then, after
an explicit observation-only resume, `CA=0`, `D5=4000`, `D4=4000`, `D7=0`
and actual RET with stack zero. Unknown writes are recorded without assigning
device semantics. All RAM banks remain unchanged. C5 startup clear, unidentified
CA context and the D5 end-of-list seed form one unmet startup contract: admitting
an individual write would leave the following original behavior guarded.

Twenty-four unchanged builder calls cover X/Y/P targets. EP `276/288/29A/2B1`
all set descriptor bit nine. GP `DB/EB` clear it, while GP `133/144` set it.
Thus it is neither an EP-only nor a P-only producer marker; these observations
do not establish its effect. No DMA is submitted in this producer audit.

The [NXP DSP56362 manual](https://www.nxp.com/docs/en/user-guide/DSP56362UM.pdf),
table D-1, maps these aliases to different derivative-specific peripherals
and reserves CA. It cannot supply MCPX CA semantics. Pinned xemu silently
ignores CA and the unknown descriptor bit; that supplies no independent proof.
Production guards remain intact. A coherent future batch must establish the
startup seed/context, real input/event producer, preserved yield/resume stack,
actual queued transfer and reuse/failure ownership together. Hardware work first
needs verified owned SG mapping/bootstrap; the earlier failed SG store and
unmapped inherited aliases preclude retrying the existing bit-nine experiment.
No console operation or production behavior change follows from this audit.

Private proof: `build/blinx/audio-ep-startup-context-audit-20261010/investigation-plan.aot-private.json`.

## Actual ACI PCM consumption foundation (2026-10-11)

Bounded issue: `beads-xbox.1.113.12`. Maintained ACI formerly advanced its
sample counter and completed descriptors without reading PCM payload RAM.
It now reads real owned bytes, with an optional borrowed-byte observer, before
PICB/address/completion changes. Headless operation performs the same fetch.
Failed reads, rejected delivery and reentrant owner/register mutation preserve
the unconsumed cursor and remain terminal. Successful old-buffer completion is
retained if the next descriptor fetch fails. Pause/resume retains the actual
descriptor/offset; interrupt-enable changes while RUN remains set do not skip
data. Last-valid extension, zero-length nodes, ring wrap/remap, reset cleanup
and completion-IRQ reentry are exercised together. Unsupported PCM rates,
reserved formats and odd stereo counts remain guarded. Other streams retain
their existing register behavior.

The [Intel AC97 programmer reference](https://www.alsa-project.org/files/pub/manuals/intel/29802801_801_AC97.pdf),
sections 2.2 and 2.4, supplies descriptor length, sample pairing, completion and
pause/last-valid rules. Pinned xemu's MCPX wrapper corroborates the existing
NAM/NABM layout. No donor implementation body or second APU/DSP owner is added.

Three coherent tests pass. The exact signed-sample consumer/lifetime test and
preserved prior ACI check accompany three actual maintained VP→GP→physical
scratch→EP→physical FIFO→ACI lifetimes, with six complete stereo buffers and
physical remapping. Independently expected nominal input samples differ by at
most one PCM16 LSB through the existing resampler; this retains the precision
deferral. Every complete consumer buffer separately matches a pre-consumption
physical-RAM capture byte for byte. Real queued DSP transfers and accepted PCM
bytes precede completion/IRQ. There is no host audio playback.

This qualifies the supported PCM transport/consumer foundation. The DSP
programs in the coherent fixture are authored supported stimuli. Original EP
C5/CA/D5 startup, bit-nine effects, B0..B3/configuration, original frame
admission/cadence, codec gain/mute/underrun output and full title audio remain
open. Existing fast HLE selection and preserved reference libraries are intact.

Private proof: `build/blinx/audio-ac97-output-20261011/qualified-batch.aot-private.json`.

## Original composed GP/EP startup and shared input transport (2026-10-11)

Bounded issue: `beads-xbox.1.113.13`. The new authored reference tool executes
the unchanged original resource allocator, GP setup `17B870`, EP setup
`17B999` (including the actual `17CD84/17CBD4` producer), and original
destructors over explicitly owned offline kernel RAM. Three lifetimes select
original-encoded caller format inputs 0/1/2, use different actual physical
layouts, and release every allocated page/pool/private-heap object. The small
settings input is a borrowed authored caller fixture, not a qualified original
settings-object constructor/refcount lifetime. FE capacity reuses the existing
synchronous maintained `0x80` floor. Reset/core-control MMIO writes are recorded
only; they do not reset or start the production DSP owner.

Actual GP and EP scratch/FIFO SG tables and defined control/context bytes feed
the maintained APU directly. Both mapped bootstraps preserve all 4,096 loaded
P words and all twelve undefined directory slots exactly, including their real
caller-stack residue. The actual GP and EP CPUs independently reach and stop at
`FFFFB2=FFFFFF`. Revoked allocation ownership rejects EP bootstrap before P
publication and remains terminal. No timer/configuration/wake behavior is
supplied to make the boot continue.

A separate composed transport context executes the unchanged GP prefix only
until its actual PC reaches `23`, immediately before B2. A normal bounded CPU
budget provides that boundary; no yield is resumed or event posted. The original
`9E` producer has then built six real circular `59D2` descriptors with the
original MIX source order. An explicitly authored helper caller of unchanged
`126` publishes eight distinct diagnostic MIX packets through those descriptors.
Each lifetime completes 48 actual DMA nodes and delivers exactly 1,536 DWORDs
into the three original EP scratch tail pages. Independent Python payload bytes
match the captured physical RAM exactly; inactive ring halves and every byte
outside those six published extents remain unchanged. These inputs characterize
transport; they are not original VP/frame/cadence qualification.

The actual mapping distinguishes two owners: GP scratch `8000..AFFF` aliases
EP scratch `10000..12FFF` for the six channel rings, while GP scratch `B000`
aliases a separate original driver status/control allocation. Original EP
dispatcher `180` selector seven chooses channel three at scratch word `4600`,
and unchanged `276/2E1` emits the real five-word
`41A2,5BE0,100,200,11800` input descriptor. It remains guarded at bit nine
before copying or publishing EP input; actual PC `2D7`/SP4 and the produced
physical bytes are preserved. C5 acknowledgement, CA context, D5/D4 EOL seed,
B0..B3, real initial/frame wake and full boot/output remain one unmet startup
contract. The earlier `.113.9`/`.113.11` unknowns are not resolved by this proof.

Private proof: `build/blinx/audio-original-startup-20261011/qualified-batch.aot-private.json`.
Fifteen expected contexts cover the three composed setup lifetimes. Three exact
6,144-byte shared-input captures, old failed fixture logs and source snapshots
are retained privately. Maintained APU/DSP sources and the preserved prior
libraries are unchanged. No new copied donor implementation, dependency,
hardware operation, title run, playback or fast-HLE change is introduced.

Next required dependency: establish the MCPX B2/B0/B1 configuration contract
and genuine B3 elapsed producer, then resolve the coherent EP startup context
and bit-nine input behavior with independently supported semantics. A nominal
ready-buffer owner can use these actual physical publication extents and the
existing sole CPU per DSP; it must not wake EP merely because the observer
finished eight helper calls. Existing hardware mapping failures still require
fresh owned/verified probe RAM before any later bounded DMA experiment.

## Original CPU controls and composed GP wake observation - 2026-10-11

`beads-xbox.1.113.14` adds `tools/native_dsp_control_reference.py` and a private
composed observer. No maintained APU/DSP processing behavior changes. Actual
original x86 `17BC43` and `17BC9E` run their unchanged RMW/IRQL scopes and caller
ABI: selector 1 clears the FE method-mode bits and selects SE XCNTMODE 8;
selector 0 supplies genuine FE HALTED and SE OFF states. Unrelated caller bits
are retained. Owned driver allocation/directory/source identity and original
destructors come from the previous composed producer, with a separately owned
diagnostic PCM loan rather than an invented original wave-buffer constructor.

The private observer keeps one actual GP CPU over the maintained banks/RAM/DMA
and calls the existing frame transactions through their owner friendship. It
is explicitly separate from the production `ApuDspProcessor`. Original
B2/B0/B1 writes are recorded as unresolved inputs; B3 reads take that CPU's
actual retired-cycle count as an explicitly diagnostic sample. Neither is a
runtime timer admission, a measured MCPX frequency or whole boot qualification.
The original core/configuration guards remain unchanged.

The unchanged GP startup reaches its real C4 yield. Actual VP publication
then acquires the MIX lease and posts the event before that same CPU resumes
at P2B, with its original stack/PC. Original P160/P162 polls and acknowledges
the event; the acknowledgement retains the lease. Each original default frame
performs eight actual queued descriptor completions: one six-word context
load, six circular channel stores and one 640-word status/debug store. Actual
P29 yield releases the lease. The real driver context at scratch 800 is zero,
so this observes the original default branch, not dynamic effects or code load.

Three original layouts/settings, each with three genuine VP/GP frames, yield
72 completed nodes and 7488 exact transported DWORDs. Varying owned PCM and
distinct channel attenuation make ordering and duplication errors observable.
An independent Python byte oracle compares physical captures with the actual
published planar bank: channel order 0/2/1/4/5/3 reaches the existing three-page
EP scratch alias, and channels 11..30 reach the separate GP status allocation.
All physical RAM outside the precise output and voice-cursor extents remains
unchanged. Existing nominal VP/resampler evidence is reused; this batch does
not add another ideal precision claim.

Actual original FE HALTED and SE OFF control calls cause no VP publication,
frame event, DSP progress or source read. A genuine source-read failure leaves
MIX unpublished, posts no event and does not resume GP; failed reentry fetches
nothing. All three original driver lifetimes clean up their real allocations,
and the diagnostic loan is released separately. The private proof covers
12 expected contexts. Its initial error-message fixture assumption is retained
with the failing source/executable/log; no original or production bytes were
changed to satisfy it.

Counter identity/configuration is still unresolved. The cached DSP56362 map
does not match these MCPX X aliases. NXP's [primary EFCOP note APR39](https://www.nxp.com/docs/en/application-note/APR39.pdf)
explicitly places its B0..B8 registers in Y I/O space, and the [original xqemu
investigator](https://github.com/xqemu/xqemu/issues/240) still leaves the MCPX
timer operation/configuration unknown. The private
counter/context plan first considers a read-only batch only if already-running
GP firmware can be verified exactly. The last known probe context holds GP at 0,
so it cannot supply new samples. Otherwise an independently owned bootstrap
calibration must resolve the earlier SG-address readback failure before any
counter program. No failed store/load is retried by this batch.

B0..B3, silicon phase/rate/epoch, hardware reset/cadence, coherent EP
C5/CA/D5 startup, bit9, real EP wake and whole original output remain open.
The demonstrated GP producer/transport/lease route can be reused once its
instrumentation contract is established; it does not manufacture EP readiness.

## Fresh owned-RAM calibration prerequisite - 2026-10-11

The independent `.113.15` preparation adds a closed three-stage probe builder:
fresh nine-page uncached allocation with mapping/physical/protection/extent
checks and pinning; retained ownership/table/image inspection; then one SG
store returning the immediate raw readback. Every SG entry addresses retained
STOP-filled input/guard pages. No inherited SG RAM is loaded, neither DSP starts,
and no device operation follows the calibration readback. The finite authored
B3 stimulus is checked on the pinned CPU using injected samples, exact X targets
and STOP. It does not establish MCPX timer semantics. The frozen original
preparation passed11/11 offline checks;93 identities were reviewed.

A root-coordinated distinct hardware observation passes fresh quiet context,
owned allocation and retained inspection. The sole GP SG-address store requested
03E8A000 and returned03E88000. The host stops immediately, with no subsequent
request, restoration, free/unlock or DSP start. No boot/storage operation occurs.
This supplies the exact previously missing readback and proves the observed
kernel mapping/physical/protection/extent/pin and owned-RAM preparation path;
it does not prove that the changed register points to owned storage or permits
bootstrap. Hardware probing waits for owner inspection/restart.

The probe's4KiB alignment was insufficient. Independently matched original GP
and EP allocator calls in all three retained layouts request16KiB SG-table
alignment. The corrected preparation now requests and guards16KiB, rejecting
the exact failed address before any owned RAM or device access. All12 focused
checks pass in5.64s, including three preparation/inspection/calibration layouts,
six raw unexpected readbacks and the unchanged pinned B3 instruction check.
The old code/payload/hardware reply and an initial changed-context fixture
expectation retain their separate source/proof identities. One cleared physical
bit is not a complete silicon mask measurement, and exact calibration remains
required before any start. No production APU/DSP behavior changes.

The [three-stage contract](https://github.com/mstan/xboxlle-probe/blob/feat/av-handshake-probe-20261009/docs/GP-OWNED-CALIBRATION.md)
is concrete and reviewable; original images and console data stay private.
Private evidence is `audio-gp-owned-20261011/qualified-preparation.aot-private.json`,
`hardware-20261011/status.aot-private.json`, `original-sg-alignment.aot-private.json`
and `aligned16k/final-qualified.aot-private.json` under that directory. Actual
SG encoding/calibration, GP boot, B0..B3, original EP C5/CA/D5/bit9/wake, cadence
and whole audio output remain required. The family count stays4/9.

## Finite bit9 discrimination preparation - 2026-10-11

The authored [`native_gp_bit9_reference.py`](../../tools/native_gp_bit9_reference.py)
prepares an input-only clear/set comparison without admitting either bit9
interpretation. Six coherent contexts pass in0.451263s on the actual pinned DSP
core and maintained DMA owner: ordinary SG consumption with four independently
checked words; existing set-bit rejection before any source read or following
instruction; ignored pointer publication; stale EOL; source failure before
publication; and sixteen passive polls ending in a genuine withheld-work timeout.
Whole X/Y/P/MIX banks and all36KiB of physical ownership are independently
checked. The unchanged24 original builder observations are reused.

Root reviewed79 source, executable, input, capture and proof identities. Private
evidence remains under `build/blinx/audio-gp-bit9-20261011`, including
`qualified-preparation.aot-private.json`, `root-review.aot-private.json` and the
concrete `bit9-probe-plan.aot-private.md`. The539-word finite program ends before
its reserved P target; it repairs no status, acknowledges no frame and performs
no playback. This is preparation, not a physical bit9 result.

A future measurement requires separately established owned bootstrap and exact
SG calibration, then an actual16KiB-aligned physical base at or below00600000.
Distinct owned page6/page7 signatures distinguish direct physical from ordinary
SG source bytes without classifying a match as complete semantics. Both actual
high addresses from the stopped calibration fail that floor. No masked reuse,
hardware runner, automatic retry or production APU change is supplied. GP/EP
startup, timer/cadence, bit9, full output and the audio parent remain open;
family count stays4/9 and the26 safe precision deferrals are unchanged.

## Owned marker bootstrap preparation - 2026-10-11

The independent `.113.17` preparation supplies separate closed read-only
preflight, single-reset-store start and bounded read-only observation payloads.
They require a fresh successful corrected16KiB calibration, the actual retained
nine-page mapping/protection/physical/extent and pin record, exact provenance,
all2048 SG entries and all4096 STOP/marker source words. The initial marker must
differ from the token, and a genuine advancing bounded kernel counter is checked
before RESET3. Start returns its immediate raw reset readback and performs no
further device, kernel, time or RAM operation. Root may consider observation
only after an actual returned3; an unexpected reply stops the session.

Observation revalidates the owner and exact controls, bounds marker reads by
100000 polls/50ms and compares all2048 reference-bootstrap P words. All10 checks
pass across70 actual compiled x86 contexts in14.765973s; root verified351 source,
binary, input, capture and proof identities. The pinned DSP executes the authored
marker and STOP, with a wrong-target rejection. The hardware loader/control
model remains an explicit offline assumption. Earlier failed relocation source,
object and log are retained.

The [three-phase plan](https://github.com/mstan/xboxlle-probe/blob/feat/av-handshake-probe-20261009/docs/GP-MARKER.md)
is reviewable; private evidence remains under
`build/blinx/audio-gp-marker-20261011`. No physical console, network, title or
playback operation is part of this preparation. Original GP/EP boot, timers,
bit9, cadence and full output remain required; the family count stays4/9 and
the26 safe precision deferrals are unchanged.

## Distinct low-RAM calibration preparation - 2026-10-11

The `.113.18` prerequisite adds separate closed prepare, inspect and single-SG-store
payloads for the finite bit9 comparison. Actual kernel ordinal166 requests36864
bytes with lowest0, inclusive highest00608FFF, alignment16384 and protection204,
constraining the entire allocation to a base at or below00600000. High, misaligned,
zero or wrong-alias returns fail before owned RAM access. NULL ends immediately
without pinning, another allocation or later MMIO. Every retained page passes
mapping, physical, protection and extent checks before owned access; prepare
pins it and repeats mapping checks before initializing the table and marker.

Seven checks pass across59 actual compiled x86 contexts in4.825409s. Root reviewed
the source and verified217 identities. The exact600000 endpoint has distinct owned
606000/607000 candidate pages; actual prepared bytes flow through read-only
inspection and calibration. The sole SG store returns its immediate raw readback
with no following device, kernel or RAM operation, including unexpected results.
The [reviewable plan](https://github.com/mstan/xboxlle-probe/blob/feat/av-handshake-probe-20261009/docs/GP-LOWRAM.md)
and frozen proof under `build/blinx/audio-gp-lowram-20261011` retain the exact
allocation and ownership assumptions. Prior general-RAM and marker sources remain
unchanged; no console, network, title, playback or DSP-start operation occurred.
Physical availability, successful calibration, bootstrap, bit9, timers, EP and
full output remain required. Family count stays4/9, with26 safe precision deferrals.

## Effects replacement failure state question - 2026-10-11

Independent complete original-source audit `.112.2.15.12.1` confirms the successful
whole DownloadEffectsImage ABI and borrowed-input/owned-descriptor lifetimes.
Its original CPU composition passes twelve successful installs/replacements,
immediate original199 source release and exact cleanup across three lifetimes;
the integrated original-scheduling title installs effects and renders its loading
screen. See the [coupled scheduling evidence](SCHEDULING-LLE.md#whole-audio-effects-setup-on-original-cpu---2026-10-11).

Original location updates and old descriptor retirement precede new allocation;
the maintained native graph implementation retains transactional rollback until
installation succeeds. The bounded `.113.19` investigation now executes three
complete original failure calls under real captured-kernel pool pressure. Failed
descriptor allocation frees/clears the old descriptor, stores the requested
location, clears readiness and leaves output untouched. Failed scratch allocation
after successful descriptor replacement retains the new zeroed descriptor and
clears its scratch pointer while recording the requested size. No allocation
callee is replaced, and no original GP rollback state is inferred.

Default HLE now terminally guards failed effects allocation/install after
releasing the unsuccessful candidate. Continued-call failure parity is explicitly
unsupported: retaining the known previous native resources until shutdown does
not reproduce the original failure postconditions. Genuine Windows commit failure
and actual closed-backend installation reproduce both former negative returns;
the guarded candidates pass, and three successful replacements have byte-identical
logs before/after. The prior twelve-install original success proof is reused.
Source-bound evidence is under `build/blinx/effects-failure-20261011`. This meets
the bounded issue's guarded-unsupported acceptance alternative; whole audio,
continued-call failure semantics and original GP/EP output remain open. There is
no timing-policy, successful playback or performance-path change.

## Native partial-loop ingress provenance - 2026-10-11

The bounded `.113.20` diagnostic now records actual loop start/end and looping/
playing state in version2 ingress headers under the existing XAudio2 owner lock.
Live-window and new-buffer snapshots retain actual initialized/caller bytes and
controls; successful submissions, control changes and lifetime retirement carry
the same process-global capture ID. Caller writes wrap at capacity. Submission
verification uses the captured loop span, including prefix/beyond-loop seeks and
multiple repeats inside one native chunk. Unknown bytes, absent/invalid metadata,
uncaptured control changes and reuse before retirement are rejected.

The [independent verifier checks](../../tests/test_native_audio_ingress.py) pass
16 focused cases. One isolated muted backend run completes in3.359s with two
complete one-second capture windows, zero ingress/timeline overflow and three
buffer lifetimes, including actual reuse of the same pointer with a new ID.
Four lifetime/window reports verify71,506 mono PCM16 submitted frames with zero
byte mismatches. The actual101504-byte ring exercises2432..99072 and offset98784;
a late snapshot includes a caller write made during the gap. A16-byte loop also
repeats inside882-byte submissions, and finite mode retains its real tail.

Private frozen evidence is under `build/blinx/audio-loop-provenance-20261011`.
Removing only the actual loop metadata while retaining literal observed bytes
reproduces854/92 mismatched bytes in the old capacity-based verifier. Those
converted captures and historical movie-repeat version1 negatives remain
excluded; the current verifier refuses to infer their missing controls. Earlier
build/analysis failures are retained separately. This closes the loop-provenance
task only: submission is not consumption or speaker correctness, and original
GP/EP, hardware output, continued-call effects-failure semantics and the audio
family remain open.

## Original audio shared-device adapter - 2026-10-12

The original DirectSound route previously had no complete APU/AC97 mapping in
the native-title runtime. `PILOT_AUDIO_DEVICE=AUTO` now selects one maintained
APU/AC97 pair and held GP/EP processors when ORIGINAL audio uses the shared LLE
memory, scheduling, IO and USB owners. Native and APU_REFERENCE audio exclude
this adapter; incompatible explicit selections fail configuration. No native
buffer, decoded source, independent RAM, clock or processor substitutes for
the selected original devices.

Registered raw CPU MMIO and device work use the engine's synchronous DMA
capability, existing PIT time and shared PIC IRQ5/6. Only established register
fields are admitted. Unknown DSP reset/wake/core state and active FE ordering
remain terminal. ACI creation starts at the current shared clock position;
stopped/drained teardown removes only this owner's mappings. Active or paused
descriptors and failed engine unbinding retain the terminal owner. Compiled
MMIO is guarded until a serialized shared-engine dispatch exists. The runtime
requires a real whole-chunk PCM consumer before codec RUN; absent consumers
cannot discard bytes and publish completion.

Nine focused controls include an actual original CPU thread executing registered
MMIO, original allocation-backed DMA, clock-pumped consumption of512 PCM words,
exact1024 output bytes, shared IRQ6 acknowledge/EOI and drained cleanup. Unknown
reset/FIFO gaps, invalid extents, absent/rejecting consumers, paused descriptors,
partial-open rollback and engine-fault retention pass. The fixture opens no HDD
or storage initializer. Root reviewed29 frozen sources and50 artifact identities,
verified six actual build-selection cases and compiled the changed runtime
translation unit. Source-bound acceptance is
`build/blinx/audio-original-device-20261012/accepted-root.aot-private.json`,
SHA256 `2ed6a4a206695499d15d4da5e20ba00a1b62323f54ebff20b45a2808c702e7aa`.

This closes the bounded wiring task `.113.23`, not A05 or the audio family.
Actual original title startup, GP/EP processing, ISR/DPC, output, timing and
stop/reuse remain required. A03/A04 hardware-dependent semantics stay deferred
to the final console batch; no title, GPU, playback or console run was repeated.

## Original AC97 output consumer - 2026-10-12

`XLLE_PILOT_ORIGINAL_PCM_CAPTURE=<new private file>` connects the selected
original AC97 owner to a bounded PCM16 memory consumer. It copies the actual
DMA-fetched stereo bytes before descriptor completion and records the device
lifetime, original PIT epoch, observed shared PIT and actual codec frame. It
does not add a native buffer, decoder, audio worker or clock. The callback
performs no allocation, file I/O or CPU-service reentry. The default128MiB byte
and262144-stamp limits cover ordinary600-second48k stereo output; unusually
small chunks may exhaust stamps earlier. Any exhausted limit rejects the whole
chunk without truncation or changing already accepted bytes/stamps.

Capture setup creates an exclusive `.partial` file. Complete publication
requires the matching device consumer to retire, then successful write, flush
and close before a non-replacing move to the requested destination. Existing
files are preserved. Failed or active lifetimes retain explicit partial output
and any still-registered callback context. A complete-looking footer in a
failed partial does not certify persistence. Memory acceptance and subsequent
file persistence are separate contracts; file failure cannot undo an earlier
genuine descriptor consumption. No destination retains the before-DMA RUN guard.

Fifteen focused controls pass. The real raw CPU/owned DMA/common clock/PIC path
publishes exactly1024 expected bytes with epoch60000, observed PIT66000 and
codec-frame11. Byte/stamp capacity, failed/finalized consumption and active
publication controls preserve an accepted1024-byte prefix, its whole stamp and
unchanged ACI completion counters. Write/flush/close failures and publication
races preserve partial evidence and existing destinations. Root verifies31
frozen/current sources and71 artifact identities, independently reads the
published bytes/stamp and builds the production library and runtime unit.
Acceptance: `build/blinx/audio-original-pcm-root-20261012/accepted-root.aot-private.json`,
SHA256 `109bbd2b68bea463dfe5fb1a20fda428031d64cd9ee05fc25fbafca6a986f1ba`.

This closes bounded consumer task `.113.24`. Original GP/EP startup and active
FE semantics, actual title ISR/DPC/processing/output, stop/reuse and timing
remain required. No title, playback, GPU or console run was performed.

## Final attract voice census A01 - 2026-10-13

A01 (`.113.21`) closes for the completed 600-GP-second Q03 window. Root acceptance
`build/blinx/q03-final-20261013/qualified-root.aot-private.json`, SHA256
`aee59a1cab5ee2e8e26d6d818f5ddad010b87150cf2e98c0b4bdfc0d2d301ea7`, binds the immutable journal, independent
Sol 6.1 parser/review and existing stage evidence. There are 10,348 complete
accepted records, 1,094 generations, 1,077 played mono ADPCM generations,
112 configured send vectors, 65 Play vectors and 84 nonzero-looping Plays.
New scalar/vector/loop combinations reuse the qualified codec, source and
arbitrary-bin gain stages; no newly reached uncovered stage was found.

The 1,090 terminal releases are observed zero-reference releases. Four buffers
remain at cutoff: two stopped, one never played, and the final diagnostic op01
still playing. No teardown is invented. All recorded loop setters precede their
own generation's first Play and nonzero extents are aligned/in owned source.
Constructor flags are not raw APU flags; absence of mapped filter calls is not
proof that all internal original processing is unreachable.

A02 ordinary/default composition evidence is reused. Downstream DSP graph and
bins 11/12 output still belong to A03/A05. Original initial SECTL, GP/EP/reset/wake/
B0-B3/bit9, active FE ordering and actual original output/lifetimes/timing remain
required under A03-A05. **The audio family remains open.** No test, build, title
or hardware run was added for census closure.


## Narrow GP input bit9 profile admission - 2026-10-14

Eight fresh-owner observations across four clear/set pairs now support GP input controls
`5BE0` (linear buffer15, format6) and `4BD0` (circular buffer14, format2 with
cursor writeback). Linear X/Y/P cases transferred the exact four-word SG
signature; circular Y cases used a 32-byte ring at offset 28, wrapped four words,
and published cursor 12. Every case retained exact descriptors, eight witnesses,
2044 immutable P words and the separately captured four P target words, EOL and
terminal D6 idle. The Y capture aperture was explicitly corrected from the old
unmapped gap to `FE837000..C`; original and reconciled captures remain private
under the probe worktree's `results/gp-shape-batch-physical-20261014`, including
`summary.aot-private.json`. No failed capture was relabeled without corrected
read-only evidence. Exact completed-program retirement followed each case.

The shared DMA implementation admits bit9 only for those two exact controls
on GP input. EP, output, FIFO, format1 and all neighboring bit9 profiles remain
rejected. Existing count, bank, physical transport, MIX lease and executable P
publication ownership guards are unchanged. Larger counts use the existing
bounded transport contract as a profile interpretation: the physical trials
were count 4, not original count 32 or an arbitrary larger extent. Paired offline
byte/cursor/EOL tests at counts 4/32 exercise that distinction and retain negative
profile, extent, ownership and P-publication tests. These observations do not
establish a general meaning for bit9, active-code reload safety, silicon timing,
whole original graph correctness, EP, output or a new family acceptance scope.

Seven focused DMA/linear/lifecycle/processor/EP tests passed. The preserved
original graph, rebuilt against this admission without editing its original
bytes or descriptors, now completes its first4BD0/count32 input. It then stops
at node0093, control4BD2/count32 (circular GP output), after one completed node.
The standalone authored graph contract passes. The graph diagnostic now records
the descriptor and completed-node count on a device-work failure, so an
asynchronous guard identifies the actual next boundary. Private results are in
`build/blinx/audio-gp-original-admission-20261014/`; output remains unsupported
pending its separate owned-RAM clear/set observation. No whole graph or audio
family pass follows from this progress.

## Observed idle GP status correction - 2026-10-14

The fresh owned hardware programs repeatedly observed initial D6=0 and an idle
STOP command returning0, followed by FREEZE8 and UNFREEZE0. The maintained GP
model previously initialized D6 to32 and set that STOPPED flag even with no
active work. GP now starts idle at0, and idle STOP preserves only FROZEN.
This corrects the observed idle path without changing busy STOP, EP, device-work
dispatch or HLE. Busy cancellation and EP status retain their separate existing
contracts and are not newly claimed as physical observations.

A regression checks the observed initial/STOP/FREEZE/UNFREEZE values without
producing RAM writes, nodes or EOL. Existing paired transfers and post-completion
idle STOP also pass, along with active cancellation, processor publication and
EP guards:7 focused tests in5.09s. Private evidence is
`build/blinx/audio-gp-idle-status-20261014/`. No additional hardware run was needed.

## GP circular output and preserved graph qualification - 2026-10-14

Two fresh-owner physical observations, controls49D2/4BD2, copied four Y words
as exact format2 bytes into the SG destination while preserving the entire
alternative direct destination. The32-byte ring wrapped from28 to12; descriptor,
source banks, witnesses and immutable program remained exact. Both observed
STOP/FREEZE/UNFREEZE/START0/8/0/16, EOL80 on poll11 and terminalD6=0.
Each passed separate completed-program hold and original SG restoration.
Private captures and immutable source snapshots are in the probe worktree's
`results/gp-output-physical-20261014/summary.aot-private.json`.

GP now also admits exactly4BD2. All existing extent, bank, physical transport,
MIX lease and executable-publication owners remain in force; EP, linear output,
FIFO and neighboring bit9 profiles remain guarded. Paired count4/32 offline
regressions compare literal packed bytes across noncontiguous SGE pages, whole
RAM preservation, cursor and completion. Count32 is an existing transport
regression, not a physical measurement. Seven focused tests pass in4.83s.

The preserved original graph passes1,501 blocks and73,549 real DMA nodes:
192,078 nonzero output words and14,931 retained scratch bytes match between
the native graph and maintained GP banks/physical SGE transport. No original
descriptor or program byte changed. This closes the bounded `.114` bit9 task.
The shared DSP instruction core and explicit graph shell limit this result:
original boot/framework scheduling, EP/FIFO/AC97 output and actual title
lifetimes still require A03-A05. HLE processing is unchanged; audio remains
open and the family count stays6/9 with26 accepted precision deferrals.

Private model evidence is `build/blinx/audio-gp-output-admission-20261014/`;
graph executable, inputs, source identities and logs are pinned under
`build/blinx/audio-gp-original-output-20261014/`. Probe compatibility checks
retain GP unknown-bit and EP rejection plus16 synthetic DSP control cases.

## Physical GP configuration/counter observation - 2026-10-14

A fresh owned finite observer executed exactly the original X:B2=FFFFFF,
B0=1,B1=1 sequence, without DMA descriptors/START or frame events. Seven B3
samples were0,0,0,0,0,20,280. The16-NOP preconfiguration interval stayed zero;
postconfiguration intervals containing16/256 NOPs plus ordinary sample/store
instructions advanced20/260. These are raw observed deltas, not yet proof of
the clock source, rate, epoch, wrap or progress while halted/yielded.

All38 bank fields, exact counts/stage/token, eight fresh witnesses, preserved
X/Y/P targets and2,044 immutable P words passed. Root bound every reply and
payload to the reviewed owner, including immutable-P verification. Separate
retirement rechecked the completed program and fields before hold and original
SG restoration; both immediate readbacks matched. No old RAM read/free or
B-register restoration was used. Four focused offline tests and17 retirement
scenarios passed. Private summary and21 source snapshots are probe
`results/gp-b3-physical-20261014/summary.aot-private.json`.

This resolves whether the exact original GP configuration can produce changing
B3 samples in this bounded program. Runtime producer/lifecycle admission and
the corresponding EP questions remain in A03; no full audio pass is claimed.

## GP configuration producer and original first yield - 2026-10-14

The existing serialized GP CPU now owns the exact composite profile
B2=FFFFFF,B0=1,B1=1. B3 passively observes actual retired core progress; its
nominal epoch begins only after the successful B1 instruction retires. Callback
reads use the pre-instruction boundary cached outside the callback, with no FFI
reentry or read-driven progress. The isolated policy is `apu_gp_counter.hpp`.
Unsupported values/order, repeated configuration and configuration-register
reads remain terminal. EP's matching passive profile is covered by the later
bounded EP milestone below.

All11 focused tests pass in5.34s using cached backend libraries. They cover
passive reads, instruction budgets, partial/complete profile lifetimes, five
terminal negatives, the unchanged physical observer and the actual original
371-word GP entry. The observer reproduces0/0/0/0/0/20/280 with exact witnesses,
P and source RAM. Original entry reaches its first C4 yield at PC002B without
manufactured wake, DMA, frame or output. Root independently checked all25
handoff pins, ownership, callback boundaries and core cycle accounting.

Partial/active profile reset and detach/rebind remain explicitly unsupported.
A retained APU lifetime marker prevents a new CPU from silently recreating
unconfigured counter state. Untouched CPU reset remains supported. Original
GP profiling branches reconverge after the same required output calls; their
clock identity/rate/epoch/halt/yield/wrap precision is accepted deferral AUDIO-D08.
EP is excluded because its threshold can writeBBBBBB and spin permanently.
Warm reset, actual wake/cadence, EP and connected title output remain active
work. HLE processing is unchanged;6/9 families remain qualified with27 accepted
precision questions. Private evidence and root review are in
`build/blinx/audio-gp-counter-profile-20261014/`.

The independent probe app-entry snapshot also records GP reset0, EP reset1,
FECTL071F8F and SECTL7 before video/network, on kernel1.0.4983.6. The retained
owned kernel's export324 reports the same version. This strengthens launch
context investigation without proving original Blinx entry or authorizing a
guessed initial register value. Initial SECTL context remains an A05 gate.

## Original default-context GP frame through the production owner - 2026-10-14

The retained original driver/scratch/ownership fixture now runs through
`ApuGpProcessor`, replacing its former diagnostic raw-core counter shim. The
unchanged original prefix yields, one real VP frame publishes its actual MIX
bank and event2, and the original firmware completes eight DMA nodes before
returning to C4 yield PC002B. All832 output DWORDs match an independent byte
oracle over the published MIX ordering. Actual shared GP/EP physical aliases,
unchanged P, cleared frame event, released MIX lease and preservation outside
the known voice/output extents are checked. EP is not started.

The diagnostic PCM/voice loan remains authored; original driver allocations,
scratch, default control packet and descriptors are retained unchanged. Zero
flags take P0064 to P009D, so P0066 program reload and P0084 parameter reload
are not exercised. Actual nonzero original effects/control publication and
acknowledgement are the next required GP step. No full shell, original wave
constructor, hardware reset or whole audio pass follows from this one frame.

The small owner/checker build, one-frame byte/ownership contract and two input/
output isolation controls pass using cached libraries. The checker is registered
as an explicit artifact CLI under the DSP build selection; it does not create
an automatic private-data test. Root verified45 pins including the local header
closure, exact frozen inputs and output/MIX bytes. A stale build-file pin in the
second handoff was corrected by preserving the original build CMake byte-exact
and separately identifying the registration build; earlier handoffs remain.
Current proof is `build/blinx/audio-gp-continuation-20261014/handoff-v3.aot-private.json`.

## Original effects publication and acknowledgement - 2026-10-14

The actual original177AB4/1778D3 upload now allocates, decodes and publishes the
retained effects asset, then pauses at its real177A3A wait. One production VP/GP
frame executes60 DMA nodes, loads the exact decoder-produced program and clears
the context flags through actual GP DMA. Returning591 changed bytes from recorded
device writes to unchanged, uniquely owned live original allocations lets the
original wait return EAX0. Stack, nonvolatiles and borrowed asset remain exact.

The authored allocation fixture needed page alignment for its reached413696-byte
pool request. A separate actual original-kernel allocation/lock/101-page
translation/unlock/free case establishes that boundary; the production SG guard
was retained. The diagnostic PCM/voice and checked physical/pin floor remain
authored inputs. Two malformed flags/count cases stop before frame output.
Root verified43 handoff pins, the ownership/return path and six additional
registration/core/library identities. Existing graph numerical evidence remains
separate; this fixture establishes one original upload and acknowledgement.

The explicit artifact checker is registered as `pilot_original_gp_publication_test`.
Evidence and root review are in `build/blinx/audio-gp-publication-20261014/`.
All1712 initial parameter words now also match the original decoded source at
the actual pre-effects PC0037, after four reload/ack DMA nodes. The reached
flags3 upload exercises both P0066 program and P0084 parameter reload. No separate
later effect-data/commit caller is established by the retained caller/binding
audit; an unreached flags2 transaction is not an additional family gate.
Root verified54 pins for `handoff-initial-state-v2.aot-private.json`; it binds
the changed checker and fresh capture. Its rebuilt executable supersedes the
earlier binary, while historical captures/logs remain retained.

EP configuration/wake/reset, active FE ordering, initial
SECTL context and connected title output remain required. HLE is unchanged;
the family total remains6/9 with27 precision deferrals.

## EP passive configuration producer - 2026-10-14

The existing EP CPU now accepts exactly B2=FFFFFF, B0=1, B1=1 and exposes
passive elapsed core progress at B3 through the same reviewed post-retirement
epoch policy as GP. Fixed-kind program ownership and separate retained APU
markers prevent cross-DSP state reuse. Partial or active reset and warm rebind
remain terminal; unsupported values, order, repeats and config reads still stop.

Nine focused checks pass in8.23s using cached backend libraries. The frozen
physical EP observer matches all34 fields,2048 loaded P words and seven raw
samples0/0/0/0/0/20/280. Budget variants, unchanged RAM/GP banks, independent
GP/EP lifetimes and prior GP observer/original-prefix behavior also pass. Root
verified53 handoff pins and reviewed callback/ownership boundaries. Maintained
CTest targets cover the new EP contract and terminal guards. Private evidence:
`build/blinx/audio-ep-counter-profile-20261014/handoff.aot-private.json`.

At this checkpoint, unchanged full EP entry reached P0017 (`BSET #20,OMR`) and stopped at
the retained unsupported stack-extension mode guard, SR=C00300/OMR=100300.
This is earlier than the first unsupported peripheral operation C5=FFF at
P02EF. The original-entry check asserts that actual boundary; it does not
claim C5 or first-yield reachability. The initial failed check is retained.
No C5/CA/D5 event, cadence, output, reset or 708000 threshold behavior is newly
admitted. AUDIO-D08 remains GP-only. The observed raw hardware FF14=100 is
not normalized or assigned a meaning. Hardware remains deferred at the owner's
request while documented stack semantics are investigated offline. HLE is
unchanged; the family total remains6/9 with27 accepted precision deferrals.

## Resident EP stack startup - 2026-10-14

`native_dsp_donor.py` now applies a maintained, pinned-source patch from
`native_dsp_sen.py`. The [DSP56300 family manual](https://www.nxp.com/docs/en/reference-manual/DSP56300FM.pdf),
sections5.4.2/5.4.3 and restrictionsA.3.2/A.3.9/A.3.10, supplies the resident
stack contract and the boundaries deliberately left guarded. SEN is admitted
only at SP=SC, depths0..14 within SZ, with no extension status/history or pending
interrupt. Ordinary nested calls and natural DOR completion use the existing
hardware stack. Spill/refill, direct task/configuration changes, explicit SSH/SSL,
early loop abort and interrupts stop. Empty mode transitions are validated;
double-push/pop operations preflight before stack, LC or SR changes. Already
retired instruction PC movement is not rolled back on a later guard.

The unchanged original EP entry now reaches P02EF's C5=FFF attempt through the
actual production EP owner. Maximum stack depth is3, final SP=SC2, EP=BE0 and
SZ=32. Original P1..5 startup values, saved directory fields P19E/19F and30 cleared
P122..13F context words match exactly. X/Y, including the proposed extension
tail, remain untouched; GP banks and source RAM are preserved. This result
supersedes the preceding SEN abort as the current original-entry boundary.
No C5 acknowledgement, event, yield, DMA completion or frame is manufactured.

Validation passes6 Rust resident tests,30 single/block terminal negatives,
2 empty transitions, both original-prefix checkers, and24 existing non-SEN
stack/control/DOR regressions. The maintained CMake guard wrapper and ordinary
EP packet/guard regression also pass with the rebuilt backend. Root's one
actual original GP effects frame performs60 DMA nodes, loads all1712 initial
parameter words correctly, and reproduces accepted RAM and device writes byte
for byte. Frozen producer inputs were reused; no title or audible test ran.

Private worker proof is
`build/blinx/audio-ep-stack-extension-20261014-worker/handoff.aot-private.json`;
root independently verified72 pins and regenerated all8 patched Rust sources.
Root proof and the updated original-entry/bit9 checks are in
`build/blinx/audio-ep-stack-extension-20261014-root/`. Maintained DSP builds expose
`pilot_dsp_sen_guards`; original EP image checks require explicit private input.
Use fresh donor/Cargo directories so earlier evidence keeps its backend identity.

Full stack extension is unimplemented, but is not a new family gate unless the
supported title route reaches it. C5/CA/D5 event behavior, wake/reset, cadence,
EP708000 threshold, active FE ordering and connected output remain required.
Hardware stays deferred, HLE processing is unchanged, and the total stays6/9
with27 accepted precision deferrals.
