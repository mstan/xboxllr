# Native audio buffer notifications

Tracking: `beads-xbox.1.137`. Actual dashboard ambient initialization requests
buffer flags `0x1a0`, including position-notification capability `0x100`. The
original caller installs an event for each block at block-end minus the source
sample alignment, frees the caller's temporary list, then consumes those events
through real waits. Returning success without event delivery would leave its
streaming worker stalled.

The shared implementation admits notification-enabled PCM and Xbox ADPCM
buffers with sample/block-aligned source offsets within their storage and up
to 64 positions. ADPCM source offsets use the existing 64-frame decoder block
conversion to the transport's PCM byte positions. Original wrapper/inner
body review proves three stack arguments/RET12, an owned copy of the 8-byte
guest position/handle pairs and sorted offsets. Native storage separately owns
typed event references; the transport owns independent duplicates. Guest close
and storage detach/rebind cannot invalidate those references. A bad later
entry releases all staged references before replacing any configuration.

The feeder observes the real source voice's monotonic `SamplesPlayed` counter.
It signals positions only after the corresponding PCM frame is consumed,
including loop wrap and multiple crossed loops. Existing completion wakes and
the bounded feeder wait schedule observations; elapsed host time never invents
sample progress. Seeks start a new observation epoch; removing positions and
destroying voices release their references. Storage replacement installs the
retained list on the replacement voice before publishing it.

This is a native HLE completion transport with a retained contract seam.
Precise original interrupt timing, byte-within-sample/block edges and stop
sentinels remain guarded/unqualified. Native decoded-PCM transport
is selected explicitly; the original scheduler/reference APU and stereo-only
transport do not acquire unimplemented notification support. Existing generated
Blinx headers without the capability marker retain their admitted path.

Prepared authored checks cover real event delivery from consuming PCM, no
pre-play signal, failure before list replacement, reference survival after
guest close and storage replacement, removal during continuing playback, and
complete lifetimes. The initial PCM-only build and all eight combined affected
checks pass in 3.50 seconds. Actual run `20261019-154817-r0z95sk6`, executable
`c1fa1687`, creates the original 32,544-byte stereo ADPCM ambient buffer and
starts two native workers. It stops at the deliberate PCM-only notification
guard. The subsequent block-offset adapter and extended fixture build and pass:
nine combined notification/headroom/source-versus-LLE/event/mutex/media/storage/
scheduler/thread-lifetime checks complete in 4.99 seconds. The notification
fixture covers both PCM and ADPCM, including storage replacement after guest
event closure. Actual run `20261019-155202-2ac167tr`, executable `07ad5396`,
passes the reached notification setup and enters the ambient workers. One
worker next stops at `KeDelayExecutionThread` (99) after its first audio-service
call. No menu frame or sustained ambient stream is claimed.
