// xlle — deterministic host-keyboard to Xbox Controller S state mapping.
#pragma once

#include "machine/ohci.hpp"

namespace xlle {

enum class XidHostKey {
    Up,
    Down,
    Left,
    Right,
    A,
    B,
    Start,
    Back,
};

// Keeps host event handling separate from the guest-visible XID packet layout so the mapping
// has a deterministic, SDL-free unit test. A and B are pressure-sensitive XID analog buttons.
class XidKeyboardMapper {
public:
    void set(XidHostKey key, bool pressed);
    Ohci::XidGamepadState state() const;

private:
    bool up_ = false, down_ = false, left_ = false, right_ = false;
    bool a_ = false, b_ = false, start_ = false, back_ = false;
};

}  // namespace xlle
