#pragma once
#include "cpu/unicorn_floor.hpp"
#include <array>
#include <algorithm>
#include <stdexcept>

namespace xlle {
inline void constant_ram(UnicornFloor& cpu, uint32_t va, void* data, size_t bytes, bool write) {
  if (uint64_t(va) + bytes > (uint64_t(1) << 32))
    throw std::runtime_error("XDK constants: address overflow");
  auto* p = static_cast<uint8_t*>(data);
  while (bytes) {
    const size_t n = std::min(bytes, size_t(4096 - (va & 4095)));
    uint32_t physical;
    if (!cpu.translate(va, physical) || uint64_t(physical) + n > 64 * 1024 * 1024 ||
        !(write ? cpu.write_mem(physical, p, n) : cpu.read_mem(physical, p, n)))
      throw std::runtime_error("XDK constants: guest RAM transfer failed");
    p += n; va += uint32_t(n); bytes -= n;
  }
}
inline uint32_t constant_read(UnicornFloor& cpu, uint32_t va) {
  uint32_t value; constant_ram(cpu, va, &value, 4, false); return value;
}
inline void constant_write(UnicornFloor& cpu, uint32_t va, uint32_t value) {
  constant_ram(cpu, va, &value, 4, true);
}
// Whole API effect: ordered GPU command plus the SDK's readable constant bank.
// The host packet has one lifetime and preserves exact constant bit patterns.
inline void upload_constants(UnicornFloor& cpu, X86Regs& regs, unsigned count,
                             uint32_t cursor, uint32_t bank) {
  if ((count != 1 && count != 4) || regs.ecx > 192 - count)
    throw std::runtime_error("XDK constants: invalid bank extent");
  std::array<uint32_t, 19> packet{};
  packet[0] = 0x41ea4; packet[1] = regs.ecx;
  packet[2] = (count * 16 << 16) | 0xb80;
  constant_ram(cpu, regs.edx, packet.data()+3, count*16, false);
  constant_ram(cpu, cursor, packet.data(), count*16+12, true);
  constant_ram(cpu, bank+regs.ecx*16, packet.data()+3, count*16, true);
  if (count == 4) {
    // The ABI returns with an empty x87 stack after the original MMX/EMMS path.
    uint32_t tags = 0xffff;
    if (!cpu.write_reg(XReg::FPTAG, &tags, sizeof(tags)))
      throw std::runtime_error("XDK constants: cannot restore FP calling convention");
  }
  regs.eax = cursor + count*16 + 12;
  regs.eip = constant_read(cpu, regs.esp);
  regs.esp += 4;
}
} // namespace xlle
