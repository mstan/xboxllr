// xlle — focused SMC telemetry/readback test.
#include <cstdio>
#include <string>

#include "machine/smbus.hpp"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL: %s (line %d)\n", #c, __LINE__); ++fails; } } while (0)

int main() {
    std::string fault;
    xlle::XboxSmc smc([&](const std::string& why) { fault = why; });
    uint8_t value = 0;

    CHECK(smc.read_byte(0x10, value));
    CHECK(value == 50);
    CHECK(fault.empty());

    CHECK(smc.write_byte(0x06, 25));
    CHECK(smc.read_byte(0x10, value));
    CHECK(value == 25);

    CHECK(smc.write_byte(0x06, 0xff));
    CHECK(smc.read_byte(0x10, value));
    CHECK(value == 50);
    CHECK(fault.empty());

    std::printf(fails ? "test_smc: %d checks failed\n" : "test_smc: ok\n", fails);
    return fails ? 1 : 0;
}
