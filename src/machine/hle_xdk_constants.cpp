#include "machine/hle_xdk_constants.hpp"
#include "machine/machine.hpp"
#include XLLE_XDK_CONSTANT_PROFILE_HEADER
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>

namespace xlle {
namespace {
namespace P = xdk_constant_profile;
struct Uploads {
  struct Pending { uint32_t index, source; unsigned count; };
  std::map<uint32_t, Pending> pending;
  uint64_t calls1 = 0, calls4 = 0, refills = 0;
  ~Uploads() {
    std::printf("[hle-xdk-constants] calls1=%llu calls4=%llu real-refills=%llu pending=%zu\n",
        (unsigned long long)calls1, (unsigned long long)calls4,
        (unsigned long long)refills, pending.size());
  }
  void invoke(UnicornFloor& cpu, unsigned count) {
    auto regs = cpu.get_regs();
    auto previous = pending.find(regs.esp);
    if (previous != pending.end()) {
      if (previous->second.count != count) throw std::runtime_error("XDK constants: wrong refill continuation");
      regs.ecx = previous->second.index; regs.edx = previous->second.source;
      pending.erase(previous);
    }
    uint32_t cursor = constant_read(cpu, P::cursor), limit = constant_read(cpu, P::limit);
    const uint64_t end = uint64_t(cursor) + count*16 + 12;
    if (end > UINT32_MAX) throw std::runtime_error("XDK constants: ring overflow");
    if (end >= limit) {
      pending.emplace(regs.esp, Pending{regs.ecx, regs.edx, count});
      regs.esp -= 4;
      constant_write(cpu, regs.esp, count == 1 ? P::entry1 : P::entry4);
      regs.eip = P::reserve;
      ++refills;
      cpu.redirect_native_service(regs);
      return; // Genuine ring allocation, not a fabricated completed upload.
    }
    upload_constants(cpu, regs, count, cursor, P::bank);
    constant_write(cpu, P::cursor, static_cast<uint32_t>(end));
    if (count == 1) ++calls1; else ++calls4;
    cpu.redirect_native_service(regs);
  }
};
}
void XboxMachine::install_hle_xdk_constants() {
  auto& cpu = *static_cast<UnicornFloor*>(cpu_.get());
  std::array<uint8_t, sizeof(P::code4)> code;
  uint32_t physical;
  if (!cpu.translate(P::entry1, physical) || uint64_t(physical) + code.size() > kRamSize ||
      !cpu.read_mem(physical, code.data(), sizeof(P::code1)) ||
      std::memcmp(code.data(), P::code1, sizeof(P::code1))) return;
  constant_ram(cpu, P::entry4, code.data(), code.size(), false);
  if (std::memcmp(code.data(), P::code4, code.size())) return;
  auto uploads = std::make_shared<Uploads>();
  if (!cpu.observe_entry(P::entry1, [uploads,&cpu]{ uploads->invoke(cpu,1); }) ||
      !cpu.observe_entry(P::entry4, [uploads,&cpu]{ uploads->invoke(cpu,4); }))
    throw std::runtime_error("XDK constants: cannot install API entries");
  hle_xdk_constants_installed_ = true;
  std::puts("[hle-xdk-constants] verified complete fastcall uploads; ordered GPU and CPU-bank effects");
}
} // namespace xlle
