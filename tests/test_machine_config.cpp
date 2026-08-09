// Focused construction coverage for the explicit, test-only USB topology.
#include <cstdio>

#include "machine/machine.hpp"

int main() {
    int fails = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "CHECK failed: %s (%d)\n", #x, __LINE__); ++fails; } } while (0)
    xlle::XboxMachine direct;
    CHECK(direct.ok());
    CHECK(direct.init_ram());
    direct.init_devices();
    CHECK(direct.ohci0() != nullptr);
    CHECK(direct.ohci0()->synthetic_hub_port_status(0) == 0);

    xlle::XboxMachine::Config synthetic_config;
    synthetic_config.ohci0_topology =
        xlle::Ohci::UsbTopology::kSyntheticControllerSHubForTest;
    xlle::XboxMachine synthetic(synthetic_config);
    CHECK(synthetic.ok());
    CHECK(synthetic.init_ram());
    synthetic.init_devices();
    CHECK(synthetic.ohci0() != nullptr);
    CHECK((synthetic.ohci0()->synthetic_hub_port_status(0) & 0x00010001u) ==
          0x00010001u);
    std::printf(fails ? "test_machine_config: %d checks failed\n"
                      : "test_machine_config: ok\n", fails);
    return fails ? 1 : 0;
#undef CHECK
}
