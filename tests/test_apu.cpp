// xlle — focused MCPX APU VOICE_OFF -> timed idle-voice trap/IRQ lifecycle test.
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "machine/apu.hpp"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL: %s (line %d)\n", #c, __LINE__); ++fails; } } while (0)

static void put32(std::vector<uint8_t>& mem, uint32_t addr, uint32_t value) {
    std::memcpy(mem.data() + addr, &value, sizeof(value));
}

static uint32_t get32(const std::vector<uint8_t>& mem, uint32_t addr) {
    uint32_t value = 0;
    std::memcpy(&value, mem.data() + addr, sizeof(value));
    return value;
}

int main() {
    constexpr uint32_t base = xlle::McpxApu::kBase;
    constexpr uint16_t voice = 7;
    constexpr uint32_t voice_table = 0x1000;
    constexpr uint32_t voice_addr = voice_table + voice * 0x80;
    constexpr uint32_t notifier_table = 0x3000;

    std::string fault;
    std::vector<std::pair<int, bool>> irq;
    std::vector<uint8_t> ram(0x10000, 0);
    xlle::McpxApu apu([&](const std::string& why) { fault = why; });
    apu.set_mem_access(
        [&](uint32_t addr, void* dst, size_t n) {
            if (addr + n > ram.size()) return false;
            std::memcpy(dst, ram.data() + addr, n);
            return true;
        },
        [&](uint32_t addr, const void* src, size_t n) {
            if (addr + n > ram.size()) return false;
            std::memcpy(ram.data() + addr, src, n);
            return true;
        });
    apu.set_irq([&](int line, bool level) { irq.emplace_back(line, level); }, 5);

    // Submit the actual list-building methods for one 2D voice; the other lists are empty.
    put32(ram, voice_addr + 0x7c, 0xffff);
    apu.mmio_write(base + 0x202c, 4, voice_table);      // VPVADDR
    apu.mmio_write(base + 0x2054, 4, 0xffff);           // TVL2D
    apu.mmio_write(base + 0x2060, 4, 0xffff);           // TVL3D
    apu.mmio_write(base + 0x206c, 4, 0xffff);           // TVLMP
    apu.mmio_write(base + 0x115c, 4, notifier_table);   // FENADDR
    apu.mmio_write(base + 0x1504, 4, 1u << 15);         // force idle-voice trap
    apu.mmio_write(base + 0x1004, 4,
                   (1u << 0) | (1u << 4) | (1u << 5) | (1u << 6));  // all FE causes
    apu.mmio_write(base + 0x2000, 4, 0x08);             // setup engine XCNTMODE on
    apu.mmio_write(base + 0x20000 + 0x120, 4, 0x0001ffff);  // 2D list top
    apu.mmio_write(base + 0x20000 + 0x124, 4, voice);       // VP VOICE_ON
    CHECK(apu.mmio_read(base + 0x2054, 4) == voice);
    CHECK((get32(ram, voice_addr + 0x54) & (1u << 21)) != 0);

    apu.mmio_write(base + 0x20000 + 0x128, 4, voice);   // VP VOICE_OFF
    CHECK(fault.empty());
    CHECK((get32(ram, voice_addr + 0x54) & (1u << 21)) == 0);
    CHECK(apu.voice_off_methods() == 1);
    CHECK(apu.idle_voice_traps() == 0);
    CHECK(apu.mmio_read(base + 0x1300, 4) == 0x128);
    CHECK(apu.mmio_read(base + 0x1304, 4) == voice);

    // VOICE_OFF posts SSLA_DONE immediately: byte 14 is envelope state, byte 15 status.
    uint32_t notify = notifier_table + 16 * (2 + voice * 4);
    CHECK(ram[notify + 14] == 1);
    CHECK(ram[notify + 15] == 1);
    CHECK((apu.mmio_read(base + 0x1000, 4) & ((1u << 5) | (1u << 6))) ==
          ((1u << 5) | (1u << 6)));
    CHECK(irq.empty());  // notification IRQ aggregation is deferred to the APU frame

    // No synchronous escape hatch: completion appears only at the next APU frame.
    apu.set_frame(1);
    CHECK(fault.empty());
    CHECK(apu.idle_voice_traps() == 1);
    CHECK(apu.mmio_read(base + 0x1110, 4) == voice);
    CHECK(apu.mmio_read(base + 0x1300, 4) == 0x8000);
    CHECK(apu.mmio_read(base + 0x1304, 4) == voice);
    CHECK((apu.mmio_read(base + 0x1100, 4) & 0x00000fe0) == 0x00000fe0);
    CHECK((apu.mmio_read(base + 0x1000, 4) & ((1u << 0) | (1u << 4))) ==
          ((1u << 0) | (1u << 4)));
    CHECK(irq.size() == 1 && irq[0] == std::make_pair(5, true));

    // FET is level-owned by trapped FECTL: W1C alone reasserts it.
    apu.mmio_write(base + 0x1000, 4, 0xffffffff);
    CHECK((apu.mmio_read(base + 0x1000, 4) & ((1u << 0) | (1u << 4))) ==
          ((1u << 0) | (1u << 4)));
    CHECK(irq.size() == 1);

    // Releasing the FE consumes the trap reason; W1C then clears the final cause.
    apu.mmio_write(base + 0x1100, 4, 0);
    CHECK((apu.mmio_read(base + 0x1100, 4) & 0x00000f00) == 0);
    CHECK(irq.size() == 1);
    apu.mmio_write(base + 0x1000, 4, 0xffffffff);
    CHECK(apu.mmio_read(base + 0x1000, 4) == 0);
    CHECK(irq.size() == 2 && irq[1] == std::make_pair(5, false));

    std::printf(fails ? "test_apu: %d checks failed\n" : "test_apu: ok\n", fails);
    return fails ? 1 : 0;
}
