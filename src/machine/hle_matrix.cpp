#include "cpu/unicorn_floor.hpp"
#include "machine/hle_matrix_math.hpp"
#include "machine/machine.hpp"
#include XLLE_MATRIX_PROFILE_HEADER
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace xlle {
namespace {
namespace P = matrix_profile;
void transfer(UnicornFloor &cpu, uint32_t va, void *data, size_t bytes,
              bool write) {
  if (uint64_t(va) + bytes > (uint64_t(1) << 32))
    throw std::runtime_error("native matrix: address overflow");
  auto *p = static_cast<uint8_t *>(data);
  while (bytes) {
    const size_t n = std::min(bytes, size_t(4096 - (va & 4095)));
    uint32_t physical;
    if (!cpu.translate(va, physical) || uint64_t(physical) + n > 64 * 1024 * 1024 ||
        !(write ? cpu.write_mem(physical, p, n) : cpu.read_mem(physical, p, n)))
      throw std::runtime_error("native matrix: guest RAM transfer failed");
    p += n;
    va += uint32_t(n);
    bytes -= n;
  }
}
struct NativeMatrix {
  uint64_t calls = 0;
  ~NativeMatrix() {
    std::printf("[hle-matrix] multiply-calls=%llu\n", (unsigned long long)calls);
  }
  void invoke(UnicornFloor &cpu) {
    auto regs = cpu.get_regs();
    uint32_t frame[4];
    transfer(cpu, regs.esp, frame, sizeof(frame), false);
    HleMatrix a, b;
    transfer(cpu, frame[2], a.data(), sizeof(a), false);
    transfer(cpu, frame[3], b.data(), sizeof(b), false);
    auto out = hle_matrix_multiply(a, b);
    transfer(cpu, frame[1], out.data(), sizeof(out), true);
    regs.eax = frame[1];
    regs.eip = frame[0];
    regs.esp += 16;
    ++calls;
    cpu.redirect_native_service(regs);
  }
};
} // namespace
void XboxMachine::install_hle_matrix() {
  auto &cpu = *static_cast<UnicornFloor *>(cpu_.get());
  uint32_t physical;
  uint8_t prefix[8];
  if (!cpu.translate(P::entry, physical) || physical >= kRamSize ||
      !cpu.read_mem(physical, prefix, sizeof(prefix)) ||
      std::memcmp(prefix, P::code, sizeof(prefix)))
    return;
  std::array<uint8_t, sizeof(P::code)> code;
  transfer(cpu, P::entry, code.data(), code.size(), false);
  if (std::memcmp(code.data(), P::code, code.size()))
    return;
  auto native = std::make_shared<NativeMatrix>();
  if (!cpu.observe_entry(P::entry, [native, &cpu] { native->invoke(cpu); }))
    throw std::runtime_error("native matrix: cannot install entry");
  hle_matrix_installed_ = true;
  std::printf("[hle-matrix] verified stdcall(out,left,right); native float\n");
}
} // namespace xlle
