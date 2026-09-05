// whpx_bench — what does Windows Hypervisor Platform actually cost on this host?
//
// Tracker: beads-xbox.1.16.2. Companion to tools/bench/unicorn_hook_bench.cpp (TCG throughput)
// and tools/bench/block_size_bench.cpp (per-block cost decomposition).
//
// The CPU-tier replacement decision (docs/PERF-METHODOLOGY.md section 1 item 11) turns on
// whether hardware virtualization's floor is below the TCG floor for THIS workload. Guest
// instructions run at native speed under WHPX, so the floor is set almost entirely by VM
// EXITS. The canonical 12 B replay takes 17 546 710 MMIO dispatches and 41 951 I/O port
// accesses ([perf-excl] cumulative, build/p4-perf-B.stdout.log), so the question is arithmetic
// once the per-exit cost is known -- and that is what this measures.
//
// It runs, on one vCPU in 32-bit protected mode with 64 MB of guest RAM:
//   1. TIGHT / MIXED / IDLE  -- the same three workloads unicorn_hook_bench.cpp uses, so the
//                               insn/s numbers are directly comparable to the TCG table.
//   2. MMIO exit round trip  -- a loop storing to an UNMAPPED GPA, against an identical loop
//                               storing to mapped RAM. The difference is the marginal cost of
//                               one EPT/NPT exit. Measured two ways, because they bound a real
//                               implementation from both sides:
//                                 bare     -- host decodes nothing, just advances RIP. The
//                                             absolute floor; no device could be slower.
//                                 emulated -- WHvEmulatorTryMmioEmulation, the WinHvEmulation
//                                             instruction emulator, which is what an
//                                             implementation that does not write its own x86
//                                             decoder would actually pay.
//   3. I/O port exit round trip -- `out dx, eax` against a RAM-store control.
//   4. HLT exit, and HLT exit + interrupt injection via the pending-interruption register
//      (the VMCB/VMCS event-injection field), with a real IDT in guest RAM. The difference is
//      the marginal cost of delivering one interrupt.
//
// Every arm reports its own exit count and verifies the guest's final architectural state, so
// a configuration that silently executed something else fails rather than producing a fast
// number. Interleaved repetitions, min and median both reported (section 2 rules 3 and 4).
//
// Build: tools/bench/build_whpx.ps1

#include <windows.h>
#include <winhvplatform.h>
#include <winhvemulation.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Guest memory map
// ---------------------------------------------------------------------------
static const uint64_t kRamBase  = 0x00000000ull;
static const size_t   kRamSize  = 64ull * 1024 * 1024;  // 64 MB, as on the Xbox

static const uint32_t kGdtBase  = 0x00001000u;
static const uint32_t kIdtBase  = 0x00002000u;
static const uint32_t kCodeBase = 0x00100000u;
static const uint32_t kDataBase = 0x00200000u;
static const uint32_t kTableAddr= 0x00220000u;
static const uint32_t kScratch  = 0x00230000u;
static const uint32_t kIdleCell = 0x00240000u;
static const uint32_t kStackTop = 0x00300000u;

// Unmapped: a store here raises WHvRunVpExitReasonMemoryAccess. Chosen above the 64 MB of RAM
// so it cannot alias anything, mirroring the NV2A aperture's position above guest RAM.
static const uint32_t kMmioAddr = 0x04000000u;
static const uint16_t kIoPort   = 0x8000u;
// `mov [disp32], eax` is opcode 89, ModRM 05 (mod=00 reg=000 rm=101), then a 4-byte
// displacement = SIX bytes. The bare MMIO arm advances RIP by this known constant; see the
// long note at its use site for why it cannot ask the hypervisor for it.
static const uint32_t kMmioStoreInsnLen = 6;

// ---------------------------------------------------------------------------
// Tiny x86-32 emitter (same instruction shapes as unicorn_hook_bench.cpp, so the workloads
// are the same programs and the insn/s numbers are comparable).
// ---------------------------------------------------------------------------
enum Reg { EAX = 0, ECX = 1, EDX = 2, EBX = 3, ESP = 4, EBP = 5, ESI = 6, EDI = 7 };

struct Emitter {
    std::vector<uint8_t> buf;
    uint32_t base = kCodeBase;

    uint32_t here() const { return base + (uint32_t)buf.size(); }
    void b(uint8_t v) { buf.push_back(v); }
    void d32(uint32_t v) {
        b((uint8_t)(v & 0xFF)); b((uint8_t)((v >> 8) & 0xFF));
        b((uint8_t)((v >> 16) & 0xFF)); b((uint8_t)((v >> 24) & 0xFF));
    }
    void alu_rr(uint8_t op, Reg dst, Reg src) { b(op); b((uint8_t)(0xC0 | (src << 3) | dst)); }
    void alu_ri8(uint8_t ext, Reg dst, int8_t imm) {
        b(0x83); b((uint8_t)(0xC0 | (ext << 3) | dst)); b((uint8_t)imm);
    }
    void and_ri32(Reg dst, uint32_t imm) {
        b(0x81); b((uint8_t)(0xC0 | (4 << 3) | dst)); d32(imm);
    }
    void mov_mem_ebx_r(uint32_t disp, Reg src) { b(0x89); b((uint8_t)(0x83 | (src << 3))); d32(disp); }
    void mov_r_mem_ebx(Reg dst, uint32_t disp) { b(0x8B); b((uint8_t)(0x83 | (dst << 3))); d32(disp); }
    void mov_abs_r(uint32_t addr, Reg src) { b(0x89); b((uint8_t)(0x05 | (src << 3))); d32(addr); }
    void mov_r_table(Reg dst, uint32_t table, Reg idx) {
        b(0x8B); b((uint8_t)(0x04 | (dst << 3))); b((uint8_t)(0x80 | (idx << 3) | 0x05)); d32(table);
    }
    void mov_r_imm32(Reg r, uint32_t v) { b((uint8_t)(0xB8 | r)); d32(v); }
    void cmp_r_mem_ebp(Reg dst) { b(0x3B); b((uint8_t)(0x40 | (dst << 3) | 0x05)); b(0x00); }
    void dec_r(Reg r) { b((uint8_t)(0x48 | r)); }
    void inc_r(Reg r) { b((uint8_t)(0x40 | r)); }
    void nop() { b(0x90); }
    void sti() { b(0xFB); }
    void cli() { b(0xFA); }
    void hlt() { b(0xF4); }
    void ret() { b(0xC3); }
    void iret() { b(0xCF); }
    void out_dx_eax() { b(0xEF); }
    void jmp_r(Reg r) { b(0xFF); b((uint8_t)(0xC0 | (4 << 3) | r)); }

    size_t jcc32(uint8_t cc) { b(0x0F); b((uint8_t)(0x80 | cc)); size_t at = buf.size(); d32(0); return at; }
    size_t jmp32() { b(0xE9); size_t at = buf.size(); d32(0); return at; }
    size_t call32() { b(0xE8); size_t at = buf.size(); d32(0); return at; }
    void patch_rel32(size_t at, uint32_t target) {
        uint32_t next = base + (uint32_t)(at + 4);
        uint32_t rel = target - next;
        buf[at + 0] = (uint8_t)(rel & 0xFF);       buf[at + 1] = (uint8_t)((rel >> 8) & 0xFF);
        buf[at + 2] = (uint8_t)((rel >> 16) & 0xFF); buf[at + 3] = (uint8_t)((rel >> 24) & 0xFF);
    }
};

// ---------------------------------------------------------------------------
// Partition wrapper
// ---------------------------------------------------------------------------
struct Vm {
    WHV_PARTITION_HANDLE part = nullptr;
    uint8_t* ram = nullptr;
    std::string err;

    bool ok() const { return err.empty(); }
    void fail(const char* what, HRESULT hr) {
        char b[192];
        std::snprintf(b, sizeof(b), "%s: hr=0x%08lX", what, (unsigned long)hr);
        if (err.empty()) err = b;
    }

    bool create() {
        HRESULT hr = WHvCreatePartition(&part);
        if (FAILED(hr)) { fail("WHvCreatePartition", hr); return false; }
        WHV_PARTITION_PROPERTY p;
        std::memset(&p, 0, sizeof(p));
        p.ProcessorCount = 1;
        hr = WHvSetPartitionProperty(part, WHvPartitionPropertyCodeProcessorCount, &p, sizeof(p));
        if (FAILED(hr)) { fail("SetProcessorCount", hr); return false; }
        hr = WHvSetupPartition(part);
        if (FAILED(hr)) { fail("WHvSetupPartition", hr); return false; }
        hr = WHvCreateVirtualProcessor(part, 0, 0);
        if (FAILED(hr)) { fail("WHvCreateVirtualProcessor", hr); return false; }

        ram = (uint8_t*)VirtualAlloc(nullptr, kRamSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!ram) { fail("VirtualAlloc", (HRESULT)GetLastError()); return false; }
        hr = WHvMapGpaRange(part, ram, kRamBase, kRamSize,
                            (WHV_MAP_GPA_RANGE_FLAGS)(WHvMapGpaRangeFlagRead |
                                                      WHvMapGpaRangeFlagWrite |
                                                      WHvMapGpaRangeFlagExecute));
        if (FAILED(hr)) { fail("WHvMapGpaRange", hr); return false; }
        return true;
    }

    ~Vm() {
        if (part) {
            WHvDeleteVirtualProcessor(part, 0);
            if (ram) WHvUnmapGpaRange(part, kRamBase, kRamSize);
            WHvDeletePartition(part);
        }
        if (ram) VirtualFree(ram, 0, MEM_RELEASE);
    }

    void write(uint32_t gpa, const void* p, size_t n) { std::memcpy(ram + gpa, p, n); }
    void read(uint32_t gpa, void* p, size_t n) const { std::memcpy(p, ram + gpa, n); }
    void zero_ram() { std::memset(ram, 0, kRamSize); }
};

// Flat 32-bit protected mode, no paging. The hidden descriptor state is settable directly
// through WHPX, so no GDT has to exist in guest memory for the segments to be loaded --
// exactly the environment the MCPX->2BL handoff leaves behind.
static const uint16_t kCsAttr = 0xC09B;  // type=B code r/x accessed, S=1, P=1, D/B=1, G=1
static const uint16_t kDsAttr = 0xC093;  // type=3 data r/w accessed, S=1, P=1, D/B=1, G=1

struct InitRegs {
    uint32_t eax = 0x12345678, ecx = 0, edx = 0x9ABCDEF0, ebx = 0;
    uint32_t esp = kStackTop, ebp = 0, esi = 0x0F0F0F0F, edi = 0xDEADBEEF;
    uint32_t eip = kCodeBase;
    uint32_t eflags = 0x2;
    bool set_idt = false;
};

static bool set_initial_state(Vm& vm, const InitRegs& ir) {
    WHV_REGISTER_NAME names[24];
    WHV_REGISTER_VALUE vals[24];
    std::memset(vals, 0, sizeof(vals));
    int n = 0;
    auto put64 = [&](WHV_REGISTER_NAME nm, uint64_t v) {
        names[n] = nm; vals[n].Reg64 = v; ++n;
    };
    auto putseg = [&](WHV_REGISTER_NAME nm, uint16_t sel, uint16_t attr) {
        names[n] = nm;
        vals[n].Segment.Base = 0;
        vals[n].Segment.Limit = 0xFFFFFFFFu;
        vals[n].Segment.Selector = sel;
        vals[n].Segment.Attributes = attr;
        ++n;
    };

    put64(WHvX64RegisterRax, ir.eax);   put64(WHvX64RegisterRcx, ir.ecx);
    put64(WHvX64RegisterRdx, ir.edx);   put64(WHvX64RegisterRbx, ir.ebx);
    put64(WHvX64RegisterRsp, ir.esp);   put64(WHvX64RegisterRbp, ir.ebp);
    put64(WHvX64RegisterRsi, ir.esi);   put64(WHvX64RegisterRdi, ir.edi);
    put64(WHvX64RegisterRip, ir.eip);   put64(WHvX64RegisterRflags, ir.eflags);
    // CR0: PE (protected mode) + ET, PG clear -> flat physical addressing.
    put64(WHvX64RegisterCr0, 0x00000021ull);
    put64(WHvX64RegisterCr3, 0);
    put64(WHvX64RegisterCr4, 0);
    put64(WHvX64RegisterEfer, 0);

    putseg(WHvX64RegisterCs, 0x08, kCsAttr);
    putseg(WHvX64RegisterDs, 0x10, kDsAttr);
    putseg(WHvX64RegisterEs, 0x10, kDsAttr);
    putseg(WHvX64RegisterFs, 0x10, kDsAttr);
    putseg(WHvX64RegisterGs, 0x10, kDsAttr);
    putseg(WHvX64RegisterSs, 0x10, kDsAttr);

    // GDTR. Setting the hidden segment state directly is enough to ENTER protected mode, but
    // it is NOT enough to take an interrupt: delivering through a gate makes the CPU RELOAD CS
    // from the descriptor table, and with no GDT that raises #GP, which needs the GDT to
    // deliver, which triple-faults. So a real GDT has to exist in guest RAM for the injection
    // arm to work at all -- measured the hard way (exit reason 0x4 UnrecoverableException).
    names[n] = WHvX64RegisterGdtr;
    vals[n].Table.Base = kGdtBase;
    vals[n].Table.Limit = 3 * 8 - 1;
    ++n;

    // IDTR. Only the interrupt-injection arm installs a real table; the others still need a
    // defined IDTR so an unexpected fault reports cleanly instead of triple-faulting silently.
    names[n] = WHvX64RegisterIdtr;
    vals[n].Table.Base = ir.set_idt ? kIdtBase : 0;
    vals[n].Table.Limit = ir.set_idt ? (256 * 8 - 1) : 0;
    ++n;

    HRESULT hr = WHvSetVirtualProcessorRegisters(vm.part, 0, names, n, vals);
    if (FAILED(hr)) { vm.fail("WHvSetVirtualProcessorRegisters(init)", hr); return false; }
    return true;
}

static bool get_rip(Vm& vm, uint64_t& rip) {
    WHV_REGISTER_NAME nm = WHvX64RegisterRip;
    WHV_REGISTER_VALUE v;
    std::memset(&v, 0, sizeof(v));
    HRESULT hr = WHvGetVirtualProcessorRegisters(vm.part, 0, &nm, 1, &v);
    if (FAILED(hr)) return false;
    rip = v.Reg64;
    return true;
}
static bool set_rip(Vm& vm, uint64_t rip) {
    WHV_REGISTER_NAME nm = WHvX64RegisterRip;
    WHV_REGISTER_VALUE v;
    std::memset(&v, 0, sizeof(v));
    v.Reg64 = rip;
    return SUCCEEDED(WHvSetVirtualProcessorRegisters(vm.part, 0, &nm, 1, &v));
}

// ---------------------------------------------------------------------------
// Watchdog.
//
// A guest that loops forever produces NO exit, so WHvRunVirtualProcessor simply never returns
// and the benchmark hangs with no output. That is exactly what a mis-loaded image does (the
// bytes decode to something, and "something" is usually a loop), so it is not a hypothetical.
// A watchdog thread calls WHvCancelRunVirtualProcessor, which forces a Canceled exit that the
// run loops surface as a failure.
// ---------------------------------------------------------------------------
struct Watchdog {
    WHV_PARTITION_HANDLE part;
    HANDLE thread = nullptr;
    volatile LONG done = 0;
    DWORD timeout_ms;

    static DWORD WINAPI proc(LPVOID p) {
        Watchdog* w = (Watchdog*)p;
        const DWORD step = 50;
        DWORD waited = 0;
        while (waited < w->timeout_ms) {
            if (InterlockedCompareExchange(&w->done, 0, 0)) return 0;
            Sleep(step);
            waited += step;
        }
        if (!InterlockedCompareExchange(&w->done, 0, 0))
            WHvCancelRunVirtualProcessor(w->part, 0, 0);
        return 0;
    }
    Watchdog(WHV_PARTITION_HANDLE h, DWORD ms) : part(h), timeout_ms(ms) {
        thread = CreateThread(nullptr, 0, &Watchdog::proc, this, 0, nullptr);
    }
    ~Watchdog() {
        InterlockedExchange(&done, 1);
        if (thread) { WaitForSingleObject(thread, 2000); CloseHandle(thread); }
    }
};
static DWORD g_watchdog_ms = 30000;

// What the FIRST MMIO exit of the run actually reported. Printed with the results so the
// instruction-length question below is answered by this host rather than from the manual.
static bool     g_mmio_probe_seen = false;
static uint32_t g_mmio_probe_vp_insn_len = 0;
static uint32_t g_mmio_probe_ma_byte_count = 0;
static uint64_t g_mmio_probe_gpa = 0;
static uint32_t g_mmio_probe_access = 0;

struct Vm;
static void write_gdt(Vm& vm);  // defined below; used by both run paths

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
static LARGE_INTEGER g_freq;
static double secs(LARGE_INTEGER a, LARGE_INTEGER b) {
    return double(b.QuadPart - a.QuadPart) / double(g_freq.QuadPart);
}
static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (!n) return 0.0;
    return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}
static double vmin(const std::vector<double>& v) {
    return v.empty() ? 0.0 : *std::min_element(v.begin(), v.end());
}

// ---------------------------------------------------------------------------
// Workloads. Same programs as unicorn_hook_bench.cpp, terminated by HLT instead of by an
// `until` address (WHPX has no such facility; a halt exit is the natural stop).
// ---------------------------------------------------------------------------
struct Program {
    std::string name;
    std::vector<uint8_t> code;
    // The buffer always starts at kCodeBase; `entry` may be LATER than that when the program
    // emits a subroutine first (MIXED does). Loading the image at `entry` instead of at
    // code_base shifts every byte and the guest executes garbage -- which is a hang, not a
    // fault, so it must not be possible to get wrong twice.
    uint32_t code_base = kCodeBase;
    uint32_t entry = 0;
    uint64_t iters = 0;
    uint64_t insns_per_iter = 0;
    uint64_t epilogue_insns = 0;
    uint32_t table[4] = {0, 0, 0, 0};
    bool has_table = false;
    uint32_t ebp_init = 0;
    uint64_t expected_insns() const { return iters * insns_per_iter + epilogue_insns; }
};

static uint64_t emit_epilogue(Emitter& e) {
    e.mov_abs_r(kScratch + 0, EAX);   e.mov_abs_r(kScratch + 4, ECX);
    e.mov_abs_r(kScratch + 8, EDX);   e.mov_abs_r(kScratch + 12, EBX);
    e.mov_abs_r(kScratch + 16, ESI);  e.mov_abs_r(kScratch + 20, EDI);
    e.mov_abs_r(kScratch + 24, EBP);
    return 7;
}

static Program build_tight(uint64_t iters) {
    Emitter e; Program p; p.name = "TIGHT"; p.entry = e.here();
    uint32_t top = e.here();
    for (int g = 0; g < 10; ++g) {
        e.alu_rr(0x01, EAX, EBX); e.alu_rr(0x29, EDX, ESI); e.alu_rr(0x31, EDI, EAX);
        e.alu_rr(0x01, ESI, EDX); e.alu_rr(0x39, EAX, EDX); e.alu_rr(0x29, EAX, EDI);
        e.alu_rr(0x31, EDX, EBX); e.alu_rr(0x01, EDI, ESI); e.alu_rr(0x21, EBX, EAX);
        e.alu_rr(0x09, EBX, EDX);
    }
    e.dec_r(ECX);
    size_t back = e.jcc32(0x05); e.patch_rel32(back, top);
    p.insns_per_iter = 102;
    p.epilogue_insns = emit_epilogue(e);
    e.hlt();
    p.code = e.buf; p.iters = iters;
    return p;
}

static Program build_mixed(uint64_t iters) {
    Emitter e; Program p; p.name = "MIXED";
    uint32_t func = e.here(); e.ret();
    p.entry = e.here();
    uint32_t top = e.here();
    for (int blk = 0; blk < 20; ++blk) {
        e.mov_mem_ebx_r(kDataBase + 0x00, EAX); e.mov_r_mem_ebx(EAX, kDataBase + 0x10);
        e.alu_rr(0x01, EAX, EDX);
        e.mov_mem_ebx_r(kDataBase + 0x20, ESI); e.mov_r_mem_ebx(ESI, kDataBase + 0x30);
        e.alu_rr(0x31, ESI, EAX);
        e.alu_ri8(0, EBX, 16); e.and_ri32(EBX, 0x0000FFC0u);
    }
    size_t call_at = e.call32(); e.patch_rel32(call_at, func);
    e.mov_r_table(EDI, kTableAddr, EBP);
    e.jmp_r(EDI);
    uint32_t pad[4]; size_t pad_jmp[4];
    for (int i = 0; i < 4; ++i) { pad[i] = e.here(); e.nop(); pad_jmp[i] = e.jmp32(); }
    uint32_t cont = e.here();
    for (int i = 0; i < 4; ++i) e.patch_rel32(pad_jmp[i], cont);
    e.alu_ri8(0, EBP, 1); e.alu_ri8(4, EBP, 3);
    e.dec_r(ECX);
    size_t back = e.jcc32(0x05); e.patch_rel32(back, top);
    p.insns_per_iter = 170;
    p.epilogue_insns = emit_epilogue(e);
    e.hlt();
    p.code = e.buf; p.iters = iters; p.has_table = true;
    for (int i = 0; i < 4; ++i) p.table[i] = pad[i];
    return p;
}

static Program build_idle(uint64_t iters) {
    Emitter e; Program p; p.name = "IDLE"; p.entry = e.here();
    uint32_t top = e.here();
    e.sti(); e.nop(); e.nop(); e.cli();
    e.cmp_r_mem_ebp(EBP);
    size_t never = e.jcc32(0x05);
    e.dec_r(ECX);
    size_t back = e.jcc32(0x05); e.patch_rel32(back, top);
    p.insns_per_iter = 8;
    p.epilogue_insns = emit_epilogue(e);
    uint32_t after = e.here();
    e.patch_rel32(never, after);
    e.hlt();
    p.code = e.buf; p.iters = iters; p.ebp_init = kIdleCell;
    return p;
}

struct ExecResult {
    bool ok = false;
    std::string err;
    double seconds = 0.0;
    uint64_t exits = 0;
    uint32_t scratch[7] = {0, 0, 0, 0, 0, 0, 0};
};

// Run a workload to its terminating HLT. Any other exit reason is a failure: this benchmark
// must never report a fast number for a guest that stopped early.
static ExecResult run_program(Vm& vm, const Program& p) {
    ExecResult r;
    vm.zero_ram();
    write_gdt(vm);
    vm.write(p.code_base, p.code.data(), p.code.size());
    if (p.has_table) vm.write(kTableAddr, p.table, sizeof(p.table));
    { uint32_t v = kIdleCell; vm.write(kIdleCell, &v, 4); }

    InitRegs ir;
    ir.ecx = (uint32_t)p.iters;
    ir.ebp = p.ebp_init;
    ir.eip = p.entry;
    if (!set_initial_state(vm, ir)) { r.err = vm.err; return r; }

    Watchdog wd(vm.part, g_watchdog_ms);
    WHV_RUN_VP_EXIT_CONTEXT ex;
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    for (;;) {
        std::memset(&ex, 0, sizeof(ex));
        HRESULT hr = WHvRunVirtualProcessor(vm.part, 0, &ex, sizeof(ex));
        if (FAILED(hr)) {
            char b[128];
            std::snprintf(b, sizeof(b), "WHvRunVirtualProcessor: hr=0x%08lX", (unsigned long)hr);
            r.err = b;
            break;
        }
        r.exits++;
        if (ex.ExitReason == WHvRunVpExitReasonX64Halt) break;
        if (ex.ExitReason == WHvRunVpExitReasonCanceled) {
            r.err = "WATCHDOG: guest did not reach its terminating HLT (runaway or mis-loaded image)";
            break;
        }
        char b[192];
        std::snprintf(b, sizeof(b), "unexpected exit reason 0x%08lX at rip=0x%llX",
                      (unsigned long)ex.ExitReason,
                      (unsigned long long)ex.VpContext.Rip);
        r.err = b;
        break;
    }
    QueryPerformanceCounter(&t1);
    r.seconds = secs(t0, t1);
    vm.read(kScratch, r.scratch, sizeof(r.scratch));
    r.ok = r.err.empty();
    return r;
}

// ---------------------------------------------------------------------------
// Exit-cost arms.
//
// Each builds the SAME loop shape and differs only in the store target / instruction, so the
// marginal cost of the exit is a subtraction against the RAM-store control rather than an
// absolute number that includes loop overhead.
// ---------------------------------------------------------------------------
enum ExitArm {
    ARM_RAM = 0,      // control: mov [ram], eax     -- no exit
    ARM_MMIO_BARE,    // mov [unmapped], eax         -- exit, host advances RIP only
    ARM_MMIO_EMU,     // mov [unmapped], eax         -- exit, WHvEmulatorTryMmioEmulation
    ARM_IO,           // out dx, eax                 -- I/O port exit
    ARM_HLT,          // hlt                         -- halt exit, host advances RIP
    ARM_HLT_INTR,     // sti; hlt                    -- halt exit + interrupt injection
    NARM
};
static const char* kArmName[NARM] = {
    "RAM store (control)", "MMIO exit (bare)", "MMIO exit (emulated)",
    "IO port exit", "HLT exit", "HLT + intr inject",
};

// The loop body is one "event" instruction plus `dec ecx; jnz top`, so subtracting the RAM
// control removes the loop overhead exactly.
static std::vector<uint8_t> build_exit_loop(ExitArm arm, uint32_t& entry_out,
                                            uint32_t& handler_out) {
    Emitter e;
    handler_out = 0;

    if (arm == ARM_HLT_INTR) {
        // Interrupt handler: `inc edi; iret`. edi therefore counts delivered interrupts and
        // the guest's own state proves the injections landed.
        handler_out = e.here();
        e.inc_r(EDI);
        e.iret();
    }

    entry_out = e.here();
    if (arm == ARM_IO) e.mov_r_imm32(EDX, kIoPort);  // out uses dx

    uint32_t top = e.here();
    switch (arm) {
        case ARM_RAM:       e.mov_abs_r(kDataBase, EAX); break;
        case ARM_MMIO_BARE:
        case ARM_MMIO_EMU:  e.mov_abs_r(kMmioAddr, EAX); break;
        case ARM_IO:        e.out_dx_eax(); break;
        case ARM_HLT:       e.hlt(); break;
        case ARM_HLT_INTR:  e.sti(); e.hlt(); e.cli(); break;
        default: break;
    }
    e.dec_r(ECX);
    size_t back = e.jcc32(0x05);
    e.patch_rel32(back, top);
    // Terminating halt. For the HLT arms the loop's own halt is indistinguishable from this
    // one by exit reason, so those arms count exits and stop on the count instead (below).
    e.hlt();
    return e.buf;
}

// Flat 32-bit GDT: null, code (selector 0x08), data (selector 0x10). Base 0, limit 4 GB,
// granularity 4 KB, D/B = 32-bit -- the same descriptors the hidden state above encodes, so
// a CS reload during interrupt delivery lands on an identical segment.
static void write_gdt(Vm& vm) {
    static const uint8_t gdt[24] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // 0x00 null
        0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00,  // 0x08 code32 r/x
        0xFF, 0xFF, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00,  // 0x10 data32 r/w
    };
    vm.write(kGdtBase, gdt, sizeof(gdt));
}

// Build a 32-bit interrupt gate.
static void write_idt_entry(Vm& vm, int vec, uint32_t handler) {
    uint8_t d[8];
    d[0] = (uint8_t)(handler & 0xFF);
    d[1] = (uint8_t)((handler >> 8) & 0xFF);
    d[2] = 0x08; d[3] = 0x00;      // selector = code segment
    d[4] = 0x00;                    // reserved
    d[5] = 0x8E;                    // present, DPL 0, 32-bit interrupt gate
    d[6] = (uint8_t)((handler >> 16) & 0xFF);
    d[7] = (uint8_t)((handler >> 24) & 0xFF);
    vm.write(kIdtBase + vec * 8, d, 8);
}

struct ArmResult {
    bool ok = false;
    std::string err;
    double seconds = 0.0;
    uint64_t exits = 0;
    uint64_t delivered = 0;  // ARM_HLT_INTR: interrupts the guest actually took (edi delta)
};

static ArmResult run_exit_arm(Vm& vm, ExitArm arm, uint64_t iters) {
    ArmResult r;
    vm.zero_ram();
    write_gdt(vm);
    uint32_t entry = 0, handler = 0;
    std::vector<uint8_t> code = build_exit_loop(arm, entry, handler);
    vm.write(kCodeBase, code.data(), code.size());

    InitRegs ir;
    ir.ecx = (uint32_t)iters;
    ir.eip = entry;
    ir.edi = 0;  // ARM_HLT_INTR counts deliveries here
    if (arm == ARM_HLT_INTR) {
        ir.set_idt = true;
        for (int v = 0; v < 256; ++v) write_idt_entry(vm, v, handler);
    }
    if (!set_initial_state(vm, ir)) { r.err = vm.err; return r; }

    // The WinHvEmulation instruction emulator, used only by ARM_MMIO_EMU.
    WHV_EMULATOR_HANDLE emu = nullptr;
    struct EmuCtx { Vm* vm; } ectx{&vm};
    WHV_EMULATOR_CALLBACKS cbs;
    std::memset(&cbs, 0, sizeof(cbs));
    cbs.Size = sizeof(cbs);
    cbs.WHvEmulatorIoPortCallback = [](void*, WHV_EMULATOR_IO_ACCESS_INFO*) -> HRESULT {
        return S_OK;  // benchmark: accept and discard
    };
    cbs.WHvEmulatorMemoryCallback = [](void*, WHV_EMULATOR_MEMORY_ACCESS_INFO* ma) -> HRESULT {
        if (ma->Direction == 0) std::memset(ma->Data, 0, ma->AccessSize);  // read
        return S_OK;  // write: discard, as an unimplemented device register would
    };
    cbs.WHvEmulatorGetVirtualProcessorRegisters =
        [](void* c, const WHV_REGISTER_NAME* n, UINT32 cnt, WHV_REGISTER_VALUE* v) -> HRESULT {
            return WHvGetVirtualProcessorRegisters(((EmuCtx*)c)->vm->part, 0, n, cnt, v);
        };
    cbs.WHvEmulatorSetVirtualProcessorRegisters =
        [](void* c, const WHV_REGISTER_NAME* n, UINT32 cnt, const WHV_REGISTER_VALUE* v) -> HRESULT {
            return WHvSetVirtualProcessorRegisters(((EmuCtx*)c)->vm->part, 0, n, cnt, v);
        };
    cbs.WHvEmulatorTranslateGvaPage =
        [](void* c, WHV_GUEST_VIRTUAL_ADDRESS gva, WHV_TRANSLATE_GVA_FLAGS fl,
           WHV_TRANSLATE_GVA_RESULT_CODE* res, WHV_GUEST_PHYSICAL_ADDRESS* gpa) -> HRESULT {
            (void)c; (void)fl;
            *res = WHvTranslateGvaResultSuccess;
            *gpa = gva & ~0xFFFull;  // flat, unpaged: GVA == GPA
            return S_OK;
        };
    if (arm == ARM_MMIO_EMU) {
        HRESULT hr = WHvEmulatorCreateEmulator(&cbs, &emu);
        if (FAILED(hr)) { r.err = "WHvEmulatorCreateEmulator failed"; return r; }
    }

    const bool halt_arm = (arm == ARM_HLT || arm == ARM_HLT_INTR);
    Watchdog wd(vm.part, g_watchdog_ms);
    WHV_RUN_VP_EXIT_CONTEXT ex;
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    for (;;) {
        std::memset(&ex, 0, sizeof(ex));
        HRESULT hr = WHvRunVirtualProcessor(vm.part, 0, &ex, sizeof(ex));
        if (FAILED(hr)) {
            char b[128];
            std::snprintf(b, sizeof(b), "WHvRunVirtualProcessor: hr=0x%08lX", (unsigned long)hr);
            r.err = b; break;
        }
        if (ex.ExitReason == WHvRunVpExitReasonCanceled) {
            r.err = "WATCHDOG: arm did not complete (runaway guest)";
            break;
        }

        if (ex.ExitReason == WHvRunVpExitReasonX64Halt) {
            if (!halt_arm) break;  // the terminating halt after the loop
            r.exits++;
            if (r.exits > iters) break;  // loop finished; this was the terminator
            // Advance past `hlt` and, for the injection arm, deliver a vector through the
            // pending-interruption register -- the VMCB/VMCS event-injection field, which is
            // what a device model would use. No APIC is involved.
            if (arm == ARM_HLT_INTR) {
                WHV_REGISTER_NAME nms[2] = {WHvX64RegisterRip, WHvRegisterPendingInterruption};
                WHV_REGISTER_VALUE vs[2];
                std::memset(vs, 0, sizeof(vs));
                vs[0].Reg64 = ex.VpContext.Rip + ex.VpContext.InstructionLength;
                vs[1].PendingInterruption.InterruptionPending = 1;
                vs[1].PendingInterruption.InterruptionType = WHvX64PendingInterrupt;
                vs[1].PendingInterruption.InterruptionVector = 0x20;
                if (FAILED(WHvSetVirtualProcessorRegisters(vm.part, 0, nms, 2, vs))) {
                    r.err = "set RIP+PendingInterruption failed"; break;
                }
            } else {
                if (!set_rip(vm, ex.VpContext.Rip + ex.VpContext.InstructionLength)) {
                    r.err = "set RIP failed"; break;
                }
            }
            continue;
        }

        if (ex.ExitReason == WHvRunVpExitReasonMemoryAccess) {
            // What does the hypervisor actually hand back on THIS host? On AMD/SVM a nested
            // page fault does not necessarily carry a decoded instruction length. If it does
            // not, "advance RIP past the faulting instruction" is not implementable without
            // decoding the instruction yourself -- which is a design input for the CPU-tier
            // decision, not a bug to paper over. Record it from the first exit.
            if (r.exits == 0) {
                g_mmio_probe_seen = true;
                g_mmio_probe_vp_insn_len = ex.VpContext.InstructionLength;
                g_mmio_probe_ma_byte_count = ex.MemoryAccess.InstructionByteCount;
                g_mmio_probe_gpa = ex.MemoryAccess.Gpa;
                g_mmio_probe_access = ex.MemoryAccess.AccessInfo.AsUINT32;
            }
            r.exits++;
            if (arm == ARM_MMIO_EMU) {
                WHV_EMULATOR_STATUS st;
                std::memset(&st, 0, sizeof(st));
                HRESULT ehr = WHvEmulatorTryMmioEmulation(emu, &ectx, &ex.VpContext,
                                                          &ex.MemoryAccess, &st);
                if (FAILED(ehr) || !st.EmulationSuccessful) {
                    char b[160];
                    std::snprintf(b, sizeof(b),
                                  "WHvEmulatorTryMmioEmulation hr=0x%08lX status=0x%08lX",
                                  (unsigned long)ehr, (unsigned long)st.AsUINT32);
                    r.err = b; break;
                }
                // The emulator advances RIP itself.
            } else {
                // MEASURED ON THIS HOST: VpContext.InstructionLength is 0 on a memory-access
                // exit, and MemoryAccess.InstructionByteCount is 16 -- the number of bytes
                // COPIED INTO InstructionBytes[16], not a decoded instruction length. On
                // AMD/SVM a nested page fault carries no decode, so the hypervisor cannot tell
                // you how far to advance RIP. Using the byte count as a length walks RIP into
                // the middle of the next instruction.
                //
                // So this arm advances by the length the BENCHMARK knows statically, because
                // it emitted the instruction itself (`mov [disp32], eax`, 5 bytes). That makes
                // it a genuine floor measurement -- exit, write RIP, resume, and nothing else
                // -- while recording that a real implementation cannot obtain this length from
                // the hypervisor and must carry its own x86 decoder to reach this floor.
                if (!set_rip(vm, ex.VpContext.Rip + kMmioStoreInsnLen)) {
                    r.err = "set RIP failed"; break;
                }
            }
            continue;
        }

        if (ex.ExitReason == WHvRunVpExitReasonX64IoPortAccess) {
            r.exits++;
            if (!set_rip(vm, ex.VpContext.Rip + ex.VpContext.InstructionLength)) {
                r.err = "set RIP failed"; break;
            }
            continue;
        }

        char b[192];
        std::snprintf(b, sizeof(b), "unexpected exit reason 0x%08lX at rip=0x%llX",
                      (unsigned long)ex.ExitReason, (unsigned long long)ex.VpContext.Rip);
        r.err = b;
        break;
    }
    QueryPerformanceCounter(&t1);
    r.seconds = secs(t0, t1);

    if (arm == ARM_HLT_INTR) {
        WHV_REGISTER_NAME nm = WHvX64RegisterRdi;
        WHV_REGISTER_VALUE v;
        std::memset(&v, 0, sizeof(v));
        if (SUCCEEDED(WHvGetVirtualProcessorRegisters(vm.part, 0, &nm, 1, &v)))
            r.delivered = v.Reg64 & 0xFFFFFFFFull;
    }
    if (emu) WHvEmulatorDestroyEmulator(emu);

    // Every arm must have taken exactly the number of exits its loop asks for. Without this
    // the benchmark happily reports a fast per-event cost for an arm that took a handful of
    // exits and then ran off into the weeds -- which is precisely what a wrong RIP advance
    // does, and how the 5-vs-6 byte length of `mov [disp32], eax` was caught.
    if (r.err.empty()) {
        const uint64_t want = (arm == ARM_RAM)    ? 0
                              : halt_arm          ? iters + 1  // + the terminating HLT
                                                  : iters;
        if (r.exits != want) {
            char b[160];
            std::snprintf(b, sizeof(b),
                          "exit count %llu != expected %llu -- the guest did not execute the "
                          "loop this arm claims to measure",
                          (unsigned long long)r.exits, (unsigned long long)want);
            r.err = b;
        }
    }
    if (arm == ARM_HLT_INTR && r.err.empty() && r.delivered != iters) {
        char b[160];
        std::snprintf(b, sizeof(b),
                      "guest took %llu interrupts, expected %llu",
                      (unsigned long long)r.delivered, (unsigned long long)iters);
        r.err = b;
    }
    r.ok = r.err.empty();
    return r;
}

// ---------------------------------------------------------------------------
static void print_caps() {
    WHV_CAPABILITY cap;
    UINT32 w = 0;
    std::memset(&cap, 0, sizeof(cap));
    if (SUCCEEDED(WHvGetCapability(WHvCapabilityCodeProcessorVendor, &cap, sizeof(cap), &w))) {
        const char* v = cap.ProcessorVendor == WHvProcessorVendorAmd    ? "AMD"
                        : cap.ProcessorVendor == WHvProcessorVendorIntel ? "Intel"
                                                                         : "other";
        std::printf("whpx processor vendor: %s\n", v);
    }
    std::memset(&cap, 0, sizeof(cap));
    if (SUCCEEDED(WHvGetCapability(WHvCapabilityCodeFeatures, &cap, sizeof(cap), &w)))
        std::printf("whpx features: 0x%llX\n", (unsigned long long)cap.Features.AsUINT64);
}
static void print_cpu_model() {
    HKEY k; char name[256] = {0}; DWORD sz = sizeof(name);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegQueryValueExA(k, "ProcessorNameString", nullptr, nullptr, (LPBYTE)name, &sz);
        RegCloseKey(k);
    }
    SYSTEM_INFO si; GetSystemInfo(&si);
    std::printf("host cpu: %s (%lu logical processors)\n", name[0] ? name : "?",
                (unsigned long)si.dwNumberOfProcessors);
}

int main(int argc, char** argv) {
    int reps = 5;
    uint64_t target_insns = 200000000ull;
    uint64_t exit_iters = 200000ull;
    for (int i = 1; i < argc; ++i) {
        if (!std::strncmp(argv[i], "--reps=", 7)) reps = std::atoi(argv[i] + 7);
        if (!std::strncmp(argv[i], "--insns=", 8)) target_insns = (uint64_t)std::atoll(argv[i] + 8);
        if (!std::strncmp(argv[i], "--exits=", 8)) exit_iters = (uint64_t)std::atoll(argv[i] + 8);
        if (!std::strncmp(argv[i], "--watchdog-ms=", 14)) g_watchdog_ms = (DWORD)std::atoi(argv[i] + 14);
    }
    setvbuf(stdout, nullptr, _IONBF, 0);  // a hang must not also lose the output that locates it
    QueryPerformanceFrequency(&g_freq);
    print_cpu_model();

    // Gate: is WHPX usable at all? Report and stop cleanly if not (beads-xbox.1.16.2).
    WHV_CAPABILITY cap;
    UINT32 written = 0;
    std::memset(&cap, 0, sizeof(cap));
    HRESULT hr = WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &cap, sizeof(cap), &written);
    if (FAILED(hr) || !cap.HypervisorPresent) {
        std::printf("WHPX NOT AVAILABLE (hr=0x%08lX present=%d).\n"
                    "Enable with: Enable-WindowsOptionalFeature -Online "
                    "-FeatureName HypervisorPlatform\n",
                    (unsigned long)hr, FAILED(hr) ? -1 : (int)cap.HypervisorPresent);
        return 2;
    }
    print_caps();
    std::printf("reps=%d insns/cell=%llu exit iters/cell=%llu\n\n", reps,
                (unsigned long long)target_insns, (unsigned long long)exit_iters);

    Vm vm;
    if (!vm.create()) { std::printf("partition setup failed: %s\n", vm.err.c_str()); return 3; }
    std::printf("partition up: 1 vcpu, %zu MB guest RAM mapped at GPA 0, "
                "MMIO hole at 0x%08X, IO port 0x%04X\n\n",
                kRamSize / (1024 * 1024), kMmioAddr, kIoPort);

    std::vector<Program> progs;
    progs.push_back(build_tight(target_insns / 102));
    progs.push_back(build_mixed(target_insns / 170));
    progs.push_back(build_idle(target_insns / 8));

    std::vector<std::vector<double>> psecs(progs.size());
    std::vector<ExecResult> plast(progs.size());
    std::vector<std::vector<double>> asecs(NARM);
    std::vector<ArmResult> alast(NARM);
    int failures = 0;

    for (int rep = 0; rep < reps; ++rep) {
        for (size_t i = 0; i < progs.size(); ++i) {
            ExecResult r = run_program(vm, progs[i]);
            if (!r.ok) { std::printf("FAIL %s: %s\n", progs[i].name.c_str(), r.err.c_str()); failures++; }
            else psecs[i].push_back(r.seconds);
            plast[i] = r;
        }
        for (int a = 0; a < NARM; ++a) {
            ArmResult r = run_exit_arm(vm, (ExitArm)a, exit_iters);
            if (!r.ok) { std::printf("FAIL %s: %s\n", kArmName[a], r.err.c_str()); failures++; }
            else asecs[a].push_back(r.seconds);
            alast[a] = r;
        }
        std::printf("rep %d/%d done\n", rep + 1, reps);
        std::fflush(stdout);
    }

    std::printf("\n=== guest throughput (native execution, no instrumentation possible) ===\n");
    std::printf("%-8s %14s %14s %12s %8s\n", "workload", "insn/s median", "insn/s min",
                "seconds med", "exits");
    for (size_t i = 0; i < progs.size(); ++i) {
        if (psecs[i].empty()) { std::printf("%-8s %14s\n", progs[i].name.c_str(), "FAIL"); continue; }
        double med = median(psecs[i]), mn = vmin(psecs[i]);
        double ins = double(progs[i].expected_insns());
        std::printf("%-8s %12.0fM %12.0fM %12.6f %8llu\n", progs[i].name.c_str(),
                    ins / med / 1e6, ins / mn / 1e6, med,
                    (unsigned long long)plast[i].exits);
    }

    std::printf("\n=== exit round-trip cost (%llu events per cell) ===\n",
                (unsigned long long)exit_iters);
    std::printf("%-22s %12s %12s %10s %12s\n", "arm", "ns/event med", "ns/event min",
                "exits", "marginal ns");
    double ram_ns = asecs[ARM_RAM].empty() ? 0.0
                                           : median(asecs[ARM_RAM]) / double(exit_iters) * 1e9;
    for (int a = 0; a < NARM; ++a) {
        if (asecs[a].empty()) { std::printf("%-22s %12s\n", kArmName[a], "FAIL"); continue; }
        double med = median(asecs[a]) / double(exit_iters) * 1e9;
        double mn = vmin(asecs[a]) / double(exit_iters) * 1e9;
        std::printf("%-22s %12.1f %12.1f %10llu", kArmName[a], med, mn,
                    (unsigned long long)alast[a].exits);
        if (a == ARM_RAM) std::printf(" %12s\n", "(baseline)");
        else std::printf(" %12.1f\n", med - ram_ns);
    }
    if (!asecs[ARM_HLT].empty() && !asecs[ARM_HLT_INTR].empty()) {
        double h = median(asecs[ARM_HLT]) / double(exit_iters) * 1e9;
        double hi = median(asecs[ARM_HLT_INTR]) / double(exit_iters) * 1e9;
        std::printf("\ninterrupt injection marginal cost: %.1f ns "
                    "(HLT+inject %.1f - HLT %.1f); interrupts the guest actually took: %llu of %llu\n",
                    hi - h, hi, h, (unsigned long long)alast[ARM_HLT_INTR].delivered,
                    (unsigned long long)exit_iters);
        if (alast[ARM_HLT_INTR].delivered == 0)
            std::printf("  WARNING: zero deliveries -- the injection arm did not measure "
                        "what it claims.\n");
    }

    if (g_mmio_probe_seen)
        std::printf("\nMMIO exit context as reported by this host: "
                    "VpContext.InstructionLength=%u MemoryAccess.InstructionByteCount=%u "
                    "gpa=0x%llX accessinfo=0x%08X\n",
                    g_mmio_probe_vp_insn_len, g_mmio_probe_ma_byte_count,
                    (unsigned long long)g_mmio_probe_gpa, g_mmio_probe_access);

    std::printf("\n=== median-to-min spread (contamination detector) ===\n");
    for (size_t i = 0; i < progs.size(); ++i)
        if (!psecs[i].empty()) {
            double m = median(psecs[i]), n = vmin(psecs[i]);
            std::printf("  %-22s median %.6f min %.6f (%.1f%%)\n", progs[i].name.c_str(), m, n,
                        m > 0 ? 100.0 * (m - n) / m : 0.0);
        }
    for (int a = 0; a < NARM; ++a)
        if (!asecs[a].empty()) {
            double m = median(asecs[a]), n = vmin(asecs[a]);
            std::printf("  %-22s median %.6f min %.6f (%.1f%%)\n", kArmName[a], m, n,
                        m > 0 ? 100.0 * (m - n) / m : 0.0);
        }

    std::printf("\nfailures=%d\n", failures);
    return failures ? 1 : 0;
}
