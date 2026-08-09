// xlle — explicit, test-only deterministic XID report scripts.
#pragma once

#include <cstdint>

#include "machine/ohci.hpp"

namespace xlle {

// This state machine is driven only by the endpoint-2 IN counter sampled on
// the emulation thread.  It never creates USB work: Ohci observes this state
// only when the guest has scheduled an endpoint-2 IN TD.
class XidDownReportScript {
public:
    enum class Phase : uint8_t {
        kWaitingForConfiguration,
        kWaitingForMenu,
        kDown,
        kNeutralTail,
        kComplete,
        kCaptureFailed,
        kDeliveryFailed,
    };
    struct Transition {
        bool pressed = false;
        bool released = false;
        bool completed = false;
        bool delivery_failed = false;
    };

    static constexpr uint64_t kMenuReadyEndpoint2Reports = 320;
    static constexpr uint64_t kDownDeliveredReports = 5;
    static constexpr uint64_t kNeutralDeliveredReports = 5;

    Phase phase() const { return phase_; }
    bool will_press(bool configured, uint64_t endpoint2_reports) const {
        return phase_ == Phase::kWaitingForMenu && configured &&
               endpoint2_reports >= kMenuReadyEndpoint2Reports;
    }
    Transition update(bool configured, uint64_t endpoint2_reports) {
        Transition transition;
        if (phase_ == Phase::kWaitingForConfiguration) {
            if (configured) phase_ = Phase::kWaitingForMenu;
            return transition;
        }
        if (phase_ == Phase::kWaitingForMenu) {
            if (!configured) {
                phase_ = Phase::kWaitingForConfiguration;
            } else if (endpoint2_reports >= kMenuReadyEndpoint2Reports) {
                phase_ = Phase::kDown;
                down_started_at_ = endpoint2_reports;
                transition.pressed = true;
            }
            return transition;
        }
        if ((phase_ == Phase::kDown || phase_ == Phase::kNeutralTail) &&
            !configured) {
            phase_ = Phase::kDeliveryFailed;
            transition.delivery_failed = true;
        } else if (phase_ == Phase::kDown &&
            endpoint2_reports >= down_started_at_ + kDownDeliveredReports) {
            phase_ = Phase::kNeutralTail;
            neutral_started_at_ = endpoint2_reports;
            transition.released = true;
        } else if (phase_ == Phase::kNeutralTail &&
                   endpoint2_reports >=
                       neutral_started_at_ + kNeutralDeliveredReports) {
            phase_ = Phase::kComplete;
            transition.completed = true;
        }
        return transition;
    }
    void capture_failed() { phase_ = Phase::kCaptureFailed; }
    Ohci::XidGamepadState state() const {
        Ohci::XidGamepadState state;
        if (phase_ == Phase::kDown) state.buttons = 0x0002;  // XID D-pad Down
        return state;
    }
    static const char* phase_name(Phase phase) {
        switch (phase) {
            case Phase::kWaitingForConfiguration: return "waiting-config";
            case Phase::kWaitingForMenu: return "waiting-menu";
            case Phase::kDown: return "down";
            case Phase::kNeutralTail: return "neutral-tail";
            case Phase::kComplete: return "complete";
            case Phase::kCaptureFailed: return "capture-failed";
            case Phase::kDeliveryFailed: return "delivery-failed";
        }
        return "unknown";
    }

private:
    Phase phase_ = Phase::kWaitingForConfiguration;
    uint64_t down_started_at_ = 0;
    uint64_t neutral_started_at_ = 0;
};

// Separate test-only experiment: move selection with Down, leave it neutral
// long enough to capture the selected item, then activate with analog A.  It
// shares the endpoint-2-only scheduling rule but deliberately does not alter
// the established Down-only experiment above.
class XidDownThenAReportScript {
public:
    enum class Phase : uint8_t {
        kWaitingForConfiguration,
        kWaitingForMenu,
        kDown,
        kNeutralAfterDown,
        kWaitingForPreA,
        kA,
        kNeutralAfterA,
        kPostACaptureSettling,
        kFinalNeutral,
        kComplete,
        kCaptureFailed,
        kDeliveryFailed,
    };
    struct Transition {
        bool down_pressed = false;
        bool down_released = false;
        bool selected_capture_ready = false;
        bool pre_a_capture_ready = false;
        bool a_pressed = false;
        bool a_released = false;
        bool post_a_capture_ready = false;
        bool settled_capture_ready = false;
        bool completed = false;
        bool delivery_failed = false;
    };

    static constexpr uint64_t kMenuReadyEndpoint2Reports = 320;
    static constexpr uint64_t kDownDeliveredReports = 5;
    static constexpr uint64_t kNeutralAfterDownReports = 5;
    static constexpr uint64_t kPreAEndpoint2Reports = 408;
    static constexpr uint64_t kADeliveredReports = 5;
    static constexpr uint64_t kNeutralAfterAReports = 5;
    static constexpr uint64_t kPostACaptureSettlingReports = 78;
    static constexpr uint64_t kFinalNeutralReports = 78;

    Phase phase() const { return phase_; }
    bool will_press_down(bool configured, uint64_t endpoint2_reports) const {
        return phase_ == Phase::kWaitingForMenu && configured &&
               endpoint2_reports >= kMenuReadyEndpoint2Reports;
    }
    bool will_press_a(bool configured, uint64_t endpoint2_reports) const {
        return phase_ == Phase::kWaitingForPreA && configured &&
               endpoint2_reports >= kPreAEndpoint2Reports;
    }
    Transition update(bool configured, uint64_t endpoint2_reports) {
        Transition transition;
        if (phase_ == Phase::kWaitingForConfiguration) {
            if (configured) phase_ = Phase::kWaitingForMenu;
            return transition;
        }
        if (phase_ == Phase::kWaitingForMenu) {
            if (!configured) {
                phase_ = Phase::kWaitingForConfiguration;
            } else if (endpoint2_reports >= kMenuReadyEndpoint2Reports) {
                phase_ = Phase::kDown;
                down_started_at_ = endpoint2_reports;
                transition.down_pressed = true;
            }
            return transition;
        }
        if ((phase_ == Phase::kDown || phase_ == Phase::kNeutralAfterDown ||
             phase_ == Phase::kWaitingForPreA || phase_ == Phase::kA ||
             phase_ == Phase::kNeutralAfterA ||
             phase_ == Phase::kPostACaptureSettling ||
             phase_ == Phase::kFinalNeutral) && !configured) {
            phase_ = Phase::kDeliveryFailed;
            transition.delivery_failed = true;
            return transition;
        }
        if (phase_ == Phase::kDown &&
            endpoint2_reports >= down_started_at_ + kDownDeliveredReports) {
            phase_ = Phase::kNeutralAfterDown;
            neutral_after_down_started_at_ = endpoint2_reports;
            transition.down_released = true;
        } else if (phase_ == Phase::kNeutralAfterDown &&
                   endpoint2_reports >= neutral_after_down_started_at_ +
                                            kNeutralAfterDownReports) {
            phase_ = Phase::kWaitingForPreA;
            transition.selected_capture_ready = true;
        } else if (phase_ == Phase::kWaitingForPreA &&
                   endpoint2_reports >= kPreAEndpoint2Reports) {
            phase_ = Phase::kA;
            a_started_at_ = endpoint2_reports;
            transition.pre_a_capture_ready = true;
            transition.a_pressed = true;
        } else if (phase_ == Phase::kA &&
                   endpoint2_reports >= a_started_at_ + kADeliveredReports) {
            phase_ = Phase::kNeutralAfterA;
            neutral_after_a_started_at_ = endpoint2_reports;
            transition.a_released = true;
        } else if (phase_ == Phase::kNeutralAfterA &&
                   endpoint2_reports >= neutral_after_a_started_at_ +
                                            kNeutralAfterAReports) {
            phase_ = Phase::kPostACaptureSettling;
            post_a_capture_started_at_ = endpoint2_reports;
            transition.post_a_capture_ready = true;
        } else if (phase_ == Phase::kPostACaptureSettling &&
                   endpoint2_reports >= post_a_capture_started_at_ +
                                            kPostACaptureSettlingReports) {
            phase_ = Phase::kFinalNeutral;
            final_neutral_started_at_ = endpoint2_reports;
            transition.settled_capture_ready = true;
        } else if (phase_ == Phase::kFinalNeutral &&
                   endpoint2_reports >= final_neutral_started_at_ +
                                            kFinalNeutralReports) {
            phase_ = Phase::kComplete;
            transition.completed = true;
        }
        return transition;
    }
    void capture_failed() { phase_ = Phase::kCaptureFailed; }
    Ohci::XidGamepadState state() const {
        Ohci::XidGamepadState state;
        if (phase_ == Phase::kDown) {
            state.buttons = 0x0002;
        } else if (phase_ == Phase::kA) {
            state.analog[0] = 0xff;
        }
        return state;
    }
    static const char* phase_name(Phase phase) {
        switch (phase) {
            case Phase::kWaitingForConfiguration: return "waiting-config";
            case Phase::kWaitingForMenu: return "waiting-menu";
            case Phase::kDown: return "down";
            case Phase::kNeutralAfterDown: return "neutral-after-down";
            case Phase::kWaitingForPreA: return "waiting-pre-a";
            case Phase::kA: return "a";
            case Phase::kNeutralAfterA: return "neutral-after-a";
            case Phase::kPostACaptureSettling: return "post-a-settling";
            case Phase::kFinalNeutral: return "final-neutral";
            case Phase::kComplete: return "complete";
            case Phase::kCaptureFailed: return "capture-failed";
            case Phase::kDeliveryFailed: return "delivery-failed";
        }
        return "unknown";
    }

private:
    Phase phase_ = Phase::kWaitingForConfiguration;
    uint64_t down_started_at_ = 0;
    uint64_t neutral_after_down_started_at_ = 0;
    uint64_t a_started_at_ = 0;
    uint64_t neutral_after_a_started_at_ = 0;
    uint64_t post_a_capture_started_at_ = 0;
    uint64_t final_neutral_started_at_ = 0;
};

}  // namespace xlle
