# Native texture transformation state

Tracking: `beads-xbox.1.161`. Actual JSRF `20261020-081611-qfjtkzlw`
passes camera-normal generation and reaches deferred Xbox texture state 21
value 2 (COUNT2), with default 0 (disabled).

The original deferred cache is retained and synchronized by the draw owner.
The PHOBOS backend now admits disabled and COUNT2 state and carries each
stage's value in the existing native fixed-function vertex constant upload.
Disabled stages pass their original/generated coordinates through. COUNT2
applies the stage's actual texture matrix and emits its first two components
with missing components defaulted to zero/one. Programmed vertex outputs
remain under their own owner. Other count modes and projection remain guarded;
their future implementation does not require a title-specific interface.

The prepared donor previously applied a texture matrix unconditionally. This
correction implements actual enable/disable behavior; accepting the cache
value alone would have left that discrepancy. Preparation verifies the
immutable upstream checkout and records both original/prepared hashes and
notices. No original game code/data is shipped in this change.

The independent four-color GPU fixture now checks 384 exact samples across
four stages/selectors, explicit and generated coordinates, separate world and
camera rotations, a nonidentity texture matrix, and both disabled/COUNT2
states. Rejected modes preserve the prior valid state. The baseline and
fixed-declaration/FVF graphics fixtures also pass (3.33 seconds combined).
Actual-title progression is tracked separately from these component checks.

Actual optimized native stage `20261020-082613-9joj9emm`, executable SHA256
`ae3f63238b25968271d2e77fabefc2765965a903d47e41f609f5a745b0940fd8`,
passes this state, creates two more fixed-function shader layouts and reaches
a separate original nonzero point-list submission. The existing triangle/quad
owner stops that primitive explicitly (`beads-xbox.1.162`). The title/menu and
complete repeating attract remain open; loading-screen captures do not prove
those milestones.

API reference:
[Microsoft texture-coordinate transformations](https://learn.microsoft.com/en-us/windows/win32/direct3d9/texture-coordinate-transformations),
[transform flags](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dtexturetransformflags).
These describe the interface, not exact Xbox raster precision.
