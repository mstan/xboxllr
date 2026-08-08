// xlle — deterministic host-keyboard to Xbox Controller S state mapping.
#include "host/xid_input.hpp"

namespace xlle {
namespace {
constexpr uint16_t kDpadUp = 0x0001;
constexpr uint16_t kDpadDown = 0x0002;
constexpr uint16_t kDpadLeft = 0x0004;
constexpr uint16_t kDpadRight = 0x0008;
constexpr uint16_t kStart = 0x0010;
constexpr uint16_t kBack = 0x0020;
}  // namespace

void XidKeyboardMapper::set(XidHostKey key, bool pressed) {
    switch (key) {
        case XidHostKey::Up: up_ = pressed; break;
        case XidHostKey::Down: down_ = pressed; break;
        case XidHostKey::Left: left_ = pressed; break;
        case XidHostKey::Right: right_ = pressed; break;
        case XidHostKey::A: a_ = pressed; break;
        case XidHostKey::B: b_ = pressed; break;
        case XidHostKey::Start: start_ = pressed; break;
        case XidHostKey::Back: back_ = pressed; break;
    }
}

Ohci::XidGamepadState XidKeyboardMapper::state() const {
    Ohci::XidGamepadState output{};
    if (up_) output.buttons |= kDpadUp;
    if (down_) output.buttons |= kDpadDown;
    if (left_) output.buttons |= kDpadLeft;
    if (right_) output.buttons |= kDpadRight;
    if (start_) output.buttons |= kStart;
    if (back_) output.buttons |= kBack;
    output.analog[0] = a_ ? 0xff : 0;  // XID A
    output.analog[1] = b_ ? 0xff : 0;  // XID B
    return output;
}

}  // namespace xlle
