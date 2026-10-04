// Native XDK indexed command construction. Inspired by NoRain's API adapter,
// but replaces its costly guest index-copy loop instead of adding a second
// draw. Real guest preparation, ring allocation and completion helpers remain
// active.
#include "cpu/unicorn_floor.hpp"
#include "machine/machine.hpp"
#include XLLE_XDK_DRAW_PROFILE_HEADER
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace xlle {
namespace {
namespace P = xdk_draw_profile;
void transfer(UnicornFloor &cpu, uint32_t va, void *data, size_t bytes,
              bool write) {
  if (uint64_t(va) + bytes > (uint64_t(1) << 32))
    throw std::runtime_error("native XDK: address overflow");
  auto *p = static_cast<uint8_t *>(data);
  while (bytes) {
    const size_t n = std::min(bytes, size_t(4096 - (va & 4095)));
    uint32_t physical;
    if (!cpu.translate(va, physical) || physical >= 64 * 1024 * 1024 ||
        !(write ? cpu.write_mem(physical, p, n) : cpu.read_mem(physical, p, n)))
      throw std::runtime_error("native XDK: guest RAM transfer failed");
    p += n;
    va += uint32_t(n);
    bytes -= n;
  }
}
uint32_t read(UnicornFloor &cpu, uint32_t va) {
  uint32_t value;
  transfer(cpu, va, &value, 4, false);
  return value;
}
void write(UnicornFloor &cpu, uint32_t va, uint32_t value) {
  transfer(cpu, va, &value, 4, true);
}
struct Draw {
  uint32_t device = 0, done = 0;
  std::vector<uint16_t> indices;
};
struct NativeDraws {
  std::map<uint32_t, Draw> frames;
  uint64_t calls = 0, indices = 0, refills = 0;
  ~NativeDraws() {
    std::printf("[hle-xdk] native-draws=%llu indices=%llu "
                "real-ring-refills=%llu pending=%zu\n",
                (unsigned long long)calls, (unsigned long long)indices,
                (unsigned long long)refills, frames.size());
  }
  void resume(UnicornFloor &cpu) {
    auto regs = cpu.get_regs();
    if (!regs.ebp || regs.esp != regs.ebp - 16 || (regs.eflags & 0x400))
      throw std::runtime_error("native XDK: unexpected prepared call frame");
    auto it = frames.find(regs.ebp);
    uint32_t cursor = regs.eax;
    if (it == frames.end()) {
      Draw draw;
      draw.device = read(cpu, P::device);
      if (draw.device != read(cpu, regs.ebp - 8))
        throw std::runtime_error(
            "native XDK: device changed during preparation");
      const auto primitive = read(cpu, regs.ebp + 8);
      const auto count = read(cpu, regs.ebp + 12);
      const auto data = read(cpu, regs.ebp + 16);
      if (!primitive || primitive > 10 || count > (1u << 24))
        throw std::runtime_error(
            "native XDK: unsupported indexed draw parameters");
      draw.indices.resize(count);
      transfer(cpu, data, draw.indices.data(), count * 2, false);
      write(cpu, cursor, 0x417fc);
      write(cpu, cursor + 4, primitive);
      cursor += 8;
      it = frames.emplace(regs.ebp, std::move(draw)).first;
      ++calls;
      indices += count;
    }
    auto &draw = it->second;
    while (draw.done < draw.indices.size()) {
      const uint64_t end = uint64_t(read(cpu, draw.device + 4)) + 0x200;
      if (end > UINT32_MAX || cursor > end)
        throw std::runtime_error("native XDK: invalid ring extent");
      const size_t free = size_t(end - cursor);
      const size_t remaining = draw.indices.size() - draw.done;
      if (free < 24) {
        write(cpu, draw.device, cursor);
        const uint32_t growth = read(cpu, P::growth);
        regs.esp -= 12;
        write(cpu, regs.esp, P::prepared);
        regs.eax = std::max(growth >> 1, 0xa28u);
        regs.ecx = std::max(growth, 0xa28u);
        write(cpu, regs.esp + 4, regs.eax);
        write(cpu, regs.esp + 8, regs.ecx);
        regs.eip = P::reserve;
        ++refills;
        cpu.redirect_native_service(regs);
        return;
      }
      if (remaining == 1) {
        write(cpu, cursor, 0x41808);
        write(cpu, cursor + 4, draw.indices[draw.done++]);
        cursor += 8;
      } else {
        const size_t pairs =
            std::min({remaining / 2, size_t(511), (free - 20) / 4});
        write(cpu, cursor, 0x40001800u | (uint32_t(pairs) << 18));
        transfer(cpu, cursor + 4, draw.indices.data() + draw.done, pairs * 4,
                 true);
        cursor += uint32_t(pairs * 4 + 4);
        draw.done += uint32_t(pairs * 2);
      }
    }
    write(cpu, cursor, 0x417fc);
    write(cpu, cursor + 4, 0);
    regs.ecx = draw.device;
    regs.ebx = cursor;
    regs.eip = P::cleanup;
    frames.erase(it);
    cpu.redirect_native_service(regs);
  }
};
} // namespace

void XboxMachine::install_hle_xdk_draw() {
  auto &cpu = *static_cast<UnicornFloor *>(cpu_.get());
  uint32_t physical;
  uint8_t prefix[8];
  if (!cpu.translate(P::entry, physical) || physical >= kRamSize ||
      !cpu.read_mem(physical, prefix, sizeof(prefix)) ||
      std::memcmp(prefix, P::code, sizeof(prefix)))
    return;
  std::vector<uint8_t> code(sizeof(P::code));
  transfer(cpu, P::entry, code.data(), code.size(), false);
  if (std::memcmp(code.data(), P::code, code.size()))
    return;
  auto native = std::make_shared<NativeDraws>();
  if (!cpu.observe_entry(P::prepared, [native, &cpu] { native->resume(cpu); }))
    throw std::runtime_error("native XDK: cannot install draw boundary");
  hle_xdk_draw_installed_ = true;
  std::printf("[hle-xdk] indexed draw ABI verified; original "
              "preparation/allocation/completion retained\n");
}
} // namespace xlle
