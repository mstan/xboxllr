#include <cstdio>

#include "host/xid_script.hpp"

int main() {
    int fails = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "CHECK failed: %s (%d)\n", #x, __LINE__); ++fails; } } while (0)
    xlle::XidDownThenAReportScript script;
    CHECK(script.state().buttons == 0 && script.state().analog[0] == 0);
    script.update(false, 400);
    CHECK(script.phase() == xlle::XidDownThenAReportScript::Phase::kWaitingForConfiguration);
    script.update(true, 319);
    CHECK(script.will_press_down(true, 320));
    CHECK(script.update(true, 320).down_pressed && script.state().buttons == 0x0002);
    for (uint64_t report = 321; report < 325; ++report)
        CHECK(script.update(true, report).down_released == false &&
              script.state().buttons == 0x0002);
    CHECK(script.update(true, 325).down_released && script.state().buttons == 0 &&
          script.state().analog[0] == 0);
    for (uint64_t report = 326; report < 330; ++report)
        CHECK(!script.update(true, report).selected_capture_ready &&
              script.state().buttons == 0 && script.state().analog[0] == 0);
    CHECK(script.update(true, 330).selected_capture_ready);
    // Keep the proven My Files selection neutral through the exact no-input
    // final boundary.  Capture is armed before A at endpoint-2 report 408.
    for (uint64_t report = 331; report < 408; ++report)
        CHECK(!script.update(true, report).a_pressed && script.state().analog[0] == 0);
    CHECK(script.will_press_a(true, 408));
    const auto a_press = script.update(true, 408);
    CHECK(a_press.pre_a_capture_ready && a_press.a_pressed &&
          script.state().buttons == 0 && script.state().analog[0] == 0xff);
    for (uint64_t report = 409; report < 413; ++report)
        CHECK(!script.update(true, report).a_released && script.state().analog[0] == 0xff);
    CHECK(script.update(true, 413).a_released && script.state().analog[0] == 0);
    for (uint64_t report = 414; report < 418; ++report)
        CHECK(!script.update(true, report).post_a_capture_ready &&
              script.state().analog[0] == 0);
    CHECK(script.update(true, 418).post_a_capture_ready && script.state().analog[0] == 0);
    for (uint64_t report = 419; report < 496; ++report)
        CHECK(!script.update(true, report).settled_capture_ready &&
              script.state().analog[0] == 0);
    CHECK(script.update(true, 496).settled_capture_ready && script.state().analog[0] == 0);
    for (uint64_t report = 497; report < 574; ++report)
        CHECK(!script.update(true, report).completed && script.state().analog[0] == 0);
    CHECK(script.update(true, 574).completed &&
          script.phase() == xlle::XidDownThenAReportScript::Phase::kComplete);

    xlle::XidDownThenAReportScript deconfigured;
    deconfigured.update(true, 0);
    deconfigured.update(true, 320);
    CHECK(deconfigured.update(false, 321).delivery_failed &&
          deconfigured.state().buttons == 0 && deconfigured.state().analog[0] == 0);

    xlle::XidDownThenAReportScript capture_failed;
    capture_failed.capture_failed();
    CHECK(capture_failed.phase() ==
          xlle::XidDownThenAReportScript::Phase::kCaptureFailed);
    CHECK(capture_failed.state().buttons == 0 &&
          capture_failed.state().analog[0] == 0);
    std::printf(fails ? "test_xid_down_a_script: %d checks failed\n"
                      : "test_xid_down_a_script: ok\n", fails);
    return fails ? 1 : 0;
#undef CHECK
}
