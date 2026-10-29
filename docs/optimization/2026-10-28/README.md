# Accepted native architecture package, 2026-10-28

Tracking: `beads-xbox.1.206`, optimization campaign `beads-2wy8.1`. Owner accepted the candidate visually/audibly and authorized integration and a new Blinx release.

The package replaces generic API submission boundaries with native-effect ownership, gives native mappings authoritative commitment/logical-access views, and retains immutable blend/depth/raster objects across material changes. Native CPU execution and HLE were already present. Existing kernel/device/file/scheduler ownership and portable fallbacks remain.

Matched full-package comparisons use frozen framework `68c04bd5dcaef0b7fd5f56292d5411ce2fd9d4b9` and qualified code `1a794cdfab523bb9392b3c9f1d1d1c2a58036cb9`, with current title features and Release IPO. `qualification.json` preserves every final A/A and alternating A/B run, hashes, settings scope, costs and limitations.

| Workload | Result |
|---|---|
| Blinx Stage2-1 on NAS | Three CPU reductions 10.70/15.39/19.32%; original-update throughput changes +6.25/+26.66/+27.14%. Paired medians about 15% less CPU and 27% more updates/s. Positive in this workload; busy-host magnitudes vary, initial A/A CPU variation 8.14%. Candidate host load exceeded its paired control in every pair. |
| JSRF USA Garage | Conservatively neutral/inconclusive overall: lower CPU observations, source swaps essentially capped60/s and host-load confounding. |
| JSRF rev4 Garage | Conservatively neutral/inconclusive overall: lower CPU in all 3 pairs, mixed throughput including a retained negative7.94% pair within the broader observed baseline range. |

Each game is judged independently; aggregate wins cannot offset a reproducible regression. Original updates/source swaps are not interpolated display frames. Similar observed working sets do not remove the deterministic 1 MiB mapping metadata cost or the bounded pipeline-object storage. Menu-route timing includes fixed navigation waits and screenshots and is not a cold-load benchmark.

All3 staged builds passed the documented functional gates. The frozen-source legacy allocator assertion remains explicitly shared, not counted as passing. Assisted navigation/save routes do not establish organic story completion or exhaustive REC clone behavior. Native saves/settings are preserved; save-state backward compatibility is outside the contract.

## All27 opportunity dispositions

Five entries produced implemented changes;18 are deferred with concrete reopening conditions;3 were already covered and 1 selected reuse. This is not a claim that every possible architectural improvement is exhausted.

### R01: One native operation stream for named XDK calls and inline producers

Implemented and accepted for integration.

Retain required behavior and qualify any later extension independently; current package passed the final per-game gates.

### R02: Versioned graphics state across inline writes and services

deferred.

Complete page-write observation is a feasible alternative to assuming named setters own all writes. Current phase samples include actual backend state application as well as byte observation; snapshot arming/reuse is not automatically cheaper than the small hot arrays. Select the measured immutable-object churn first.

Reopen: Reopen with separated built-game cost for guest-array observation after the retained package, then compare certified views against byte observation including hot-page/native-writer fallback costs.

### R03: One CPU/GPU resource lifetime and residency model

deferred.

Existing resource indices, consumed-range validation, texture versions and GPU residency already cover substantial ownership. A unified asynchronous/COW resource graph remains feasible; current snapshots do not isolate a dominant transfer/readback or alias bookkeeping cost.

Reopen: Reopen on an inclusive resource-validation/transfer hotspot or observed producer/render-owner overlap blocked by mutable payload lifetime; qualify all aliases and native writers.

### R04: Prepared draw plans survive incidental queue flushes

Implemented and accepted for integration.

Retain required behavior and qualify any later extension independently; current package passed the final per-game gates.

### R05: Decode and validate inline commands once

deferred.

Blinx does not compile the packet decoder. Both JSRF revisions have a real initialized arena with zero methods/submissions at native-load and two-area checkpoints; USA baseline steady-Garage observations also provide controls. There is no demonstrated decoder work to remove in these workloads.

Reopen: Reopen when an actual title/workload consumes packets at material cost. Zero checkpoint counters are not coverage of every scene or proof against transient owner resets.

### R06: Dependency-based GPU completion and guest callbacks

deferred.

The earlier timestamp-only overlap prototype remains rejected. Current caller snapshots distinguish real callback ordering waits from ordinary frame waits; wall wait samples do not prove removable CPU work. A coherent completion/dependency owner must include guest callback writes and DPC eligibility.

Reopen: Reopen on attributed GPU-versus-callback/CPU eligibility latency plus a complete dependency/effect contract; retain actual GPU completion and callback order.

### R07: Render/presentation ownership independent of guest CPU preparation

deferred.

Source images/interpolation are already GPU owned. A dedicated native submit/present owner is feasible with immutable command/resource publication, but current thread attribution does not establish presenter CPU as the main obstacle. It depends on the R03/R06 lifetime contract.

Reopen: Reopen on measured graphics-lock/presentation tail or input-latency loss with source cadence, queue depth and native-context access separated.

### R08: Shader and pipeline preparation by immutable identity

Implemented and accepted for integration.

Retain required behavior and qualify any later extension independently; current package passed the final per-game gates.

### C01: Larger native CPU regions and a typed service ABI

deferred.

CPU bodies are already static native with IPO. Larger local regions and typed exits remain possible; the current call chains include renderer/OS work and are not inclusive proof that virtual register materialization dominates. Address-cache/large-body history did not establish a win.

Reopen: Reopen with a current hot pure CPU region or materialization cost separated from native services, then qualify flags/FP/SEH/indirect calls and suspension.

### C02: Replace complete hot SDK/game algorithms

deferred.

Whole SDK/game algorithms can replace instruction-shaped work, but the prior matrix binding removed 30.9 percent of counted guest instructions without a proven substantial wall gain. The sampled material producer includes substantial renderer work; source call counts are not an algorithm speedup.

Reopen: Reopen on an inclusive hot complete algorithm and source-closed memory/numerical/ABI contract; compare native reuse and built-game cost.

### C03: Callback and helper context materialization

deferred.

Full integer/SIMD/x87/environment preservation occurs at specific reentrant helpers, not every HLE entry. No current recurring materialization hotspot is established.

Reopen: Reopen on dynamic frequency and inclusive context-save/restore cost at a certified boundary; preserve nested callbacks and host/guest FP environments.

### C04: Normal-play function-entry and checkpoint bookkeeping

deferred.

Normal entry bookkeeping retains fault evidence and required safe points; full-session flow hashing is conditional. Current wall stacks cannot isolate inlined entry bookkeeping, so absence of its name is not a rejection proof.

Reopen: Reopen with targeted uninstrumented cost attribution that separates optional evidence from required higher-priority readiness, suspension and replay boundaries.

### M01: Authoritative complete memory accounting

Implemented and accepted for integration.

Retain required behavior and qualify any later extension independently; current package passed the final per-game gates.

### M02: Resource allocation/retirement around lifetimes

deferred.

Host ownership is already independent of original pool buddy lists. M01 removes complete-budget reconstruction; further indexed lifetime/arena work needs allocation-churn evidence rather than changing guest placement semantics.

Reopen: Reopen if allocation/free lookup or lifetime churn remains a material runtime/load-tail cost after M01; validate exhaustion, reuse and stale handles.

### M03: Writer ownership instead of repeated observation

Implemented and accepted for integration.

Retain required behavior and qualify any later extension independently; current package passed the final per-game gates.

### L01: Whole asset loading transaction

deferred.

Reuse the existing stall issue beads-xbox.1.100. The historical approximately 25-second stall was not reproduced in later short entries. Current native-save and actual resource-loading routes pass; they do not establish cold critical-path attribution.

Reopen: Reopen on a reproduced material cold/warm load stall with read/decode/relocation/allocator/upload/completion timing; preserve async IOSB/event/APC and native saves.

### L02: One immutable-content cache and preload policy

deferred.

Resident reads return before FSC locking/copy/store, so the hypothesized duplicate resident/FSC byte cache is already absent. Lazy residency/transform sharing remain workload tradeoffs, not free wins.

Reopen: Reopen on measured duplicate transform/staging bytes, cold-start latency or peak resident pressure; retain file sharing/identity, streaming exclusions and writable guest payload semantics.

### V01: Movie decode-to-display/audio pipeline

deferred.

Packed-YUV conversion and movie observation paths exist, but draw-heavy gameplay does not establish a hot movie decode/display/audio transaction. Diagnostic readback must not be mistaken for normal movie cost.

Reopen: Reopen with an actual movie workload and isolated decode/conversion/upload/mixer cost plus matching frame color/range, A/V timing and dynamic stream behavior.

### A01: One versioned PCM/decode buffer lifetime

deferred.

Versioned decoded PCM and completion-owned spans are feasible replacements for copies, but current copies enforce live playback/guest-writer lifetimes. Main-thread samples do not establish PCM/decode as a dominant current cost.

Reopen: Reopen on measured uploaded/decoded/copied bytes and inclusive cost in music+SFX scenes; cover partial/wrapped locks, looping, seek and release during playback.

### A02: Audio services, voice updates and mixer quantum ownership

deferred.

A single typed voice/quantum owner could combine audio service and backend work. Atomic spatial operation sets already exist. The sampled XAudio2 thread cost is substantially below combined renderer/main/driver work, and is not all removable service overhead.

Reopen: Reopen with service/lock/refill and tail-latency attribution, preserving synchronous observations, cursor/notification timing, pause, effects and A/V sync.

### A03: Whole downloaded DSP graph specialization

deferred.

The actual downloaded DSP graph already executes through JitEngine. Whole graph/dataflow specialization is a valid later alternative, but no interpreter-removal opportunity or isolated DSP hotspot is established here.

Reopen: Reopen on inclusive DSP-graph cost after audio transfer/mixer separation and a bit/sample/state contract for recognized and dynamic programs.

### S01: Native CPU eligibility and service wait ownership

Retain existing native ownership; no new gain claimed..

Reopen: Current matched gameplay shows material remaining wait/admission/poll cost with a source-qualified replacement.

### S02: Host events and qualified guest polling

Retain existing native ownership; no new gain claimed..

Reopen: Current matched gameplay shows material remaining wait/admission/poll cost with a source-qualified replacement.

### I01: Input reports and host event pumping

Retain existing native ownership; no new gain claimed..

Reopen: Current matched gameplay shows material remaining wait/admission/poll cost with a source-qualified replacement.

### D01: Recording, replay and diagnostics consume owned data

deferred.

Normal play skips full-session evidence work; input-route recording is a distinct feature, and screenshots/readback are request driven. Owned immutable operations could feed recording once, but feature cost needs its own workload.

Reopen: Reopen on owner-enabled recording/replay/capture cost while retaining journal order, divergence evidence and real audio/video timing.

### B01: Preparation, admission and launch work by verified identity

deferred.

Launch/admission already verifies source, preparation and binary identity; shader caches exist. No material identity-check startup blocker is demonstrated. Build/generation time is not the stricter runtime target.

Reopen: Reopen on measured cold/warm time-to-game attribution to verification, helper creation or preload; retain receipts, prefilled game and isolated running staging.

### X01: Substantial component reuse or replacement

evaluated-reuse-selected.

Compare whole ownership changes, not only renderer swaps. This campaign reuses the complete existing write observer and immutable sampler-cache ownership pattern for native mappings and all pipeline-state kinds. A new backend would still require producer/resource/completion contracts; no qualified drop-in removing that work was found.

Reopen: Reopen with a concrete compatible component removing an attributed cost, including all title features, supported hosts and matched runtime evidence.

