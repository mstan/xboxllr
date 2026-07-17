// xlle — UnicornFloor implementation.
//
// A0 note on fidelity (PRINCIPLES §6): UC_HOOK_CODE fires once per instruction *before*
// execution, so `seq_` here counts dispatch, not true architectural retirement. That is
// adequate for the A0 fixture spike; precise retirement / restartable-fault boundaries are
// a later contract we validate against hardware. This is called out, not papered over.
#include "cpu/unicorn_floor.hpp"

#include <unicorn/unicorn.h>

#include <cstdio>
#include <cstring>

namespace xlle {

static uint64_t to_hook(void* p) { return reinterpret_cast<uint64_t>(p); }
static void* from_hook(uc_hook h) { return reinterpret_cast<void*>(static_cast<uintptr_t>(h)); }

UnicornFloor::UnicornFloor() {
    uc_engine* uc = nullptr;
    if (uc_open(UC_ARCH_X86, UC_MODE_32, &uc) != UC_ERR_OK) {
        uc_ = nullptr;
        return;
    }
    uc_ = reinterpret_cast<uc_struct*>(uc);

    uc_hook hc = 0, hm = 0;
    uc_hook_add(uc, &hc, UC_HOOK_CODE, reinterpret_cast<void*>(&UnicornFloor::code_trampoline),
                this, 1, 0);
    uc_hook_add(uc, &hm, UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE,
                reinterpret_cast<void*>(&UnicornFloor::mem_trampoline), this, 1, 0);
    hook_code_ = from_hook(hc);
    hook_mem_ = from_hook(hm);

    // Capture the address of any unmapped access so a fault-stop can name it (PRINCIPLES §3).
    uc_hook hu = 0;
    uc_hook_add(uc, &hu,
                UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED |
                    UC_HOOK_MEM_FETCH_UNMAPPED,
                reinterpret_cast<void*>(&UnicornFloor::unmapped_trampoline), this, 1, 0);
    hook_unmapped_ = from_hook(hu);

    // Guest-raised vectors (INT n / INT3 / CPU exceptions). Unicorn's patched cpu-exec removes
    // QEMU's native IDT delivery entirely and funnels every vector to this hook (no hook ->
    // UC_ERR_EXCEPTION). We put the architectural behavior back: deliver through the guest IDT.
    uc_hook hi = 0;
    uc_hook_add(uc, &hi, UC_HOOK_INTR, reinterpret_cast<void*>(&UnicornFloor::intr_trampoline),
                this, 1, 0);
    hook_intr_ = from_hook(hi);
}

UnicornFloor::~UnicornFloor() {
    if (uc_) uc_close(reinterpret_cast<uc_engine*>(uc_));
}

bool UnicornFloor::map_mem(uint32_t addr, size_t size) {
    return uc_mem_map(reinterpret_cast<uc_engine*>(uc_), addr, size, UC_PROT_ALL) == UC_ERR_OK;
}
bool UnicornFloor::map_mem_ptr(uint32_t addr, size_t size, void* host) {
    return uc_mem_map_ptr(reinterpret_cast<uc_engine*>(uc_), addr, size, UC_PROT_ALL, host) ==
           UC_ERR_OK;
}
bool UnicornFloor::write_mem(uint32_t addr, const void* data, size_t size) {
    return uc_mem_write(reinterpret_cast<uc_engine*>(uc_), addr, data, size) == UC_ERR_OK;
}
bool UnicornFloor::read_mem(uint32_t addr, void* data, size_t size) {
    return uc_mem_read(reinterpret_cast<uc_engine*>(uc_), addr, data, size) == UC_ERR_OK;
}

void UnicornFloor::set_regs(const X86Regs& r) {
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    struct { int id; uint32_t v; } regs[] = {
        {UC_X86_REG_EAX, r.eax}, {UC_X86_REG_ECX, r.ecx}, {UC_X86_REG_EDX, r.edx},
        {UC_X86_REG_EBX, r.ebx}, {UC_X86_REG_ESP, r.esp}, {UC_X86_REG_EBP, r.ebp},
        {UC_X86_REG_ESI, r.esi}, {UC_X86_REG_EDI, r.edi}, {UC_X86_REG_EIP, r.eip},
        {UC_X86_REG_EFLAGS, r.eflags},
    };
    for (auto& reg : regs) {
        uint32_t v = reg.v;
        uc_reg_write(uc, reg.id, &v);
    }
}

X86Regs UnicornFloor::get_regs() {
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    X86Regs r;
    struct { int id; uint32_t* out; } regs[] = {
        {UC_X86_REG_EAX, &r.eax}, {UC_X86_REG_ECX, &r.ecx}, {UC_X86_REG_EDX, &r.edx},
        {UC_X86_REG_EBX, &r.ebx}, {UC_X86_REG_ESP, &r.esp}, {UC_X86_REG_EBP, &r.ebp},
        {UC_X86_REG_ESI, &r.esi}, {UC_X86_REG_EDI, &r.edi}, {UC_X86_REG_EIP, &r.eip},
        {UC_X86_REG_EFLAGS, &r.eflags},
    };
    for (auto& reg : regs) uc_reg_read(uc, reg.id, reg.out);
    return r;
}

static int xreg_to_uc(XReg r) {
    switch (r) {
        case XReg::CR0: return UC_X86_REG_CR0;
        case XReg::CR4: return UC_X86_REG_CR4;
        case XReg::MXCSR: return UC_X86_REG_MXCSR;
        case XReg::XMM0: return UC_X86_REG_XMM0;
        case XReg::XMM1: return UC_X86_REG_XMM1;
        case XReg::XMM2: return UC_X86_REG_XMM2;
        case XReg::ST0: return UC_X86_REG_ST0;
        case XReg::ST1: return UC_X86_REG_ST1;
        case XReg::MM0: return UC_X86_REG_MM0;
    }
    return -1;
}

bool UnicornFloor::read_reg(XReg r, void* buf, size_t) {
    int id = xreg_to_uc(r);
    if (id < 0) return false;
    return uc_reg_read(reinterpret_cast<uc_engine*>(uc_), id, buf) == UC_ERR_OK;
}
bool UnicornFloor::write_reg(XReg r, const void* buf, size_t) {
    int id = xreg_to_uc(r);
    if (id < 0) return false;
    return uc_reg_write(reinterpret_cast<uc_engine*>(uc_), id, const_cast<void*>(buf)) ==
           UC_ERR_OK;
}

bool UnicornFloor::setup_flat_segments(uint32_t gdt_addr) {
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    // 3 descriptors: null, flat 32-bit code (0x08), flat 32-bit data (0x10).
    uint64_t gdt[3] = {
        0x0000000000000000ull,
        0x00CF9A000000FFFFull,  // base 0, limit 4GB, code exec/read, G=1 D=1
        0x00CF92000000FFFFull,  // base 0, limit 4GB, data read/write, G=1 B=1
    };
    if (uc_mem_write(uc, gdt_addr, gdt, sizeof(gdt)) != UC_ERR_OK) return false;
    uc_x86_mmr gdtr{};
    gdtr.base = gdt_addr;
    gdtr.limit = sizeof(gdt) - 1;
    if (uc_reg_write(uc, UC_X86_REG_GDTR, &gdtr) != UC_ERR_OK) return false;
    int cs = 0x08, ds = 0x10;
    uc_reg_write(uc, UC_X86_REG_CS, &cs);
    for (int r : {UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_FS, UC_X86_REG_GS})
        uc_reg_write(uc, r, &ds);
    return true;
}

void UnicornFloor::on_io(IoInCb in_cb, IoOutCb out_cb) {
    io_in_cb_ = std::move(in_cb);
    io_out_cb_ = std::move(out_cb);
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    // UC_HOOK_INSN with a specific x86 instruction id (IN/OUT). Registered once, when the
    // servicing callbacks are installed.
    if (!hook_in_) {
        uc_hook h = 0;
        uc_hook_add(uc, &h, UC_HOOK_INSN, reinterpret_cast<void*>(&UnicornFloor::in_trampoline),
                    this, 1, 0, UC_X86_INS_IN);
        hook_in_ = from_hook(h);
    }
    if (!hook_out_) {
        uc_hook h = 0;
        uc_hook_add(uc, &h, UC_HOOK_INSN, reinterpret_cast<void*>(&UnicornFloor::out_trampoline),
                    this, 1, 0, UC_X86_INS_OUT);
        hook_out_ = from_hook(h);
    }
}

bool UnicornFloor::map_mmio(uint32_t addr, size_t size, MmioReadCb r, MmioWriteCb w) {
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    auto region = std::make_unique<MmioRegion>();
    region->self = this;
    region->base = addr;
    region->r = std::move(r);
    region->w = std::move(w);
    MmioRegion* raw = region.get();
    uc_err e = uc_mmio_map(uc, addr, size, &UnicornFloor::mmio_read_trampoline, raw,
                           &UnicornFloor::mmio_write_trampoline, raw);
    if (e != UC_ERR_OK) return false;
    mmio_regions_.push_back(std::move(region));
    return true;
}

void UnicornFloor::request_stop(std::string reason) {
    stop_reason_ = std::move(reason);
    uc_emu_stop(reinterpret_cast<uc_engine*>(uc_));
}

bool UnicornFloor::interrupts_enabled() {
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    uint32_t eflags = 0;
    uc_reg_read(uc, UC_X86_REG_EFLAGS, &eflags);
    return (eflags & 0x200u) != 0;  // IF
}

bool UnicornFloor::translate(uint32_t linear, uint32_t& phys) {
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    uint32_t cr0 = 0;
    uc_reg_read(uc, UC_X86_REG_CR0, &cr0);
    if (!(cr0 & 0x80000000u)) { phys = linear; return true; }  // paging off -> identity
    uint32_t cr3 = 0;
    uc_reg_read(uc, UC_X86_REG_CR3, &cr3);
    uint32_t pde = 0;
    if (uc_mem_read(uc, (cr3 & 0xFFFFF000u) + ((linear >> 22) << 2), &pde, 4) != UC_ERR_OK)
        return false;
    if (!(pde & 1)) return false;
    if (pde & 0x80) {  // 4 MB large page
        phys = (pde & 0xFFC00000u) | (linear & 0x003FFFFFu);
        return true;
    }
    uint32_t pte = 0;
    if (uc_mem_read(uc, (pde & 0xFFFFF000u) + (((linear >> 12) & 0x3FF) << 2), &pte, 4) !=
        UC_ERR_OK)
        return false;
    if (!(pte & 1)) return false;
    phys = (pte & 0xFFFFF000u) | (linear & 0xFFFu);
    return true;
}

bool UnicornFloor::inject_interrupt(uint8_t vector, std::string& err) {
    // Replay a 32-bit protected-mode interrupt-gate entry (same privilege, no error code), the
    // sequence do_interrupt_protected() would run natively (see the interrupt-injection notes).
    // Assumes a flat ring-0 SS (base 0) — true for the Xbox kernel's GDT.
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);

    uc_x86_mmr idtr{};
    uc_reg_read(uc, UC_X86_REG_IDTR, &idtr);
    if (static_cast<uint64_t>(vector) * 8 + 7 > idtr.limit) return false;  // gate not in IDT
    uint32_t gate_phys = 0;
    if (!translate(static_cast<uint32_t>(idtr.base) + vector * 8, gate_phys)) return false;
    uint8_t gate[8] = {0};
    if (uc_mem_read(uc, gate_phys, gate, 8) != UC_ERR_OK) return false;
    uint8_t attr = gate[5];
    if (!(attr & 0x80)) return false;  // present bit clear -> nothing to deliver
    uint32_t handler = gate[0] | (gate[1] << 8) | (static_cast<uint32_t>(gate[6]) << 16) |
                       (static_cast<uint32_t>(gate[7]) << 24);
    uint16_t selector = gate[2] | (gate[3] << 8);
    int type = attr & 0x0F;  // 0xE = interrupt gate, 0xF = trap gate

    uint32_t eip = 0, esp = 0, eflags = 0, cs = 0;
    uc_reg_read(uc, UC_X86_REG_EIP, &eip);
    uc_reg_read(uc, UC_X86_REG_ESP, &esp);
    uc_reg_read(uc, UC_X86_REG_EFLAGS, &eflags);
    uc_reg_read(uc, UC_X86_REG_CS, &cs);

    // Push EFLAGS, CS, EIP (dwords), no error code for a hardware IRQ. ESP is a linear address;
    // translate each slot to physical for the write (assumes a flat SS, true for the kernel).
    uint32_t frame[3] = {eflags, cs, eip};
    const char* what[3] = {"irq push eflags", "irq push cs", "irq push eip"};
    for (int i = 0; i < 3; ++i) {
        esp -= 4;
        uint32_t p = 0;
        if (!translate(esp, p) || uc_mem_write(uc, p, &frame[i], 4) != UC_ERR_OK) {
            err = what[i];
            return false;
        }
    }
    uc_reg_write(uc, UC_X86_REG_ESP, &esp);

    uint32_t new_eflags = eflags & ~(0x100u | 0x20000u | 0x10000u | 0x4000u);  // TF,VM,RF,NT
    if ((type & 1) == 0) new_eflags &= ~0x200u;  // interrupt gate clears IF
    uc_reg_write(uc, UC_X86_REG_EFLAGS, &new_eflags);

    uint32_t new_cs = selector & ~3u;  // RPL forced to CPL0
    uc_reg_write(uc, UC_X86_REG_CS, &new_cs);
    uc_reg_write(uc, UC_X86_REG_EIP, &handler);
    return true;
}

uint64_t UnicornFloor::run(uint32_t start_eip, uint64_t max_insns, std::string& err) {
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    const uint64_t before = seq_;
    stop_reason_.clear();
    last_fault_addr_ = 0;
    last_fault_type_ = 0;
    uc_err e = uc_emu_start(uc, start_eip, 0, /*timeout=*/0, /*count=*/max_insns);
    if (!stop_reason_.empty()) {
        // A device/servicing callback requested a loud stop; its reason wins over Unicorn's
        // generic "stopped" code.
        X86Regs r = get_regs();
        char buf[320];
        std::snprintf(buf, sizeof(buf), "%s (eip=0x%08x, after %llu insns)", stop_reason_.c_str(),
                      r.eip, static_cast<unsigned long long>(seq_ - before));
        err = buf;
    } else if (e != UC_ERR_OK) {
        X86Regs r = get_regs();
        char buf[320];
        if (last_fault_addr_) {
            const char* what = last_fault_type_ == UC_MEM_WRITE_UNMAPPED ? "write"
                             : last_fault_type_ == UC_MEM_FETCH_UNMAPPED ? "fetch" : "read";
            std::snprintf(buf, sizeof(buf),
                          "unicorn fault: %s at unmapped %s addr=0x%08x (eip=0x%08x, after %llu "
                          "insns)", uc_strerror(e), what, last_fault_addr_, r.eip,
                          static_cast<unsigned long long>(seq_ - before));
        } else {
            std::snprintf(buf, sizeof(buf), "unicorn fault: %s (eip=0x%08x, after %llu insns)",
                          uc_strerror(e), r.eip,
                          static_cast<unsigned long long>(seq_ - before));
        }
        err = buf;
    }
    return seq_ - before;
}

void UnicornFloor::code_trampoline(uc_struct* uc, uint64_t addr, uint32_t size, void* user) {
    auto* self = static_cast<UnicornFloor*>(user);
    RetiredInsn ev{self->seq_++, static_cast<uint32_t>(addr), size};
    if (self->retired_cb_) self->retired_cb_(ev);

    // If an IRQ is waiting, stop the moment the guest has interrupts enabled so the controller
    // can deliver it at this boundary (before `addr` executes). This catches the narrow window
    // the idle loop's `sti; nop; cli` opens each iteration — a coarse chunk boundary misses it.
    if (self->irq_pending_) {
        uint32_t eflags = 0;
        uc_reg_read(reinterpret_cast<uc_engine*>(uc), UC_X86_REG_EFLAGS, &eflags);
        if (eflags & 0x200u) uc_emu_stop(reinterpret_cast<uc_engine*>(uc));
    }
}

void UnicornFloor::mem_trampoline(uc_struct*, int type, uint64_t addr, int size, int64_t value,
                                  void* user) {
    auto* self = static_cast<UnicornFloor*>(user);
    MemEvent ev{};
    ev.seq = self->seq_;
    ev.write = (type == UC_MEM_WRITE) ? 1 : 0;
    ev.addr = static_cast<uint32_t>(addr);
    ev.size = static_cast<uint8_t>(size);
    ev.value = static_cast<uint64_t>(value);
    if (self->mem_cb_) self->mem_cb_(ev);
}

uint32_t UnicornFloor::in_trampoline(uc_struct*, uint32_t port, int size, void* user) {
    auto* self = static_cast<UnicornFloor*>(user);
    return self->io_in_cb_ ? self->io_in_cb_(static_cast<uint16_t>(port), size) : 0;
}

void UnicornFloor::out_trampoline(uc_struct*, uint32_t port, int size, uint32_t value,
                                  void* user) {
    auto* self = static_cast<UnicornFloor*>(user);
    if (self->io_out_cb_) self->io_out_cb_(static_cast<uint16_t>(port), size, value);
}

void UnicornFloor::intr_trampoline(uc_struct*, uint32_t intno, void* user) {
    auto* self = static_cast<UnicornFloor*>(user);
    if (self->intr_cb_) self->intr_cb_(intno);

    // Vectors whose architectural frame includes an error code (#DF, #TS, #NP, #SS, #GP,
    // #PF, #AC). inject_interrupt() builds the no-error-code frame, and for #PF CR2 would
    // also matter — deliver those only once a real boot path needs them (characterize first).
    // Note: for software INTs Unicorn has already advanced EIP to the next instruction (the
    // architecturally correct return address); for faults EIP stays at the faulting
    // instruction, which is likewise what the frame should hold.
    switch (intno) {
        case 8: case 10: case 11: case 12: case 13: case 14: case 17: {
            char b[96];
            std::snprintf(b, sizeof(b),
                          "guest CPU exception vector %u (error-code class) — delivery unmodeled",
                          intno);
            self->request_stop(b);
            return;
        }
        default:
            break;
    }
    std::string err;
    if (!self->inject_interrupt(static_cast<uint8_t>(intno & 0xFF), err)) {
        char b[128];
        std::snprintf(b, sizeof(b), "guest vector %u: IDT delivery failed (%s)", intno,
                      err.empty() ? "gate absent/invalid" : err.c_str());
        self->request_stop(b);
    }
}

bool UnicornFloor::unmapped_trampoline(uc_struct*, int type, uint64_t addr, int, int64_t,
                                       void* user) {
    auto* self = static_cast<UnicornFloor*>(user);
    self->last_fault_addr_ = static_cast<uint32_t>(addr);
    self->last_fault_type_ = type;
    return false;  // do not resolve: let Unicorn take the fault (we only record it)
}

uint64_t UnicornFloor::mmio_read_trampoline(uc_struct*, uint64_t offset, unsigned size,
                                            void* user) {
    auto* reg = static_cast<MmioRegion*>(user);
    return reg->r ? reg->r(reg->base + static_cast<uint32_t>(offset), static_cast<int>(size)) : 0;
}

void UnicornFloor::mmio_write_trampoline(uc_struct*, uint64_t offset, unsigned size,
                                         uint64_t value, void* user) {
    auto* reg = static_cast<MmioRegion*>(user);
    if (reg->w)
        reg->w(reg->base + static_cast<uint32_t>(offset), static_cast<int>(size),
               static_cast<uint32_t>(value));
}

// Silence unused-parameter warnings for the hook handles we retain for lifetime clarity.
static_assert(sizeof(void*) >= sizeof(uc_hook), "uc_hook must fit in void* storage");

}  // namespace xlle
