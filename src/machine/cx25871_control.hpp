// Bounded CX25871 control transport for original-kernel AV qualification.
// Register facts: Conexant 100381B, September 2001, sections 2.3/2.4,
// tables 2-1 and 2-5. Authored from the specification, no donor code bodies.
// This starts at an UNKNOWN warm-device state: a RAM-only kernel capture
// does not capture the encoder. Never infer its programmed state from zeros.
#pragma once
#include "machine/smbus.hpp"
#include "machine/cx25871_timing.hpp"
#include <array>
#include <cstdio>
#include <numeric>

namespace xlle {
class Cx25871Control final : public SmbusSlave {
public:
    struct Rate {
        uint64_t numerator=0, denominator=1;
        Rate scaled(uint64_t n,uint64_t d) const {
            Rate result{numerator*n,denominator*d};
            const auto factor=std::gcd(result.numerator,result.denominator);
            result.numerator/=factor;result.denominator/=factor;return result;
        }
    };
    struct Clocks {
        Rate encoder, input, clko;
        bool clko_driven=false;
    };
    enum class ClockState { Unknown, Undriven, Driven, Unsupported };
    // Received command bytes, NOT hardware register readback. The public
    // register map does not specify DA..FC effects. Keep their provenance
    // separate from the documented controls; it does not qualify their unknown
    // effects. Clocks/sync still follow their independently specified inputs.
    struct ExtensionCommands {
        std::array<uint8_t,18> bytes{};
        uint32_t received=0;
    };
    const ExtensionCommands& extension_commands() const { return extensions_; }
    // PAL is an explicit board strap, not inferred from the game's region.
    // Construction still represents unknown warm state, not a reset event.
    explicit Cx25871Control(FaultFn fault, bool pal_pin=false, bool slave_pin=false)
        : fault_(std::move(fault)), timing_(fault_), pal_pin_(pal_pin),slave_pin_(slave_pin) {}
    // Explicit board input. Zero means no known CLKI source, not a stopped
    // oscillator. No title rate is inferred from the game's intended cadence.
    void set_clki_hz(uint32_t hz) { clki_hz_=hz; }
    // Board scheduling may observe incomplete original programming without
    // faulting the guest. Only a consumer that needs an unavailable clock
    // calls clocks(). Neither query advances time or changes device state.
    ClockState observe_clocks(Clocks& output,const char*& reason) const {
        reason=nullptr;
        const auto unsupported=[&](const char* why) {
            reason=why;return ClockState::Unsupported;
        };
        // Software reset establishes the power/oscillator control defaults.
        // A RAM-only warm capture does not establish those pins/registers.
        if(!clock_defaults_known_ || known_[0x3a]!=0xff ||
           known_[0xa0]!=0xff || known_[0x9c]!=0xff || known_[0x9e]!=0xff ||
           known_[0x38]!=0xff || (known_[0xd4]&0xe0)!=0xe0 || !(known_[0xc4]&1)) {
            reason="CX25871 clock tree has unknown control state";
            return ClockState::Unknown;
        }
        // Conexant 100381B sections 1.3.6/1.3.6.1 and table 2-5. This
        // bounded profile uses the same 13.5-MHz crystal as autoconfig 0/1.
        // PLL lock time, sleep, external PLL reference and MODE2X remain open.
        if(registers_[0xd4]&0x80)
            return unsupported("CX25871 MODE2X input clock remains unqualified");
        const bool bypass=registers_[0xa0]&0x40;
        const bool external=!bypass && (registers_[0xa0]&0x80);
        const bool async=registers_[0xd4]&0x20;
        if(bypass && (registers_[0xa0]&0x80))
            return unsupported("CX25871 combined PLL bypass/external pin gating remains unqualified");
        if((external || async) && !clki_hz_)
            return unsupported("CX25871 clock tree requires a real CLKI source");
        Rate source{13500000,1};
        if(external) source={clki_hz_,1};
        else if(!bypass) {
            const uint32_t multiplier=((registers_[0xa0]&0x3fu)<<16) |
                (uint32_t(registers_[0x9e])<<8) | registers_[0x9c];
            if(!multiplier)return unsupported("CX25871 zero PLL multiplier is unqualified");
            source=source.scaled(multiplier,65536u*((registers_[0x3a]&4)?10u:6u));
        }
        Clocks result;
        result.encoder=source;
        result.input=async ? Rate{clki_hz_,1} : source;
        if(registers_[0x38]&0x20) {
            if(bypass || external)
                return unsupported("CX25871 3:2 clocking requires the qualified internal PLL");
            result.encoder=source.scaled(2,3);
        }
        if(registers_[0x38]&0x10)result.input=result.input.scaled(1,2);
        result.clko=source.scaled(1,(registers_[0x3a]&8)?2:1);
        // EN_XCLK three-states CLKO even with EN_OUT set. Combined bypass
        // and external-clock pin gating is deliberately not inferred here.
        result.clko_driven=(registers_[0xc4]&1) && !(registers_[0xa0]&0x80);
        output=result;return result.clko_driven?ClockState::Driven:ClockState::Undriven;
    }
    bool clocks(Clocks& output) {
        const char* reason=nullptr;
        const auto state=observe_clocks(output,reason);
        if(state==ClockState::Unknown || state==ClockState::Unsupported)return reject(reason);
        return true;
    }
    const char* name() const override { return "cx25871-control"; }
    uint8_t address() const override { return 0x45; }
    bool read_byte(uint8_t reg, uint8_t& value) override {
        if (!(known_[0x6c] & 0x40) || !(registers_[0x6c] & 0x40))
            return reject("CX25871 standard readback requires established EN_REG_RD state");
        if (!supported(reg) || known_[reg] != 0xff)
            return reject("CX25871 read of uncaptured/unprogrammed control register");
        value = registers_[reg]; return true;
    }
    bool write_byte(uint8_t reg, uint8_t value) override {
        // 100381B sections 2.4 and 3.8.1: 00..06 are read-only, while
        // received data bytes are acknowledged. An attempted write cannot
        // establish an ID/status value or turn an unknown read into a latch.
        if(reg<=0x06 && !(reg&1))return true;
        // The same serial ACK rule applies to the original driver's DA..FC
        // commands. Accept transport and retain its bytes without inventing
        // register readback, clock, ready or activation effects. The documented
        // clock/reset/sync contracts remain independent; interpreting extensions
        // is deferred, with connected output qualification still required.
        if(reg>=0xda && reg<=0xfc && !(reg&1)) {
            const unsigned index=(reg-0xda)/2;
            extensions_.bytes[index]=value;
            extensions_.received|=uint32_t(1)<<index;
            programmed_=true;
            return true;
        }
        // 100381B table2-1 marks72 reserved; table2-5 mandates zero for
        // normal operation and the programming examples explicitly write00.
        // This establishes no readback latch, reset, clock or timing event.
        if(reg==0x72 && value==0)return true;
        if (!supported(reg)) {
            char why[96];
            std::snprintf(why,sizeof(why),
                "CX25871 control register outside qualified transport: write %02x=%02x",reg,value);
            return reject(why);
        }
        // Documented ordinary latches used by the original AV setup. Their
        // readback is independent of the still-guarded analog pixel path.
        // Commands that change unmodelled power/monitor/output modes stop
        // before changing any latch or the established timing state.
        if (reg == 0x2e && value)
            return reject("CX25871 HDTV/raster output controls remain unqualified");
        if (reg == 0x30 && value)
            return reject("CX25871 sleep/oscillator power commands remain unqualified");
        if (reg == 0x32 && (value & 0x80))
            return reject("CX25871 automatic monitor-check command remains unqualified");
        if (reg == 0x60 && (value & 0xf0))
            return reject("CX25871 WSS enable/reserved controls remain unqualified");
        if (reg == 0x6a && (value & 0xf0))
            return reject("CX25871 reserved WSS increment bits");
        if (reg == 0x70 && (value & 0x3f) < 2)
            return reject("CX25871 disallowed master HSYNC width");
        if (reg == 0x74 && (value & 0x38))
            return reject("CX25871 reserved digital VSYNC controls");
        if (reg == 0x74 && !(value & 7))
            return reject("CX25871 disallowed master VSYNC width");
        if ((reg == 0xca || reg == 0xcc) && (value & 0x38) == 0x38)
            return reject("CX25871 reserved coring selection");
        if (reg == 0xd6 && (value & 0x4c))
            return reject("CX25871 alternate output/embedded-sync profile remains unqualified");
        if (reg == 0xba && (value & 0x80)) {
            // Only the original driver's isolated reset strobe is qualified.
            if (value != 0x80) return reject("CX25871 combined reset/control write is unqualified");
            software_reset(); programmed_ = true; return true;
        }
        if (reg == 0xba && (value & 0x40))
            return reject("CX25871 monitor-check command remains unqualified");
        if (reg == 0xb8) {
            if (value > 1 || known_[0x3a] != 0xff || registers_[0x3a] != 0)
                return reject("CX25871 autoconfiguration mode/clock remains unqualified");
            autoconfigure(value); programmed_ = true; return true;
        }
        if (reg == 0x3a && (value & ~0x0c))
            return reject("CX25871 alternate crystal/clock configuration remains unqualified");
        // Table 2-5 (2-16): ESTATUS[1:0] are writable legacy readback
        // selectors, not status returned in this register. Standard readback
        // exposes their latch. ECCGATE may stay set when both caption fields
        // are disabled (the original AV disable option clears only 0x30).
        // Neither selector nor an inactive caption gate changes timing.
        if (reg == 0xc4 && (value & 0x36))
            return reject("CX25871 chroma/color-bar/enabled-caption controls remain unqualified");
        if (reg == 0xc6 && value!=0x9c && value!=0xbc)
            return reject("CX25871 digital interface control outside qualified timing profile");
        // 6C=46 selects standard readback; C6 additionally strobes timing reset.
        // Color bars, SCART and other field-detection modes remain unqualified.
        if (reg == 0x6c && value != 0x46 && value != 0xc6)
            return reject("CX25871 timing/reset/output control outside qualified transport");
        if (reg == 0x34 && (value & 0x64))
            return reject("CX25871 reserved adaptive-filter control bits");
        if ((reg == 0x38 && (value & 0x80)) || (reg == 0x8e && (value & 0xe0)))
            return reject("CX25871 reserved timing control bits");
        if(reg==0x6c && value==0xc6) {
            if(!reset_timing())return false;
            value=0x46; // strobe self-clears only after real counter/phase reset
        } else if(timing_.known() && reg!=0xc4 && reg!=0xc6 && reg!=0x6c &&
                  reg!=0x34 && reg!=0x36 && reg!=0xc8 &&
                  registers_[reg]!=value) {
            // Timing changes need the documented reset before this bounded
            // route can expose output again; no old phase is silently reused.
            timing_.invalidate();
        }
        registers_[reg] = value; known_[reg] = 0xff; programmed_ = true;
        // Conexant table 2-5: DIV2 is one latch exposed through two registers.
        if (reg == 0x38) mirror_div2(value & 0x10);
        if (reg == 0xd4) {
            registers_[0x38] = (registers_[0x38] & ~0x10) | ((value & 0x40) >> 2);
            known_[0x38] |= 0x10;
        }
        return true;
    }
    bool read_word(uint8_t, uint16_t&) override { return reject("CX25871 word read outside qualified transport"); }
    bool write_word(uint8_t, uint16_t) override { return reject("CX25871 word write outside qualified transport"); }
    bool receive_byte(uint8_t&) override { return reject("CX25871 legacy/stream read outside qualified transport"); }
    bool quick(bool) override { return reject("CX25871 quick command outside qualified transport"); }
    bool programmed() const { return programmed_; }
    bool timing_known() const { return timing_.known(); }
    bool timing_selected() const { return timing_selected_; }
    uint64_t timing_epoch() const { return timing_epoch_; }
    // The admitted C6 values both select positive HSYNC/VSYNC (bits3/4).
    bool positive_sync_input() const { return known_[0xc6]==0xff && (registers_[0xc6]&0x18)==0x18; }
    bool advance_timing(uint64_t clocks) { return timing_.advance(clocks); }
    bool input_sync(bool vertical) { return timing_.sync(vertical); }
    bool input_vsync() { return timing_.vertical_sync(); }
    bool timing_state(Cx25871Timing::State& state) const { return timing_.state(state); }
    // -1 unknown; -2 high impedance; otherwise physical FIELD low/high.
    int field_signal() const {
        if(!(known_[0xc4]&1))return -1;
        if(!(registers_[0xc4]&1))return -2;
        Cx25871Timing::State state;
        if(!(known_[0xc6]&0x20) || !timing_.state(state))return -1;
        return int(state.field_even ^ bool(registers_[0xc6]&0x20));
    }
private:
    bool reset_timing() {
        Clocks rates;
        if(!clocks(rates))return false;
        for(uint8_t reg:{0x76,0x86,0x88,0x8a,0x8e,0x90,0x96,0xa2,0xae,0xb0,0xb2,0xb4})
            if(known_[reg]!=0xff)return reject("CX25871 timing reset has unknown counters");
        if(!(known_[0xba]&0x20) || !(known_[0xc6]&0x20))
            return reject("CX25871 timing reset has unknown interface");
        if((registers_[0x38]&0x30) || (registers_[0xd4]&0xe0) || registers_[0x3a] ||
           (registers_[0xa0]&0xc0) || registers_[0x88] || (registers_[0xa2]&0x81))
            return reject("CX25871 timing reset outside synchronous interlaced-output profile");
        Cx25871Timing::Config config;
        config.input_clocks=registers_[0x8a] | ((registers_[0x8e]&7u)<<8);
        config.input_lines=registers_[0x90] | ((registers_[0x96]&3u)<<8) | ((registers_[0x38]&2u)<<9);
        config.output_clocks=registers_[0x76] | ((registers_[0x86]&15u)<<8);
        config.output_lines=(registers_[0xa2]&4)?625:525;
        config.subcarrier_increment=uint32_t(registers_[0xae]) | (uint32_t(registers_[0xb0])<<8) |
            (uint32_t(registers_[0xb2])<<16) | (uint32_t(registers_[0xb4])<<24);
        config.pal=registers_[0xa2]&0x20;config.disable_subcarrier_reset=registers_[0xa2]&0x10;
        config.slave=slave_pin_ || (registers_[0xba]&0x20);
        // Table 2-5: signed HSYNOFFSET moves the master's digital HSYNC
        // edge. Section 1.3.31 derives FIELD from that edge, so the existing
        // zero-offset model cannot expose its phase for a shifted master.
        // Software reset establishes offset zero; subsequent writes are
        // exact latches. The offset has no effect in slave interface.
        if(!config.slave && ((known_[0x6e]==0xff && registers_[0x6e]) ||
                            (known_[0x70]==0xff && (registers_[0x70]&0xc0))))
            return reject("CX25871 nonzero master HSYNC offset phase remains unqualified");
        if(!timing_.reset(config))return false;
        timing_selected_=true;++timing_epoch_;return true;
    }
    static bool supported(uint8_t reg) {
        // Conexant 100381B table 2-1 and programming detail table 2-5:
        // disabled HDTV/power (2E/30), drive/input (32), color compensation
        // (3C/3E/40), luma/hue (5A/5C), disabled WSS/data/increment (60..6A),
        // master sync/data delay (6E/70/74), subcarrier phase offset (B6), coring/attenuation
        // (CA/CC), DAC mux (CE), output/blanking (D6) and chroma/bypass (D8).
        // Reserved72 has only its specified zero-write policy above; readback
        // and undocumented extension DA..FC readbacks remain unsupported.
        const bool standard = reg == 0x2e || reg == 0x30 || reg == 0x32 ||
            reg == 0x3c || reg == 0x3e || reg == 0x40 || reg == 0x5a || reg == 0x5c ||
            (reg >= 0x60 && reg <= 0x6a && !(reg & 1)) ||
            reg == 0x6e || reg == 0x70 || reg == 0x74 || reg == 0xb6 ||
            reg == 0xca || reg == 0xcc || reg == 0xce || reg == 0xd6 || reg == 0xd8;
        return standard || reg == 0x34 || reg == 0x36 || reg == 0x38 || reg == 0x3a || reg == 0x6c ||
               (reg >= 0x76 && reg <= 0xb4 && !(reg & 1)) ||
               reg == 0xb8 || reg == 0xba || reg == 0xc4 || reg == 0xc6 || reg == 0xc8 || reg == 0xd4;
    }
    void mirror_div2(bool enabled) {
        registers_[0xd4] = (registers_[0xd4] & ~0x40) | (enabled ? 0x40 : 0);
        known_[0xd4] |= 0x40;
    }
    void autoconfigure(unsigned mode) {
        timing_.invalidate();
        // Factual ROM values from Conexant 100381B table C-1, modes 0/1,
        // 13.500 MHz crystal. Register 38 and even registers 76..B4 only.
        // All other control latches survive this command, including readback,
        // DAC/slave controls and the unrelated fields of DIV2's second view.
        static constexpr uint8_t timing[2][32] = {
            {0x00,0x80,0x84,0x96,0x60,0x7d,0x22,0xd4,0x27,0x00,0x10,0x7e,0x03,0x58,0x4b,0xe0,
             0x36,0x92,0x54,0x0e,0x88,0x0c,0x0a,0xe5,0x76,0x79,0x44,0x85,0x00,0x00,0x80,0x20},
            {0x60,0x80,0x8a,0xa6,0x68,0xc1,0x2e,0xf2,0x27,0x00,0xb0,0x0a,0x0b,0x71,0x5a,0xe0,
             0x36,0x00,0x50,0x72,0x1c,0x0d,0x24,0xf0,0x58,0x81,0x49,0x8c,0x0c,0x8c,0x79,0x26}
        };
        for (unsigned i=0;i<32;++i) {
            registers_[0x76+2*i] = timing[mode][i]; known_[0x76+2*i] = 0xff;
        }
        registers_[0x38] = 0; known_[0x38] = 0xff; mirror_div2(false);
        registers_[0xb8] = uint8_t(mode); known_[0xb8] = 0xff;
    }
    void software_reset() {
        // Invalidating all old knowledge is essential: a warm-state byte
        // cannot survive reset merely because its default is unqualified.
        registers_.fill(0); known_.fill(0);
        // Section 1.3.4 resets all serial-interface registers. The receipt is
        // scoped to commands since that reset; no extension default is exposed.
        extensions_={};
        for (uint8_t reg : {0x34,0x36,0x3a,0xc8}) known_[reg] = 0xff;
        // Table 2-5 says EACTIVE defaults to 1; section 2.1 says to enable it.
        // Likewise CHECK_STAT's auto-clear description conflicts with its
        // manual-clear description. Neither disputed bit is manufactured.
        registers_[0x6c] = 2; known_[0x6c] = 0xfb;
        known_[0xba] = 0xbf; // SRESET itself is documented self-clearing.
        known_[0xc4] = 1; // EN_OUT=0: digital timing pins are three-stated.
        known_[0xc6] = 0x20; // FIELDI=0; other interface fields stay unknown.
        known_[0xd4] = 0xe0; // MODE2X=EN_ASYNC=DIV2=0; caption fields unknown.
        clock_defaults_known_=true;
        autoconfigure(pal_pin_ ? 1 : 0);
    }
    bool reject(const char* reason) { if (fault_) fault_(reason); return false; }
    FaultFn fault_;
    Cx25871Timing timing_;
    std::array<uint8_t, 256> registers_{};
    std::array<uint8_t, 256> known_{};
    ExtensionCommands extensions_{};
    bool pal_pin_ = false;
    bool slave_pin_ = false;
    bool programmed_ = false;
    bool clock_defaults_known_=false;
    bool timing_selected_=false;
    uint64_t timing_epoch_=0;
    uint32_t clki_hz_=0;
};
} // namespace xlle
