#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "host/xid_script.hpp"

namespace {

int fails = 0;
#define CHECK(x) \
    do { \
        if (!(x)) { \
            std::fprintf(stderr, "CHECK failed: %s (%d)\n", #x, __LINE__); \
            ++fails; \
        } \
    } while (0)

bool states_equal(const xlle::Ohci::XidGamepadState& a,
                   const xlle::Ohci::XidGamepadState& b) {
    return a.buttons == b.buttons && a.analog == b.analog &&
           a.thumb_lx == b.thumb_lx && a.thumb_ly == b.thumb_ly &&
           a.thumb_rx == b.thumb_rx && a.thumb_ry == b.thumb_ry;
}

}  // namespace

int main() {
    // --- Parser basics: relative counts, capture directive, comments/blank
    // lines, and the documented error cases (bad first-event relative count,
    // out-of-order count, malformed fields). ---
    {
        std::istringstream in(
            "# comment\n"
            "\n"
            "10 0002 0 0 0 0 0 0 0 0 0 0 0 0 capture first\n"
            "+5 0000 0xff 0 0 0 0 0 0 0 1 -2 3 -4\n");
        std::vector<xlle::GenericInputScript::Event> events;
        std::string error;
        CHECK(xlle::GenericInputScript::parse(in, events, error));
        CHECK(events.size() == 2);
        CHECK(events[0].at == 10 && events[0].state.buttons == 0x0002 &&
              events[0].capture && events[0].capture_name == "first");
        CHECK(events[1].at == 15 && events[1].state.analog[0] == 0xff &&
              events[1].state.thumb_lx == 1 && events[1].state.thumb_ly == -2 &&
              events[1].state.thumb_rx == 3 && events[1].state.thumb_ry == -4 &&
              !events[1].capture);
    }
    {
        // First event cannot be relative.
        std::istringstream in("+5 0000 0 0 0 0 0 0 0 0 0 0 0 0\n");
        std::vector<xlle::GenericInputScript::Event> events;
        std::string error;
        CHECK(!xlle::GenericInputScript::parse(in, events, error));
        CHECK(!error.empty());
    }
    {
        // Counts must be non-decreasing.
        std::istringstream in(
            "20 0000 0 0 0 0 0 0 0 0 0 0 0 0\n"
            "10 0000 0 0 0 0 0 0 0 0 0 0 0 0\n");
        std::vector<xlle::GenericInputScript::Event> events;
        std::string error;
        CHECK(!xlle::GenericInputScript::parse(in, events, error));
    }
    {
        // Missing fields.
        std::istringstream in("10 0002\n");
        std::vector<xlle::GenericInputScript::Event> events;
        std::string error;
        CHECK(!xlle::GenericInputScript::parse(in, events, error));
    }
    {
        // 'capture' with no name.
        std::istringstream in(
            "10 0000 0 0 0 0 0 0 0 0 0 0 0 0 capture\n");
        std::vector<xlle::GenericInputScript::Event> events;
        std::string error;
        CHECK(!xlle::GenericInputScript::parse(in, events, error));
    }
    {
        // Empty script.
        std::istringstream in("# only a comment\n");
        std::vector<xlle::GenericInputScript::Event> events;
        std::string error;
        CHECK(!xlle::GenericInputScript::parse(in, events, error));
    }

    // --- Engine basics on the simple two-event script above. ---
    {
        std::istringstream in(
            "10 0002 0 0 0 0 0 0 0 0 0 0 0 0 capture first\n"
            "+5 0000 0xff 0 0 0 0 0 0 0 0 0 0 0\n");
        std::vector<xlle::GenericInputScript::Event> events;
        std::string error;
        CHECK(xlle::GenericInputScript::parse(in, events, error));
        xlle::GenericInputScript script;
        script.reset(events);
        CHECK(script.phase() == xlle::GenericInputScript::Phase::kRunning);
        CHECK(script.state().buttons == 0);
        for (uint64_t count = 0; count < 10; ++count) {
            std::string name;
            CHECK(!script.will_capture(count, name));
            CHECK(!script.update(count).advanced);
            CHECK(script.state().buttons == 0);
        }
        std::string name;
        CHECK(script.will_capture(10, name) && name == "first");
        CHECK(script.state().buttons == 0);  // capture observes pre-transition state
        CHECK(script.update(10).advanced);
        CHECK(script.state().buttons == 0x0002);
        for (uint64_t count = 11; count < 15; ++count) {
            CHECK(!script.update(count).advanced);
            CHECK(script.state().buttons == 0x0002);
        }
        const auto final_transition = script.update(15);
        CHECK(final_transition.advanced && final_transition.completed);
        CHECK(script.phase() == xlle::GenericInputScript::Phase::kComplete);
        CHECK(script.state().analog[0] == 0xff && script.state().buttons == 0);
        // Complete is sticky; further updates are no-ops.
        CHECK(!script.update(1000).advanced);
        CHECK(script.state().analog[0] == 0xff);
    }

    // --- The real proof: tests/scripts/down-a.txt, parsed and driven
    // side-by-side against the pinned XidDownThenAReportScript across its
    // entire schedule (0..600), must deliver an identical XidGamepadState at
    // every single endpoint-2 report count. ---
    {
#ifndef XLLE_SOURCE_DIR
#error "XLLE_SOURCE_DIR must be defined by the build"
#endif
        const std::string path =
            std::string(XLLE_SOURCE_DIR) + "/tests/scripts/down-a.txt";
        std::ifstream file(path);
        CHECK(file.is_open());
        std::vector<xlle::GenericInputScript::Event> events;
        std::string error;
        const bool parsed = xlle::GenericInputScript::parse(file, events, error);
        CHECK(parsed);
        if (!parsed) std::fprintf(stderr, "down-a.txt parse error: %s\n", error.c_str());

        xlle::GenericInputScript generic;
        generic.reset(events);
        xlle::XidDownThenAReportScript pinned;

        bool pinned_completed = false;
        bool generic_completed = false;
        for (uint64_t report = 0; report <= 600; ++report) {
            std::string capture_name;
            const bool wants_capture = generic.will_capture(report, capture_name);
            const auto generic_transition = generic.update(report);
            const auto pinned_transition = pinned.update(/*configured=*/true, report);
            CHECK(states_equal(generic.state(), pinned.state()));
            if (pinned_transition.completed) pinned_completed = true;
            if (generic_transition.completed) generic_completed = true;
            (void)wants_capture;
        }
        CHECK(pinned_completed && generic_completed);
        CHECK(pinned.phase() == xlle::XidDownThenAReportScript::Phase::kComplete);
        CHECK(generic.phase() == xlle::GenericInputScript::Phase::kComplete);
    }

    std::printf(fails ? "test_xid_generic_script: %d checks failed\n" : "test_xid_generic_script: ok\n",
                fails);
    return fails ? 1 : 0;
}
