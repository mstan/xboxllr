# Connected native execution: retain as a performance candidate

Tracking: `beads-xbox.1.48`. Branch: `spike/aot-native-regions-20261003`.
Independent baseline: `4d2370a`. No raw-stream, XDK-service or presentation
changes are included in this comparison.

The xboxrecomp donors execute function-level native code. Our small compiled
regions previously returned their integer state to Unicorn even when the next
region was already compiled. The executor now keeps that state across connected
native regions, checking the next region's identity, mapping, observer status and
remaining budget before entering it. It writes registers back when native work
actually ends. Unsupported work still reaches the CPU floor; device deadlines,
code modification and allocator observations remain effective.

This changes execution granularity, not CPU instruction semantics or coverage.
It is not a new whole-function lifter, nor removal of the remaining dynarec.

## Validation and observed result

- All 79 checks pass (`build/spike-aot-all-tests.log` in the main checkout).
  New machine cases prove connected execution, observation at the second region,
  and self-modification through that boundary. Existing deadline, interrupt,
  paging, alias, fault and extended-state checks also pass.
- Both real 27B dashboard/Settings/return routes complete all 14 input events;
  stderr is empty and all 28 captures are byte-identical.
- Final 123-vblank menu interval: **11.4993427 s baseline**, **9.4719009 s
  candidate**, an observed **17.63% wall-time reduction**. Summed frame-window
  times are 178.2551249 s and 153.3725010 s.
- In that interval, 6,581,932 native region entries retain live integer state
  instead of intermediate register transfers. Executed native instructions
  remain 144,274,436; fallback instructions remain 41,679,287. The 4,509,381
  actual fallback-to-native handoffs remain and need other coverage/service work.
- Extra zero-progress eligibility attempts account for 23 additional invocations
  and memory declines. They perform no guest effects. Native/fallback/idle totals
  and all output captures remain unchanged.

Foreign compilers/runners were active during both legs. These are screening
observations, not isolated causal estimates or original-speed qualification.
The result must not be multiplied by other branches' observed improvements.

Private evidence: main checkout `build/spike-aot-regions-r1*`, including argv,
binary hashes, process observations, captures and the summary JSON. Run the
existing replay with the two separate executables, 27B maximum instructions,
1.5B frame intervals, `tests/scripts/stockdash-settings-exact.txt`, native AOT,
idle fast-forward, provisioned HDD security, public register transfers and the
decoded-DXT cache disabled.

Decision: retain for integration and quiet-host confirmation. Larger prebuilt
functions and complete native services remain eligible follow-ups; this branch
only removes unnecessary transitions between code we already compile.
