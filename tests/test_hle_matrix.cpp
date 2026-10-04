#include "machine/hle_matrix_math.hpp"
#include "cpu/unicorn_metrics.h"
#include XLLE_MATRIX_PROFILE_HEADER
#include <unicorn/unicorn.h>
#include <unicorn/x86.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

static void require(bool ok, const char *message) {
  if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
static void check(uc_err error) { require(error == UC_ERR_OK, uc_strerror(error)); }
int main() {
  xlle_guest_tsc_ticks = 0; // Pull in the runtime's actual deterministic clock.
  using namespace xlle;
  uc_engine *cpu = nullptr;
  check(uc_open(UC_ARCH_X86, UC_MODE_32, &cpu));
  check(uc_mem_map(cpu, 0, 2 * 1024 * 1024, UC_PROT_ALL));
  check(uc_mem_write(cpu, matrix_profile::entry, matrix_profile::code,
                    sizeof(matrix_profile::code)));
  std::mt19937 random(42);
  std::uniform_real_distribution<float> values(-16, 16);
  for (unsigned trial = 0; trial < 500; ++trial) {
    HleMatrix a, b;
    for (auto &v : a) v = values(random);
    for (auto &v : b) v = values(random);
    for (unsigned alias = 0; alias < 4; ++alias) {
      auto right = alias == 3 ? a : b;
      const auto expected = hle_matrix_multiply(a, right);
      const uint32_t out = alias == 0 ? 0x20200 : alias == 2 ? 0x20100 : 0x20000;
      uint32_t args[] = {0x1000, out, 0x20000, alias == 3 ? 0x20000u : 0x20100u};
      const uint32_t guard = 0x1234abcd;
      check(uc_mem_write(cpu, 0x20000, a.data(), sizeof(a)));
      check(uc_mem_write(cpu, 0x20100, right.data(), sizeof(right)));
      check(uc_mem_write(cpu, out + 64, &guard, 4));
      uint32_t stack = 0x1f0000, control = 0x37f, tags = 0xffff, status = 0;
      check(uc_mem_write(cpu, stack, args, sizeof(args)));
      check(uc_reg_write(cpu, UC_X86_REG_ESP, &stack));
      check(uc_reg_write(cpu, UC_X86_REG_FPCW, &control));
      check(uc_reg_write(cpu, UC_X86_REG_FPTAG, &tags));
      check(uc_reg_write(cpu, UC_X86_REG_FPSW, &status));
      for (auto reg : {UC_X86_REG_EBX, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI})
        check(uc_reg_write(cpu, reg, &guard));
      check(uc_emu_start(cpu, matrix_profile::entry, 0x1000, 0, 10000));
      HleMatrix actual;
      check(uc_mem_read(cpu, out, actual.data(), sizeof(actual)));
      for (unsigned i = 0; i < 16; ++i) {
        // Absolute error scaled by the sum of magnitudes covers cancellation
        // while bounding the permitted native-float arithmetic difference.
        float scale = 1;
        for (unsigned k = 0; k < 4; ++k)
          scale += std::abs(a[(i / 4) * 4 + k] * right[k * 4 + i % 4]);
        require(std::abs(actual[i] - expected[i]) <= scale * 5e-7f,
                "native matrix differs beyond float tolerance");
      }
      uint32_t value;
      check(uc_reg_read(cpu, UC_X86_REG_EAX, &value)); require(value == out, "return pointer");
      check(uc_reg_read(cpu, UC_X86_REG_ESP, &value)); require(value == stack + 16, "RET 12");
      check(uc_reg_read(cpu, UC_X86_REG_EIP, &value)); require(value == 0x1000, "return PC");
      for (auto reg : {UC_X86_REG_EBX, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI}) {
        check(uc_reg_read(cpu, reg, &value)); require(value == guard, "callee register");
      }
      check(uc_mem_read(cpu, out + 64, &value, 4)); require(value == guard, "output extent");
    }
  }
  check(uc_close(cpu));
  std::puts("2000 original-function comparisons passed, including all aliases");
}
