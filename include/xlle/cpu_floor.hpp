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

// A0 scope: general-purpose registers + EIP + EFLAGS. x87/MMX/SSE state joins this struct
// when a boot/game trace first needs it (PRINCIPLES §5: scope by trace, not by the manual).
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
enum class XReg { CR0, CR4, MXCSR, XMM0, XMM1, XMM2, ST0, ST1, MM0 };

// Callbacks fired by the floor as it executes, so the platform can feed its rings without
// the floor knowing about ring types.
using RetiredCb = std::function<void(const RetiredInsn&)>;
using MemCb     = std::function<void(const MemEvent&)>;

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

    // Install the I/O-port servicing callbacks (see IoInCb/IoOutCb). Installed once.
    virtual void on_io(IoInCb in_cb, IoOutCb out_cb) = 0;

    // Register a device MMIO region; reads/writes in [addr, addr+size) route to the callbacks
    // instead of RAM. Returns false if the region could not be mapped.
    virtual bool map_mmio(uint32_t addr, size_t size, MmioReadCb r, MmioWriteCb w) = 0;

    // Request a loud fault-stop from within a device/servicing callback: emulation halts and
    // `reason` is surfaced by run()'s `err`. This is how a device reports "unimplemented"
    // (PRINCIPLES §3: a halt records the exact context, never a silent default).
    virtual void request_stop(std::string reason) = 0;

    // Execute up to `max_insns` instructions starting at the current EIP. Returns the
    // number actually retired. `err` is set to a human-readable reason on fault-stop
    // (PRINCIPLES §3: a halt records the exact context, never a silent default).
    virtual uint64_t run(uint32_t start_eip, uint64_t max_insns, std::string& err) = 0;
};

}  // namespace xlle
