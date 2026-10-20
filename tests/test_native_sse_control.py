"""Compile and execute native and portable MXCSR read-contract probes."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_continuation import fixture_compiler


ROOT = Path(__file__).resolve().parents[1]
HEADER_DIR = ROOT / "pilots" / "native-title"
FIXTURE = ROOT / "tests" / "fixtures" / "native_sse_control_host.c"


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=True, text=True, capture_output=True)


class NativeSseControlTests(unittest.TestCase):
    def test_native_and_forced_portable_host_paths(self) -> None:
        gcc = fixture_compiler(os.environ.get("GCC") or os.environ.get("CC"))
        with tempfile.TemporaryDirectory(prefix="native-sse-control-") as temporary:
            temp = Path(temporary)
            common = ["-O2", "-std=c11", "-frounding-math", "-Wall", "-Wextra", "-Werror",
                      f"-I{HEADER_DIR}"]
            for label, extra in (("native", []), ("portable", ["-DPILOT_SSE_FORCE_PORTABLE=1"])):
                executable = temp / f"{label}.exe"
                run([gcc, *common, *extra, str(FIXTURE), "-lm", "-o", str(executable)])
                result = run([str(executable)])
                self.assertIn("PASS", result.stdout, result.stderr)
                if label == "native":
                    self.assertRegex(result.stdout, r"PASS (native|portable) MXCSR")
                else:
                    self.assertIn("portable MXCSR", result.stdout)

    def test_portable_header_compiles_for_aarch64(self) -> None:
        clang = os.environ.get("CLANG") or os.environ.get("XLLE_CROSS_CC") or shutil.which("clang")
        if not clang:self.skipTest("clang cross compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="native-sse-aarch64-") as temporary:
            temp = Path(temporary)
            stub = temp / "fenv.h"
            stub.write_text(
                "#ifndef TEST_FENV_H\n#define TEST_FENV_H\n"
                "#define FE_INVALID 0x01\n#define FE_DIVBYZERO 0x04\n"
                "#define FE_OVERFLOW 0x08\n#define FE_UNDERFLOW 0x10\n"
                "#define FE_INEXACT 0x20\n#define FE_ALL_EXCEPT 0x3d\n"
                "#define FE_TONEAREST 0\n#define FE_DOWNWARD 0x400\n"
                "#define FE_UPWARD 0x800\n#define FE_TOWARDZERO 0xc00\n"
                "int fegetround(void); int fetestexcept(int);\n#endif\n",
                encoding="ascii",
            )
            run([clang, "--target=aarch64-none-elf", "-ffreestanding", "-c",
                 "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-DPILOT_SSE_CROSS_COMPILE_PROBE=1", f"-I{temp}", f"-I{HEADER_DIR}", str(FIXTURE),
                 "-o",str(temp/"sse-control-aarch64.o")])


if __name__ == "__main__":
    unittest.main()
