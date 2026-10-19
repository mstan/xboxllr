# Native device mix-bin headroom

Tracking: `beads-xbox.1.137`. Dashboard ambient initialization exposed a missing
shared audio capability after the native mutex startup calls passed. The
original caller creates DirectSound, then sets bins 0..30 to zero headroom.
The previously selected method fell into the buffer-only dispatch branch and
stopped on the device interface. The device and buffer identities themselves
were correct.

`IDirectSound_SetMixBinHeadroom` is now a distinct device operation. Its
exact-image, body/RET-qualified profile admits the original three-stack-argument
wrapper. Older generated Blinx headers remain usable without this optional
capability marker. New binders emit the marker and a unique out-of-range enum
when the method is absent. No title address is in the runtime implementation.

The original setter stores the low byte of the amount in device settings; its
unchanged hardware helper writes `amount & 7` to the selected submix method.
The shared maintained 2D send model and pinned xemu voice processor implement
the corresponding power-of-two attenuation. The native transport applies that
device gain to the mixed bin before GP input quantization and clipping. Gains
are sampled once per host processing quantum; updates affect existing and
future voices without altering their own volume or headroom. The original
voice default and caller's separate 1,200-unit voice headroom remain separate.

The actual native output tap observes these gains; it does not synthesize
results from cached settings. A focused fixture covers Xbox stack cleanup,
nonvolatile registers, amount masking, independent bins, setting before voice
creation, live changes, GP output and full device/voice lifetimes. The reference
backend forwards the same method to the maintained APU; a stereo-only backend
guards this unsupported multi-bin operation. Invalid bin indices stop before
mutating the native mixer. Precise hardware update latency is not qualified.

Original dashboard caller/wrapper/helper body hashes and disassembly are kept
in private `audio-extra-apis-assessment.aot-private.json`. The existing
[original headroom component tool](../tools/native_audio_headroom_reference.py)
records the earlier full helper/method/gain comparison; this is distinct from
an execution claim for the dashboard's complete audio initializer.

The optimized native executable builds. The integrated device/ABI/output
fixture passes in 0.81 seconds. The existing PCM/ADPCM source-versus-LLE
comparison now varies all 32 bins' headroom and restores them while playing;
all four routing/gain scenarios per format/channel pass in 4.46 seconds.
Its diagnostic capture waits on the real audio-processing completion event,
with a five-second ceiling, instead of assuming 180 ms of host sleep produces
4,096 frames. The feeder lock is released during that wait. The first fixed
sleep attempt failed on frame count; this was a test synchronization failure,
and the sample assertions are unchanged.

Actual muted run `20261019-153440-wiqgqzja`, executable `d253eef1`, passes
the reached mix-bin setup and stops at buffer flags `0x1a0`, after 193,957 native
entries, before a frame. Its next required capability is notification-enabled
streaming buffers. That actual executable predates the diagnostic capture-wait
change; the final integrated fixture contains it. Ambient streaming, menu
rendering/navigation and original interrupt timing remain unqualified.
