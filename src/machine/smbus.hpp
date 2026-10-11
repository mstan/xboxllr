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

    void set_tray_state_reader(std::function<uint8_t()> reader) { tray_state_ = std::move(reader); }
    // Retain programmed LED requests for the board/output owner. This is
    // host telemetry, not invented SMBus readback or automatic/blink timing.
    struct LedRequests {uint8_t known=0,mode=0,sequence=0;};
    LedRequests led_requests() const { return led_; }

private:
    FaultFn fault_;
    std::function<uint8_t()> tray_state_;
    uint8_t scratch_reg_ = 0;
    uint8_t error_reg_ = 0;
    uint8_t fan_speed_ = 50;  // SMC units are 0..50; fixed auto-mode telemetry for now.
    uint8_t version_[3] = {0x50, 0x31, 0x31};  // "P11" (SMC/PIC version, read from silicon)
    int version_index_ = 0;
    LedRequests led_;
};

// Conexant CX25871 video encoder, 7-bit address 0x45 (the v1.x boards' TV encoder; this
// board confirmed Conexant via silicon AVPACK/probes). Semantics from xemu/hw/xbox/
// smbus_cx25871.c: a 256-byte register file — writes store, reads return the stored value
// (reset 0). The kernel's AV worker programs it over interrupt-driven SMBus during
// AvSetDisplayMode; an absent encoder wedges the boot in an error-retry loop.
class Cx25871 : public SmbusSlave {
public:
    // Optional private-capture observation of actual guest writes. This does
    // not claim that this legacy register store models all encoder effects.
    void observe_writes(std::function<void(uint8_t,uint8_t)> callback) { write_observer_=std::move(callback); }
    const char* name() const override { return "cx25871"; }
    uint8_t address() const override { return 0x45; }

    bool read_byte(uint8_t cmd, uint8_t& out) override {
        out = regs_[cmd];
        return true;
    }
    bool read_word(uint8_t cmd, uint16_t& out) override {
        out = static_cast<uint16_t>(regs_[cmd] | (regs_[static_cast<uint8_t>(cmd + 1)] << 8));
        return true;
    }
    bool write_byte(uint8_t cmd, uint8_t val) override {
        if (write_observer_) write_observer_(cmd,val);
        regs_[cmd] = val;
        cmd_ = cmd;
        return true;
    }
    bool write_word(uint8_t cmd, uint16_t val) override {
        if (write_observer_) {
            write_observer_(cmd,static_cast<uint8_t>(val));
            write_observer_(static_cast<uint8_t>(cmd+1),static_cast<uint8_t>(val>>8));
        }
        regs_[cmd] = static_cast<uint8_t>(val);
        regs_[static_cast<uint8_t>(cmd + 1)] = static_cast<uint8_t>(val >> 8);
        cmd_ = cmd;
        return true;
    }
    bool receive_byte(uint8_t& out) override {
        out = regs_[cmd_++];
        return true;
    }
    bool quick(bool) override { return true; }

private:
    std::function<void(uint8_t,uint8_t)> write_observer_;
    uint8_t regs_[256] = {0};
    uint8_t cmd_ = 0;
};

// The Xbox configuration EEPROM (24C02-class, 256 bytes), 7-bit address 0x54. Semantics
// follow xemu/hw/xbox/smbus_storage.c: a write latches the offset (and optionally stores
// bytes); reads return data at the latched offset (word reads span two bytes). Backed by the
// real console's dump (machine/eeprom.bin) — the kernel HMAC-verifies and RC4-decrypts it,
// and derives the HDD-unlock key from it, so real bytes are required. Guest writes are kept
// in-memory only (never persisted over the oracle artifact) and logged.
class XboxEeprom : public SmbusSlave {
public:
    // An external owner supplies each complete byte/word access atomically.
    // It owns the256 bytes; this device retains only the serial offset.
    using Access = std::function<bool(uint8_t,unsigned,bool,uint16_t&)>;
    explicit XboxEeprom(std::vector<uint8_t> data) : data_(std::move(data)) {}
    explicit XboxEeprom(Access access) : access_(std::move(access)) {}
    const char* name() const override { return "eeprom"; }
    uint8_t address() const override { return 0x54; }

    bool read_byte(uint8_t cmd, uint8_t& out) override {
        offset_ = cmd;
        uint16_t value=0;
        if(!access_data(offset_,1,false,value))return false;
        out = static_cast<uint8_t>(value);
        offset_ = (offset_ + 1) % data_size();
        return true;
    }
    bool read_word(uint8_t cmd, uint16_t& out) override {
        offset_ = cmd;
        uint16_t value=0;
        if(!access_data(offset_,2,false,value))return false;
        out=value;
        offset_ = (offset_ + 2) % data_size();
        return true;
    }
    bool write_byte(uint8_t cmd, uint8_t val) override;
    bool write_word(uint8_t cmd, uint16_t val) override;
    bool receive_byte(uint8_t& out) override {
        uint16_t value=0;
        if(!access_data(offset_,1,false,value))return false;
        out = static_cast<uint8_t>(value);
        offset_ = (offset_ + 1) % data_size();
        return true;
    }
    bool quick(bool) override { return true; }

private:
    uint32_t data_size() const { return access_?256u:static_cast<uint32_t>(data_.size()); }
    bool access_data(uint32_t offset,unsigned width,bool write,uint16_t& value) {
        if(access_)return access_(static_cast<uint8_t>(offset),width,write,value);
        if(data_.empty())return false;
        uint16_t result=0;
        for(unsigned i=0;i<width;++i) {
            auto& byte=data_[(offset+i)%data_.size()];
            if(write)byte=static_cast<uint8_t>(value>>(8*i));
            else result|=static_cast<uint16_t>(byte)<<(8*i);
        }
        if(!write)value=result;
        return true;
    }
    Access access_;
    std::vector<uint8_t> data_;
    uint32_t offset_ = 0;
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
