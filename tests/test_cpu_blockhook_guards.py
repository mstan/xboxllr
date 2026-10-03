#!/usr/bin/env python3
"""Guards for the block-granularity CPU tier (beads-xbox.1.4.6).

The change these guard is easy to undo by accident, because every regression here is
*silent*: reinstating a per-instruction hook costs 3-17x on the TCG bucket without changing
a single output byte, and reinstating Unicorn's count hook additionally crashes the process
the first time a count==0 call follows it (tb_flush -> tcg_region_assign's memset over the
1 GiB reserved code buffer). So the assertions are structural, on the source.

Run: py -3 tests/test_cpu_blockhook_guards.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FLOOR_CPP = ROOT / "src" / "cpu" / "unicorn_floor.cpp"
FLOOR_HPP = ROOT / "src" / "cpu" / "unicorn_floor.hpp"
MACHINE_CPP = ROOT / "src" / "machine" / "machine.cpp"
MACHINE_HPP = ROOT / "src" / "machine" / "machine.hpp"
METRICS_C = ROOT / "src" / "cpu" / "unicorn_metrics.c"
CMAKELISTS = ROOT / "CMakeLists.txt"
MAIN_CPP = ROOT / "src" / "app" / "main.cpp"

failures: list[str] = []
checks = 0


def check(name: str, ok: bool, detail: str = "") -> None:
    global checks
    checks += 1
    if not ok:
        failures.append(f"{name}: {detail}" if detail else name)


def strip_comments(text: str) -> str:
    """Drop // and /* */ comments so assertions read code, not prose about code."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


floor_cpp = FLOOR_CPP.read_text(encoding="utf-8")
floor_cpp_code = strip_comments(floor_cpp)
floor_hpp = FLOOR_HPP.read_text(encoding="utf-8")
machine_cpp = MACHINE_CPP.read_text(encoding="utf-8")
machine_cpp_code = strip_comments(machine_cpp)
machine_hpp = MACHINE_HPP.read_text(encoding="utf-8")
metrics_c = METRICS_C.read_text(encoding="utf-8")
cmake = CMAKELISTS.read_text(encoding="utf-8")
main_cpp = MAIN_CPP.read_text(encoding="utf-8")
main_cpp_code = strip_comments(main_cpp)

# ---------------------------------------------------------------------------
# 1. No UC_HOOK_CODE on the default path.
# ---------------------------------------------------------------------------
code_hook_adds = re.findall(
    r"uc_hook_add\s*\([^;]*?UC_HOOK_CODE[^;]*?;", floor_cpp_code, flags=re.S
)
check(
    "exactly one UC_HOOK_CODE registration site",
    len(code_hook_adds) == 1,
    f"found {len(code_hook_adds)}",
)
if code_hook_adds:
    # ... and it lives inside set_insn_mode(), which is only reachable from on_retired()
    # (a caller asking for per-instruction events) or force_insn_scheduling() (the A/B
    # toggle-off arm). The constructor must not install it.
    ctor = floor_cpp_code.split("UnicornFloor::UnicornFloor()", 1)
    check("UnicornFloor::UnicornFloor() is present", len(ctor) == 2)
    if len(ctor) == 2:
        ctor_body = ctor[1].split("UnicornFloor::~UnicornFloor", 1)[0]
        check(
            "constructor installs no UC_HOOK_CODE",
            "UC_HOOK_CODE" not in ctor_body,
            "the per-instruction hook is back on the default path",
        )
    setter = floor_cpp_code.split("void UnicornFloor::set_insn_mode", 1)
    check("set_insn_mode() exists", len(setter) == 2)
    if len(setter) == 2:
        body = setter[1].split("\n}", 1)[0]
        check(
            "UC_HOOK_CODE is installed only by set_insn_mode()",
            "UC_HOOK_CODE" in body,
        )

check(
    "on_retired() is what turns the per-instruction path on",
    re.search(r"void UnicornFloor::on_retired[^{]*\{[^}]*set_insn_mode\(true\)",
              floor_cpp_code, flags=re.S) is not None,
)
check(
    "the retire diagnostic in main.cpp stays behind its flag",
    re.search(r"if\s*\(!reduced_retire_diag\)\s*\{\s*mach\.cpu\(\)\.on_retired",
              main_cpp_code, flags=re.S) is not None,
    "on_retired() must only be installed when the diagnostic is requested",
)

# ---------------------------------------------------------------------------
# 2. The block hook, the edge hook and the icount table are all present.
# ---------------------------------------------------------------------------
check("UC_HOOK_BLOCK is installed", "UC_HOOK_BLOCK" in floor_cpp_code)
check("UC_HOOK_EDGE_GENERATED is installed", "UC_HOOK_EDGE_GENERATED" in floor_cpp_code)
check("block trampoline exists", "UnicornFloor::block_trampoline" in floor_cpp_code)
check("edge trampoline exists", "UnicornFloor::edge_trampoline" in floor_cpp_code)
check(
    "the edge hook records prev_tb too",
    re.search(r"edge_trampoline[^}]*prev_tb[^}]*icount_insert", floor_cpp_code, flags=re.S)
    is not None,
    "refresh predecessor identities when the generation callback provides one",
)
check(
    "the icount table is keyed on (pc, size)",
    "slot.pc == pc && slot.size == size" in floor_cpp_code,
)
check(
    "an icount lookup miss is a loud stop, never a guess",
    re.search(r"icount == 0.*?stop_reason_\s*=.*?uc_emu_stop", floor_cpp_code, flags=re.S)
    is not None,
)
check(
    "the block hook charges the whole block's icount",
    "seq_ += icount" in floor_cpp_code,
)
check(
    "block-hook stats are exported with their denominators",
    all(k in floor_hpp for k in ("icount_misses", "tb_entries", "tb_insns",
                                 "deadline_stops", "irq_window_stops")),
)
check(
    "the always-on block-PC ring is written by the block hook",
    "block_pc_ring_[" in floor_cpp_code,
)

# ---------------------------------------------------------------------------
# 3. uc_emu_start: count is always 0, timeout is always 0.
# ---------------------------------------------------------------------------
starts = re.findall(r"uc_emu_start\s*\(([^;]*?)\)\s*;", floor_cpp_code, flags=re.S)
check("uc_emu_start is called", len(starts) >= 1, f"found {len(starts)}")
for idx, args in enumerate(starts):
    parts = [a.strip() for a in args.split(",")]
    check(
        f"uc_emu_start[{idx}] takes 5 arguments",
        len(parts) == 5,
        f"got {len(parts)}: {parts}",
    )
    if len(parts) == 5:
        timeout = parts[3].replace("/*timeout=*/", "").strip()
        count = parts[4].replace("/*count=*/", "").strip()
        check(
            f"uc_emu_start[{idx}] passes timeout=0 (the host clock is never the schedule)",
            timeout == "0",
            f"timeout={timeout!r}",
        )
        check(
            f"uc_emu_start[{idx}] passes count=0 (no Unicorn count hook, no tb_flush)",
            count == "0",
            f"count={count!r}",
        )
check(
    "no enable_emu_timer / timeout path is reachable from the floor",
    "enable_emu_timer" not in floor_cpp_code,
)

# ---------------------------------------------------------------------------
# 4. The slice budget is enforced by our own hooks, and forward progress is guarded.
# ---------------------------------------------------------------------------
check(
    "the per-instruction path enforces the budget itself",
    "seq_ >= self->run_deadline_" in floor_cpp_code
    or "self->seq_ >= self->run_deadline_" in floor_cpp_code,
)
check(
    "the block path stops before a block that would cross the budget",
    "seq_ + icount > self->run_deadline_" in floor_cpp_code,
)
check(
    "bounded translations make forward progress without first-block overshoot",
    "tbs_this_run_" not in floor_cpp_code
    and "xlle_unicorn_limit_next_tb(uc_, deadline_tail_)" in floor_cpp_code,
)
check(
    "the interrupt window is checked at block entry while an IRQ is armed",
    re.search(r"irq_pending_[^}]*UC_X86_REG_EFLAGS[^}]*0x200u", floor_cpp_code, flags=re.S)
    is not None,
)

# ---------------------------------------------------------------------------
# 5. Device clocks stay pure functions of the retired-instruction count.
# ---------------------------------------------------------------------------
for name, expr in (
    ("PIT", r"pit_->set_time\(total_insns_ \* Pit8254::kPitFreq / kCpuHz\)"),
    ("NV2A frame", r"uint64_t t60 = total_insns_ \* 60;"),
    ("APU frame", r"apu_->set_frame\(total_insns_ \* 375 / \(kCpuHz \* 2\)\)"),
    ("ACI frame", r"aci_->set_frame\(total_insns_ \* 375 / \(kCpuHz \* 2\)\)"),
    ("OHCI0 frame", r"ohci0_->set_frame\(total_insns_ \* 1000 / kCpuHz\)"),
    ("OHCI1 frame", r"ohci1_->set_frame\(total_insns_ \* 1000 / kCpuHz\)"),
    ("host poll ms", r"total_insns_ \* 1000 / kCpuHz"),
):
    check(f"{name} clock is derived from total_insns_",
          re.search(expr, machine_cpp_code) is not None)

check(
    "no host clock feeds the slice loop",
    not re.search(r"(steady_clock|system_clock|__rdtsc|QueryPerformanceCounter)",
                  machine_cpp_code),
)
check(
    "the slice budget is bounded by the next device event deadline",
    "next_device_deadline(total_insns_)" in machine_cpp_code,
)
check(
    "next_device_deadline covers host poll, vblank, APU and the PIT IRQ0 edge",
    all(t in machine_cpp_code for t in ("take(next_rate_boundary(insns, 1000, kCpuHz))",
                                        "take(next_rate_boundary(insns, 60, kCpuHz))",
                                        "take(next_rate_boundary(insns, 375, kCpuHz * 2))",
                                        "pit_->next_irq0_tick()")),
)
check(
    "the slice census asks the floor why it stopped, not `retired < chunk`",
    "cpu_->stopped_for_irq_window()" in machine_cpp_code,
)
check("the census exports the block-quantisation slack", "budget_slack" in machine_hpp)

# ---------------------------------------------------------------------------
# 6. The guest time-stamp counter is the emulator's, not the host's.
# ---------------------------------------------------------------------------
check(
    "the pinned QEMU host-TSC definition is renamed out of the way",
    "cpu_get_tsc_x86_64=xlle_unused_host_cpu_get_tsc" in cmake,
)
check(
    "xlle owns the symbol every guest RDTSC resolves to",
    "uint64_t cpu_get_tsc_x86_64(void* env) {" in metrics_c,
)
check(
    "the deterministic guest TSC is the default",
    re.search(r"int xlle_guest_tsc_deterministic\s*=\s*1", metrics_c) is not None,
)
for clock_site in ("code_trampoline", "block_trampoline", "restore_clock"):
    clock_body = floor_cpp_code.split("void UnicornFloor::" + clock_site + "(", 1)[-1].split("\n}", 1)[0]
    check(
        "the guest TSC is published from the instruction clock in " + clock_site,
        "xlle_guest_tsc_ticks = self->seq_;" in clock_body,
        "instruction/block scheduling and precise restore must all publish the clock",
    )
check(
    "the host-TSC passthrough survives only as an explicit opt-out flag",
    "--legacy-host-tsc" in main_cpp_code,
)
check(
    "guest TSC reads are counted and reported",
    "xlle_guest_tsc_reads" in metrics_c and "guest_tsc_reads=" in main_cpp,
)

# ---------------------------------------------------------------------------
# 7. The A/B toggle-off arm exists and is the same binary.
# ---------------------------------------------------------------------------
check("--cpu-insn-hook is parsed", '"--cpu-insn-hook"' in main_cpp_code)
check(
    "--cpu-insn-hook forces the per-instruction floor",
    "force_insn_scheduling()" in main_cpp_code,
)
check(
    "the selected scheduling mode is printed into the run log",
    "[cpu] scheduling=%s" in main_cpp,
)

print(f"test_cpu_blockhook_guards: {checks - len(failures)}/{checks} checks passed")
for f in failures:
    print(f"  FAIL {f}")
sys.exit(1 if failures else 0)
