// xlle — Xbox IDE/ATA controller. Harvested from xemu/hw/ide/{core.c,atapi.c,pci.c}; see
// ide.hpp for the fidelity notes and silicon captures.
#include "machine/ide.hpp"

#include <array>
#include <cstring>

namespace xlle {

// --- Status / error / control bits (xemu ide-internal.h) --------------------------------------
enum {
    ERR_STAT  = 0x01,
    DRQ_STAT  = 0x08,
    SEEK_STAT = 0x10,
    READY_STAT = 0x40,
    BUSY_STAT = 0x80,
};
enum {
    ABRT_ERR = 0x04,
    MC_ERR   = 0x20,
};
enum {
    ATA_DEV_SELECT    = 0x10,
    ATA_DEV_ALWAYS_ON = 0xA0,
    ATA_DEV_LBA       = 0x40,
    ATA_DEV_LBA_MSB   = 0x0F,
    ATA_DEV_HS        = 0x0F,
};
enum {
    IDE_CTRL_HOB         = 0x80,
    IDE_CTRL_RESET       = 0x04,
    IDE_CTRL_DISABLE_IRQ = 0x02,
};
enum {  // ATAPI interrupt reason (in nsector)
    ATAPI_INT_REASON_CD = 0x01,
    ATAPI_INT_REASON_IO = 0x02,
};
enum {  // bus-master DMA (xemu pci.c)
    BM_CMD_START = 0x01,
    BM_STATUS_DMAING = 0x01,
    BM_STATUS_ERROR  = 0x02,
    BM_STATUS_INT    = 0x04,
};
enum {  // SCSI sense
    NO_SENSE = 0, NOT_READY = 2, ILLEGAL_REQUEST = 5, UNIT_ATTENTION = 6,
};
enum {
    ASC_ILLEGAL_OPCODE = 0x20,
    ASC_INV_FIELD_IN_CMD_PACKET = 0x24,
    ASC_MEDIUM_NOT_PRESENT = 0x3a,
};
constexpr int kMaxMultSectors = 16;

namespace {
void put_le16(uint8_t* p, int word, uint16_t v) {
    p[word * 2] = static_cast<uint8_t>(v);
    p[word * 2 + 1] = static_cast<uint8_t>(v >> 8);
}
// ATA string: space-padded, bytes swapped within each word (xemu padstr).
void padstr(uint8_t* p, const char* s, int len) {
    for (int i = 0; i < len; ++i) {
        uint8_t c = *s ? static_cast<uint8_t>(*s++) : ' ';
        p[i ^ 1] = c;
    }
}
// SCSI string: plain space-padded (xemu padstr8).
void padstr8(uint8_t* p, int len, const char* s) {
    for (int i = 0; i < len; ++i) p[i] = *s ? static_cast<uint8_t>(*s++) : ' ';
}
}  // namespace

IdeController::~IdeController() {
    if (drives_[0].blk) std::fclose(drives_[0].blk);
}

bool IdeController::attach_hdd(const std::string& path, bool writeback) {
    IdeDrive& d = drives_[0];
    hdd_writeback_ = writeback;
    overlay_.clear();
    // Read-only unless writeback was explicitly asked for: see the write-policy note in
    // ide.hpp. Opening "rb" is itself part of the guarantee — nothing can modify the image
    // even by accident.
    d.blk = std::fopen(path.c_str(), writeback ? "r+b" : "rb");
    if (!d.blk) return false;
#ifdef _WIN32
    _fseeki64(d.blk, 0, SEEK_END);
    int64_t size = _ftelli64(d.blk);
#else
    std::fseek(d.blk, 0, SEEK_END);
    int64_t size = std::ftell(d.blk);
#endif
    if (size <= 0) { std::fclose(d.blk); d.blk = nullptr; return false; }
    d.nb_sectors = static_cast<uint64_t>(size) / 512;
    // Geometry as qemu derives it: 16 heads x 63 sectors, cylinders capped at 16383.
    d.heads = d.drive_heads = 16;
    d.sectors = d.drive_sectors = 63;
    int64_t cyl = size / 512 / (16 * 63);
    d.cylinders = static_cast<int>(cyl > 16383 ? 16383 : cyl);
    // The retail kernel requires the drive LOCKED at boot (error 05 otherwise); it then sends
    // SECURITY UNLOCK with its EEPROM-derived key, which we accept (xemu ide_security_cmd).
    d.locked = true;
    d.identify_set = false;
    return true;
}

void IdeController::reset() {
    unit_ = 0;
    bus_cmd_ = 0;
    for (auto& d : drives_) reset_drive(d);
    bm_cmd_ = 0;
    bm_status_ = 0;
    bm_addr_ = 0;
}

void IdeController::reset_drive(IdeDrive& d) {
    d.mult_sectors = kMaxMultSectors;
    d.feature = d.error = 0;
    d.nsector = 0;
    d.sector = d.lcyl = d.hcyl = 0;
    d.hob_feature = d.hob_nsector = d.hob_sector = d.hob_lcyl = d.hob_hcyl = 0;
    d.select = ATA_DEV_ALWAYS_ON;
    d.status = READY_STAT | SEEK_STAT;
    d.lba48 = false;
    d.sense_key = 0;
    d.asc = 0;
    d.atapi_dma = false;
    d.packet_reply_size = 0;
    d.dma_cmd = IdeDrive::DmaCmd::None;
    d.req_nb_sectors = 0;
    set_signature(d);
    d.data_ptr = d.data_end = 0;
    d.end_transfer = IdeDrive::End::None;
}

void IdeController::set_signature(IdeDrive& d) {
    d.select &= ~ATA_DEV_HS;
    d.nsector = 1;
    d.sector = 1;
    if (d.kind == IdeDrive::kCd) {
        d.lcyl = 0x14;
        d.hcyl = 0xeb;
    } else if (d.blk) {
        d.lcyl = 0;
        d.hcyl = 0;
    } else {
        d.lcyl = 0xff;
        d.hcyl = 0xff;
    }
}

// --- IRQ (level to the PIC line, gated by nIEN; BM status latches INT) ------------------------
void IdeController::set_irq_level(bool level) {
    if (irq_cb_ && level != irq_asserted_) {
        irq_asserted_ = level;
        irq_cb_(irq_num_, level);
    }
}

void IdeController::raise_irq() {
    irq_pulses_++;
    bm_status_ |= BM_STATUS_INT;  // xemu bmdma_irq: the BM intercepts every bus IRQ
    if (!(bus_cmd_ & IDE_CTRL_DISABLE_IRQ)) set_irq_level(true);
}

void IdeController::lower_irq() { set_irq_level(false); }

// --- Sector addressing (xemu ide_get_sector / ide_set_sector / lba48 transform) ---------------
int64_t IdeController::get_sector(const IdeDrive& d) const {
    if (d.select & ATA_DEV_LBA) {
        if (d.lba48) {
            return (static_cast<int64_t>(d.hob_hcyl) << 40) |
                   (static_cast<int64_t>(d.hob_lcyl) << 32) |
                   (static_cast<int64_t>(d.hob_sector) << 24) |
                   (static_cast<int64_t>(d.hcyl) << 16) |
                   (static_cast<int64_t>(d.lcyl) << 8) | d.sector;
        }
        return ((d.select & ATA_DEV_LBA_MSB) << 24) | (d.hcyl << 16) | (d.lcyl << 8) | d.sector;
    }
    return (static_cast<int64_t>((d.hcyl << 8) | d.lcyl) * d.heads * d.sectors) +
           ((d.select & ATA_DEV_HS) * d.sectors) + (d.sector - 1);
}

void IdeController::set_sector(IdeDrive& d, int64_t sector_num) {
    if (d.select & ATA_DEV_LBA) {
        if (d.lba48) {
            d.sector = static_cast<uint8_t>(sector_num);
            d.lcyl = static_cast<uint8_t>(sector_num >> 8);
            d.hcyl = static_cast<uint8_t>(sector_num >> 16);
            d.hob_sector = static_cast<uint8_t>(sector_num >> 24);
            d.hob_lcyl = static_cast<uint8_t>(sector_num >> 32);
            d.hob_hcyl = static_cast<uint8_t>(sector_num >> 40);
        } else {
            d.select = static_cast<uint8_t>((d.select & ~ATA_DEV_LBA_MSB) |
                                            ((sector_num >> 24) & ATA_DEV_LBA_MSB));
            d.hcyl = static_cast<uint8_t>(sector_num >> 16);
            d.lcyl = static_cast<uint8_t>(sector_num >> 8);
            d.sector = static_cast<uint8_t>(sector_num);
        }
    } else {
        unsigned cyl = static_cast<unsigned>(sector_num / (d.heads * d.sectors));
        unsigned r = static_cast<unsigned>(sector_num % (d.heads * d.sectors));
        d.hcyl = static_cast<uint8_t>(cyl >> 8);
        d.lcyl = static_cast<uint8_t>(cyl);
        d.select = static_cast<uint8_t>((d.select & ~ATA_DEV_HS) | ((r / d.sectors) & 0x0f));
        d.sector = static_cast<uint8_t>((r % d.sectors) + 1);
    }
}

void IdeController::cmd_lba48_transform(IdeDrive& d, bool lba48) {
    d.lba48 = lba48;
    if (!lba48) {
        if (!(d.nsector & 0xff)) d.nsector = 256;
        else d.nsector &= 0xff;
    } else {
        if (!(d.nsector & 0xff) && !d.hob_nsector) d.nsector = 65536;
        else d.nsector = (static_cast<uint32_t>(d.hob_nsector) << 8) | (d.nsector & 0xff);
    }
}

// --- IDENTIFY payloads (xemu ide_identify / ide_atapi_identify) -------------------------------
// Identity strings: shaped like the real console's Seagate; the retail kernel derives its
// SECURITY UNLOCK key from model+serial, but we accept any key (as xemu does), so these are
// not load-bearing. Oracle debt: capture the live console's real IDENTIFY when convenient.
void IdeController::build_identify(IdeDrive& d) {
    uint8_t* p = d.identify_data;
    if (!d.identify_set) {
        std::memset(p, 0, 512);
        put_le16(p, 0, 0x0040);
        put_le16(p, 1, static_cast<uint16_t>(d.cylinders));
        put_le16(p, 3, static_cast<uint16_t>(d.heads));
        put_le16(p, 4, 512 * d.sectors);
        put_le16(p, 5, 512);
        put_le16(p, 6, static_cast<uint16_t>(d.sectors));
        padstr(p + 10 * 2, "XLLE000000000000000", 20);      // serial
        put_le16(p, 20, 3);
        put_le16(p, 21, 512);
        put_le16(p, 22, 4);
        padstr(p + 23 * 2, "3.04", 8);                      // firmware
        padstr(p + 27 * 2, "ST310211A", 40);                // model
        put_le16(p, 47, 0x8000 | kMaxMultSectors);
        put_le16(p, 48, 1);
        put_le16(p, 49, (1 << 11) | (1 << 9) | (1 << 8));   // DMA + LBA supported
        put_le16(p, 51, 0x200);
        put_le16(p, 52, 0x200);
        put_le16(p, 53, 1 | (1 << 1) | (1 << 2));
        put_le16(p, 54, static_cast<uint16_t>(d.cylinders));
        put_le16(p, 55, static_cast<uint16_t>(d.heads));
        put_le16(p, 56, static_cast<uint16_t>(d.sectors));
        uint32_t oldsize = static_cast<uint32_t>(d.cylinders) * d.heads * d.sectors;
        put_le16(p, 57, static_cast<uint16_t>(oldsize));
        put_le16(p, 58, static_cast<uint16_t>(oldsize >> 16));
        if (d.mult_sectors) put_le16(p, 59, 0x100 | d.mult_sectors);
        put_le16(p, 62, 0x07);
        put_le16(p, 63, 0x07);
        put_le16(p, 64, 0x03);
        put_le16(p, 65, 120);
        put_le16(p, 66, 120);
        put_le16(p, 67, 120);
        put_le16(p, 68, 120);
        put_le16(p, 80, 0xf0);                              // ata3..ata6
        put_le16(p, 81, 0x16);                              // conforms to ata5
        put_le16(p, 82, (1 << 14) | (1 << 5) | 1);          // NOP, WCACHE, SMART
        put_le16(p, 83, (1 << 14) | (1 << 13) | (1 << 12) | (1 << 10));
        put_le16(p, 84, (1 << 14));
        put_le16(p, 85, (1 << 14) | (1 << 5) | 1);
        put_le16(p, 86, (1 << 13) | (1 << 12) | (1 << 10));
        put_le16(p, 87, (1 << 14));
        put_le16(p, 88, 0x3f | (1 << 13));                  // udma5
        put_le16(p, 93, 1 | (1 << 14) | 0x2000);
        d.identify_set = true;
    }
    // Word 128, security status (xemu, Xbox-specific): 2=locked, 1=enabled, 0=supported.
    // Refreshed every IDENTIFY so the post-unlock state reads back correctly.
    put_le16(p, 128, d.locked ? ((1 << 2) | (1 << 1) | 1) : ((1 << 1) | 1));
    // LBA28 capacity (words 60-61) + LBA48 capacity (words 100-103).
    uint64_t nb = d.nb_sectors;
    uint32_t lba28 = nb > 0x0FFFFFFF ? 0x0FFFFFFF : static_cast<uint32_t>(nb);
    put_le16(p, 60, static_cast<uint16_t>(lba28));
    put_le16(p, 61, static_cast<uint16_t>(lba28 >> 16));
    put_le16(p, 100, static_cast<uint16_t>(nb));
    put_le16(p, 101, static_cast<uint16_t>(nb >> 16));
    put_le16(p, 102, static_cast<uint16_t>(nb >> 32));
    put_le16(p, 103, static_cast<uint16_t>(nb >> 48));
    std::memcpy(d.io_buffer.data(), p, 512);
}

void IdeController::build_identify_packet(IdeDrive& d) {
    uint8_t* p = d.identify_data;
    if (!d.identify_set) {
        std::memset(p, 0, 512);
        // Removable CD-ROM, 50us response, 12-byte packets.
        put_le16(p, 0, (2 << 14) | (5 << 8) | (1 << 7) | (2 << 5));
        padstr(p + 10 * 2, "XLLE0DVD00000000000", 20);
        put_le16(p, 20, 3);
        put_le16(p, 21, 512);
        put_le16(p, 22, 4);
        padstr(p + 23 * 2, "1.0", 8);
        padstr(p + 27 * 2, "PHILIPS XBOX DVD-ROM", 40);
        put_le16(p, 48, 1);
        put_le16(p, 49, (1 << 9) | (1 << 8));  // DMA and LBA supported
        put_le16(p, 53, 7);
        put_le16(p, 62, 7);
        put_le16(p, 63, 7);
        put_le16(p, 64, 3);
        put_le16(p, 65, 0xb4);
        put_le16(p, 66, 0xb4);
        put_le16(p, 67, 0x12c);
        put_le16(p, 68, 0xb4);
        put_le16(p, 71, 30);
        put_le16(p, 72, 30);
        put_le16(p, 80, 0x1e);                 // up to ATA/ATAPI-4
        put_le16(p, 88, 0x3f | (1 << 13));     // udma5
        d.identify_set = true;
    }
    std::memcpy(d.io_buffer.data(), p, 512);
}

// --- PIO transfer machinery (xemu ide_transfer_start / stop / data ports) ---------------------
void IdeController::transfer_start(IdeDrive& d, size_t size, IdeDrive::End end) {
    d.data_ptr = 0;
    d.data_end = size;
    if (!(d.status & ERR_STAT)) d.status |= DRQ_STAT;
    d.end_transfer = end;
}

void IdeController::transfer_stop(IdeDrive& d) {
    d.end_transfer = IdeDrive::End::None;
    d.data_ptr = d.data_end = 0;
    d.status &= ~DRQ_STAT;
}

void IdeController::end_transfer(IdeDrive& d) {
    d.status &= ~DRQ_STAT;
    IdeDrive::End action = d.end_transfer;
    d.end_transfer = IdeDrive::End::None;
    switch (action) {
        case IdeDrive::End::TransferStop:
        case IdeDrive::End::None:
            transfer_stop(d);
            break;
        case IdeDrive::End::SectorRead:
            sector_read(d);
            break;
        case IdeDrive::End::SectorWrite:
            sector_write_done(d);
            break;
        case IdeDrive::End::AtapiCmd:
            atapi_cmd(d);
            break;
        case IdeDrive::End::AtapiReplyStop:
            atapi_cmd_ok(d);
            break;
        case IdeDrive::End::SecurityCmd:
            // xemu ide_security_cmd: accept the password (the kernel always computes the
            // right one), transition to unlocked.
            d.locked = false;
            d.error = 0;
            d.status = READY_STAT | SEEK_STAT;
            raise_irq();
            break;
    }
}

uint32_t IdeController::data_read(int size) {
    IdeDrive& d = active();
    // Data flows device->guest only during a PIO-out transfer with DRQ set.
    bool pio_out = d.end_transfer != IdeDrive::End::SectorWrite &&
                   d.end_transfer != IdeDrive::End::AtapiCmd &&
                   d.end_transfer != IdeDrive::End::SecurityCmd;
    if (!(d.status & DRQ_STAT) || !pio_out) return 0;
    if (d.data_ptr + static_cast<size_t>(size) > d.data_end) return 0;
    uint32_t v = 0;
    std::memcpy(&v, d.io_buffer.data() + d.data_ptr, size);
    d.data_ptr += size;
    if (d.data_ptr >= d.data_end) end_transfer(d);
    return v;
}

void IdeController::data_write(uint32_t val, int size) {
    IdeDrive& d = active();
    bool pio_in = d.end_transfer == IdeDrive::End::SectorWrite ||
                  d.end_transfer == IdeDrive::End::AtapiCmd ||
                  d.end_transfer == IdeDrive::End::SecurityCmd;
    if (!(d.status & DRQ_STAT) || !pio_in) return;
    if (d.data_ptr + static_cast<size_t>(size) > d.data_end) return;
    std::memcpy(d.io_buffer.data() + d.data_ptr, &val, size);
    d.data_ptr += size;
    if (d.data_ptr >= d.data_end) end_transfer(d);
}

// --- Disk backing ------------------------------------------------------------------------------
bool IdeController::disk_rw(IdeDrive& d, uint64_t sector, uint32_t count, bool write,
                            uint8_t* buf) {
    if (!d.blk) return false;
    if (sector + count > d.nb_sectors) return false;
    const size_t n = static_cast<size_t>(count) * 512;

    // ---- copy-on-write path (default; beads-xbox.1.14.11) -------------------------------
    // A guest write never reaches the file: it is recorded per LBA and served back to every
    // later read of that LBA in the same run. The guest cannot tell the difference, and the
    // image is byte-identical after the run, so the NEXT run reads the same disk.
    if (!hdd_writeback_ && &d == &drives_[0]) {
        if (write) {
            if (overlay_.size() + count > kMaxOverlaySectors) {
                if (fault_)
                    fault_("IDE copy-on-write overlay is full — raise "
                           "IdeController::kMaxOverlaySectors or pass --hdd-writeback");
                return false;
            }
            for (uint32_t i = 0; i < count; ++i) {
                std::array<uint8_t, 512>& slot = overlay_[sector + i];
                std::memcpy(slot.data(), buf + static_cast<size_t>(i) * 512, 512);
            }
            sectors_written_ += count;
            return true;
        }
    }

#ifdef _WIN32
    if (_fseeki64(d.blk, static_cast<int64_t>(sector) * 512, SEEK_SET) != 0) return false;
#else
    if (std::fseek(d.blk, static_cast<long>(sector * 512), SEEK_SET) != 0) return false;
#endif
    size_t done = write ? std::fwrite(buf, 1, n, d.blk) : std::fread(buf, 1, n, d.blk);
    if (write) { sectors_written_ += count; std::fflush(d.blk); }
    else sectors_read_ += count;
    if (done != n) return false;
    // Overlay wins over the file for any sector the guest has already written.
    if (!write && !hdd_writeback_ && &d == &drives_[0] && !overlay_.empty()) {
        for (uint32_t i = 0; i < count; ++i) {
            const auto it = overlay_.find(sector + i);
            if (it != overlay_.end())
                std::memcpy(buf + static_cast<size_t>(i) * 512, it->second.data(), 512);
        }
    }
    return true;
}

// --- PIO sector read/write (xemu ide_sector_read / ide_sector_write_cb) -----------------------
void IdeController::sector_read(IdeDrive& d) {
    d.status = READY_STAT | SEEK_STAT;
    d.error = 0;
    int64_t sector_num = get_sector(d);
    uint32_t n = d.nsector;
    if (n == 0) {
        transfer_stop(d);
        return;
    }
    if (n > static_cast<uint32_t>(d.req_nb_sectors)) n = d.req_nb_sectors;
    if (!disk_rw(d, static_cast<uint64_t>(sector_num), n, false, d.io_buffer.data())) {
        char b[128];
        std::snprintf(b, sizeof(b), "IDE PIO read out of range/failed: lba=%lld n=%u",
                      static_cast<long long>(sector_num), n);
        if (fault_) fault_(b);
        abort_command(d);
        return;
    }
    set_sector(d, sector_num + n);
    d.nsector -= n;
    transfer_start(d, n * 512, IdeDrive::End::SectorRead);
    raise_irq();
}

void IdeController::sector_write_done(IdeDrive& d) {
    int64_t sector_num = get_sector(d);
    uint32_t n = d.nsector;
    if (n > static_cast<uint32_t>(d.req_nb_sectors)) n = d.req_nb_sectors;
    if (!disk_rw(d, static_cast<uint64_t>(sector_num), n, true, d.io_buffer.data())) {
        char b[128];
        std::snprintf(b, sizeof(b), "IDE PIO write out of range/failed: lba=%lld n=%u",
                      static_cast<long long>(sector_num), n);
        if (fault_) fault_(b);
        abort_command(d);
        return;
    }
    d.nsector -= n;
    set_sector(d, sector_num + n);
    d.status = READY_STAT | SEEK_STAT;
    if (d.nsector == 0) {
        transfer_stop(d);
    } else {
        uint32_t n1 = d.nsector;
        if (n1 > static_cast<uint32_t>(d.req_nb_sectors)) n1 = d.req_nb_sectors;
        transfer_start(d, n1 * 512, IdeDrive::End::SectorWrite);
    }
    raise_irq();
}

// --- ATAPI (xemu atapi.c; drive present, no media) ---------------------------------------------
void IdeController::atapi_cmd_ok(IdeDrive& d) {
    d.error = 0;
    d.status = READY_STAT | SEEK_STAT;
    d.nsector = (d.nsector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
    transfer_stop(d);
    raise_irq();
}

void IdeController::atapi_cmd_error(IdeDrive& d, int sense_key, int asc) {
    d.error = static_cast<uint8_t>(sense_key << 4);
    d.status = READY_STAT | ERR_STAT;
    d.nsector = (d.nsector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
    d.sense_key = static_cast<uint8_t>(sense_key);
    d.asc = static_cast<uint8_t>(asc);
    transfer_stop(d);
    raise_irq();
}

void IdeController::atapi_reply(IdeDrive& d, int size, int max_size) {
    if (size > max_size) size = max_size;
    if (d.atapi_dma) {
        // Reply is delivered by the bus master when the guest starts it.
        d.packet_reply_size = size;
        d.status = READY_STAT | SEEK_STAT | DRQ_STAT;
        d.dma_cmd = IdeDrive::DmaCmd::Atapi;
        bm_maybe_run();
        return;
    }
    // PIO reply, single elementary transfer. The byte-count limit registers must cover the
    // reply; the boot's replies are tiny (sense/inquiry/GESN). A smaller BCL would need the
    // full chunked reply loop — characterize before modelling.
    int bcl = d.lcyl | (d.hcyl << 8);
    if (bcl == 0xffff) bcl = 0xfffe;
    if (bcl < size) {
        char b[96];
        std::snprintf(b, sizeof(b), "ATAPI reply %d bytes > BCL %d — chunked reply unmodeled",
                      size, bcl);
        if (fault_) fault_(b);
        return;
    }
    d.lcyl = static_cast<uint8_t>(size);
    d.hcyl = static_cast<uint8_t>(size >> 8);
    d.nsector = (d.nsector & ~7) | ATAPI_INT_REASON_IO;
    d.status = READY_STAT | SEEK_STAT;
    transfer_start(d, static_cast<size_t>(size), IdeDrive::End::AtapiReplyStop);
    raise_irq();
}

void IdeController::atapi_cmd(IdeDrive& d) {
    uint8_t* buf = d.io_buffer.data();
    const uint8_t op = buf[0];

    // UNIT ATTENTION gating (xemu): only REQUEST SENSE / INQUIRY / GESN bypass it.
    const bool allow_ua = (op == 0x03 || op == 0x12 || op == 0x4a || op == 0x46);
    if (d.sense_key == UNIT_ATTENTION && !allow_ua) {
        d.error = MC_ERR | (UNIT_ATTENTION << 4);
        d.status = READY_STAT | ERR_STAT;
        d.nsector = 0;
        raise_irq();
        return;
    }

    // Commands that require media report Not Ready — the tray is empty (faithful state the
    // kernel polls for, not a fault).
    const bool check_ready = (op == 0x00 || op == 0x25 || op == 0x28 || op == 0x2b ||
                              op == 0x43 || op == 0x51 || op == 0xa8 || op == 0xad ||
                              op == 0xbe);
    if (check_ready) {
        atapi_cmd_error(d, NOT_READY, ASC_MEDIUM_NOT_PRESENT);
        return;
    }

    switch (op) {
        case 0x03: {  // REQUEST SENSE
            int max_len = buf[4];
            std::memset(buf, 0, 18);
            buf[0] = 0x70 | (1 << 7);
            buf[2] = d.sense_key;
            buf[7] = 10;
            buf[12] = d.asc;
            if (d.sense_key == UNIT_ATTENTION) d.sense_key = NO_SENSE;
            atapi_reply(d, 18, max_len);
            return;
        }
        case 0x12: {  // INQUIRY
            if (buf[1] & 0x01) {  // EVPD pages — characterize before modelling
                if (fault_) fault_("ATAPI INQUIRY with EVPD set — unmodeled VPD page");
                return;
            }
            int max_len = buf[4];
            buf[0] = 0x05;  // CD-ROM
            buf[1] = 0x80;  // removable
            buf[2] = 0x00;
            buf[3] = 0x21;  // ATAPI-2
            buf[4] = 36 - 5;
            buf[5] = buf[6] = buf[7] = 0;
            padstr8(buf + 8, 8, "PHILIPS");
            padstr8(buf + 16, 16, "XBOX DVD-ROM");
            padstr8(buf + 32, 4, "1.0");
            atapi_reply(d, 36, max_len);
            return;
        }
        case 0x1e:  // PREVENT/ALLOW MEDIUM REMOVAL
            atapi_cmd_ok(d);
            return;
        case 0x1b:  // START STOP UNIT (tray ops accepted; no media either way)
            atapi_cmd_ok(d);
            return;
        case 0x4a: {  // GET EVENT STATUS NOTIFICATION
            int max_len = (buf[7] << 8) | buf[8];
            if (!(buf[1] & 0x01)) {  // async mode unsupported (xemu)
                atapi_cmd_error(d, ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET);
                return;
            }
            unsigned used_len;
            buf[2] = 0;             // notification class
            buf[3] = 1 << 4;        // supported events: media
            if (buf[4] & (1 << 4)) {
                buf[2] |= 4;        // media class
                buf[4] = 0;         // event: no change
                buf[5] = 0;         // media status: no media, tray closed
                buf[6] = buf[7] = 0;
                used_len = 8;
            } else {
                buf[2] = 0x80;      // no event available
                used_len = 4;
            }
            uint16_t hdr_len = static_cast<uint16_t>(used_len - 4);
            buf[0] = static_cast<uint8_t>(hdr_len >> 8);
            buf[1] = static_cast<uint8_t>(hdr_len);
            atapi_reply(d, static_cast<int>(used_len), max_len);
            return;
        }
        case 0x5a: {  // MODE SENSE(10) — harvested verbatim from xemu cmd_mode_sense. An
                      // unlisted page (incl. the Xbox DVD-auth page 0x3E) gets ILLEGAL
                      // REQUEST / INVALID FIELD; xemu boots this same retail kernel through
                      // that exact response, so it is the boot-correct witness.
            int max_len = (buf[7] << 8) | buf[8];
            int action = buf[2] >> 6;
            int code = buf[2] & 0x3f;
            static int ms_logged = 0;
            if (ms_logged < 8) {
                std::printf("[ide] ATAPI MODE SENSE action=%d page=0x%02x\n", action, code);
                ++ms_logged;
            }
            if (action == 0 && code == 0x01) {  // R/W error recovery
                std::memset(buf, 0, 16);
                buf[1] = 16 - 2;
                buf[2] = 0x70;
                buf[8] = 0x01;
                buf[9] = 16 - 10;
                buf[11] = 0x05;
                atapi_reply(d, 16, max_len);
            } else if (action == 0 && code == 0x0e) {  // audio control
                std::memset(buf, 0, 24);
                buf[1] = 24 - 2;
                buf[2] = 0x70;
                buf[8] = 0x0e;
                buf[9] = 24 - 10;
                atapi_reply(d, 24, max_len);
            } else if (action == 0 && code == 0x2a) {  // capabilities
                std::memset(buf, 0, 30);
                buf[1] = 30 - 2;
                buf[2] = 0x70;
                buf[8] = 0x2a;
                buf[9] = 30 - 10;
                buf[10] = 0x3b;
                buf[12] = 0x71;
                buf[13] = 3 << 5;
                buf[14] = (1 << 0) | (1 << 3) | (1 << 5);
                buf[16] = 704 >> 8; buf[17] = 704 & 0xff;
                buf[19] = 2;
                buf[20] = 512 >> 8; buf[21] = 512 & 0xff;
                buf[22] = 704 >> 8; buf[23] = 704 & 0xff;
                atapi_reply(d, 30, max_len);
            } else if (action == 3) {
                atapi_cmd_error(d, ILLEGAL_REQUEST, 0x39);  // saving params not supported
            } else {
                atapi_cmd_error(d, ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET);
            }
            return;
        }
        case 0x46: {  // GET CONFIGURATION (xemu: current profile 0 = no media)
            if (buf[2] != 0 || buf[3] != 0) {
                atapi_cmd_error(d, ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET);
                return;
            }
            int max_len = (buf[7] << 8) | buf[8];
            if (max_len > 512) max_len = 512;
            std::memset(buf, 0, max_len > 32 ? static_cast<size_t>(max_len) : 32);
            // No media: current profile stays 0. Feature 0 lists DVD-ROM + CD-ROM profiles.
            buf[10] = 0x03;  // persistent | current
            // profile list feature descriptor: 2 profiles x 4 bytes
            buf[11] = 8;
            buf[12] = 0x00; buf[13] = 0x10;  // DVD-ROM (0x0010)
            buf[16] = 0x00; buf[17] = 0x08;  // CD-ROM (0x0008)
            uint32_t len = 12 + 8;
            buf[0] = static_cast<uint8_t>((len - 4) >> 24);
            buf[1] = static_cast<uint8_t>((len - 4) >> 16);
            buf[2] = static_cast<uint8_t>((len - 4) >> 8);
            buf[3] = static_cast<uint8_t>(len - 4);
            atapi_reply(d, static_cast<int>(len), max_len);
            return;
        }
        default: {
            char b[96];
            std::snprintf(b, sizeof(b),
                          "ATAPI packet opcode 0x%02x unmodeled — characterize vs hardware", op);
            if (fault_) fault_(b);
            return;
        }
    }
}

// --- Command execution (xemu ide_bus_exec_cmd + handlers) --------------------------------------
void IdeController::abort_command(IdeDrive& d) {
    d.status = READY_STAT | ERR_STAT;
    d.error = ABRT_ERR;
    transfer_stop(d);
}

void IdeController::exec_cmd(uint8_t cmd) {
    IdeDrive& d = active();

    // Only DEVICE RESET is allowed while BSY/DRQ, and only to ATAPI devices.
    if (d.status & (BUSY_STAT | DRQ_STAT)) {
        if (cmd != 0x08 || d.kind != IdeDrive::kCd) return;
    }

    const bool is_cd = d.kind == IdeDrive::kCd;
    bool complete = false;
    bool set_dsc = false;
    d.status = READY_STAT | BUSY_STAT;
    d.error = 0;

    switch (cmd) {
        case 0xEC:  // IDENTIFY DEVICE
            if (!is_cd && d.blk) {
                build_identify(d);
                d.status = READY_STAT | SEEK_STAT;
                transfer_start(d, 512, IdeDrive::End::TransferStop);
                raise_irq();
            } else {
                if (is_cd) set_signature(d);
                abort_command(d);
                complete = true;
            }
            break;
        case 0xA1:  // IDENTIFY PACKET DEVICE
            if (is_cd) {
                build_identify_packet(d);
                d.status = READY_STAT | SEEK_STAT;
                transfer_start(d, 512, IdeDrive::End::TransferStop);
                raise_irq();
            } else {
                abort_command(d);
                complete = true;
            }
            break;
        case 0xA0:  // PACKET
            if (is_cd) {
                if (d.feature & 0x02) { abort_command(d); complete = true; break; }
                d.status = READY_STAT | SEEK_STAT;
                d.atapi_dma = (d.feature & 1) != 0;
                d.nsector = 1;
                transfer_start(d, 12, IdeDrive::End::AtapiCmd);
            } else {
                abort_command(d);
                complete = true;
            }
            break;
        case 0x08:  // DEVICE RESET (ATAPI only)
            if (is_cd) {
                reset_drive(d);
                d.status = 0x00;
            } else {
                abort_command(d);
                complete = true;
            }
            break;
        case 0x90: {  // EXECUTE DEVICE DIAGNOSTIC
            d.select = ATA_DEV_ALWAYS_ON;
            set_signature(d);
            if (is_cd) {
                d.status = 0;
                d.error = 0x01;
            } else {
                d.status = READY_STAT | SEEK_STAT;
                d.error = 0x01;
                raise_irq();
            }
            break;
        }
        case 0x91:  // INITIALIZE DEVICE PARAMETERS
            if (!is_cd && d.blk) {
                d.heads = (d.select & ATA_DEV_HS) + 1;
                d.sectors = d.nsector & 0xff;
                raise_irq();
            } else {
                abort_command(d);
            }
            complete = true;
            set_dsc = true;
            break;
        case 0x10:  // RECALIBRATE
            complete = true;
            set_dsc = true;
            break;
        case 0x20: case 0x21: case 0x24:  // READ SECTORS (PIO; 0x24 = EXT)
            if (is_cd) { set_signature(d); abort_command(d); complete = true; break; }
            if (!d.blk) { abort_command(d); complete = true; break; }
            cmd_lba48_transform(d, cmd == 0x24);
            d.req_nb_sectors = 1;
            sector_read(d);
            break;
        case 0x30: case 0x31: case 0x34:  // WRITE SECTORS (PIO; 0x34 = EXT)
            if (!d.blk) { abort_command(d); complete = true; break; }
            cmd_lba48_transform(d, cmd == 0x34);
            d.req_nb_sectors = 1;
            d.status = SEEK_STAT | READY_STAT;
            transfer_start(d, 512, IdeDrive::End::SectorWrite);
            break;
        case 0xC4: case 0x29:  // READ MULTIPLE (0x29 = EXT)
            if (!d.blk || !d.mult_sectors) { abort_command(d); complete = true; break; }
            cmd_lba48_transform(d, cmd == 0x29);
            d.req_nb_sectors = d.mult_sectors;
            sector_read(d);
            break;
        case 0xC5: case 0x39: {  // WRITE MULTIPLE (0x39 = EXT)
            if (!d.blk || !d.mult_sectors) { abort_command(d); complete = true; break; }
            cmd_lba48_transform(d, cmd == 0x39);
            d.req_nb_sectors = d.mult_sectors;
            uint32_t n = d.nsector;
            if (n > static_cast<uint32_t>(d.req_nb_sectors)) n = d.req_nb_sectors;
            d.status = SEEK_STAT | READY_STAT;
            transfer_start(d, 512 * n, IdeDrive::End::SectorWrite);
            break;
        }
        case 0xC6:  // SET MULTIPLE MODE
            if ((d.nsector & 0xff) != 0 &&
                ((d.nsector & 0xff) > kMaxMultSectors ||
                 ((d.nsector & 0xff) & ((d.nsector & 0xff) - 1)) != 0)) {
                abort_command(d);
            } else {
                d.mult_sectors = d.nsector & 0xff;
            }
            complete = true;
            set_dsc = true;
            break;
        case 0xC8: case 0xC9: case 0x25:  // READ DMA (0x25 = EXT)
            if (!d.blk) { abort_command(d); complete = true; break; }
            cmd_lba48_transform(d, cmd == 0x25);
            d.status = READY_STAT | SEEK_STAT | DRQ_STAT;
            d.dma_cmd = IdeDrive::DmaCmd::Read;
            bm_maybe_run();
            break;
        case 0xCA: case 0xCB: case 0x35:  // WRITE DMA (0x35 = EXT)
            if (!d.blk) { abort_command(d); complete = true; break; }
            cmd_lba48_transform(d, cmd == 0x35);
            d.status = READY_STAT | SEEK_STAT | DRQ_STAT;
            d.dma_cmd = IdeDrive::DmaCmd::Write;
            bm_maybe_run();
            break;
        case 0x40: case 0x41: case 0x42:  // READ VERIFY (0x42 = EXT)
            cmd_lba48_transform(d, cmd == 0x42);
            complete = true;
            set_dsc = true;
            break;
        case 0x70:  // SEEK
            complete = true;
            set_dsc = true;
            break;
        case 0xE7: case 0xEA:  // FLUSH CACHE (EXT)
            if (d.blk) std::fflush(d.blk);
            d.status = READY_STAT | SEEK_STAT;
            raise_irq();
            break;
        case 0xE0: case 0xE1: case 0xE2: case 0xE3:  // STANDBY/IDLE family: nop
        case 0x94: case 0x95: case 0x96: case 0x97:
        case 0xE6: case 0x99:                        // SLEEP: nop
            complete = true;
            break;
        case 0xE5: case 0x98:  // CHECK POWER MODE
            d.nsector = 0xff;  // active/idle
            complete = true;
            set_dsc = true;
            break;
        case 0xEF: {  // SET FEATURES (xemu cmd_set_features subset)
            if (!d.blk) { abort_command(d); complete = true; break; }
            complete = true;
            set_dsc = true;
            switch (d.feature) {
                case 0x02:  // write cache enable
                    put_le16(d.identify_data, 85, (1 << 14) | (1 << 5) | 1);
                    break;
                case 0x82:  // write cache disable
                    put_le16(d.identify_data, 85, (1 << 14) | 1);
                    std::fflush(d.blk);
                    break;
                case 0xcc: case 0x66: case 0xaa: case 0x55: case 0x05: case 0x85:
                case 0x69: case 0x67: case 0x96: case 0x9a: case 0x42: case 0xc2:
                    break;  // accepted no-ops (xemu)
                case 0x03: {  // set transfer mode
                    uint8_t val = d.nsector & 0x07;
                    uint8_t* p = d.identify_data;
                    switch ((d.nsector & 0xff) >> 3) {
                        case 0x00: case 0x01:  // PIO
                            put_le16(p, 62, 0x07);
                            put_le16(p, 63, 0x07);
                            put_le16(p, 88, 0x3f);
                            break;
                        case 0x02:  // single-word DMA
                            put_le16(p, 62, 0x07 | (1 << (val + 8)));
                            put_le16(p, 63, 0x07);
                            put_le16(p, 88, 0x3f);
                            break;
                        case 0x04:  // multi-word DMA
                            put_le16(p, 62, 0x07);
                            put_le16(p, 63, 0x07 | (1 << (val + 8)));
                            put_le16(p, 88, 0x3f);
                            break;
                        case 0x08:  // UDMA
                            put_le16(p, 62, 0x07);
                            put_le16(p, 63, 0x07);
                            put_le16(p, 88, 0x3f | (1 << (val + 8)));
                            break;
                        default:
                            abort_command(d);
                            break;
                    }
                    break;
                }
                default:
                    abort_command(d);
                    break;
            }
            break;
        }
        case 0xF2:  // SECURITY UNLOCK (Xbox-critical; xemu cmd_security_unlock)
            d.error = 0;
            d.status = READY_STAT | SEEK_STAT;
            transfer_start(d, 512, IdeDrive::End::SecurityCmd);
            break;
        case 0xF5:  // SECURITY FREEZE LOCK (xemu maps to a nop that clears nsector)
            d.nsector = 0;
            complete = true;
            set_dsc = true;
            break;
        case 0xF8:  // READ NATIVE MAX ADDRESS
            if (d.nb_sectors == 0) {
                abort_command(d);
            } else {
                int aheads = d.heads, asectors = d.sectors;
                d.heads = d.drive_heads;
                d.sectors = d.drive_sectors;
                cmd_lba48_transform(d, false);
                set_sector(d, static_cast<int64_t>(d.nb_sectors) - 1);
                d.heads = aheads;
                d.sectors = asectors;
            }
            complete = true;
            set_dsc = true;
            break;
        default: {
            char b[96];
            std::snprintf(b, sizeof(b),
                          "IDE command 0x%02x to %s unmodeled — characterize vs hardware", cmd,
                          is_cd ? "atapi-dvd" : "hdd");
            if (fault_) fault_(b);
            abort_command(d);
            complete = true;
            break;
        }
    }

    if (complete) {
        d.status &= ~BUSY_STAT;
        if (set_dsc && !d.error) d.status |= SEEK_STAT;
        raise_irq();
    } else {
        d.status &= ~BUSY_STAT;  // synchronous device: BUSY is never guest-visible
    }
}

// --- Task-file port dispatch (xemu ide_ioport_read/write) --------------------------------------
uint32_t IdeController::ioport_read(uint32_t reg) {
    IdeDrive& d = active();
    const bool hob = (bus_cmd_ & IDE_CTRL_HOB) != 0;
    switch (reg) {
        case 1: return hob ? d.hob_feature : d.error;
        case 2: return hob ? d.hob_nsector : (d.nsector & 0xff);
        case 3: return hob ? d.hob_sector : d.sector;
        case 4: return hob ? d.hob_lcyl : d.lcyl;
        case 5: return hob ? d.hob_hcyl : d.hcyl;
        case 6: return d.select;
        case 7:
        default:
            lower_irq();
            return d.status;
    }
}

void IdeController::ioport_write(uint32_t reg, uint32_t val) {
    IdeDrive& d0 = drives_[0];
    IdeDrive& d1 = drives_[1];
    // Writes to the command block are ignored while the active drive is busy (except command).
    if (reg != 7 && (active().status & (BUSY_STAT | DRQ_STAT))) return;
    const uint8_t v = static_cast<uint8_t>(val);
    switch (reg) {
        case 0:
            break;
        case 1:  // features (both drives receive register writes — same cable)
            bus_cmd_ &= ~IDE_CTRL_HOB;
            d0.hob_feature = d0.feature; d1.hob_feature = d1.feature;
            d0.feature = v; d1.feature = v;
            break;
        case 2:
            bus_cmd_ &= ~IDE_CTRL_HOB;
            d0.hob_nsector = static_cast<uint8_t>(d0.nsector);
            d1.hob_nsector = static_cast<uint8_t>(d1.nsector);
            d0.nsector = v; d1.nsector = v;
            break;
        case 3:
            bus_cmd_ &= ~IDE_CTRL_HOB;
            d0.hob_sector = d0.sector; d1.hob_sector = d1.sector;
            d0.sector = v; d1.sector = v;
            break;
        case 4:
            bus_cmd_ &= ~IDE_CTRL_HOB;
            d0.hob_lcyl = d0.lcyl; d1.hob_lcyl = d1.lcyl;
            d0.lcyl = v; d1.lcyl = v;
            break;
        case 5:
            bus_cmd_ &= ~IDE_CTRL_HOB;
            d0.hob_hcyl = d0.hcyl; d1.hob_hcyl = d1.hcyl;
            d0.hcyl = v; d1.hcyl = v;
            break;
        case 6:
            bus_cmd_ &= ~IDE_CTRL_HOB;
            d0.select = v | ATA_DEV_ALWAYS_ON;
            d1.select = v | ATA_DEV_ALWAYS_ON;
            unit_ = (v & ATA_DEV_SELECT) ? 1 : 0;
            break;
        case 7:
        default:
            bus_cmd_ &= ~IDE_CTRL_HOB;
            lower_irq();
            exec_cmd(v);
            break;
    }
}

void IdeController::ctrl_write(uint32_t val) {
    // Software reset on the rising edge of SRST (synchronous: perform it immediately).
    if (!(bus_cmd_ & IDE_CTRL_RESET) && (val & IDE_CTRL_RESET)) {
        for (auto& d : drives_) {
            reset_drive(d);
            // Post-SRST diagnostic (xemu ide_perform_srst -> cmd_exec_dev_diagnostic).
            d.select = ATA_DEV_ALWAYS_ON;
            set_signature(d);
            if (d.kind == IdeDrive::kCd) {
                d.status = 0;
                d.error = 0x01;
            } else {
                d.status = READY_STAT | SEEK_STAT;
                d.error = 0x01;
            }
        }
        unit_ = 0;
        bus_cmd_ = static_cast<uint8_t>(val & ~IDE_CTRL_RESET);
        return;
    }
    bus_cmd_ = static_cast<uint8_t>(val);
    // nIEN change re-evaluates the line level.
    if (bus_cmd_ & IDE_CTRL_DISABLE_IRQ) set_irq_level(false);
}

// --- Bus-master DMA (xemu pci.c, synchronous) ---------------------------------------------------
uint8_t IdeController::bm_read(uint32_t off) {
    switch (off) {
        case 0: return bm_cmd_;
        case 2: return bm_status_;
        case 4: return static_cast<uint8_t>(bm_addr_);
        case 5: return static_cast<uint8_t>(bm_addr_ >> 8);
        case 6: return static_cast<uint8_t>(bm_addr_ >> 16);
        case 7: return static_cast<uint8_t>(bm_addr_ >> 24);
        default: return 0xFF;  // secondary channel (no devices) + reserved bytes
    }
}

void IdeController::bm_write(uint32_t off, uint8_t val) {
    switch (off) {
        case 0:
            if ((val & BM_CMD_START) != (bm_cmd_ & BM_CMD_START)) {
                if (!(val & BM_CMD_START)) {
                    bm_status_ &= ~BM_STATUS_DMAING;
                } else {
                    bm_status_ |= BM_STATUS_DMAING;
                }
            }
            bm_cmd_ = val & 0x09;
            if (bm_cmd_ & BM_CMD_START) bm_maybe_run();
            break;
        case 2:
            bm_status_ = static_cast<uint8_t>((val & 0x60) | (bm_status_ & BM_STATUS_DMAING) |
                                              (bm_status_ & ~val &
                                               (BM_STATUS_ERROR | BM_STATUS_INT)));
            break;
        case 4: bm_addr_ = (bm_addr_ & 0xFFFFFF00u) | val; bm_addr_ &= ~3u; break;
        case 5: bm_addr_ = (bm_addr_ & 0xFFFF00FFu) | (static_cast<uint32_t>(val) << 8); break;
        case 6: bm_addr_ = (bm_addr_ & 0xFF00FFFFu) | (static_cast<uint32_t>(val) << 16); break;
        case 7: bm_addr_ = (bm_addr_ & 0x00FFFFFFu) | (static_cast<uint32_t>(val) << 24); break;
        default: break;  // secondary channel: accept + ignore (no devices behind it)
    }
}

void IdeController::bm_maybe_run() {
    if (!(bm_cmd_ & BM_CMD_START)) return;
    IdeDrive& d = active();
    if (d.dma_cmd == IdeDrive::DmaCmd::None) return;
    if (!mem_read_ || !mem_write_) {
        if (fault_) fault_("IDE bus-master DMA started but no guest memory access wired");
        return;
    }

    // Byte count of the request.
    uint32_t total;
    if (d.dma_cmd == IdeDrive::DmaCmd::Atapi) {
        total = static_cast<uint32_t>(d.packet_reply_size);
    } else {
        total = d.nsector * 512;
    }
    if (total > kIoBufferSize) {
        if (fault_) fault_("IDE DMA request exceeds transfer buffer — unmodeled");
        return;
    }

    // For reads, stage the data first.
    int64_t sector_num = 0;
    if (d.dma_cmd == IdeDrive::DmaCmd::Read) {
        sector_num = get_sector(d);
        if (!disk_rw(d, static_cast<uint64_t>(sector_num), d.nsector, false,
                     d.io_buffer.data())) {
            char b[128];
            std::snprintf(b, sizeof(b), "IDE DMA read out of range/failed: lba=%lld n=%u",
                          static_cast<long long>(sector_num), d.nsector);
            if (fault_) fault_(b);
            return;
        }
    } else if (d.dma_cmd == IdeDrive::DmaCmd::Write) {
        sector_num = get_sector(d);
    }

    // Walk the PRD table (physical addresses), moving up to `total` bytes.
    const bool to_memory = d.dma_cmd != IdeDrive::DmaCmd::Write;
    uint32_t cur = bm_addr_;
    uint32_t moved = 0;
    bool prd_overrun = false;  // PRDs describe more than the request (Active stays set)
    for (int guard = 0; guard < 512 && moved < total; ++guard) {
        uint8_t prd[8];
        if (!mem_read_(cur, prd, 8)) {
            if (fault_) fault_("IDE DMA: PRD table read failed");
            return;
        }
        cur += 8;
        uint32_t addr = prd[0] | (prd[1] << 8) | (prd[2] << 16) |
                        (static_cast<uint32_t>(prd[3]) << 24);
        uint32_t size = prd[4] | (prd[5] << 8);
        size &= 0xfffe;
        if (size == 0) size = 0x10000;
        bool eot = (prd[7] & 0x80) != 0;

        uint32_t chunk = size;
        if (chunk > total - moved) {
            chunk = total - moved;
            prd_overrun = true;
        }
        bool ok = to_memory ? mem_write_(addr, d.io_buffer.data() + moved, chunk)
                            : mem_read_(addr, d.io_buffer.data() + moved, chunk);
        if (!ok) {
            char b[96];
            std::snprintf(b, sizeof(b), "IDE DMA data %s failed at phys 0x%08x len %u",
                          to_memory ? "write" : "read", addr, chunk);
            if (fault_) fault_(b);
            return;
        }
        moved += chunk;
        if (eot) break;
    }
    if (moved < total) {
        char b[96];
        std::snprintf(b, sizeof(b), "IDE DMA: PRD table short (%u of %u bytes) — characterize",
                      moved, total);
        if (fault_) fault_(b);
        return;
    }

    // Complete the drive side.
    if (d.dma_cmd == IdeDrive::DmaCmd::Write) {
        if (!disk_rw(d, static_cast<uint64_t>(sector_num), d.nsector, true,
                     d.io_buffer.data())) {
            char b[128];
            std::snprintf(b, sizeof(b), "IDE DMA write out of range/failed: lba=%lld n=%u",
                          static_cast<long long>(sector_num), d.nsector);
            if (fault_) fault_(b);
            return;
        }
    }
    if (d.dma_cmd == IdeDrive::DmaCmd::Atapi) {
        d.packet_reply_size = 0;
        d.dma_cmd = IdeDrive::DmaCmd::None;
        atapi_cmd_ok(d);  // status + interrupt-reason + IRQ
    } else {
        set_sector(d, sector_num + d.nsector);
        d.nsector = 0;
        d.dma_cmd = IdeDrive::DmaCmd::None;
        d.status = READY_STAT | SEEK_STAT;
        raise_irq();
    }
    if (!prd_overrun) bm_status_ &= ~BM_STATUS_DMAING;
}

// --- IoPortDevice dispatch ----------------------------------------------------------------------
uint32_t IdeController::io_read(uint16_t port, int size) {
    if (port == 0x1F0) return data_read(size);
    if (port >= 0x1F1 && port <= 0x1F7) {
        // Multi-byte reads of byte-wide task-file registers replicate per-byte semantics.
        uint32_t v = 0;
        for (int i = 0; i < size; ++i)
            v |= (ioport_read(static_cast<uint32_t>(port - 0x1F0 + i) & 7) & 0xFF) << (8 * i);
        return v;
    }
    if (port == 0x3F6) return active().status;  // alt status: no IRQ acknowledge
    if (bm_base_ && port >= bm_base_ && port < static_cast<uint32_t>(bm_base_) + 16) {
        uint32_t v = 0;
        for (int i = 0; i < size; ++i)
            v |= static_cast<uint32_t>(bm_read(static_cast<uint32_t>(port - bm_base_ + i)))
                 << (8 * i);
        return v;
    }
    return 0xFFFFFFFFu;
}

void IdeController::io_write(uint16_t port, int size, uint32_t value) {
    if (port == 0x1F0) { data_write(value, size); return; }
    if (port >= 0x1F1 && port <= 0x1F7) {
        for (int i = 0; i < size; ++i)
            ioport_write(static_cast<uint32_t>(port - 0x1F0 + i) & 7, (value >> (8 * i)) & 0xFF);
        return;
    }
    if (port == 0x3F6) { ctrl_write(value & 0xFF); return; }
    if (bm_base_ && port >= bm_base_ && port < static_cast<uint32_t>(bm_base_) + 16) {
        for (int i = 0; i < size; ++i)
            bm_write(static_cast<uint32_t>(port - bm_base_ + i),
                     static_cast<uint8_t>(value >> (8 * i)));
        return;
    }
}

}  // namespace xlle
