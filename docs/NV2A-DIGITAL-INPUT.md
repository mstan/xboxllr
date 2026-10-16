# Fixed SD digital input contract

The maintained NV2A and Conexant owners expose the active 640x480 picture at
the encoder's digital input. This boundary precedes the encoder's 4:2:2
decimation, flicker filters and analog stages. It is neither an analog TV
picture nor a timed stream containing porches, DE and every bus edge.
Whole-family qualification remains governed by
[the Blinx closing checklist](BLINX-QUALIFICATION-CLOSURE.md).

## Evidence and supported profile

The reached original NTSC setup supplies all eleven display commands, all
fourteen FP timing words, CONTROL10100111 and final DEBUG21101100. Admission
requires that exact retained tuple, established clock/routing, slave display,
640x480 BGRA storage with pitch2560, GENERAL_CONTROL00100030 and a complete
identity DAC ramp. The FP profile disables scaling and selects the full active
picture. Different timing, matrix-command or scaling profiles remain guarded;
matching opaque command words does not assign meanings to their individual bits.

An independent physical Xbox digital-bus receiver configures 12-bit DDR444,
the matching edge packing, SD BT.601 interpretation and limited-range YCbCr:
[X-HD adv7511_xbox.c, pinned revision69babd44](https://github.com/Team-Resurgent/X-HD/blob/69babd44aaf45e2e9c0afb90efa912e681441396/src/shared/adv7511_xbox.c).
Its BIOS prerequisites and alternate RGB mode mean this is corroboration of
the functional digital format, not a measurement of this title's coefficients.

The nominal conversion uses the full-eight-bit RGB to limited YCbCr equations
and inverse in [Khronos Data Format1.4,16.4.2](https://registry.khronos.org/DataFormat/specs/1.4/dataformat.1.4.html),
consistent with [ITU-R BT.601-7,2.5.1-2.5.4](https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.601-7-201103-I!!PDF-E.pdf).
Inputs are already gamma-encoded R',G',B'; no additional transfer function is
applied. Exact integer rationals use one nearest rounding per output, with
ties toward positive infinity. Y codes span16..235, Cr/Cb16..240 with neutral128.
The identity-ramp requirement leaves LUT-versus-conversion ordering immaterial.
Nonidentity gamma on this TV path is not inferred from the separate raw GPU LUT
consumer. Quantized encode/decode error is at most two RGB levels per channel
under this declared nominal model; this is not a measured silicon error bound.

Conexant100381B Table1-2 defines mode1100: falling-edge P11:4 carries Cr and
P3:0 the high Y nibble; rising-edge P11:8 carries low Y and P7:0 Cb. The
`Ddr12` pair labels these edges rather than claiming their temporal order.
The actual receiver checks known mode bits and known bypass of DATDLY,
DATSWP and DATDLY_RE (100381B2-14). Swapped/delayed inputs require temporal
history and remain guarded. It remaps input0 to1 and255 to254 as specified
in100381B1.3.11/12. The reached original32=28,74=01,C6=9C satisfy this interface.
Manufacturer source retained at `build/blinx/av-reference/cx25871-100381b.txt`.

## Observation and ownership

`Nv2a::copy_digital_encoder_input` uses the existing checked tile-to-physical
storage mapper, converts actual stored pixels and produces labeled DDR pairs.
Known screen-off emits nominal digital black without reading RAM. The reference
consumer requires an active board clock, live encoder and acquired input timing,
passes each pair through `Cx25871Control::receive_sd601_pixel`, then decodes
that receiver result into an opaque RGB host preview. The preview is never a
copy of the original RGB framebuffer substituted for these stages.

Both operations are const. The stopped owner supplies a pure RAM-copy callback;
observation does not advance clocks, drain queued draws, execute emulated DMA,
change IRQs or invent completion. Unsupported state produces a local diagnostic
error without changing the simulation's failure state. Legacy full-video output
guards remain intact. Default HLE rendering and performance are unchanged.

The optional pilot capture uses actual paired original Swap returns, at most
four records. `XLLE_PILOT_FRAMEBUFFER_COMPLETED_ONLY=1` collects distinct observed
driver completion counts; it does not equate a Swap serial with completion or
identify allocation generations. `XLLE_PILOT_DIGITAL_INPUT_CAPTURE=1` adds the
digital record to an existing `XLLE_PILOT_FRAMEBUFFER_CAPTURE_DIR` capture.

GPD1/version1 contains sixteen LE DWORDs, a256-byte error field, falling/rising
LE16 pairs and RGBA8 preview bytes. Header fields are magic, version, header320,
pixel-success, width, height, DDR byte count, RGBA byte count, model1, mode12
(FFFFFFFF on failure), framebuffer start, pitch, identity DAC, screen-off,
reserved0 and stage2. Capacity queries perform no RAM reads. Failed observations
contain metadata/error only; successful transport alone does not mean pixels
were produced. `tools/read_gpu_digital_input.py` validates this distinction.

Numerical precision and alternate command profiles stay under GFX-D05/GFX-D18.
Enabled-noise rendering, actual connected attract output and normal graphics
lifecycle remain required. A private run omitting noise remains `qualified:false`
even if its digital pixels and completed flip progression are observed.
