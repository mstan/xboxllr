# Raw vertex streams: retain as a performance candidate

Tracking: `beads-xbox.1.46`. Branch: `spike/hle-raw-streams-20261003`.
Independent baseline: `4d2370a`, separate whole HLE renderer. This experiment
does not include the XDK, CPU-region or presentation experiments.

The NoRain DOAXVB presenter at `aa1d3c8afcd8d063171d819e27307070b63f056b`
(`d3d_presenter_d3d11.cpp`, `uploadRing` and `DrawIndexed`) retains original
vertex strides and streams through reusable buffers. This implementation applies
that policy to our GL backend: collect overlapping guest stream ranges once,
retain indices, decode all currently supported NV2A formats on the GPU, and use
non-overlapping upload rings with buffer orphaning at wrap. Immediate/inline
vertices remain supported. Constant attributes and the last indexed attribute
state are preserved; DMA bounds and unsupported formats fail explicitly.

The LLE renderer is unchanged. Selection remains a build choice. No successful
draw placeholders, omitted required work or guest clock changes were added.

## Validation and observed result

- All 79 HLE checks pass (`build/spike-raw-all-tests.log` in the main checkout).
  GPU checks cover float, normalized byte/short, integer short, packed 11/11/10,
  unaligned strides, constant attributes, ring wrap/reuse and invalid indices.
- Both executables complete the real 27B clock/menu/Settings/return route,
  deliver all 14 inputs and have empty stderr.
- 26 of 28 captures are byte-identical. The 15B frame and Settings-open capture
  each differ at one pixel by one channel level (maximum difference 1/255).
  This is within the native arithmetic tolerance; geometry and navigation remain.
- Final 123-vblank menu interval: baseline **10.4587652 s**, candidate
  **8.8606843 s**: **15.28% less wall time**, observed throughput **1.18x**.
  Sum of frame-window times: 188.4916097 s versus 137.8869311 s.
- The route submits 580,683 draws; 578,762 use raw streams. Candidate vertex/index
  upload traffic totals 5,739,769,928 bytes. The previous adapter expanded
  175,545,669 vertices into 256-byte CPU records over this route.

Both timing legs contain foreign workloads. This is a promising screening
result, not isolated attribution, sustained original-speed qualification, or a
measurement of displayed distinct FPS. Do not repeat long busy-host timing pairs
to turn this into a stronger claim. Confirm on a quiet host when available.

Private evidence: main checkout `build/spike-raw-streams-r1*`, including command
lines, binary hashes, process censuses, stdout/stderr, captures and summary JSON.
Use the existing replay script with separate executables, `-MaxInsns 27000000000`,
`-FrameInterval 1500000000`, `tests/scripts/stockdash-settings-exact.txt`, native
AOT, idle fast-forward, provisioned HDD security, public register transfers and
the decoded-DXT cache disabled. No firmware, snapshots or generated title code
belongs in this branch.

Decision: retain for integration and quiet-host confirmation. It improves the
observed dashboard workload but does not close the original-speed menu gate.
