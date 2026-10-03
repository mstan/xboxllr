#!/usr/bin/env python3
"""Guards for the idle fast-forward (beads-xbox.1.4.7).

The skip retires guest instructions WITHOUT executing them. Every one of its correctness
preconditions is a silent failure if removed: dropping the deadline bound lets an interrupt
land at the wrong instruction, dropping the whole-iteration rounding lands the guest in the
middle of the loop with the wrong EFLAGS, dropping the byte verification applies a
kernel-4983-specific analysis to a kernel that is not 4983, and forgetting to advance the
floor's instruction clock desynchronises the guest TSC from retired instructions. None of
those change a compile. So the assertions are structural, on the source.

Run: py -3 tests/test_idle_fast_forward_guards.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MACHINE_CPP = ROOT / "src" / "machine" / "machine.cpp"
MACHINE_HPP = ROOT / "src" / "machine" / "machine.hpp"
FLOOR_CPP = ROOT / "src" / "cpu" / "unicorn_floor.cpp"
FLOOR_HPP = ROOT / "src" / "cpu" / "unicorn_floor.hpp"
CPU_FLOOR_HPP = ROOT / "include" / "xlle" / "cpu_floor.hpp"
MAIN_CPP = ROOT / "src" / "app" / "main.cpp"

failures: list[str] = []
checks = 0


def check(name: str, ok: bool, detail: str = "") -> None:
    global checks
    checks += 1
    if not ok:
        failures.append(f"{name}: {detail}" if detail else name)


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


machine_cpp = MACHINE_CPP.read_text(encoding="utf-8")
machine_cpp_code = strip_comments(machine_cpp)
machine_hpp_code = strip_comments(MACHINE_HPP.read_text(encoding="utf-8"))
floor_cpp_code = strip_comments(FLOOR_CPP.read_text(encoding="utf-8"))
floor_hpp_code = strip_comments(FLOOR_HPP.read_text(encoding="utf-8"))
cpu_floor_hpp_code = strip_comments(CPU_FLOOR_HPP.read_text(encoding="utf-8"))
main_cpp_code = strip_comments(MAIN_CPP.read_text(encoding="utf-8"))

ff_body_match = re.search(
    r"bool XboxMachine::try_idle_fast_forward\(.*?\n\}", machine_cpp_code, flags=re.S
)
check("try_idle_fast_forward is defined in machine.cpp", ff_body_match is not None)
ff_body = ff_body_match.group(0) if ff_body_match else ""

# ---------------------------------------------------------------------------
# 1. The skip is bounded by the next device deadline. This is the ONLY thing
#    that proves no interrupt could have been delivered inside the skip.
# ---------------------------------------------------------------------------
check(
    "the skip is bounded by the deadline argument",
    re.search(r"deadline\s*-\s*total_insns_", ff_body) is not None,
    "try_idle_fast_forward must clamp its budget to deadline - total_insns_",
)
check(
    "the caller passes next_device_deadline()'s value as that bound",
    re.search(
        r"const uint64_t deadline = next_device_deadline\(total_insns_\);",
        machine_cpp_code,
    )
    is not None
    and re.search(r"try_idle_fast_forward\(eip, irq_armed, deadline,", machine_cpp_code)
    is not None,
)

# ---------------------------------------------------------------------------
# 2. Whole iterations only, landing on the loop head.
# ---------------------------------------------------------------------------
check(
    "the skip is a whole number of loop iterations",
    re.search(r"budget\s*/\s*kIdleLoopInsns", ff_body) is not None
    and re.search(r"iters\s*\*\s*kIdleLoopInsns", ff_body) is not None,
)
check(
    "the skip only starts at a qualified instruction boundary",
    re.search(r"eip == kIdlePhaseEip\[i\]", ff_body) is not None
    and re.search(r"if \(phase == kIdlePhases\) return false;", ff_body) is not None,
)
check(
    "the eight phases carry the IF value before each instruction",
    "kIdlePhaseEip[kIdlePhases] = {0x8001e3ceu, 0x8001e3cfu," in machine_hpp_code
    and "0x8001e3d0u, 0x8001e3d1u, 0x8001e3d2u, 0x8001e3d5u, 0x8001e3e3u, 0x8001e3e7u}" in machine_hpp_code
    and "kIdlePhaseIf[kIdlePhases] = {false, true, true, true, false, false, false, false}" in machine_hpp_code,
)
check(
    "the expected EFLAGS include IF only for the phases where sti has run",
    re.search(r"kIdlePhaseIf\[phase\] \? kIdleFlagIf : 0u", ff_body) is not None,
)
check(
    "the pinned loop is 8 instructions at 0x8001e3ce",
    "kIdleLoopHead = 0x8001e3ceu" in machine_hpp_code
    and "kIdleLoopInsns = 8" in machine_hpp_code,
)

# ---------------------------------------------------------------------------
# 3. EFLAGS fixed point and no armed IRQ.
# ---------------------------------------------------------------------------
check(
    "the skip requires the EFLAGS fixed point of the iteration",
    re.search(r"kIdleFlagsValue \| \(kIdlePhaseIf\[phase\]", ff_body) is not None
    and re.search(r"\(r\.eflags & kIdleFlagsMask\) != want", ff_body) is not None,
)
check(
    "the EFLAGS mask covers IF and TF",
    "0x100u" in machine_hpp_code and "0x200u" in machine_hpp_code,
)
check(
    "the skip refuses to run with an IRQ armed",
    re.search(r"if\s*\(irq_armed\)\s*return false;", ff_body) is not None,
)

# ---------------------------------------------------------------------------
# 4. The loop bytes are verified against the pinned shape on EVERY skip, and a
#    mismatch disables the feature loudly instead of guessing.
# ---------------------------------------------------------------------------
check(
    "the loop bytes are compared against a pinned shape",
    "kIdleLoopShape" in ff_body and "std::memcmp(code, kIdleLoopShape" in ff_body,
)
check(
    "a shape mismatch disables the skip and says so on stderr",
    "idle_ff_bytes_bad_ = true" in ff_body and "std::fprintf(stderr" in ff_body,
)
check(
    "the pinned shape starts sti; nop; nop; cli and ends je -0x1b",
    re.search(r"0xfb,\s*\n?\s*0x90,\s*0x90,\s*\n?\s*0xfa,", ff_body) is not None
    and re.search(r"0x74,\s*0xe5,", ff_body) is not None,
)

# ---------------------------------------------------------------------------
# 5. Both loop branches must be the idle ones (empty DPC list, no ready thread).
# ---------------------------------------------------------------------------
check(
    "the DPC list head and the ready-thread field are both checked",
    "dpc_head != r.ebp" in ff_body and "next_thread != 0" in ff_body,
)
check(
    "guest reads require cached read/fetch translations without a fresh page walk",
    "cpu_->read_cached_ram(r.ebp" in ff_body
    and "cpu_->read_cached_ram(r.ebx + 0x2cu" in ff_body
    and "cpu_->read_cached_ram(kIdleLoopHead, code, sizeof(code), true)" in ff_body
    and "cpu_->translate(" not in ff_body,
)
check(
    "hidden context and phase-1 STI shadow are certified without effects",
    "cpu_->idle_skip_context_ok(phase == 1)" in ff_body
    and "ff_rej_context++" in ff_body,
)

# ---------------------------------------------------------------------------
# 6. The floor's instruction clock (and therefore the guest TSC) advances by
#    exactly the number of instructions skipped, and the irq-window latch is
#    cleared because no run() happened.
# ---------------------------------------------------------------------------
check(
    "the machine advances the floor clock by the skip and stops on failure",
    "cpu_->advance_instruction_clock(skipped)" in machine_cpp_code,
)
check(
    "advance_instruction_clock republishes the guest TSC",
    re.search(
        r"bool UnicornFloor::advance_instruction_clock\(uint64_t insns\)\s*\{.*?"
        r"seq_\s*\+=\s*insns;.*?xlle_guest_tsc_ticks\s*=\s*seq_;.*?"
        r"irq_window_stop_\s*=\s*false;",
        floor_cpp_code,
        flags=re.S,
    )
    is not None,
)
check(
    "advance_instruction_clock executes nothing",
    re.search(
        r"bool UnicornFloor::advance_instruction_clock\(uint64_t insns\)\s*\{(?:(?!\n\}).)*",
        floor_cpp_code,
        flags=re.S,
    )
    is not None
    and "uc_emu_start"
    not in re.search(
        r"bool UnicornFloor::advance_instruction_clock\(uint64_t insns\)\s*\{.*?\n\}",
        floor_cpp_code,
        flags=re.S,
    ).group(0),
)
check(
    "the base CpuFloor refuses the skip by default",
    re.search(
        r"virtual bool advance_instruction_clock\(uint64_t insns\)\s*\{[^}]*return false;",
        cpu_floor_hpp_code,
    )
    is not None,
)

# ---------------------------------------------------------------------------
# 7. Accounting: skipped instructions are disjoint from block-charged ones, and
#    the census exports both, so tb_insns + tb_skipped_insns == insns is checkable.
# ---------------------------------------------------------------------------
check(
    "the floor counts skipped instructions separately from tb_insns",
    "skipped_insns" in floor_hpp_code
    and "stats_.skipped_insns += insns;" in floor_cpp_code,
)
check(
    "the census line exports the skip counters",
    "ff_skips=%llu" in main_cpp_code
    and "ff_p0=%llu" in main_cpp_code
    and "ff_insns=%llu" in main_cpp_code
    and "tb_skipped_insns=%llu" in main_cpp_code,
)
check(
    "the census line exports why a skip was refused",
    all(
        k in main_cpp_code
        for k in (
            "ff_rej_flags=%llu",
            "ff_rej_state=%llu",
            "ff_rej_short=%llu",
            "ff_rej_read=%llu",
            "ff_rej_bytes=%llu",
        )
    ),
)

# ---------------------------------------------------------------------------
# 8. Both toggle spellings exist on the same binary, so the A/B never has to
#    compare two builds (PERF-METHODOLOGY section 2 rule 3).
# ---------------------------------------------------------------------------
check(
    "--idle-fast-forward and --no-idle-fast-forward both parse",
    '"--idle-fast-forward"' in main_cpp_code
    and '"--no-idle-fast-forward"' in main_cpp_code,
)
check(
    "the flag reaches the machine",
    "mach.set_idle_fast_forward(idle_fast_forward)" in main_cpp_code,
)

print(f"test_idle_fast_forward_guards: {checks - len(failures)}/{checks} checks passed")
for f in failures:
    print(f"  FAIL {f}")
sys.exit(1 if failures else 0)
