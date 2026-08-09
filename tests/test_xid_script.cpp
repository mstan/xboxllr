#include <cstdio>

#include "host/xid_script.hpp"

int main() {
    int fails = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "CHECK failed: %s (%d)\n", #x, __LINE__); ++fails; } } while (0)
    xlle::XidDownReportScript script;
    CHECK(script.phase() == xlle::XidDownReportScript::Phase::kWaitingForConfiguration);
    CHECK(script.state().buttons == 0);
    CHECK(!script.update(false, 400).pressed);
    CHECK(script.phase() == xlle::XidDownReportScript::Phase::kWaitingForConfiguration);

    script.update(true, 319);
    CHECK(script.phase() == xlle::XidDownReportScript::Phase::kWaitingForMenu);
    CHECK(!script.will_press(true, 319));
    CHECK(script.will_press(true, 320));
    const auto press = script.update(true, 320);
    CHECK(press.pressed && script.state().buttons == 0x0002);
    for (uint64_t reports = 321; reports < 325; ++reports) {
        const auto transition = script.update(true, reports);
        CHECK(!transition.released && script.state().buttons == 0x0002);
    }
    const auto release = script.update(true, 325);
    CHECK(release.released && script.state().buttons == 0);
    for (uint64_t reports = 326; reports < 330; ++reports) {
        const auto transition = script.update(true, reports);
        CHECK(!transition.completed && script.state().buttons == 0);
    }
    const auto complete = script.update(true, 330);
    CHECK(complete.completed && script.state().buttons == 0 &&
          script.phase() == xlle::XidDownReportScript::Phase::kComplete);

    xlle::XidDownReportScript deconfigured;
    deconfigured.update(true, 0);
    CHECK(deconfigured.update(true, 320).pressed &&
          deconfigured.state().buttons == 0x0002);
    CHECK(deconfigured.update(false, 321).delivery_failed &&
          deconfigured.state().buttons == 0 &&
          deconfigured.phase() == xlle::XidDownReportScript::Phase::kDeliveryFailed);

    xlle::XidDownReportScript capture_failed;
    capture_failed.capture_failed();
    CHECK(capture_failed.phase() == xlle::XidDownReportScript::Phase::kCaptureFailed &&
          capture_failed.state().buttons == 0);
    std::printf(fails ? "test_xid_script: %d checks failed\n" : "test_xid_script: ok\n",
                fails);
    return fails ? 1 : 0;
#undef CHECK
}
