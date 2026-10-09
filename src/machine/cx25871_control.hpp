// Bounded CX25871 control transport for original-kernel AV qualification.
// Register facts: Conexant 100381B, September 2001, sections 2.3/2.4,
// tables 2-1 and 2-5. Authored from the specification, no donor code bodies.
// This starts at an UNKNOWN warm-device state: a RAM-only kernel capture
// does not capture the encoder. Never infer its programmed state from zeros.
#pragma once
#include "machine/smbus.hpp"
#include <array>

namespace xlle {
class Cx25871Control final : public SmbusSlave {
public:
    explicit Cx25871Control(FaultFn fault) : fault_(std::move(fault)) {}
    const char* name() const override { return "cx25871-control"; }
    uint8_t address() const override { return 0x45; }
    bool read_byte(uint8_t reg, uint8_t& value) override {
        if (!known_[0x6c] || !(registers_[0x6c] & 0x40))
            return reject("CX25871 standard readback requires established EN_REG_RD state");
        if (!supported(reg) || !known_[reg])
            return reject("CX25871 read of uncaptured/unprogrammed control register");
        value = registers_[reg]; return true;
    }
    bool write_byte(uint8_t reg, uint8_t value) override {
        if (!supported(reg)) return reject("CX25871 control register outside qualified transport");
        // 6C=46 is the datasheet's standard-readback setup, without reset,
        // color bars or SCART. Broader timing/output modes remain unqualified.
        if (reg == 0x6c && value != 0x46)
            return reject("CX25871 timing/reset/output control outside qualified transport");
        if (reg == 0x34 && (value & 0x64))
            return reject("CX25871 reserved adaptive-filter control bits");
        registers_[reg] = value; known_[reg] = true; programmed_ = true;
        return true;
    }
    bool read_word(uint8_t, uint16_t&) override { return reject("CX25871 word read outside qualified transport"); }
    bool write_word(uint8_t, uint16_t) override { return reject("CX25871 word write outside qualified transport"); }
    bool receive_byte(uint8_t&) override { return reject("CX25871 legacy/stream read outside qualified transport"); }
    bool quick(bool) override { return reject("CX25871 quick command outside qualified transport"); }
    bool programmed() const { return programmed_; }
private:
    static bool supported(uint8_t reg) {
        return reg == 0x34 || reg == 0x36 || reg == 0x6c || reg == 0x96 || reg == 0xc8;
    }
    bool reject(const char* reason) { if (fault_) fault_(reason); return false; }
    FaultFn fault_;
    std::array<uint8_t, 256> registers_{};
    std::array<bool, 256> known_{};
    bool programmed_ = false;
};
} // namespace xlle
