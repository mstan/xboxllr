#pragma once
#include <cstdint>

// One admission boundary for the native title and original-source observer.
// Startup evidence: native_audio_aci_startup_reference.py and probe
// docs/AC97-STARTUP.md and docs/SPDIF-STREAM.md. Digital RUN additionally
// requires the native owner's distinct consumer. Link reset stays guarded.
inline bool pilot_original_aci_admitted(uint32_t off, unsigned size, bool write, uint32_t value) {
    if (off < 0x100) {
        if (size != 2) return false;
        if (!write) return off == 0 || off == 2 || off == 0x18 || off == 0x26 ||
            off == 0x28 || off == 0x2a || off == 0x2c || off == 0x7c || off == 0x7e;
        return ((off == 2 || off == 0x26) && value == 0) ||
            (off == 0x18 && value == 0x0808) || (off == 0x2c && value == 48000);
    }
    if (off == 0x12c) return !write && size == 4;
    // Original17C8DB echoes only PCM/SPDIF interrupt summaries. Their real
    // owners derive these read-only bits again; this is not an IRQ clear.
    if (off == 0x130) return size == 4 && (!write || !(value & ~0x50u));
    if (off == 0x134) return !write && size == 1;
    if (off == 0x17c) return size == 4 && (!write || value == 0x02000000 || value == 0x02000002);
    if (off < 0x110 || off >= 0x180 || (off >= 0x120 && off < 0x170)) return false;
    const unsigned reg = off & 0xf;
    if (!write) {
        if (reg == 0) return size == 4;
        if (reg == 4 || reg == 5 || reg == 10 || reg == 11) return size == 1;
        return (reg == 6 && (size == 1 || size == 2)) || (reg == 8 && size == 2);
    }
    if (reg == 0) return size == 4 && !(value & 3);
    if (reg == 4) return size == 1 && value == 0;
    if (reg == 5) return size == 1 && value < 32;
    // Original ISR echoes the complete status, including read-only DCH/CELV.
    // The ACI owner ignores those bits and applies only the existing W1C mask.
    if (reg == 6) return (size == 1 || size == 2) && !(value & ~0x1fu);
    if (reg == 11) return size == 1 && !(value & ~0x1fu);
    return false;
}
