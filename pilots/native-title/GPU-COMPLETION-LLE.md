# GPU completion, callbacks and visibility LLE

Active family `beads-xbox.1.109`, after qualified filesystem `.108`. Burndown:
**4/9 complete, 5 open**. This checkpoint fills an original-reference execution
gap; the selectable title route is still required before family closure.

## Executed floor

`tools/native_gpu_completion_reference.py` binds original NTSC XDK execution
and the maintained NV2A PFIFO/software rasterizer to the same 64 MiB RAM, with
real MMIO, report DMA, semaphores and traps. Preowned device/pushbuffer/report
storage bounds the fast paths; no allocator or kick result is substituted.
Entry addresses come from title analysis; End's call identifies the kick.
Private device-layout offsets are isolated in this hash-bound probe.

Nine distinct cases pass across three independent device lifetimes:

| Contract | Actual execution |
| --- | --- |
| Original Begin/End/kick/Get | Real triangle rasterization gives 400 visible, 0 occluded, 200 partially occluded samples; empty index reuse gives 0 |
| Pending reports | The actual PGRAPH puller gate holds draw/report work; original Get returns incomplete with untouched outputs. Reenabling it performs actual report DMA |
| Result ownership | Indexed report reuse, repeated Get and optional timestamp outputs; real color/depth writes accompany sample counts |
| Original READ/WRITE packets | InsertCallback and kick raise traps 6/7 with actual routine/context; a later marker cannot execute before acknowledgement |
| Original fence waits | InsertFence submits real semaphore writes 2/4/6. BlockOnFence stays in its original polling loop while the puller is held, returns after DMA, then returns immediately on repeat; stack/nonvolatile registers survive |

PFIFO can prefetch through PUT while PGRAPH is blocked. DMA_GET does not prove
draw completion. The callback cases assert trap/later-method ordering, not CPU
ISR delivery. The query fixture now executes full End/kick, which the earlier
standalone oracle only reviewed. It is not a new hardware or xemu run.

Original kick exposed missing PFB write-buffer control at `0xFD100410`. The
model now publishes CPU memory writes with a host fence before returning
flush-complete. Its CPU/DMA callbacks already commit synchronously to coherent
RAM; there is no deferred write queue to discard. This neither signals draw
completion nor releases a blocked PGRAPH puller. Unqualified controls/sizes stop.
Pinned xemu supplies flush-bit/status behavior; no GPL implementation body is
copied. See THIRD_PARTY_ATTRIBUTION.md.

```powershell
cmake --build build/visibility-lle --target xlle_nv2a_reference test_nv2a --parallel 4
$env:PYTHONPATH='build/reference-python;tools'
py -3 tools/native_gpu_completion_reference.py --xbe <owned-default.xbe> --symbols <default_xdk_symbols.json> --functions <disasm/functions.json> --library build/visibility-lle/libxlle_nv2a_reference.dll --dependencies C:/msys64/mingw64/bin --output build/blinx/gpu-completion-original.aot-private.json
build/visibility-lle/test_nv2a.exe
```

XBE SHA-256:
`e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249`.
Reference library SHA-256:
`9d2d989ec66add3cff8f63614cde5ad2d515dfb2fa287769b2bad84b5657943d`.
The private result retains function hashes, MMIO and all lifetimes. The NV2A
suite passes; SDL-dependent OpenGL cases explicitly skip in this software build.

## Donor decision and remaining work

Phobos `63f18f0334fbd8c00313c4bb565accf00627a4fd` remains the native strategy
reference. Its immediate callback and eventual invented visibility count in
`hle_d3d8.c` are unsuitable. Its optional `nv2a_pb_exec.c` offers decoding ideas
but describes skipped geometry; it cannot establish this correctness floor.
Reuse our maintained NV2A and original command behavior. No donor body was copied.

Next connect a build-selected title route with actual callback dispatch and
resource ownership. Visibility must consume real rendered work; native counts
cannot be relabeled as LLE evidence. Record any dependency on the broader
graphics family instead of introducing duplicate production rendering.

Then qualify pending/error/order/reuse/teardown and run Blinx. Preowned fast
paths do not qualify allocation/refill, arbitrary IRQ latency or all resource
busy cases. Existing display-clock timestamp resolution/epoch tolerances remain.
These bounded geometry cases do not establish full-scene raster correctness.
