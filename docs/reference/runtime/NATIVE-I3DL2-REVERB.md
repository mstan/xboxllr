# Source-native I3DL2 coefficient updates

Owner: beads-xbox.1.148. The optional `original-native-i3dl2-v1` admission
retains the game's original native coefficient calculation. The reviewed
JSRF revision has fifteen functions totaling 2171 bytes, with no indirect,
hardware, kernel or external call dependency. Every body, return cleanup and
direct-call closure is checked against the original image before admission.
The original public setter and driver-commit bodies establish the parameter,
control and coefficient layout; they are not replaced by a success stub.

The shared native CPU service saves/restores general registers, FP/MMX/SIMD,
FS/SEH, dispatch metadata and the host floating environment. A short bounded
arithmetic call retains its native CPU permit and suppresses generated yield
checks while holding the audio owner. Pending requests remain intact for the
ordinary handoff after the service. This service cannot run arbitrary or
blocking guest code. The same portable context storage serves file APCs.

Listener reverb parameters remain pending until an original immediate apply or
CommitDeferredSettings. The calculation receives the CPU shadow of the current
effect state and an owned temporary workspace. Its actual control word and
264-byte coefficients publish as one validated patch batch between GP audio
quanta, without resetting source queues, the graph or existing effect history.
The workspace retires before returning. Device retirement clears parameter
ownership. Effect replacement after reverb settings remains an explicit
lifecycle boundary until its original behavior is qualified.

CPU effect state is now separate from live GP X memory. This follows original
GetEffectData's shadow read, rather than exposing evolving DSP execution as
the API cache. Returned state descriptors reference that shadow. Patch batches
validate all effect/range/alignment bounds before changing either the shadow or
live GP storage. Partial-word operations remain unqualified. Both the native
XAudio2 owner and the caller-driven reference backend use this interface.

The actual native JSRF calculation's 66 coefficient DWORDs match an independent
execution of the unmodified original x86 helpers exactly, under both original
CW027f and CW037f. No original callee is replaced in that comparison. Eighteen
combined DSP, file APC, wait, thread/FP/lifetime and mutant checks pass in 3.47
seconds. The authored GP program changes live state independently of its CPU
shadow; malformed patch batches leave both untouched, and valid patches reach
real GP execution. Native and caller-driven backend checks remain separate.

Actual JSRF passes reverb setup and loads its original LogoE.dat textures. The
next explicit boundary is changed deferred render state 104 before drawing.
This is startup/component evidence; complete scene and repeating attract
acceptance remain open. No physical Xbox is needed for these software checks.
