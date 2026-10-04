#include "host/nv2a_hle_renderer.hpp"
#include "machine/nv2a.hpp"
#include <cstring>
#include <stdexcept>
using namespace xlle;
static void require(bool ok, const std::string &why) {
  if (!ok)
    throw std::runtime_error(why);
}
static std::vector<uint8_t> draw(bool native, bool bulk) {
  std::string error, fault;
  auto renderer = create_nv2a_hle_renderer(error);
  require(bool(renderer), error);
  std::vector<uint8_t> ram(0x400000);
  Nv2a gpu([&](const std::string &why) { fault = why; }, renderer.get());
  gpu.set_pfifo_run_batching(bulk);
  gpu.set_mem_access(
      [&](uint32_t a, void *p, size_t n) {
        if (uint64_t(a) + n > ram.size())
          return false;
        std::memcpy(p, ram.data() + a, n);
        return true;
      },
      [&](uint32_t a, const void *p, size_t n) {
        if (uint64_t(a) + n > ram.size())
          return false;
        std::memcpy(ram.data() + a, p, n);
        return true;
      },
      [&](uint32_t a, size_t n) -> uint8_t * {
        return uint64_t(a) + n <= ram.size() ? ram.data() + a : nullptr;
      });
  auto mmio = [&](uint32_t off, uint32_t value) {
    gpu.mmio_write(Nv2a::kBase + off, 4, value);
  };
  for (unsigned i = 0; i < 4; ++i) {
    mmio(0x700000 + i * 8, i);
    mmio(0x700004 + i * 8, 0x80010010 + i * 0x10);
  }
  for (uint32_t obj : {0x100u, 0x180u, 0x300u, 0x400u}) {
    mmio(0x700000 + obj, 0x3d);
    mmio(0x700004 + obj, 0x3fffff);
    mmio(0x700008 + obj, obj == 0x100 || obj == 0x180 ? 0x2000 : 0);
  }
  mmio(0x700200, 0x97);
  mmio(0x003204, 0x100);
  mmio(0x00322c, 0x18);
  mmio(0x003244, 0);
  mmio(0x003220, 1);
  mmio(0x003250, 1);
  mmio(0x003200, 1);
  const float positions[]{1, 1, 100, 1, 14, 1, 100, 1, 1, 14, 100, 1};
  std::memcpy(ram.data() + 0x7000, positions, sizeof(positions));
  uint32_t cursor = 0;
  auto emit = [&](uint32_t method, uint32_t value) {
    uint32_t header = (1u << 18) | method;
    std::memcpy(ram.data() + 0x2000 + cursor, &header, 4);
    std::memcpy(ram.data() + 0x2004 + cursor, &value, 4);
    cursor += 8;
  };
  emit(0, 1);
  emit(0x194, 2);
  emit(0x198, 3);
  emit(0x200, 16u << 16);
  emit(0x204, 16u << 16);
  emit(0x2c0, 0x0fff0000);
  emit(0x2e0, 0x0fff0000);
  emit(0x208, 0x128);
  emit(0x20c, 0x00400040);
  emit(0x210, 0x100000);
  emit(0x214, 0x200000);
  emit(0x19c, 2);
  emit(0x1a0, 3);
  emit(0x1720, 0x7000);
  emit(0x1760, 0x1042);
  emit(0x194c, 0xff0000ff);
  emit(0x358, 0x01010101);
  emit(0x398, 0x4b7fffff);
  emit(0x3a0, 0x900);
  emit(0x1e60, 1);
  emit(0xac0, 0x04200000);
  emit(0x260, 0x14200000);
  emit(0x1e40, 0xc00);
  emit(0xaa0, 0xc00);
  emit(0x288, 0xc);
  emit(0x28c, 0x1c80);
  emit(0x1e9c, 0);
  const uint32_t program[]{0, 0x0020001b, 0x08000800, 0x1000f800,
                           0, 0x0020061b, 0x08000800, 0x1000f819};
  for (unsigned i = 0; i < 8; ++i)
    emit(0xb00 + i * 4, program[i]);
  emit(0x1ea0, 0);
  emit(0x1e94, 2);
  for (unsigned pass = 0; pass < 2; ++pass) {
    const uint32_t address = 0x2004 + cursor;
    if (native) {
      std::vector<uint16_t> indices{0, 1, 2};
      gpu.queue_hle_indexed_draw(address, 5, indices);
      indices.assign(3, 65535); // queued indices must have their own lifetime
      bool rejected = false;
      try {
        gpu.queue_hle_indexed_draw(address, 5, {2, 1, 0});
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      require(rejected, "pending ring address reused");
    }
    emit(0x17fc, 5);
    if (!native) {
      emit(0x1800, 0x10000);
      emit(0x1808, 2);
    }
    emit(0x17fc, 0);
    if (!pass)
      mmio(0x400720, 0);
    mmio(0x800040, cursor);
    if (!pass) {
      require(!gpu.hle_indexed_consumed(),
              "native draw overtook blocked PGRAPH");
      require(!renderer->has_resident_color_surface(0x100000, 1024, 64, 16, 0),
              "early native draw");
      mmio(0x400720, 1);
    }
    require(fault.empty(), fault);
    require(gpu.hle_indexed_consumed() == (native ? pass + 1 : 0),
            "ordered payload was not consumed");
    require(gpu.hle_indexed_pending() == 0,
            "native payload leaked after execution");
  }
  Nv2aResidentColorResolve result;
  require(
      renderer->resolve_resident_color_surface(0x100000, 1024, result, error),
      error);
  require(result.data.size() == 1024 &&
              result.data[(3 * 16 + 3) * 4 + 2] == 255,
          "actual native triangle missing");
  return result.data;
}
int main() {
  try {
    auto reference = draw(false, false);
    require(draw(true, false) == reference, "ordered native output differs");
    require(draw(true, true) == reference, "bulk parser native output differs");
  } catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
