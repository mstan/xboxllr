#include "machine/hle_xdk_constants.hpp"
#include "cpu/unicorn_metrics.h"
#include XLLE_XDK_CONSTANT_PROFILE_HEADER
#include <unicorn/unicorn.h>
#include <unicorn/x86.h>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
static void require(bool ok, const char* message) {
  if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
static void check(uc_err e) { require(e == UC_ERR_OK, uc_strerror(e)); }
int main() {
  using namespace xlle;
  namespace P = xdk_constant_profile;
  xlle_guest_tsc_ticks = 0;
  uc_engine* original = nullptr;
  check(uc_open(UC_ARCH_X86, UC_MODE_32, &original));
  check(uc_mem_map(original, 0, 2*1024*1024, UC_PROT_ALL));
  check(uc_mem_write(original, P::entry1, P::code1, sizeof(P::code1)));
  check(uc_mem_write(original, P::entry4, P::code4, sizeof(P::code4)));
  UnicornFloor native;
  require(native.ok() && native.map_mem(0, 2*1024*1024), "native memory");
  std::mt19937 random(56);
  for (unsigned trial = 0; trial < 500; ++trial) for (unsigned count : {1,4}) {
    uint32_t index = random() % (193-count), source = 0x40000;
    uint32_t cursor = 0x20000 + (trial % 32)*4, limit = 0x30000;
    uint32_t stack = 0x1f0000, ret = 0x1000, guard = 0xcab0abcd;
    std::array<uint32_t, 16> constants;
    for (auto& word : constants) word = random();
    auto write = [&](uint32_t address, const void* data, size_t bytes) {
      check(uc_mem_write(original, address, data, bytes));
      require(native.write_mem(address, data, bytes), "native write");
    };
    write(source, constants.data(), count*16);
    write(P::cursor, &cursor, 4); write(P::limit, &limit, 4); write(stack, &ret, 4);
    write(cursor + count*16 + 12, &guard, 4);
    write(P::bank + (index+count)*16, &guard, 4);
    check(uc_reg_write(original, UC_X86_REG_ECX, &index));
    check(uc_reg_write(original, UC_X86_REG_EDX, &source));
    check(uc_reg_write(original, UC_X86_REG_ESP, &stack));
    for (int reg : {UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI})
      check(uc_reg_write(original, reg, &guard));
    check(uc_emu_start(original, count == 1 ? P::entry1 : P::entry4, ret, 0, 1000));
    X86Regs state{};
    state.ecx=index; state.edx=source; state.esp=stack;
    state.ebx=state.ebp=state.esi=state.edi=guard;
    upload_constants(native, state, count, cursor, P::bank);
    constant_write(native, P::cursor, state.eax);
    require(state.esp==stack+4 && state.eip==ret, "native fastcall return");
    require(state.ebx==guard && state.ebp==guard && state.esi==guard && state.edi==guard, "callee state");
    for (auto [address, bytes] : {std::pair<uint32_t,size_t>{cursor,count*16+16},
                                {P::bank+index*16,count*16+4},{P::cursor,4}}) {
      std::vector<uint8_t> a(bytes), b(bytes);
      check(uc_mem_read(original,address,a.data(),bytes));
      require(native.read_mem(address,b.data(),bytes) && a==b, "original API memory effects differ");
    }
  }
  check(uc_close(original));
  std::puts("1000 complete original constant upload comparisons passed");
}
