// xlle — Xbox SMBus: AMD756 host controller + I2C/SMBus slaves (A2c).
//
// The MCPX southbridge exposes an AMD756-compatible SMBus host controller. The 2BL drives it
// at I/O base 0xC000; register layout and transaction semantics are harvested faithfully from
// xemu/hw/xbox/amd_smbus.c. Slaves hang off an internal I2C bus keyed by 7-bit address.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "machine/io_device.hpp"

namespace xlle {

// An SMBus/I2C slave. The op set mirrors the AMD756 protocol subset the firmware drives.
// Return false to signal a bus-level NAK/error (the controller turns this into a PRERR status
// bit the firmware polls — NOT a machine fault). To fault the machine on an unmodeled register,
// a slave calls its FaultFn directly.
class SmbusSlave {
public:
    virtual ~SmbusSlave() = default;
    virtual const char* name() const = 0;
    virtual uint8_t address() const = 0;  // 7-bit address

    virtual bool read_byte(uint8_t cmd, uint8_t& out) = 0;
    virtual bool read_word(uint8_t cmd, uint16_t& out) = 0;
    virtual bool write_byte(uint8_t cmd, uint8_t val) = 0;
    virtual bool write_word(uint8_t cmd, uint16_t val) = 0;
    virtual bool receive_byte(uint8_t& out) = 0;
    virtual bool quick(bool read) = 0;
};

// The Xbox System Management Controller (a PIC16LC), 7-bit address 0x10. Register semantics
// follow xemu/hw/xbox/smbus_xbox_smc.c. Registers the boot has not yet exercised fault-stop
// (loud) rather than returning a guessed value.
class XboxSmc : public SmbusSlave {
public:
    explicit XboxSmc(FaultFn fault) : fault_(std::move(fault)) {}
    const char* name() const override { return "smc"; }
    uint8_t address() const override { return 0x10; }

    bool read_byte(uint8_t cmd, uint8_t& out) override;
    bool read_word(uint8_t cmd, uint16_t& out) override;
    bool write_byte(uint8_t cmd, uint8_t val) override;
    bool write_word(uint8_t cmd, uint16_t val) override;
    bool receive_byte(uint8_t& out) override;
    bool quick(bool) override { return true; }

private:
    FaultFn fault_;
    uint8_t scratch_reg_ = 0;
    uint8_t error_reg_ = 0;
    uint8_t version_[3] = {0x50, 0x31, 0x31};  // "P11" (SMC/PIC version, read from silicon)
    int version_index_ = 0;
};

// AMD756 SMBus host controller as a 16-port I/O device. Register offsets (from base):
//   0x0 GLOBAL_STATUS, 0x2 GLOBAL_ENABLE (protocol+start), 0x4 HOST_ADDRESS,
//   0x6/0x7 HOST_DATA0/1, 0x8 HOST_COMMAND, 0x9 HOST_BLOCK_DATA.
class Amd756Smbus : public IoPortDevice {
public:
    Amd756Smbus(uint16_t base, FaultFn fault) : base_(base), fault_(std::move(fault)) {}

    void add_slave(std::unique_ptr<SmbusSlave> s) { slaves_.push_back(std::move(s)); }

    // Wire the controller's completion interrupt (level-triggered). The kernel's HAL SMBus
    // driver starts transactions with the interrupt-enable bit (GE_HCYC_EN) set and blocks on a
    // completion event that this IRQ's ISR signals. The Xbox routes SMBus to IRQ 11 (silicon:
    // LPC cfg 0x64 = 0x0b0c). Without this, kernel phase-1 hangs forever.
    void set_irq(std::function<void(int irq, bool level)> cb, int irq_num) {
        irq_cb_ = std::move(cb);
        irq_num_ = irq_num;
    }

    const char* name() const override { return "amd756-smbus"; }
    uint16_t io_base() const override { return base_; }
    uint16_t io_size() const override { return 16; }
    uint32_t io_read(uint16_t port, int size) override;
    void io_write(uint16_t port, int size, uint32_t value) override;

private:
    void transaction();
    void update_irq();
    SmbusSlave* find(uint8_t addr7);

    uint16_t base_;
    FaultFn fault_;
    std::vector<std::unique_ptr<SmbusSlave>> slaves_;
    std::function<void(int, bool)> irq_cb_;
    int irq_num_ = 11;
    bool irq_asserted_ = false;

    // Controller state (matches xemu's AMD756SMBus fields).
    uint8_t stat_ = 0, ctl_ = 0, addr_ = 0, cmd_ = 0, data0_ = 0, data1_ = 0;
};

}  // namespace xlle
