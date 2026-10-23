// SPDX-License-Identifier: GPL-3.0-or-later
#include "boot_timeline.h"
#include <cstdlib>
#include <iostream>

static void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main() {
    BootTimeline clock;
    require(clock.next_deadline_ms() == 17, "First picture deadline");
    require(clock.observe(17) == 17 && clock.next_deadline_ms() == 34, "First elapsed tick");
    require(clock.observe(34) == 34 && clock.next_deadline_ms() == 50, "Second elapsed tick");

    // A resize/readback/render delay advances the original clock immediately.
    // No rapid series of stale 50/67/84 ms pictures may stretch the scene.
    require(clock.observe(143) == 143 && clock.next_deadline_ms() == 150,
            "Late picture uses real time and retires missed deadlines");
    require(clock.observe(1000) == 1000 && clock.next_deadline_ms() == 1017,
            "One second stays one second after a stall");
    require(clock.observe(8000) == 8000 && clock.next_deadline_ms() == 8017,
            "Eight-second original end receives the wall-clock boundary");

    bool rejected = false;
    try {
        clock.observe(7999);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "Backward observation rejects before changing the timeline");
    require(clock.next_deadline_ms() == 8017, "Rejected clock preserves its state");
    rejected = false;
    try {
        clock.observe(std::uint64_t{1} << 32);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "Unrepresentable original tick rejects");

    // Offline rendering remains exactly the established 480-picture schedule.
    require(BootTimeline::frame_tick(0) == 0 && BootTimeline::frame_tick(1) == 17 &&
                BootTimeline::frame_tick(2) == 34 && BootTimeline::frame_tick(3) == 50 &&
                BootTimeline::frame_tick(480) == 8000,
            "Deterministic capture schedule remains stable");
    std::cout << "Native startup presentation clock checks passed\n";
    return 0;
}
