#include "host/xid_input.hpp"

#include <cstdio>

int main() {
    int fails = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "CHECK failed: %s (%d)\n", #x, __LINE__); ++fails; } } while (0)
    xlle::XidKeyboardMapper mapper;
    CHECK(mapper.state().buttons == 0);
    CHECK(mapper.state().analog[0] == 0 && mapper.state().analog[1] == 0);

    mapper.set(xlle::XidHostKey::Up, true);
    mapper.set(xlle::XidHostKey::Left, true);
    mapper.set(xlle::XidHostKey::Start, true);
    mapper.set(xlle::XidHostKey::A, true);
    auto state = mapper.state();
    CHECK(state.buttons == (0x0001 | 0x0004 | 0x0010));
    CHECK(state.analog[0] == 0xff && state.analog[1] == 0);

    mapper.set(xlle::XidHostKey::Up, false);
    mapper.set(xlle::XidHostKey::Left, false);
    mapper.set(xlle::XidHostKey::Start, false);
    mapper.set(xlle::XidHostKey::A, false);
    mapper.set(xlle::XidHostKey::Down, true);
    mapper.set(xlle::XidHostKey::Right, true);
    mapper.set(xlle::XidHostKey::Back, true);
    mapper.set(xlle::XidHostKey::B, true);
    state = mapper.state();
    CHECK(state.buttons == (0x0002 | 0x0008 | 0x0020));
    CHECK(state.analog[0] == 0 && state.analog[1] == 0xff);
    std::printf(fails ? "test_xid_input: %d checks failed\n" : "test_xid_input: ok\n", fails);
    return fails ? 1 : 0;
#undef CHECK
}
