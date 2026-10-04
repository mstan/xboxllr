// xlle — Xbox IDE/ATA controller (A4): primary channel at 0x1F0-0x1F7/0x3F6 + bus-master DMA
// at 0xFF60 (silicon: 00:09.0 BAR4 = 0xFF61, captured from the live console 2026-07-16).
//
// Register semantics and command state machine harvested faithfully from xemu/hw/ide/
// {core.c,atapi.c,pci.c} (which carries the Xbox-specific ATA SECURITY UNLOCK the retail
// kernel requires — an unlocked drive is fatal error 05 on real hardware). Drive 0 is the
// FATX HDD backed by an image file; drive 1 is the ATAPI DVD-ROM with optional
// read-only optical media (a missing DVD drive is fatal error 11).
//
// Completion interrupts are level-triggered on IRQ 14 (silicon: LPC cfg 0x6C = 0x0E065491,
// dev-9 nibble = 0xE = 14), same delivery shape as the SMBus IRQ-11 completion.
//
// Doctrine (PRINCIPLES §3): any ATA command / ATAPI packet opcode / register the boot
// exercises that we do not model fault-stops loudly with full context. Bus-level errors the
// firmware polls for (no media, aborted command) are faithful device states, not faults.
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "machine/io_device.hpp"

namespace xlle {

// Provisioned virtual-drive state, independent of any guest command payload.
// The 64-byte private file contains user[32] followed by master[32].
struct HddSecurityConfig {
    enum class Mode { Unprovisioned, Locked, Disabled } mode = Mode::Unprovisioned;
    std::array<uint8_t, 32> user{}, master{};
    bool maximum = false;
};

// One drive on the channel. Field names follow xemu's IDEState so the harvest stays legible.
struct IdeDrive {
    enum Kind { kHd, kCd } kind = kHd;

    // Task-file registers.
    uint8_t feature = 0, error = 0;
    uint32_t nsector = 0;  // sector count; also ATAPI interrupt-reason bits (CD/IO)
    uint8_t sector = 0, lcyl = 0, hcyl = 0;
    uint8_t hob_feature = 0, hob_nsector = 0, hob_sector = 0, hob_lcyl = 0, hob_hcyl = 0;
    uint8_t select = 0xA0;  // ATA_DEV_ALWAYS_ON
    uint8_t status = 0;

    // Geometry / identity.
    uint64_t nb_sectors = 0;
    int cylinders = 0, heads = 0, sectors = 0;      // active translation
    int drive_heads = 0, drive_sectors = 0;         // native (for READ NATIVE MAX)
    int mult_sectors = 16;                          // MAX_MULT_SECTORS
    bool lba48 = false;
    bool locked = false;        // ATA security state (retail kernel requires locked at boot)
    bool security_frozen = false;
    uint8_t security_attempts = 5;
    bool identify_set = false;
    uint8_t identify_data[512] = {0};

    // PIO transfer state (xemu: data_ptr/data_end/end_transfer_func).
    enum class End { None, TransferStop, SectorRead, SectorWrite, AtapiCmd, AtapiReplyStop,
                     SecurityCmd };
    std::vector<uint8_t> io_buffer;
    size_t data_ptr = 0, data_end = 0;
    End end_transfer = End::None;
    int req_nb_sectors = 0;

    // DMA state: command armed on the drive, executed when the bus master starts.
    enum class DmaCmd { None, Read, Write, Atapi };
    DmaCmd dma_cmd = DmaCmd::None;

    // ATAPI state.
    uint8_t sense_key = 0, asc = 0;
    bool atapi_dma = false;
    int packet_reply_size = 0;  // pending reply bytes in io_buffer (for DMA replies)
    size_t packet_reply_offset = 0;
    uint16_t packet_byte_limit = 0;
    bool tray_open = false, tray_locked = false;

    // Owned backing file: writable only under the explicit HDD writeback policy.
    std::FILE* blk = nullptr;

    bool present() const { return kind == Kind::kCd || blk != nullptr; }
};

// The controller: primary-channel task file + control port + bus-master DMA ports.
// The secondary channel does not exist on the Xbox (single channel: HDD master, DVD slave).
class IdeController : public IoPortDevice {
public:
    IdeController(FaultFn fault) : fault_(std::move(fault)) {
        drives_[0].kind = IdeDrive::kHd;
        drives_[1].kind = IdeDrive::kCd;
        for (auto& d : drives_) d.io_buffer.resize(kIoBufferSize);
        reset();
    }
    ~IdeController() override;

    // Configure before attaching. Locked requires independently provisioned
    // user/master credentials; Disabled is an explicit unlocked-drive policy.
    // Unprovisioned retains the loud stop on authentication, never accept-all.
    bool configure_hdd_security(const HddSecurityConfig& config);
    uint16_t hdd_security_status() const;
    // Models ATA RESET- / power-on security transitions, not task-file SRST.
    void hardware_reset();

    // Attach the HDD image, applying the configured power-on security state.
    //
    // WRITE POLICY (beads-xbox.1.14.11). By default the image is opened READ-ONLY and guest
    // writes land in an in-memory copy-on-write overlay keyed by LBA. Within a run the guest
    // sees exactly what it wrote — fidelity is unchanged — but the file on disk is never
    // modified, so run N+1 starts from the same bytes as run N.
    //
    // This is not a nicety. The retail boot path reads LBA 3 (the "BRFR" refurb-info sector),
    // increments the boot counter in it and writes it straight back, within the first 18 M
    // instructions of EVERY run. With writeback on, the canonical 12 B replay therefore
    // MUTATES ITS OWN INPUT and no two runs start from the same disk: that is the mechanism
    // behind the two guest trajectories recorded in docs/PERF-METHODOLOGY.md section 3b.
    // Pass writeback=true only when persisting guest disk state is the point of the run.
    bool attach_hdd(const std::string& path, bool writeback = false);
    // Raw 2048-byte-sector optical image, opened read-only without rebasing or
    // manufacturing authentication metadata. Attach before guest execution.
    bool attach_dvd(const std::string& path);
    bool dvd_attached() const { return drives_[1].blk != nullptr; }
    bool dvd_media_present() const { return dvd_attached() && !drives_[1].tray_open; }
    uint8_t dvd_tray_state() const {
        return drives_[1].tray_open ? 0x10 : dvd_media_present() ? 0x60 : 0x00;
    }
    uint64_t dvd_sectors_read() const { return dvd_sectors_read_; }
    void set_dvd_trace(bool enable) { dvd_trace_ = enable; }
    // Sectors currently held by the overlay, and the cap that fault-stops the machine rather
    // than silently dropping a guest write.
    uint64_t overlay_sectors() const { return overlay_.size(); }
    bool hdd_writeback() const { return hdd_writeback_; }
    static constexpr size_t kMaxOverlaySectors = 512u * 1024u;  // 256 MiB of guest writes

    // Level-triggered completion interrupt to the PIC (IRQ 14).
    void set_irq(std::function<void(int irq, bool level)> cb, int irq_num) {
        irq_cb_ = std::move(cb);
        irq_num_ = irq_num;
    }
    // Guest physical memory access for bus-master DMA (PRD table + data).
    void set_dma_mem(std::function<bool(uint32_t, void*, size_t)> rd,
                     std::function<bool(uint32_t, const void*, size_t)> wr) {
        mem_read_ = std::move(rd);
        mem_write_ = std::move(wr);
    }
    // Bus-master base follows PCI 00:09.0 BAR4 (silicon preset 0xFF60).
    void set_bm_base(uint16_t base) { bm_base_ = base; }
    uint16_t bm_base() const { return bm_base_; }

    const char* name() const override { return "ide"; }
    uint16_t io_base() const override { return 0x1F0; }
    uint16_t io_size() const override { return 8; }
    bool owns(uint16_t port) const override {
        if (port >= 0x1F0 && port <= 0x1F7) return true;
        if (port == 0x3F6) return true;
        if (bm_base_ && port >= bm_base_ && port < static_cast<uint32_t>(bm_base_) + 16)
            return true;
        return false;
    }
    uint32_t io_read(uint16_t port, int size) override;
    void io_write(uint16_t port, int size, uint32_t value) override;

    // Observability.
    uint64_t irq_pulses() const { return irq_pulses_; }
    uint64_t sectors_read() const { return sectors_read_; }
    uint64_t sectors_written() const { return sectors_written_; }

private:
    static constexpr size_t kIoBufferSize = 256 * 512 + 4;  // max transfer (nsector==0 -> 256)

    IdeDrive& active() { return drives_[unit_]; }
    void reset();
    void reset_drive(IdeDrive& d);
    void set_signature(IdeDrive& d);

    // Task-file / control / data ports (xemu core.c).
    uint32_t ioport_read(uint32_t reg);
    void ioport_write(uint32_t reg, uint32_t val);
    uint32_t data_read(int size);
    void data_write(uint32_t val, int size);
    void ctrl_write(uint32_t val);

    // Command execution (xemu ide_bus_exec_cmd + cmd_* handlers).
    void exec_cmd(uint8_t cmd);
    void abort_command(IdeDrive& d);
    void transfer_start(IdeDrive& d, size_t size, IdeDrive::End end);
    void transfer_stop(IdeDrive& d);
    void end_transfer(IdeDrive& d);          // buffer exhausted -> run end action
    int64_t get_sector(const IdeDrive& d) const;
    void set_sector(IdeDrive& d, int64_t sector_num);
    void cmd_lba48_transform(IdeDrive& d, bool lba48);

    void build_identify(IdeDrive& d);        // ATA IDENTIFY (word 128 = security state)
    void build_identify_packet(IdeDrive& d); // ATAPI IDENTIFY PACKET DEVICE
    void sector_read(IdeDrive& d);           // PIO read chunk
    void sector_write_done(IdeDrive& d);     // PIO write chunk landed

    // ATAPI transport from xemu atapi.c, including real mounted-media reads.
    void atapi_cmd(IdeDrive& d);
    void atapi_reply(IdeDrive& d, int size, int max_size);
    void atapi_pio_next(IdeDrive& d);
    void atapi_read(IdeDrive& d, uint32_t lba, uint32_t count);
    void atapi_cmd_ok(IdeDrive& d);
    void atapi_cmd_error(IdeDrive& d, int sense_key, int asc);

    // Bus-master DMA (xemu pci.c BMDMA). Synchronous: runs the whole PRD walk when both the
    // drive command and the start bit are present.
    void bm_write(uint32_t off, uint8_t val);
    uint8_t bm_read(uint32_t off);
    void bm_maybe_run();
    bool disk_rw(IdeDrive& d, uint64_t sector, uint32_t count, bool write, uint8_t* buf);

    void set_irq_level(bool level);   // level to the PIC, gated by nIEN
    void raise_irq();                 // event: also latches BM_STATUS_INT
    void lower_irq();

    FaultFn fault_;
    uint64_t dvd_sectors_read_ = 0;
    bool dvd_trace_ = false;
    IdeDrive drives_[2];
    int unit_ = 0;
    uint8_t bus_cmd_ = 0;  // device-control register (nIEN, SRST, HOB)

    std::function<void(int, bool)> irq_cb_;
    int irq_num_ = 14;
    bool irq_asserted_ = false;

    std::function<bool(uint32_t, void*, size_t)> mem_read_;
    std::function<bool(uint32_t, const void*, size_t)> mem_write_;

    // Bus-master registers (primary channel; secondary is inert open space).
    uint16_t bm_base_ = 0xFF60;
    uint8_t bm_cmd_ = 0, bm_status_ = 0;
    uint32_t bm_addr_ = 0;

    uint64_t irq_pulses_ = 0, sectors_read_ = 0, sectors_written_ = 0;
    // Copy-on-write overlay for drive 0: LBA -> the 512 bytes the guest last wrote there.
    // Integer key, point lookups only, so iteration order never reaches the guest.
    std::unordered_map<uint64_t, std::array<uint8_t, 512>> overlay_;
    bool hdd_writeback_ = false;
    HddSecurityConfig hdd_security_;
};

}  // namespace xlle
