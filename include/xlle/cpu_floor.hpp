// xlle — CPU floor abstraction (DESIGN §3/§4)
//
// The "CPU floor" is the truth-tier execution engine for guest IA-32. A0 evaluates two
// candidate implementations behind this one interface:
//   * UnicornFloor  — harvested QEMU x86 core (primary candidate)
//   * (future) InterpFloor — a from-scratch checked interpreter (fallback / cross-check)
//
// Keeping them behind an identical interface is what lets the spike diff one against the
// other later, and lets the rest of the platform depend on neither concretely.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace xlle {

// Integer interchange state. x87/MMX/SSE and hidden CPU state stay in the floor;
// read_reg/write_reg expose the explicit wide-register diagnostics below.
struct X86Regs {
    uint32_t eax = 0, ecx = 0, edx = 0, ebx = 0;
    uint32_t esp = 0, ebp = 0, esi = 0, edi = 0;
    uint32_t eip = 0, eflags = 0;
};

// Ring event records (kept POD and small — they live in always-on rings).
struct RetiredInsn {
    uint64_t seq;   // global retirement index
    uint32_t eip;   // linear PC of the retired instruction
    uint32_t size;  // instruction length in bytes
};
struct MemEvent {
    uint64_t seq;
    uint8_t  write;  // 0 = read, 1 = write
    uint32_t addr;
    uint8_t  size;   // access width in bytes
    uint64_t value;
};
struct IoEvent {
    uint64_t seq;
    uint8_t  out;    // 0 = in (port read), 1 = out (port write)
    uint16_t port;
    uint8_t  size;   // access width in bytes (1/2/4)
    uint32_t value;  // value written (out) or returned (in)
};

// Wide / control registers addressed by name for typed read/write. Added as the fixture
// corpus reaches x87/SSE and the control bits needed to enable them (PRINCIPLES §5: scope
// by trace — the fixtures are that trace).
enum class XReg { CR0, CR4, MXCSR, XMM0, XMM1, XMM2, ST0, ST1, MM0,
    CR3, XMM3, XMM4, XMM5, XMM6, XMM7, ST2, ST3, ST4, ST5, ST6, ST7,
    FPCW, FPSW, FPTAG, CS, DS, SS, IDTR, DR7, FIP, FDP, FCS, FDS, FOP };

struct X86DescriptorTable {
    uint16_t selector = 0;
    uint64_t base = 0;
    uint32_t limit = 0;
    uint32_t flags = 0;
};

// Callbacks fired by the floor as it executes, so the platform can feed its rings without
// the floor knowing about ring types.
using RetiredCb = std::function<void(const RetiredInsn&)>;
using MemCb     = std::function<void(const MemEvent&)>;

// Guest interrupt/exception observer: fired when the guest raises a vector internally
// (software INT n, INT3, INTO, or a CPU exception) BEFORE it is delivered through the guest
// IDT. Purely observational — delivery proceeds regardless.
using IntrCb = std::function<void(uint32_t vector)>;

// I/O-port servicing. Unlike memory, port space is not backed by RAM — the floor cannot
// answer an `in` on its own, so `IoInCb` IS the functional read path (its return value is
// what the guest reads), and `IoOutCb` is the functional write path. The platform routes
// these to device models and fault-stops loudly on an unhandled port (PRINCIPLES §3).
using IoInCb  = std::function<uint32_t(uint16_t port, int size)>;
using IoOutCb = std::function<void(uint16_t port, int size, uint32_t value)>;

// Memory-mapped I/O servicing for a device region (e.g. NV2A at 0xFD000000). Same contract:
// the callbacks ARE the functional path; the region is not backed by RAM.
using MmioReadCb  = std::function<uint32_t(uint32_t addr, int size)>;
using MmioWriteCb = std::function<void(uint32_t addr, int size, uint32_t value)>;

class CpuFloor {
public:
    virtual ~CpuFloor() = default;

    virtual const char* name() const = 0;

    // Machine setup.
    virtual bool map_mem(uint32_t addr, size_t size) = 0;
    // Map a caller-owned host buffer as guest RAM. Mapping the SAME buffer at two guest bases
    // models address-line aliasing (the 64 MB Xbox mirrors 0x04000000-0x07FFFFFF onto 0x0).
    virtual bool map_mem_ptr(uint32_t addr, size_t size, void* host) = 0;
    virtual bool write_mem(uint32_t addr, const void* data, size_t size) = 0;
    virtual bool read_mem(uint32_t addr, void* data, size_t size) = 0;

    virtual void set_regs(const X86Regs& r) = 0;
    virtual X86Regs get_regs() = 0;

    // Typed access to wide/control registers (n bytes: CR*/MXCSR=4, ST*/MM*=10/8, XMM*=16).
    // Returns false if the register is unsupported by this floor.
    virtual bool read_reg(XReg r, void* buf, size_t n) = 0;
    virtual bool write_reg(XReg r, const void* buf, size_t n) = 0;

    // Install a standard flat protected-mode GDT (code=0x08, data=0x10) at gdt_addr and
    // load GDTR + segment registers — reproduces the MCPX->2BL handoff environment.
    virtual bool setup_flat_segments(uint32_t gdt_addr) = 0;

    // Instrumentation. Installed once; fired for every retired instruction / memory access.
    virtual void on_retired(RetiredCb cb) = 0;
    virtual void on_mem(MemCb cb) = 0;
    // Observer for guest-raised vectors (soft INT / CPU exception); see IntrCb.
    virtual void on_interrupt(IntrCb cb) { (void)cb; }

    // Install the I/O-port servicing callbacks (see IoInCb/IoOutCb). Installed once.
    virtual void on_io(IoInCb in_cb, IoOutCb out_cb) = 0;

    // Register a device MMIO region; reads/writes in [addr, addr+size) route to the callbacks
    // instead of RAM. Returns false if the region could not be mapped.
    virtual bool map_mmio(uint32_t addr, size_t size, MmioReadCb r, MmioWriteCb w) = 0;
    // Exact number of instructions preceding the current synchronous MMIO
    // instruction. Available only within that instruction's device callback;
    // failure leaves clock untouched. Queries never retire work or restore CPU state.
    virtual bool mmio_instruction_clock(uint64_t& clock) const { (void)clock; return false; }

    // Request a loud fault-stop from within a device/servicing callback: emulation halts and
    // `reason` is surfaced by run()'s `err`. This is how a device reports "unimplemented"
    // (PRINCIPLES §3: a halt records the exact context, never a silent default).
    virtual void request_stop(std::string reason) = 0;

    // Are maskable interrupts currently enabled (EFLAGS.IF set)? The interrupt controller
    // consults this before delivering an external IRQ.
    virtual bool interrupts_enabled() = 0;

    // Tell the floor an external IRQ is waiting. While set, the floor stops execution at the
    // first instruction boundary where IF is enabled, so the controller can deliver the IRQ in
    // exactly the window the guest opened (e.g. the `sti; nop; cli` idle loop) instead of only
    // at coarse chunk boundaries. Cleared by the controller once delivered.
    //
    // A block-granularity floor checks IF at translation-block entry instead of before every
    // instruction. That is not a weakening: in the harvested QEMU x86 translator every
    // instruction that can raise IF (`sti`, `popf`, `iret`) ends its block, so IF can only
    // transition 0->1 at a block boundary, and set_irq_pending() itself is only called at a
    // block boundary (between execution slices). See src/cpu/unicorn_floor.cpp.
    virtual void set_irq_pending(bool pending) = 0;

    // Did the last run() stop because the guest opened its interrupt window (IF became set
    // while an IRQ was armed) rather than because the instruction budget was reached? The
    // machine's slice census uses this instead of `retired < budget`, which a block-granularity
    // floor trips on every slice by construction (a slice ends at the last whole block that
    // fits the budget).
    virtual bool stopped_for_irq_window() const { return false; }

    // Deliver an external hardware interrupt: synthesize the CPU's protected-mode interrupt-gate
    // entry for `vector` (read the IDT via IDTR, push EFLAGS/CS/EIP with no error code, clear IF
    // for an interrupt gate, load handler CS:EIP). Unicorn has no native IRQ facility, so this
    // replays the microcode manually. Call only at an instruction boundary with IF set. Returns
    // false (delivering nothing) if the gate is absent/not-present. `err` is set on a hard error.
    virtual bool inject_interrupt(uint8_t vector, std::string& err) = 0;

    // Translate a guest linear address to physical via the guest page tables (identity if
    // paging is off). Useful for reading guest structures that live in non-identity mappings.
    virtual bool translate(uint32_t linear, uint32_t& phys) = 0;

    // Advance the floor's retired-instruction clock by `insns` WITHOUT executing anything,
    // and republish it as the guest TSC. This exists for one caller: an exactness-preserving
    // fast-forward over a guest loop the platform has proved has no guest-visible effect for
    // the whole skipped range (the kernel idle loop -- see XboxMachine::try_idle_fast_forward).
    // It is not a general "skip instructions" facility: the caller owns the proof. Also clears
    // any stopped_for_irq_window() latch, because no run() happened. Returns false on a floor
    // that has no such clock, in which case the caller must not skip.
    virtual bool advance_instruction_clock(uint64_t insns) { (void)insns; return false; }

    // Qualification for a whole idle-loop iteration with unchanged hidden state.
    // Floors must explicitly certify flat ring-0 execution, the expected IRQ
    // shadow, and absence of debug/observer/stop conditions. No effects on refusal.
    virtual bool idle_skip_context_ok(bool irq_shadow) { (void)irq_shadow; return false; }
    // Inspect only already-cached guest RAM, with the actual fetch/read permission.
    // Never fill a TLB, read MMIO, or update page-table accessed bits.
    virtual bool read_cached_ram(uint32_t linear, void* data, size_t size, bool fetch) {
        (void)linear; (void)data; (void)size; (void)fetch; return false;
    }

    // Execute up to `max_insns` instructions starting at the current EIP. Returns the
    // number actually retired. `err` is set to a human-readable reason on fault-stop
    // (PRINCIPLES §3: a halt records the exact context, never a silent default).
    //
    // `max_insns` is an upper bound. The Unicorn floor splits a translated tail
    // to meet it exactly; faults, explicit stops and interrupt windows may end
    // earlier. Treat the actual return value as authoritative.
    virtual uint64_t run(uint32_t start_eip, uint64_t max_insns, std::string& err) = 0;
};

}  // namespace xlle
