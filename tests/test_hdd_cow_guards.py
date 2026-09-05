#!/usr/bin/env python3
"""Guards for the copy-on-write HDD attach and the determinism instruments
(beads-xbox.1.14.11).

WHAT THIS PROTECTS. The canonical 12 B replay was not run-to-run deterministic for a
mechanical reason: the retail boot reads LBA 3 (the Xbox "BRFR" refurb-info sector),
increments the boot counter in it, and writes it back. With the image opened "r+b" every run
therefore MUTATED ITS OWN INPUT, so run N+1 read a different disk than run N. Measured
2026-09-04: `sectors rd/wr=1/1` within the first 19 M instructions, and the image's LBA-3
counter walking 0x0435 -> 0x0436 -> 0x0437 across consecutive runs.

Everything here is a silent regression if removed:
  * re-opening the image "r+b" by default restores the mutation with no error and no log line;
  * dropping the overlay read-back makes the guest unable to see its own writes, which is a
    fidelity break in the opposite direction;
  * dropping the overlay cap turns an unexpected write storm into unbounded host memory;
  * losing the always-on digests removes the only cheap way to detect the next such leak.

Run: py -3 tests/test_hdd_cow_guards.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IDE_CPP = ROOT / "src" / "machine" / "ide.cpp"
IDE_HPP = ROOT / "src" / "machine" / "ide.hpp"
MACHINE_CPP = ROOT / "src" / "machine" / "machine.cpp"
MACHINE_HPP = ROOT / "src" / "machine" / "machine.hpp"
MAIN_CPP = ROOT / "src" / "app" / "main.cpp"
POISON_CPP = ROOT / "src" / "host" / "heap_poison.cpp"
CMAKE = ROOT / "CMakeLists.txt"

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


ide_cpp = IDE_CPP.read_text(encoding="utf-8", errors="replace")
ide_hpp = IDE_HPP.read_text(encoding="utf-8", errors="replace")
machine_cpp = MACHINE_CPP.read_text(encoding="utf-8", errors="replace")
machine_hpp = MACHINE_HPP.read_text(encoding="utf-8", errors="replace")
main_cpp = MAIN_CPP.read_text(encoding="utf-8", errors="replace")
ide_cpp_code = strip_comments(ide_cpp)
ide_hpp_code = strip_comments(ide_hpp)
machine_cpp_code = strip_comments(machine_cpp)
machine_hpp_code = strip_comments(machine_hpp)
main_cpp_code = strip_comments(main_cpp)

# ---------------------------------------------------------------------------
# 1. The image is opened read-only unless writeback was explicitly requested.
# ---------------------------------------------------------------------------
check(
    "attach_hdd takes a writeback flag defaulting to false",
    "attach_hdd(const std::string& path, bool writeback = false)" in ide_hpp_code,
)
check(
    'the open mode is chosen by that flag, "rb" when it is off',
    'std::fopen(path.c_str(), writeback ? "r+b" : "rb")' in ide_cpp_code,
)
check(
    'no unconditional "r+b" open of the image survives',
    'std::fopen(path.c_str(), "r+b")' not in ide_cpp_code,
)
check(
    "XboxMachine::attach_hdd forwards the flag and defaults it off",
    "attach_hdd(const std::string& path, bool writeback = false)" in machine_hpp_code
    and "ide_->attach_hdd(path, writeback)" in machine_cpp_code,
)

# ---------------------------------------------------------------------------
# 2. Copy-on-write: writes are captured, and reads see them again.
# ---------------------------------------------------------------------------
check(
    "the overlay is an LBA-keyed sector map (integer key: no pointer iteration order)",
    "std::unordered_map<uint64_t, std::array<uint8_t, 512>> overlay_" in ide_hpp_code,
)
check(
    "a guest write with writeback off is stored in the overlay and never fwritten",
    "if (!hdd_writeback_ && &d == &drives_[0])" in ide_cpp_code
    and "overlay_[sector + i]" in ide_cpp_code,
)
check(
    "a read overlays any sector the guest has already written",
    "const auto it = overlay_.find(sector + i);" in ide_cpp_code
    and "it->second.data(), 512" in ide_cpp_code,
)
check(
    "the overlay is cleared on attach so a re-attach cannot leak the last run's writes",
    "overlay_.clear();" in ide_cpp_code,
)

# ---------------------------------------------------------------------------
# 3. The overlay is bounded and fault-stops rather than dropping a write.
# ---------------------------------------------------------------------------
check(
    "there is a cap constant",
    "kMaxOverlaySectors" in ide_hpp_code,
)
check(
    "exceeding it fault-stops loudly instead of silently dropping the write",
    "overlay_.size() + count > kMaxOverlaySectors" in ide_cpp_code
    and "copy-on-write overlay is full" in ide_cpp,
)

# ---------------------------------------------------------------------------
# 4. Both toggle spellings on one binary, and the policy is in the log.
# ---------------------------------------------------------------------------
check(
    "--hdd-writeback and --no-hdd-writeback both parse",
    '"--hdd-writeback"' in main_cpp_code and '"--no-hdd-writeback"' in main_cpp_code,
)
check(
    "the default is copy-on-write",
    re.search(r"bool hdd_writeback = false;", main_cpp_code) is not None,
)
check(
    "the flag reaches the machine",
    "mach.attach_hdd(hdd, hdd_writeback)" in main_cpp_code,
)
check(
    "the attach line states the write policy",
    "copy-on-write, image never modified" in main_cpp,
)
check(
    "the IDE census reports the overlay size and the policy",
    "cow_sectors=%llu writeback=%d" in main_cpp
    and "mach.ide()->overlay_sectors()" in main_cpp_code,
)

# ---------------------------------------------------------------------------
# 5. The always-on determinism instruments that located this stay always-on.
#    Capture is unconditional; only the report sinks are opt-in.
# ---------------------------------------------------------------------------
check(
    "the per-slice trajectory digest is folded unconditionally in the run loop",
    "det_record_slice(det);" in machine_cpp_code,
)
check(
    "the device-value digest is folded at both funnels",
    machine_cpp_code.count("det_fold_dev(") >= 5,
)
check(
    "every device DMA write into guest RAM goes through the one funnel",
    "dev_dma_write(kDmaIde," in machine_cpp_code
    and "dev_dma_write(kDmaNv2a," in machine_cpp_code
    and "dev_dma_write(kDmaApu," in machine_cpp_code
    and "dev_dma_write(kDmaNvNet," in machine_cpp_code
    and "kDmaOhci1 : kDmaOhci0" in machine_cpp_code,
)
check(
    "the funnel is the ONLY device path to write_mem(addr, buf, n) -- no lambda bypasses it",
    machine_cpp_code.count("cpu_->write_mem(addr, buf, n)") == 1
    and machine_cpp_code.index("bool XboxMachine::dev_dma_write")
    < machine_cpp_code.index("cpu_->write_mem(addr, buf, n)"),
)
check(
    "all three digests are on the always-on idle-census line",
    "det_digest=%016llx" in main_cpp
    and "dev_digest=%016llx" in main_cpp
    and "dma_digest=%016llx" in main_cpp,
)
check(
    "the three rings can be dumped at a guest-anchored instruction count",
    all(
        f'"{flag}"' in main_cpp_code
        for flag in ("--det-log", "--det-ring-dump", "--det-dev-dump", "--det-dma-dump",
                     "--det-ram-dump")
    ),
)

# ---------------------------------------------------------------------------
# 6. Heap poisoning stays available as the uninitialised-memory test.
# ---------------------------------------------------------------------------
check("heap_poison.cpp exists", POISON_CPP.exists())
poison = POISON_CPP.read_text(encoding="utf-8", errors="replace") if POISON_CPP.exists() else ""
check("it is selected by an environment variable, not argv",
      'std::getenv("XLLE_HEAP_POISON")' in poison)
check("it replaces global operator new", "void* operator new(std::size_t n)" in poison)
check("it is linked into xlle",
      "src/host/heap_poison.cpp" in CMAKE.read_text(encoding="utf-8", errors="replace"))

print(f"test_hdd_cow_guards: {checks - len(failures)}/{checks} checks passed")
for f in failures:
    print(f"  FAIL {f}")
sys.exit(1 if failures else 0)
