# Source-bound native gamma presentation

Tracking: beads-xbox.1.143. The classic profile proves the original setter's
miniport/cache references, swap-counter bank selection, current/pending words
and immediate flag test against the complete original routine. Flags 0 and 1
copy the requested 768 bytes into the selected original cache bank. Its generated
native getter continues reading that same cache. Immediate flag 2 remains a
guarded timing boundary; no physical DAC or global desktop gamma is changed.

The native GPU compositor maps each channel through the actual requested byte
ramp at presentation. It preserves alpha, stored guest images and readbacks.
Identity ramps retain the existing direct resolve path. A nonidentity ramp adds
one shared fullscreen pass, with AA work skipped when disabled. The ramp follows
AA and interpolation; interpolation history retains unmodified source colors.
No extra original frames, simulation ticks or guest completion serials result.
Touched native state is restored. A graphics Reset retains the logical ramp;
closing the device restores identity for its next lifetime.

Validation: 25 source/profile tests pass, including setter-reference/flags drift
rejection and additive proof admission. Five combined GPU/initializer/visual
tests pass in 2.28 seconds. The final gamma fixture also passes after explicitly
releasing/recreating its independent RHI resources at device boundaries. Real
GPU pixels distinguish all three channel tables and nonlinear mapping after
interpolation, preserve guest targets, survive Reset and restore identity on
new creation. Existing upscale, AA, interpolation, COPY and initializer checks
remain passing.

Actual native JSRF advances through this setter and its first black presentation
to a disc-volume query. Black presentation is not a complete title/menu frame
or attract acceptance. Exact physical DAC calibration, immediate timing and
console RGB565 framebuffer quantization remain separate precision questions.
