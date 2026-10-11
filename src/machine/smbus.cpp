// xlle — Xbox SMBus (AMD756 controller + SMC), faithful to xemu/hw/xbox/{amd_smbus,
// smbus_xbox_smc}.c. See smbus.hpp for the register map.
#include "machine/smbus.hpp"

#include <cstdio>

namespace xlle {

// --- AMD756 register offsets & bits (xemu amd_smbus.c) --------------------------------------
enum {
    SMB_GLOBAL_STATUS = 0x0,
    SMB_GLOBAL_ENABLE = 0x2,
    SMB_HOST_ADDRESS  = 0x4,
    SMB_HOST_DATA     = 0x6,
    SMB_HOST_COMMAND  = 0x8,
};
enum {  // GLOBAL_STATUS bits
    GS_ABRT_STS  = 1 << 0,
    GS_COL_STS   = 1 << 1,
    GS_PRERR_STS = 1 << 2,
    GS_HST_STS   = 1 << 3,
    GS_HCYC_STS  = 1 << 4,
    GS_TO_STS    = 1 << 5,
    GS_CLEAR_STS = GS_ABRT_STS | GS_COL_STS | GS_PRERR_STS | GS_HCYC_STS | GS_TO_STS,
};
enum {  // GLOBAL_ENABLE bits
    GE_CYC_TYPE_MASK = 7,
    GE_HOST_STC      = 1 << 3,
    GE_HCYC_EN       = 1 << 4,  // raise the completion interrupt
    GE_ABORT         = 1 << 5,
};
enum {  // AMD756 protocol (cycle) types
    AMD756_QUICK     = 0x00,
    AMD756_BYTE      = 0x01,
    AMD756_BYTE_DATA = 0x02,
    AMD756_WORD_DATA = 0x03,
};

// --- XboxSmc (PIC16LC), 7-bit address 0x10 --------------------------------------------------
bool XboxSmc::read_byte(uint8_t cmd, uint8_t& out) {
    switch (cmd) {
        // PIC challenge request bytes — must be non-zero; fixed values (xemu smbus_xbox_smc.c).
        // The real handshake is a rolling code the SMC verifies; our SMC, like xemu, does not
        // verify the 2BL's response, so fixed non-zero values are functionally faithful for
        // boot. (Authentic challenge verification is a later faithfulness item, with MCPX.)
        case 0x1c: out = 0x52; return true;
        case 0x1d: out = 0x72; return true;
        case 0x1e: out = 0xea; return true;
        case 0x1f: out = 0x46; return true;
        case 0x01: out = version_[version_index_++ % 3]; return true;  // VER "P11" (cycles)
        case 0x03: out = tray_state_ ? tray_state_() : 0x00; return true;
        case 0x04: out = 0x06; return true;  // AVPACK = COMPOSITE (agent read from silicon)
        case 0x11: out = 0x00; return true;  // INTSTATUS: no SMC events pending at boot (clr-on-rd)
        // POWER_FAN_READBACK. Cxbx identifies the 0..50 unit range; the prior HLE witness
        // uses 50 as its stable automatic-mode reading. The live console probe is currently
        // offline, so retain that provenance explicitly instead of presenting it as silicon.
        case 0x10: out = fan_speed_; return true;
        case 0x1b: out = scratch_reg_; return true;  // SCRATCH
        case 0x0f: out = error_reg_; return true;     // ERROR_READ
        default: break;
    }
    char b[128];
    std::snprintf(b, sizeof(b), "SMC read of unmodeled register 0x%02x — characterize vs hardware",
                  cmd);
    if (fault_) fault_(b);
    out = 0;
    return true;
}

bool XboxSmc::read_word(uint8_t cmd, uint16_t& out) {
    char b[128];
    std::snprintf(b, sizeof(b), "SMC word-read of register 0x%02x — unmodeled", cmd);
    if (fault_) fault_(b);
    out = 0;
    return true;
}

bool XboxSmc::write_byte(uint8_t cmd, uint8_t val) {
    // Bring-up observability: SMC writes carry boot-state signals (LED override 0x07/0x08,
    // ERROR_WRITE 0x0e boot error codes, tray 0x0c, interrupt enable 0x1a). Few at boot.
    std::printf("[smc] write reg=0x%02x val=0x%02x\n", cmd, val);
    switch (cmd) {
        case 0x01: version_index_ = val; return true;   // VER: reset version-string read index
        case 0x0e: error_reg_ = val; return true;       // ERROR_WRITE
        case 0x06: fan_speed_ = val > 50 ? 50 : val; return true;  // requested fan speed
        case 0x07:
            if(val>1) {
                if(fault_)fault_("SMC LED mode outside automatic/programmed profile");
                return false;
            }
            led_.mode=val;led_.known|=1;return true;
        case 0x08: led_.sequence=val;led_.known|=2;return true;
        case 0x1b: scratch_reg_ = val; return true;     // SCRATCH
        case 0x0c:
            // Do not inherit the empty-drive bring-up's ignored motor command
            // when a mounted image makes tray transitions observable.
            if (tray_state_ && tray_state_() != 0 && fault_)
                fault_("SMC tray motor command with mounted optical media is unimplemented");
            return true;
        case 0x02:  // POWER: reset/cycle/shutdown request — surface it loudly
            if (val & 0xC1) {  // RESET(0x01)|CYCLE(0x40)|SHUTDOWN(0x80)
                char b[96];
                std::snprintf(b, sizeof(b), "SMC POWER write 0x%02x (reset/cycle/shutdown)", val);
                if (fault_) fault_(b);
            }
            return true;
        default:
            // Other SMC registers drive physical outputs (fan 0x05/0x06,
            // tray 0x0c, interrupt enable/ack 0x1a/0x0d, challenge response 0x20/0x21, ...) with
            // no guest-visible readback. Accept + ignore, as xemu does for unhandled writes.
            return true;
    }
}

bool XboxSmc::write_word(uint8_t cmd, uint16_t val) {
    char b[128];
    std::snprintf(b, sizeof(b), "SMC word-write 0x%04x to register 0x%02x — unmodeled", val, cmd);
    if (fault_) fault_(b);
    return true;
}

bool XboxSmc::receive_byte(uint8_t& out) {
    if (fault_) fault_("SMC receive-byte (no command) — unmodeled");
    out = 0;
    return true;
}

// --- XboxEeprom (config EEPROM), 7-bit address 0x54 ------------------------------------------
bool XboxEeprom::write_byte(uint8_t cmd, uint8_t val) {
    std::printf("[eeprom] write off=0x%02x val=0x%02x (in-memory only)\n", cmd, val);
    offset_ = cmd;
    uint16_t value=val;
    if(!access_data(offset_,1,true,value))return false;
    offset_ = (offset_ + 1) % data_size();
    return true;
}

bool XboxEeprom::write_word(uint8_t cmd, uint16_t val) {
    std::printf("[eeprom] write-word off=0x%02x val=0x%04x (in-memory only)\n", cmd, val);
    offset_ = cmd;
    if(!access_data(offset_,2,true,val))return false;
    offset_ = (offset_ + 2) % data_size();
    return true;
}

// --- Amd756Smbus host controller ------------------------------------------------------------
SmbusSlave* Amd756Smbus::find(uint8_t addr7) {
    for (auto& s : slaves_)
        if (s->address() == addr7) return s.get();
    return nullptr;
}

void Amd756Smbus::transaction() {
    const uint8_t prot = ctl_ & GE_CYC_TYPE_MASK;
    const bool read = addr_ & 0x01;
    const uint8_t addr7 = (addr_ >> 1) & 0x7f;
    SmbusSlave* s = find(addr7);
    if (!s) {  // absent slave: faithful bus error the firmware polls for (PRERR), not a fault
        static int absent_logged = 0;
        if (absent_logged < 32) {  // bring-up observability: who probes for what (EEPROM=0x54)
            std::printf("[smbus] absent slave addr7=0x%02x prot=0x%02x cmd=0x%02x %s\n", addr7,
                        prot, cmd_, read ? "rd" : "wr");
            if (++absent_logged == 32) std::printf("[smbus] (absent-slave log capped)\n");
        }
        stat_ |= GS_PRERR_STS;
        return;
    }

    bool okp = false;
    switch (prot) {
        case AMD756_QUICK:
            okp = s->quick(read);
            break;
        case AMD756_BYTE:
            if (read) { okp = s->receive_byte(data0_); }
            else { if (fault_) fault_("SMBus BYTE send-byte protocol — unmodeled"); okp = false; }
            break;
        case AMD756_BYTE_DATA:
            if (read) { okp = s->read_byte(cmd_, data0_); }
            else { okp = s->write_byte(cmd_, data0_); }
            break;
        case AMD756_WORD_DATA:
            if (read) {
                uint16_t w = 0;
                okp = s->read_word(cmd_, w);
                data0_ = static_cast<uint8_t>(w);
                data1_ = static_cast<uint8_t>(w >> 8);
            } else {
                okp = s->write_word(cmd_, static_cast<uint16_t>((data1_ << 8) | data0_));
            }
            break;
        default:
            if (fault_) {
                char b[64];
                std::snprintf(b, sizeof(b), "SMBus unsupported protocol 0x%02x", prot);
                fault_(b);
            }
            okp = false;
            break;
    }
    stat_ |= okp ? GS_HCYC_STS : GS_PRERR_STS;
}

// Level-triggered completion interrupt: asserted while interrupts are enabled and a completion
// or error status bit is set (xemu amd756: raise on GE_HCYC_EN + status after a transaction;
// the ISR clears the status, which deasserts). Idempotent — only toggles on a real change.
void Amd756Smbus::update_irq() {
    bool level = (ctl_ & GE_HCYC_EN) && (stat_ & GS_CLEAR_STS);
    if (irq_cb_ && level != irq_asserted_) {
        irq_asserted_ = level;
        irq_cb_(irq_num_, level);
    }
}

uint32_t Amd756Smbus::io_read(uint16_t port, int size) {
    uint32_t val = 0;
    for (int i = 0; i < size; ++i) {
        uint16_t off = static_cast<uint16_t>((port - base_) + i);
        uint8_t b = 0;
        switch (off) {
            case SMB_GLOBAL_STATUS: b = stat_; break;
            case SMB_GLOBAL_ENABLE: b = ctl_; break;
            case SMB_HOST_ADDRESS:  b = addr_; break;
            case SMB_HOST_DATA:     b = data0_; break;
            case SMB_HOST_DATA + 1: b = data1_; break;
            case SMB_HOST_COMMAND:  b = cmd_; break;
            default: b = 0; break;
        }
        val |= static_cast<uint32_t>(b) << (8 * i);
    }
    return val;
}

void Amd756Smbus::io_write(uint16_t port, int size, uint32_t value) {
    for (int i = 0; i < size; ++i) {
        uint16_t off = static_cast<uint16_t>((port - base_) + i);
        uint8_t val = static_cast<uint8_t>(value >> (8 * i));
        switch (off) {
            case SMB_GLOBAL_STATUS:
                // AMD-766 23167B p84: each status is hardware-set/W1C.
                // A zero write cannot manufacture a completed transaction,
                // and acknowledging one condition must retain the others.
                stat_ &= static_cast<uint8_t>(~(val & GS_CLEAR_STS));
                break;
            case SMB_GLOBAL_ENABLE:
                // HOSTST/ABORT are command strobes, not readable enables
                // (23167B pp85-86). Keep only the supported RW controls.
                ctl_ = val & (GE_CYC_TYPE_MASK | GE_HCYC_EN);
                if (val & GE_ABORT) stat_ |= GS_ABRT_STS;
                if (val & GE_HOST_STC) transaction();
                break;
            case SMB_HOST_ADDRESS: addr_ = val; break;
            case SMB_HOST_DATA:     data0_ = val; break;
            case SMB_HOST_DATA + 1: data1_ = val; break;
            case SMB_HOST_COMMAND:  cmd_ = val; break;
            default: break;
        }
    }
    update_irq();  // re-evaluate the completion IRQ after any status/enable change
}

}  // namespace xlle
