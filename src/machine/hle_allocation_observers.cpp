// HLE resource lifetime follows the guest allocator. NoRain's native presenter
// retires GPU resources in free_guest before releasing the allocation. Here the
// original Xbox kernel performs the allocation/free; observers only retire host
// copies. No kernel return value or guest instruction is replaced.
#include "cpu/unicorn_floor.hpp"
#include "machine/machine.hpp"
#include <cstring>
#include <stdexcept>

namespace xlle {
namespace {
uint32_t read_linear_word(UnicornFloor &cpu, uint32_t address) {
  uint32_t physical = 0, value = 0;
  if ((address & 4095) > 4092 || !cpu.translate(address, physical) ||
      !cpu.read_mem(physical, &value, 4))
    throw std::runtime_error("HLE allocation observer: invalid guest word");
  return value;
}
} // namespace

void XboxMachine::install_hle_allocation_observers() {
  if (!config_.nv2a_renderer || ram_.size() != kRamSize)
    return;
  auto word = [&](uint32_t address) {
    uint32_t value = 0;
    if (address <= ram_.size() - 4)
      std::memcpy(&value, ram_.data() + address, 4);
    return value;
  };
  constexpr uint32_t base = 0x10000;
  if ((word(base) & 65535) != 0x5a4d)
    return;
  const uint32_t pe = base + word(base + 0x3c);
  if (pe < base || pe > base + 4096 || word(pe) != 0x4550)
    return;
  if (word(pe + 52) != 0x80010000)
    return;
  const uint32_t exports = base + word(pe + 120);
  if (exports < base || exports > 0x100000)
    return;
  const uint32_t ordinal_base = word(exports + 16), count = word(exports + 20);
  const uint32_t table = base + word(exports + 28);
  if (ordinal_base > 171 || count < 324 - ordinal_base + 1 || count > 1024 ||
      table < base || table > 0x100000)
    return;
  auto entry = [&](unsigned ordinal) {
    return base + word(table + 4 * (ordinal - ordinal_base));
  };
  const uint32_t free_contiguous = entry(171), free_system = entry(172),
                 version = entry(324);
  if (free_contiguous < base || free_contiguous >= 0x100000 ||
      free_system < base || free_system >= 0x100000 || version < base ||
      version >= 0x100000)
    return;
  // Wait until decompression has populated both exports and function bodies.
  if ((word(free_contiguous) & 0xffffff) != 0xec8b55 ||
      word(free_system) != 0x082474ff || !word(version + 4))
    return;
  auto &cpu = *static_cast<UnicornFloor *>(cpu_.get());
  hle_allocation_observers_installed_ = true;
  // The allocation boundary marker is a kernel implementation detail. This
  // adapter is verified against the running 4983 kernel's MmQueryAllocationSize
  // (ordinal 180): walk self-mapped PTEs until bit 9 marks the last page.
  // Do not guess this metadata for another kernel build.
  if ((word(version + 4) & 65535) != 4983) {
    cpu.request_stop("HLE allocation lifetime: unsupported kernel build");
    return;
  }
  auto release = [this, &cpu](bool contiguous) {
    const auto regs = cpu.get_regs();
    const uint32_t allocation = read_linear_word(cpu, regs.esp + 4);
    if (!allocation)
      return; // Guest body still determines validity/result.
    uint32_t physical = 0;
    if ((allocation & 4095) || !cpu.translate(allocation, physical) ||
        physical >= kRamSize)
      throw std::runtime_error(
          "HLE allocation lifetime: invalid allocation base");
    uint32_t bytes = 0;
    if (contiguous) {
      const uint32_t pte = 0xc0000000u + ((allocation >> 10) & 0x3ffffcu);
      bool last = false;
      while (bytes < kRamSize - physical) {
        const uint32_t value = read_linear_word(cpu, pte + (bytes >> 10));
        if (!(value & 1) || (value & 0xfffff000u) != physical + bytes)
          throw std::runtime_error(
              "HLE allocation lifetime: invalid contiguous PTE");
        bytes += 4096;
        if (value & 0x200) {
          last = true;
          break;
        }
      }
      if (!last)
        throw std::runtime_error("HLE allocation lifetime: missing end marker");
      config_.nv2a_renderer->invalidate_resident_color_surface(physical, bytes,
                                                               0);
      config_.nv2a_renderer->invalidate_resident_depth_surface(physical, bytes,
                                                               0);
    } else {
      bytes = read_linear_word(cpu, regs.esp + 8);
      if (!bytes || bytes > kRamSize ||
          uint64_t(allocation) + bytes > UINT32_MAX)
        throw std::runtime_error(
            "HLE allocation lifetime: invalid system allocation size");
      for (uint32_t offset = 0; offset < bytes; offset += 4096) {
        uint32_t page = 0;
        if (!cpu.translate(allocation + offset, page) || page >= kRamSize)
          throw std::runtime_error(
              "HLE allocation lifetime: invalid system page");
        config_.nv2a_renderer->invalidate_resident_color_surface(page, 4096, 0);
        config_.nv2a_renderer->invalidate_resident_depth_surface(page, 4096, 0);
      }
    }
  };
  if (!cpu.observe_entry(free_contiguous + 0x80000000u,
                         [release] { release(true); }) ||
      !cpu.observe_entry(free_system + 0x80000000u,
                         [release] { release(false); })) {
    cpu.request_stop(
        "HLE allocation lifetime: could not install kernel observers");
    return;
  }
  std::printf("[hle-allocation] kernel=4983 free-contiguous=%08x "
              "free-system=%08x guest-bodies=retained\n",
              free_contiguous + 0x80000000u, free_system + 0x80000000u);
}
} // namespace xlle
