// xlle — NV2A (Xbox GPU) MMIO register shell (A2c minimal shell; full engine is A4).
//
// The NV2A MMIO aperture is 0xFD000000..0xFDFFFFFF (16 MB), divided into engine blocks (PMC,
// PBUS, PFIFO, PTIMER, PFB, PSTRAPS, PGRAPH, PCRTC, PRAMDAC, ...). Block map + register
// semantics harvested from xemu/hw/xbox/nv2a/*.
//
// Value policy (a deliberate, documented deviation from blanket fault-stop, justified by the
// oracle being offline and xemu being a valid boot witness):
//   * Registers the boot reads are modelled with faithful values — from real-hardware capture
//     (machine/probes) where available, else xemu.
//   * Reads of an unmodelled register *inside a real block* return xemu's documented default
//     (0) and are LOGGED as unmodelled, not silently absorbed. xemu boots the same retail 2BL
//     with these defaults, so they are boot-correct witnesses; each is a debt to replace with
//     hardware truth via the oracle.
//   * A read/write to an aperture HOLE (no block) fault-stops loudly (PRINCIPLES §3).
//   * Writes to unmodelled registers are accepted+logged (a write carries no return value to
//     get wrong; xemu likewise ignores unknown writes).
#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "machine/io_device.hpp"   // FaultFn
#include "machine/mmio_device.hpp"

namespace xlle {

class Nv2a : public MmioDevice {
public:
    static constexpr uint32_t kBase = 0xFD000000u;
    static constexpr uint32_t kSize = 0x01000000u;  // 16 MB

    explicit Nv2a(FaultFn fault) : fault_(std::move(fault)) {}

    const char* name() const override { return "nv2a"; }
    uint32_t mmio_base() const override { return kBase; }
    uint32_t mmio_size() const override { return kSize; }
    uint32_t mmio_read(uint32_t addr, int size) override;
    void mmio_write(uint32_t addr, int size, uint32_t value) override;

    // One engine block within the aperture (offset/size relative to kBase).
    struct Block { uint32_t off, size; const char* name; };

    // Observability: called for every access that hit an unmodelled register (returned the
    // xemu default). `write` mirrors MemEvent semantics. Lets a run summarize its NV2A debt.
    void on_unmodeled(std::function<void(bool write, uint32_t addr, uint32_t rel,
                                         const char* block, uint32_t value)> cb) {
        unmodeled_cb_ = std::move(cb);
    }

private:
    // Resolve an absolute NV2A address to its block; returns nullptr (a hole) if unmapped.
    static const Block* find_block(uint32_t rel_addr, uint32_t& block_rel);

    FaultFn fault_;
    std::function<void(bool, uint32_t, uint32_t, const char*, uint32_t)> unmodeled_cb_;
};

}  // namespace xlle
