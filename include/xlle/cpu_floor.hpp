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

// Callbacks fired by the floor as it executes, so the platform can feed its rings without
// the floor knowing about ring types.
using RetiredCb = std::function<void(const RetiredInsn&)>;
using MemCb     = std::function<void(const MemEvent&)>;

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

    // Instrumentation. Installed once; fired for every retired instruction / memory access.
    virtual void on_retired(RetiredCb cb) = 0;
    virtual void on_mem(MemCb cb) = 0;

    // Execute up to `max_insns` instructions starting at the current EIP. Returns the
    // number actually retired. `err` is set to a human-readable reason on fault-stop
    // (PRINCIPLES §3: a halt records the exact context, never a silent default).
    virtual uint64_t run(uint32_t start_eip, uint64_t max_insns, std::string& err) = 0;
};

}  // namespace xlle
