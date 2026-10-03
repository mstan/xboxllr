# Dashboard menu attribution and LEA checkpoint

Tracking: `beads-xbox.1.25.5`. This advances D0/C0 workload attribution and a
bounded D1/C1 compiler operation. Full menu AOT and original-speed 60 FPS remain
unvalidated. The renderer remains the existing OpenGL implementation.

## Reproduction and observed screens

The Release census build uses `XLLE_CPU_BLOCK_CENSUS=ON` and no private AOT
input. The real kernel unlocks the provisioned HDD. Its writes remain in the
existing copy-on-write layer. From the source repository:

```powershell
./build-cpu-census/xlle.exe --nv2a-renderer opengl `
  --boot ../machine/flash-dump-ff000000.bin `
  --run2bl ../machine/bios-extract/bldr.bin `
  --hdd ../machine/xbox-hdd-stockdash.img `
  --hdd-security-file build/aot-20261002/hdd-security.key `
  --eeprom ../machine/eeprom.bin --net-disconnected --reduced-retire-diag `
  --test-synthetic-controller-hub `
  --test-input-script tests/scripts/stockdash-settings.txt `
  --test-input-capture-prefix build/aot-20261002/dashboard-phase `
  --cpu-block-census build/aot-20261002/dashboard-phase.csv `
  --max-host-memory-mb 768 --machine-chunk-insns 4096 `
  --max-insns 22000000000 `
  --frame-dump build/aot-20261002/dashboard-phase-final.ppm
```

The script completes and the CPU reaches 22,000,000,013 clock units without a
fault. Inspected captures establish main-menu Music selection, Xbox Live
selection, Settings selection, entry into Settings with Clock highlighted, and
return to the main menu with Settings selected. This does not exercise entry
into Music, Memory or the offline Xbox Live flow. Controller topology is the
existing synthetic test fixture, not evidence of retail hub qualification.

Nine image checkpoints produced nine cumulative census snapshots. Every snapshot
passes the partition checks. Final totals are 8,600,009,797 fallback instructions,
13,399,990,216 idle skips, and zero native instructions. All 23,888 PC/size rows
have first-execution byte captures; overflow and missing captures are zero.

## Intervals

Snapshots share a host-only run identity and are subtracted after completed CPU
chunks. There is no counter reset or tracing activation at a checkpoint. Each
interval reconciles `clock = fallback + idle + native`; native is zero throughout
this baseline. Counts measure instructions, not host cost or displayed frames.

| Interval, endpoint-report counts | Start clock | End clock | Executed CPU instructions | Idle skips | Active PC/size rows |
| --- | ---: | ---: | ---: | ---: | ---: |
| Main-menu idle, 855 to 1000 | 14,309,533,328 | 15,160,199,995 | 106,007,051 | 744,659,616 | 2,779 |
| Select Xbox Live, 1000 to 1145 | 15,160,199,995 | 16,010,866,661 | 105,701,538 | 744,965,128 | 4,070 |
| Select Settings, 1145 to 1290 | 16,010,866,661 | 16,861,533,326 | 107,151,969 | 743,514,696 | 4,036 |
| Enter Settings, 1290 to 1560 | 16,861,533,326 | 18,445,533,325 | 254,211,167 | 1,329,788,832 | 6,032 |
| Settings idle, 1560 to 1685 | 18,445,533,325 | 19,178,866,658 | 61,509,773 | 671,823,560 | 3,135 |
| Return to main menu, 1685 to 1955 | 19,178,866,658 | 20,762,866,659 | 224,691,209 | 1,359,308,792 | 6,501 |

No active row overlaps either of the two existing startup/delay AOT regions in
any of these intervals. Their earlier whole-run coverage is not menu coverage.

The main-menu idle interval's largest row accounts for 27,844,008 instructions,
**26.27% of executed instructions**. Its first captured bytes decode to an MMX
transfer loop using MOVQ, MOVNTQ and PREFETCHNTA. Three other prominent rows with
x87 load/multiply/add/subtract/exchange/store operations together account for
17.58% of executed instructions. A kernel CLI/memory-compare row contributes
2.31%. During Settings entry, three prominent integer rows containing rotates
account for 27.00% of executed instructions.

These are **block-level priorities**, not dynamic opcode counts. A row aggregates
all later versions at the same PC/size; first-code disassembly cannot establish
which version supplied every counted instruction. No predicted speedup follows
from these shares. Version-aware coverage and native runtime admission remain
required before claiming translated coverage.

## Compiler change and validation

The compiler now lowers general 32-bit LEA ModRM/SIB forms observed in this route.
It computes a wrapping effective offset from the old base/index registers,
preserves every flag, and performs no guest memory access. Invalid register-only
LEA and unsupported prefix forms remain explicit declines. This adds one required
operation; none of the highlighted MMX/x87 blocks is now fully translated.

Eight independently authored address cases cover scaled/absent SIB operands,
negative displacement, 32-bit wrap, unmapped offsets and destination/base/index/
ESP aliasing. They complete with memory callbacks absent and preserve all flags.
The O0/O2 generated-code tests also compare those forms across randomized states
against Unicorn, including memory-event absence. Invalid encoding declines before
effects. Decoder tests reject truncated operands and unsupported prefixes.

The census parser's eight tests cover corrected fault-prefix totals, interval
subtraction, malformed/mismatched/missing run identities, reversed/decreasing
counters, disappearing blocks and legacy cumulative input. The actual nine
snapshots and six intervals pass the same validators. Full Release CTest passes
**63/63** after the compiler change; the compiler Python suite has 21 checks.
The ordinary build is left with census OFF and private input empty.

Private logs, captures, summaries and hash records remain under
`build/aot-20261002/dashboard-phase*` and `phase-*.aot-private.json`.
`dashboard-phase-validation.aot-private.json` records the census executable,
script, flash/bootloader and frame hashes. This run does not refresh a full HDD
content hash. Source changes remain uncommitted at this checkpoint.

Next: qualify production MMX/x87 state and full-width memory operations using
the existing CPU helpers where their contracts fit, then compile actual menu
regions and measure admitted native execution. Required kernel operations,
code-version identity, remaining navigation/audio and quiet-host performance
remain open. No helper may execute guest code through a hidden interpreter/JIT.
