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

}  // namespace xlle
