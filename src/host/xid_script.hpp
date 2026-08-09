// xlle — explicit, test-only deterministic XID report scripts.
#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <istream>
#include <sstream>
#include <string>
#include <vector>

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

// General, file-driven XID report script sharing the exact endpoint-2-count-
// keyed delivery model as XidDownReportScript/XidDownThenAReportScript above
// (deterministic, guest-scheduled polls only -- no UI shortcuts, no wall-
// clock timers). Where those two classes hardcode one specific experiment's
// phases directly in C++, this class is driven entirely by a parsed script
// file, so a new navigation experiment is a new text file, not a new C++
// class. XidDownReportScript/XidDownThenAReportScript are kept exactly as
// they are -- pinned evidence for their own tests -- and are not replaced;
// tests/test_xid_generic_script.cpp instead proves this general mechanism
// reproduces XidDownThenAReportScript's own exact delivery schedule when fed
// an equivalent script, rather than asserting anything about this class in
// isolation.
//
// --- Script file format -----------------------------------------------------
// Blank lines and lines whose first non-space character is '#' are ignored.
// Every other line is one EVENT, whitespace-separated:
//
//   <count> <buttons_hex> <a0> <a1> <a2> <a3> <a4> <a5> <a6> <a7> \
//       <lx> <ly> <rx> <ry> [capture <name>]
//
// - <count> is the absolute endpoint-2 IN report count (matching
//   Ohci::xid_endpoint2_input_reports(), i.e. the same counter both pinned
//   scripts above key off of) at which this event's state becomes the
//   delivered XidGamepadState. It may instead be written "+<delta>",
//   meaning <delta> reports after the PREVIOUS event's own resolved
//   absolute count -- this is how a script expresses "hold until count M"
//   without hand-computing M, exactly what XidDownThenAReportScript's own
//   kNNNReports constants do, just spelled out per event instead of
//   hardcoded. The first event's count must be absolute (no leading '+').
//   Events must be listed in non-decreasing resolved-count order.
// - <buttons_hex> is the 16-bit XID button mask in hex, e.g. 0002 for
//   D-pad Down (matching Ohci::XidGamepadState::buttons).
// - <a0>..<a7> are the 8 XID analog bytes, decimal or 0x.. hex (matching
//   XidGamepadState::analog[0..7]; analog[0] is the A button's analog
//   pressure byte, per XidDownThenAReportScript's own state()).
// - <lx> <ly> <rx> <ry> are the 4 signed 16-bit thumbstick axes (matching
//   XidGamepadState::thumb_{lx,ly,rx,ry}).
// - an optional trailing "capture <name>" (a single word, no spaces)
//   requests a named scanout snapshot (via the existing
//   --test-input-capture-prefix mechanics) the moment this event's count
//   is FIRST reached, using the state that was active immediately BEFORE
//   this event -- i.e. the previous event's state, not this one's. This
//   matches both kinds of capture XidDownThenAReportScript's own host
//   loop performs: the ones that precede a state change (its own
//   "pre-down"/"pre-a-stable", captured via will_press_down()/
//   will_press_a() before update() applies the new state) and the ones
//   that don't (its own "selected"/"post-a"/"settled", where the state is
//   already unchanged going into that boundary, so "before" and "after"
//   are the same value) -- one consistent rule covers both.
// The script is complete once the LAST event's count is reached; the
// delivered state from then on is that last event's state, held forever
// (there is no implicit return to neutral -- write a final all-neutral
// event if that is the desired end state, exactly as every pinned script
// above ends neutral).
class GenericInputScript {
public:
    struct Event {
        uint64_t at = 0;
        Ohci::XidGamepadState state;
        bool capture = false;
        std::string capture_name;
    };
    enum class Phase : uint8_t { kEmpty, kRunning, kComplete };

    // Parses every line of `in` into `events` (appended; caller should pass
    // an empty vector for a single call). Returns false and fills `error`
    // (naming the 1-based line number) on the first malformed line or a
    // count that is not >= the previous event's resolved count.
    static bool parse(std::istream& in, std::vector<Event>& events,
                       std::string& error) {
        std::string line;
        uint64_t line_no = 0;
        uint64_t previous_at = 0;
        bool have_previous = false;
        while (std::getline(in, line)) {
            ++line_no;
            std::string trimmed = line;
            const size_t hash = trimmed.find('#');
            if (hash != std::string::npos) trimmed.resize(hash);
            std::istringstream tokens(trimmed);
            std::string first;
            if (!(tokens >> first)) continue;  // blank (or comment-only) line
            Event event;
            bool relative = false;
            std::string count_text = first;
            if (!count_text.empty() && count_text[0] == '+') {
                relative = true;
                count_text.erase(0, 1);
            }
            errno = 0;
            char* end = nullptr;
            const unsigned long long delta =
                std::strtoull(count_text.c_str(), &end, 10);
            if (count_text.empty() || !end || *end != '\0' || errno == ERANGE) {
                error = "line " + std::to_string(line_no) +
                        ": bad count '" + first + "'";
                return false;
            }
            if (relative && !have_previous) {
                error = "line " + std::to_string(line_no) +
                        ": first event's count must be absolute";
                return false;
            }
            event.at = relative ? previous_at + delta : delta;
            if (have_previous && event.at < previous_at) {
                error = "line " + std::to_string(line_no) +
                        ": count " + std::to_string(event.at) +
                        " precedes previous event's " +
                        std::to_string(previous_at);
                return false;
            }
            std::string buttons_text;
            if (!(tokens >> buttons_text)) {
                error = "line " + std::to_string(line_no) + ": missing buttons field";
                return false;
            }
            errno = 0;
            end = nullptr;
            const unsigned long buttons =
                std::strtoul(buttons_text.c_str(), &end, 16);
            if (buttons_text.empty() || !end || *end != '\0' || errno == ERANGE ||
                buttons > 0xFFFFu) {
                error = "line " + std::to_string(line_no) +
                        ": bad buttons_hex '" + buttons_text + "'";
                return false;
            }
            event.state.buttons = static_cast<uint16_t>(buttons);
            for (size_t analog = 0; analog < event.state.analog.size(); ++analog) {
                std::string text;
                if (!(tokens >> text)) {
                    error = "line " + std::to_string(line_no) +
                            ": missing analog[" + std::to_string(analog) + "] field";
                    return false;
                }
                errno = 0;
                end = nullptr;
                const unsigned long value = std::strtoul(text.c_str(), &end, 0);
                if (text.empty() || !end || *end != '\0' || errno == ERANGE ||
                    value > 0xFFu) {
                    error = "line " + std::to_string(line_no) +
                            ": bad analog value '" + text + "'";
                    return false;
                }
                event.state.analog[analog] = static_cast<uint8_t>(value);
            }
            int16_t* const thumbs[4] = {&event.state.thumb_lx, &event.state.thumb_ly,
                                        &event.state.thumb_rx, &event.state.thumb_ry};
            static const char* const thumb_names[4] = {"lx", "ly", "rx", "ry"};
            for (int thumb = 0; thumb < 4; ++thumb) {
                std::string text;
                if (!(tokens >> text)) {
                    error = "line " + std::to_string(line_no) + ": missing " +
                            thumb_names[thumb] + " field";
                    return false;
                }
                errno = 0;
                end = nullptr;
                const long value = std::strtol(text.c_str(), &end, 0);
                if (text.empty() || !end || *end != '\0' || errno == ERANGE ||
                    value < -32768 || value > 32767) {
                    error = "line " + std::to_string(line_no) + ": bad " +
                            thumb_names[thumb] + " value '" + text + "'";
                    return false;
                }
                *thumbs[thumb] = static_cast<int16_t>(value);
            }
            std::string maybe_capture;
            if (tokens >> maybe_capture) {
                if (maybe_capture != "capture") {
                    error = "line " + std::to_string(line_no) +
                            ": unexpected trailing token '" + maybe_capture + "'";
                    return false;
                }
                if (!(tokens >> event.capture_name) || event.capture_name.empty()) {
                    error = "line " + std::to_string(line_no) +
                            ": 'capture' needs a name";
                    return false;
                }
                event.capture = true;
            }
            events.push_back(event);
            previous_at = event.at;
            have_previous = true;
        }
        if (events.empty()) {
            error = "script has no events";
            return false;
        }
        return true;
    }

    void reset(std::vector<Event> events) {
        events_ = std::move(events);
        index_ = 0;
        phase_ = events_.empty() ? Phase::kEmpty : Phase::kRunning;
    }

    Phase phase() const { return phase_; }
    size_t index() const { return index_; }
    // The currently-delivered state: neutral before the first event fires,
    // then each event's own state once its count has been reached.
    Ohci::XidGamepadState state() const {
        if (index_ == 0) return Ohci::XidGamepadState{};
        return events_[index_ - 1].state;
    }
    // Callers check this BEFORE update(), exactly mirroring
    // XidDownThenAReportScript's own will_press_down()/will_press_a() pre-
    // checks -- so any requested capture happens while state() still holds
    // the pre-transition value.
    bool will_capture(uint64_t endpoint2_reports, std::string& name) const {
        if (phase_ != Phase::kRunning) return false;
        if (endpoint2_reports < events_[index_].at) return false;
        if (!events_[index_].capture) return false;
        name = events_[index_].capture_name;
        return true;
    }
    struct Transition {
        bool advanced = false;
        bool completed = false;
    };
    Transition update(uint64_t endpoint2_reports) {
        Transition transition;
        if (phase_ != Phase::kRunning) return transition;
        if (endpoint2_reports < events_[index_].at) return transition;
        ++index_;
        transition.advanced = true;
        if (index_ >= events_.size()) {
            phase_ = Phase::kComplete;
            transition.completed = true;
        }
        return transition;
    }
    static const char* phase_name(Phase phase) {
        switch (phase) {
            case Phase::kEmpty: return "empty";
            case Phase::kRunning: return "running";
            case Phase::kComplete: return "complete";
        }
        return "unknown";
    }

private:
    std::vector<Event> events_;
    size_t index_ = 0;
    Phase phase_ = Phase::kEmpty;
};

}  // namespace xlle
