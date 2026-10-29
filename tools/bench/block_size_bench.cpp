// block_size_bench — why is production TCG ~6x slower than the bare MIXED benchmark?
//
// Tracker: beads-xbox.1.16.1.
//
// The bare-Unicorn benchmark (tools/bench/unicorn_hook_bench.cpp, beads-xbox.1.14.2) reports
// 2 991 M insn/s hookless and 2 843 M with a block hook on its MIXED workload. Production's
// [perf-excl] tcg_exec bucket is 14.700 s for 7 301 344 897 executed instructions in
// 2 302 057 484 block entries -- 497 M insn/s and 6.386 ns per block entry.
//
// The two are not comparable, and this benchmark exists to say by how much and why. MIXED's
// loop body is 170 instructions in FIVE translation blocks (161 + 1 + 2 + 2 + 4) = ~34
// instructions per block. Production averages 3.172. Per-block cost -- TB dispatch, the hook
// call, QEMU's la_global_kill register spill at the hook boundary -- is amortized over ten
// times fewer instructions in the real workload than in the benchmark that was used to
// predict it.
//
// So this sweeps BLOCK SIZE as the independent variable, at three hook configurations:
//
//   B0  no hook at all                     -- the TCG floor at that block size
//   B2  trivial block hook (one counter)   -- what unicorn_hook_bench.cpp's H2 measured
//   BP  production-shaped block hook body  -- a faithful transcription of
//                                             UnicornFloor::block_trampoline
//                                             (src/cpu/unicorn_floor.cpp:568-658)
//
// Reading the table:
//   B0 across block sizes         = how much of the gap is small blocks alone (irreducible
//                                   for option A unless blocks get bigger)
//   B2 - B0 at a given block size = TB-boundary hook cost (dispatch + la_global_kill)
//   BP - B2 at a given block size = the cost of the body xboxlle actually runs
//   BP at 3.17 insns/block        = the predicted production ns/block, to be compared against
//                                   the measured 6.386 ns/block. The residual is everything
//                                   this microbenchmark does NOT have: 23 679 live blocks
//                                   instead of a handful, a 64 MB working set, softmmu TLB
//                                   refills, 17.5 M MMIO exits, and IDT delivery.
//
// Methodology (docs/reference/testing/PERF-METHODOLOGY.md section 2): interleaved repetitions in one process,
// min and median both reported, a fixed retired-instruction budget per cell so host seconds
// are the output and never the input. Every cell verifies its own architectural end state and
// its own block-entry count, so a configuration that silently executed something else is a
// failure rather than a fast number.
//
// Build: tools/bench/build_block_size.ps1 (same g++/-O3 and the same three Unicorn archives
// xlle.exe links).

#include <unicorn/unicorn.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// ---------------------------------------------------------------------------
// Guest TSC shim.
//
// CMakeLists.txt:38 compiles qemu/hw/i386/x86.c with -Dcpu_get_tsc_x86_64=
// xlle_unused_host_cpu_get_tsc, so the archive this benchmark links against exports the stock
// host-TSC reader under the renamed symbol and leaves `cpu_get_tsc_x86_64` -- which every
// RDTSC and TSC-MSR read in the pinned Unicorn calls -- undefined. src/cpu/unicorn_metrics.c
// supplies xlle's deterministic version; a standalone benchmark must supply its own.
//
// Delegating straight to the renamed original makes this benchmark behave exactly like STOCK
// Unicorn, which is what it is here to characterize. These workloads execute no RDTSC at all,
// so the choice is unobservable in the numbers; it is made explicitly rather than by accident.
extern "C" {
uint64_t xlle_unused_host_cpu_get_tsc(void* env);
uint64_t cpu_get_tsc_x86_64(void* env);
uint64_t cpu_get_tsc_x86_64(void* env) { return xlle_unused_host_cpu_get_tsc(env); }
}

// ---------------------------------------------------------------------------
// Guest memory map (flat 32-bit protected mode, one contiguous mapping)
// ---------------------------------------------------------------------------
static const uint32_t kMapBase = 0x00100000u;
static const size_t   kMapSize = 0x00800000u;  // 8 MB: the block chain can get long

static const uint32_t kCodeBase = 0x00100000u;
static const uint32_t kScratch  = 0x00700000u;  // final register dump (7 dwords)
static const uint32_t kStackTop = 0x00600000u;

// ---------------------------------------------------------------------------
// Tiny x86-32 emitter (same shape as unicorn_hook_bench.cpp)
// ---------------------------------------------------------------------------
enum Reg { EAX = 0, ECX = 1, EDX = 2, EBX = 3, ESP = 4, EBP = 5, ESI = 6, EDI = 7 };

struct Emitter {
    std::vector<uint8_t> buf;
    uint32_t base = kCodeBase;

    uint32_t here() const { return base + (uint32_t)buf.size(); }
    void b(uint8_t v) { buf.push_back(v); }
    void d32(uint32_t v) {
        b((uint8_t)(v & 0xFF));
        b((uint8_t)((v >> 8) & 0xFF));
        b((uint8_t)((v >> 16) & 0xFF));
        b((uint8_t)((v >> 24) & 0xFF));
    }
    void alu_rr(uint8_t op, Reg dst, Reg src) { b(op); b((uint8_t)(0xC0 | (src << 3) | dst)); }
    void mov_abs_r(uint32_t addr, Reg src) {
        b(0x89);
        b((uint8_t)(0x05 | (src << 3)));
        d32(addr);
    }
    void dec_r(Reg r) { b((uint8_t)(0x48 | r)); }
    void ret() { b(0xC3); }
    void push_r(Reg r) { b((uint8_t)(0x50 | r)); }

    size_t jcc32(uint8_t cc) { b(0x0F); b((uint8_t)(0x80 | cc)); size_t at = buf.size(); d32(0); return at; }
    size_t jmp32() { b(0xE9); size_t at = buf.size(); d32(0); return at; }
    size_t call32() { b(0xE8); size_t at = buf.size(); d32(0); return at; }
    void patch_rel32(size_t at, uint32_t target) {
        uint32_t next = base + (uint32_t)(at + 4);
        uint32_t rel = target - next;
        buf[at + 0] = (uint8_t)(rel & 0xFF);
        buf[at + 1] = (uint8_t)((rel >> 8) & 0xFF);
        buf[at + 2] = (uint8_t)((rel >> 16) & 0xFF);
        buf[at + 3] = (uint8_t)((rel >> 24) & 0xFF);
    }
};

// ---------------------------------------------------------------------------
// Workload
// ---------------------------------------------------------------------------
struct Program {
    std::string name;
    std::vector<uint8_t> code;
    uint32_t entry = 0;
    uint32_t exit_addr = 0;
    uint64_t iters = 0;
    uint64_t insns_per_iter = 0;
    uint64_t blocks_per_iter = 0;
    uint64_t epilogue_insns = 0;
    uint64_t epilogue_blocks = 0;
    double   insns_per_block = 0.0;
    uint64_t expected_insns() const { return iters * insns_per_iter + epilogue_insns; }
    uint64_t expected_blocks() const { return iters * blocks_per_iter + epilogue_blocks; }
};

static uint64_t emit_epilogue(Emitter& e) {
    e.mov_abs_r(kScratch + 0, EAX);
    e.mov_abs_r(kScratch + 4, ECX);
    e.mov_abs_r(kScratch + 8, EDX);
    e.mov_abs_r(kScratch + 12, EBX);
    e.mov_abs_r(kScratch + 16, ESI);
    e.mov_abs_r(kScratch + 20, EDI);
    e.mov_abs_r(kScratch + 24, EBP);
    return 7;
}

// One ALU instruction from a fixed rotation over {eax,edx,ebx,esi,edi}. ecx is the loop
// counter and ebp/esp are left alone, so the guest state at the end is a deterministic
// function of the instruction sequence and every configuration can be byte-compared.
static void emit_alu(Emitter& e, int i) {
    switch (i % 10) {
        case 0: e.alu_rr(0x01, EAX, EBX); break;  // add eax, ebx
        case 1: e.alu_rr(0x29, EDX, ESI); break;  // sub edx, esi
        case 2: e.alu_rr(0x31, EDI, EAX); break;  // xor edi, eax
        case 3: e.alu_rr(0x01, ESI, EDX); break;  // add esi, edx
        case 4: e.alu_rr(0x39, EAX, EDX); break;  // cmp eax, edx
        case 5: e.alu_rr(0x29, EAX, EDI); break;  // sub eax, edi
        case 6: e.alu_rr(0x31, EDX, EBX); break;  // xor edx, ebx
        case 7: e.alu_rr(0x01, EDI, ESI); break;  // add edi, esi
        case 8: e.alu_rr(0x21, EBX, EAX); break;  // and ebx, eax
        default: e.alu_rr(0x09, EBX, EDX); break; // or  ebx, edx
    }
}

// BLK<k>/direct: `nblocks` blocks per iteration, each exactly k instructions --
// (k-1) register ALU ops followed by a `jmp rel32` to the next block, which is what ends the
// translation block. Direct jumps let QEMU chain blocks (goto_tb + patched branch), so this is
// the BEST CASE for small blocks: the block-to-block transition itself is a direct jump.
//
// BLK<k>/indirect: the same, but each block ends with `call`/`ret` through a shared stub, so
// the transition goes through the TB lookup path instead of a patched jump. Production's guest
// code is full of calls, returns and indirect branches, so the truth is bracketed by these two.
static Program build_blk(int k, int nblocks, uint64_t iters, bool indirect) {
    Emitter e;
    Program p;
    char nm[64];
    std::snprintf(nm, sizeof(nm), "BLK%-2d %s", k, indirect ? "indirect" : "direct  ");
    p.name = nm;

    uint32_t stub = 0;
    if (indirect) {
        // A `ret` stub: `call stub` ends a block, `ret` ends another and returns through the
        // lookup path. Two blocks and two instructions of overhead per emitted block, so the
        // effective block size is computed from what is actually emitted, below.
        stub = e.here();
        e.ret();
    }

    p.entry = e.here();
    uint32_t top = e.here();

    int alu_seq = 0;
    uint64_t insns = 0, blocks = 0;
    for (int blkidx = 0; blkidx < nblocks; ++blkidx) {
        if (indirect) {
            // block: (k-1) ALU + call  -> k instructions, then the stub's `ret` block.
            for (int j = 0; j < k - 1; ++j) { emit_alu(e, alu_seq++); }
            size_t at = e.call32();
            e.patch_rel32(at, stub);
            insns += (uint64_t)k + 1;  // this block's k, plus the stub's single `ret`
            blocks += 2;               // this block, plus the stub block
        } else {
            for (int j = 0; j < k - 1; ++j) { emit_alu(e, alu_seq++); }
            size_t at = e.jmp32();
            e.patch_rel32(at, e.here());  // jump to the very next instruction
            insns += (uint64_t)k;
            blocks += 1;
        }
    }

    // Loop control forms ONE MORE block: the last emitted block ended with a jmp (or a call
    // whose stub ended with a ret), so `dec ecx; jnz top` begins a fresh translation block and
    // ends it on the conditional branch.
    e.dec_r(ECX);
    size_t back = e.jcc32(0x05);  // jnz top
    e.patch_rel32(back, top);
    insns += 2;
    blocks += 1;

    p.insns_per_iter = insns;
    p.blocks_per_iter = blocks;
    p.epilogue_insns = emit_epilogue(e);
    p.epilogue_blocks = 1;
    p.exit_addr = e.here();
    e.b(0xF4);  // hlt — never executed; `until` stops us first
    p.code = e.buf;
    p.iters = iters;
    p.insns_per_block = double(insns) / double(blocks);
    return p;
}

// ---------------------------------------------------------------------------
// Hook configurations
// ---------------------------------------------------------------------------
enum Config { B0 = 0, B2, BP, BX, NCONFIG };
static const char* kConfigName[NCONFIG] = {
    "B0 none",   // no hook: the TCG floor at this block size
    "B2 trivial",// one counter increment (what unicorn_hook_bench.cpp H2 measured)
    "BP prod",   // faithful transcription of UnicornFloor::block_trampoline
    "BX no-icnt",// BP with the icount lookup removed (see below)
};

static uint64_t g_block_hits = 0;
static void cb_block_trivial(uc_engine*, uint64_t, uint32_t, void*) { g_block_hits++; }

// --- production-shaped hook state (mirrors src/cpu/unicorn_floor.{hpp,cpp}) ---------------
struct IcountSlot {
    uint32_t pc = 0;
    uint16_t size = 0;
    uint16_t icount = 0;
};
static constexpr size_t kIcountSlots = 1u << 18;  // unicorn_floor.hpp:129
static constexpr size_t kIcountProbes = 8;        // unicorn_floor.hpp:130
static constexpr size_t kBlockPcRing = 8192;      // unicorn_floor.hpp:90
static inline size_t icount_hash(uint32_t pc) {   // unicorn_floor.hpp:132
    return (static_cast<size_t>(pc) * 2654435761u >> 4) & (kIcountSlots - 1);
}

struct ProdHookState {
    std::unique_ptr<IcountSlot[]> icount;
    std::unique_ptr<uint32_t[]> ring;
    uint64_t tb_entries = 0;
    uint64_t tb_insns = 0;
    uint64_t seq = 0;
    uint64_t run_deadline = ~0ull;
    uint64_t tbs_this_run = 0;
    uint64_t deadline_stops = 0;
    uint64_t icount_misses = 0;
    uint32_t last_pc = 0;
    uint16_t last_size = 0;
    uint16_t last_icount = 0;
    bool insn_mode = false;
    bool boot_pending = false;
    bool irq_pending = false;
    std::string stop_reason;  // the real one is a std::string; keep the same check cost

    ProdHookState()
        : icount(new IcountSlot[kIcountSlots]()), ring(new uint32_t[kBlockPcRing]()) {}

    void insert(uint32_t pc, uint16_t size, uint16_t ic) {  // unicorn_floor.cpp:110
        if (ic == 0) return;
        const size_t h = icount_hash(pc);
        for (size_t i = 0; i < kIcountProbes; ++i) {
            IcountSlot& s = icount[(h + i) & (kIcountSlots - 1)];
            if (s.icount == 0) { s.pc = pc; s.size = size; s.icount = ic; return; }
            if (s.pc == pc && s.size == size) { s.icount = ic; return; }
        }
        IcountSlot& s = icount[h];
        s.pc = pc; s.size = size; s.icount = ic;
        last_pc = 0; last_size = 0; last_icount = 0;
    }
    uint32_t lookup(uint32_t pc, uint16_t size) const {  // unicorn_floor.cpp:142
        const size_t h = icount_hash(pc);
        for (size_t i = 0; i < kIcountProbes; ++i) {
            const IcountSlot& s = icount[(h + i) & (kIcountSlots - 1)];
            if (s.icount == 0) return 0;
            if (s.pc == pc && s.size == size) return s.icount;
        }
        return 0;
    }
};
static ProdHookState* g_prod = nullptr;
static uint64_t g_prod_misses = 0;

// The edge hook, exactly as unicorn_floor.cpp:548-563 uses it: it is what populates the
// (pc,size)->icount table, and it must be installed for BP to be a faithful arm.
static void cb_edge(uc_engine*, void* cur_tb, void* prev_tb, void*) {
    const uc_tb* cur = static_cast<const uc_tb*>(cur_tb);
    if (cur) g_prod->insert((uint32_t)cur->pc, cur->size, cur->icount);
    if (prev_tb) {
        const uc_tb* prev = static_cast<const uc_tb*>(prev_tb);
        g_prod->insert((uint32_t)prev->pc, prev->size, prev->icount);
    }
}

// Faithful transcription of UnicornFloor::block_trampoline (unicorn_floor.cpp:568-658).
// Same order, same branches, same data structures. The only omissions are the fault-stop
// string formatting (never reached on a clean run) and the bootstrap-charge path, which
// production takes exactly once in a 12 B run (tb_bootstrap_blocks=1).
static void cb_block_prod(uc_engine* uc, uint64_t addr, uint32_t size, void*) {
    ProdHookState* self = g_prod;
    const uint32_t pc = (uint32_t)addr;
    self->ring[self->tb_entries & (kBlockPcRing - 1)] = pc;
    self->tb_entries++;
    if (self->insn_mode) return;
    if (self->boot_pending) { /* settle_bootstrap_block(); once per run in production */ }
    if (!self->stop_reason.empty()) return;

    const uint16_t sz = (uint16_t)size;
    uint32_t icount;
    if (pc == self->last_pc && sz == self->last_size) {
        icount = self->last_icount;
    } else {
        icount = self->lookup(pc, sz);
        if (icount == 0) { g_prod_misses++; return; }
        self->last_pc = pc;
        self->last_size = sz;
        self->last_icount = (uint16_t)icount;
    }

    if (self->tbs_this_run != 0 && self->seq + icount > self->run_deadline) {
        self->deadline_stops++;
        uc_emu_stop(uc);
        return;
    }

    if (self->irq_pending) {
        uint32_t eflags = 0;
        uc_reg_read(uc, UC_X86_REG_EFLAGS, &eflags);
        if (eflags & 0x200u) { uc_emu_stop(uc); return; }
    }

    // xlle_guest_tsc_ticks = self->seq -- a store to a global, same cost shape.
    static volatile uint64_t tsc_sink;
    tsc_sink = self->seq;
    self->tbs_this_run++;
    self->seq += icount;
    self->tb_insns += icount;
}

// BX: the production body with the (pc,size)->icount LOOKUP removed, and nothing else
// changed. It exists to size one specific option-A patch.
//
// The production hook resolves each block's instruction count through a one-entry inline
// cache backed by a 2^18-slot open-addressed table. At 3.172 instructions per block,
// CONSECUTIVE BLOCKS ARE ALMOST NEVER THE SAME BLOCK, so that one-entry cache misses on
// essentially every entry and every entry pays the full hash + probe. This arm models the
// alternative -- having the icount arrive with the block instead of being looked up, which is
// what emitting it into the translated block itself would achieve -- by using a constant.
// BP - BX is therefore the CEILING of that patch, measured rather than argued.
static void cb_block_prod_noicount(uc_engine* uc, uint64_t addr, uint32_t size, void*) {
    ProdHookState* self = g_prod;
    const uint32_t pc = (uint32_t)addr;
    self->ring[self->tb_entries & (kBlockPcRing - 1)] = pc;
    self->tb_entries++;
    if (self->insn_mode) return;
    if (self->boot_pending) { }
    if (!self->stop_reason.empty()) return;

    (void)size;
    const uint32_t icount = 1;  // supplied with the block instead of looked up

    if (self->tbs_this_run != 0 && self->seq + icount > self->run_deadline) {
        self->deadline_stops++;
        uc_emu_stop(uc);
        return;
    }
    if (self->irq_pending) {
        uint32_t eflags = 0;
        uc_reg_read(uc, UC_X86_REG_EFLAGS, &eflags);
        if (eflags & 0x200u) { uc_emu_stop(uc); return; }
    }
    static volatile uint64_t tsc_sink;
    tsc_sink = self->seq;
    self->tbs_this_run++;
    self->seq += icount;
    self->tb_insns += icount;
}

// ---------------------------------------------------------------------------
struct RunResult {
    bool ok = false;
    std::string err;
    double seconds = 0.0;
    uint64_t blocks = 0;
    uint64_t insns = 0;
    uint32_t final_eip = 0;
    uint32_t scratch[7] = {0, 0, 0, 0, 0, 0, 0};
};

static double now_s(LARGE_INTEGER f, LARGE_INTEGER a, LARGE_INTEGER b) {
    return double(b.QuadPart - a.QuadPart) / double(f.QuadPart);
}

static RunResult run_cell(const Program& p, Config cfg) {
    RunResult r;
    uc_engine* uc = nullptr;
    if (uc_open(UC_ARCH_X86, UC_MODE_32, &uc) != UC_ERR_OK) { r.err = "uc_open"; return r; }
    if (uc_mem_map(uc, kMapBase, kMapSize, UC_PROT_ALL) != UC_ERR_OK) {
        r.err = "uc_mem_map"; uc_close(uc); return r;
    }
    { std::vector<uint8_t> z(kMapSize, 0); uc_mem_write(uc, kMapBase, z.data(), z.size()); }
    uc_mem_write(uc, kCodeBase, p.code.data(), p.code.size());

    uint32_t v;
    v = 0x12345678; uc_reg_write(uc, UC_X86_REG_EAX, &v);
    v = (uint32_t)p.iters; uc_reg_write(uc, UC_X86_REG_ECX, &v);
    v = 0x9ABCDEF0; uc_reg_write(uc, UC_X86_REG_EDX, &v);
    v = 0; uc_reg_write(uc, UC_X86_REG_EBX, &v);
    v = 0x0F0F0F0F; uc_reg_write(uc, UC_X86_REG_ESI, &v);
    v = 0xDEADBEEF; uc_reg_write(uc, UC_X86_REG_EDI, &v);
    v = 0; uc_reg_write(uc, UC_X86_REG_EBP, &v);
    v = kStackTop; uc_reg_write(uc, UC_X86_REG_ESP, &v);

    g_block_hits = 0;
    g_prod_misses = 0;
    std::unique_ptr<ProdHookState> prod;
    uc_hook hb = 0, he = 0;
    if (cfg == B2) {
        if (uc_hook_add(uc, &hb, UC_HOOK_BLOCK, (void*)cb_block_trivial, nullptr, 1, 0) !=
            UC_ERR_OK) { r.err = "hook BLOCK"; uc_close(uc); return r; }
    } else if (cfg == BP || cfg == BX) {
        prod.reset(new ProdHookState());
        g_prod = prod.get();
        if (uc_hook_add(uc, &he, UC_HOOK_EDGE_GENERATED, (void*)cb_edge, nullptr, 1, 0) !=
            UC_ERR_OK) { r.err = "hook EDGE"; uc_close(uc); return r; }
        void* body = (cfg == BP) ? (void*)cb_block_prod : (void*)cb_block_prod_noicount;
        if (uc_hook_add(uc, &hb, UC_HOOK_BLOCK, body, nullptr, 1, 0) !=
            UC_ERR_OK) { r.err = "hook BLOCK"; uc_close(uc); return r; }
    }

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    uc_err run_err = uc_emu_start(uc, p.entry, p.exit_addr, /*timeout=*/0, /*count=*/0);
    QueryPerformanceCounter(&t1);
    r.seconds = now_s(freq, t0, t1);

    uc_reg_read(uc, UC_X86_REG_EIP, &r.final_eip);
    uc_mem_read(uc, kScratch, r.scratch, sizeof(r.scratch));
    if (cfg == B2) r.blocks = g_block_hits;
    if (cfg == BP || cfg == BX) { r.blocks = prod->tb_entries; r.insns = prod->tb_insns; }

    if (run_err != UC_ERR_OK) r.err = std::string("uc_emu_start: ") + uc_strerror(run_err);
    else if (r.final_eip != p.exit_addr) {
        char b[128];
        std::snprintf(b, sizeof(b), "eip=0x%08x != exit 0x%08x", r.final_eip, p.exit_addr);
        r.err = b;
    } else r.ok = true;

    g_prod = nullptr;
    uc_close(uc);
    return r;
}

// ---------------------------------------------------------------------------
static void print_cpu_model() {
    HKEY k;
    char name[256] = {0};
    DWORD sz = sizeof(name);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegQueryValueExA(k, "ProcessorNameString", nullptr, nullptr, (LPBYTE)name, &sz);
        RegCloseKey(k);
    }
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    std::printf("host cpu: %s (%lu logical processors)\n", name[0] ? name : "?",
                (unsigned long)si.dwNumberOfProcessors);
}
static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (!n) return 0.0;
    return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

int main(int argc, char** argv) {
    int reps = 5;
    uint64_t target_insns = 200000000ull;  // per cell
    for (int i = 1; i < argc; ++i) {
        if (!std::strncmp(argv[i], "--reps=", 7)) reps = std::atoi(argv[i] + 7);
        if (!std::strncmp(argv[i], "--insns=", 8)) target_insns = (uint64_t)std::atoll(argv[i] + 8);
    }

    print_cpu_model();
    std::printf("unicorn %d.%d.%d\n", UC_VERSION_MAJOR, UC_VERSION_MINOR, UC_VERSION_EXTRA);
    std::printf("reps=%d  target insns/cell=%llu\n", reps, (unsigned long long)target_insns);
    std::printf("production reference: tcg_exec 14.700 s / 7 301 344 897 insns / "
                "2 302 057 484 blocks = 497 M insn/s, 3.172 insns/block, 6.386 ns/block\n\n");

    // Block sizes bracketing production's 3.172, up to the benchmark's ~34.
    const int ks[] = {2, 3, 4, 6, 8, 16, 34, 64};
    std::vector<Program> progs;
    for (int k : ks) {
        // Keep the emitted code small enough to stay i-cache resident but long enough that
        // the loop-back branch is a small share: 64 blocks per iteration.
        const int nblocks = 64;
        // Choose iters so every cell retires ~target_insns.
        Program probe = build_blk(k, nblocks, 1, false);
        uint64_t iters = target_insns / probe.insns_per_iter;
        if (iters < 1) iters = 1;
        progs.push_back(build_blk(k, nblocks, iters, false));
        Program probe2 = build_blk(k, nblocks, 1, true);
        uint64_t iters2 = target_insns / probe2.insns_per_iter;
        if (iters2 < 1) iters2 = 1;
        progs.push_back(build_blk(k, nblocks, iters2, true));
    }

    // results[prog][cfg][rep]
    std::vector<std::vector<std::vector<double>>> secs(
        progs.size(), std::vector<std::vector<double>>(NCONFIG));
    std::vector<std::vector<RunResult>> last(progs.size(), std::vector<RunResult>(NCONFIG));
    int failures = 0;

    // Interleaved: rep-major so a thermal or scheduling drift hits every arm alike.
    for (int rep = 0; rep < reps; ++rep) {
        for (size_t pi = 0; pi < progs.size(); ++pi) {
            for (int c = 0; c < NCONFIG; ++c) {
                RunResult r = run_cell(progs[pi], (Config)c);
                if (!r.ok) {
                    std::printf("FAIL %s %s: %s\n", progs[pi].name.c_str(), kConfigName[c],
                                r.err.c_str());
                    failures++;
                } else {
                    secs[pi][c].push_back(r.seconds);
                }
                last[pi][c] = r;
            }
        }
        std::printf("rep %d/%d done\n", rep + 1, reps);
        std::fflush(stdout);
    }

    // Cross-check: every configuration must have produced the same architectural end state
    // and the counted blocks must equal the emitted block count. A fast number from a
    // configuration that executed something else is a failure, not a result.
    std::printf("\n=== consistency ===\n");
    for (size_t pi = 0; pi < progs.size(); ++pi) {
        const Program& p = progs[pi];
        bool state_ok = true;
        for (int c = 1; c < NCONFIG; ++c)
            if (std::memcmp(last[pi][0].scratch, last[pi][c].scratch, sizeof(last[pi][0].scratch)))
                state_ok = false;
        const uint64_t exp_b = p.expected_blocks();
        const uint64_t got_b = last[pi][BP].blocks;
        const uint64_t exp_i = p.expected_insns();
        const uint64_t got_i = last[pi][BP].insns;
        // The BP arm charges instructions exactly as production does, and like production it
        // cannot charge the FIRST block ever entered: UC_HOOK_EDGE_GENERATED only reports an
        // identity once a second block exists (cpu-exec.c:267 guards on uc->last_tb). xlle
        // repays that block via settle_bootstrap_block(); this benchmark deliberately does not,
        // so BP's instruction total is short by exactly the bootstrap block's icount. Anything
        // larger than one block (<= 512 instructions) is a real discrepancy.
        const uint64_t ideficit = exp_i - got_i;
        const bool insns_ok = got_i <= exp_i && ideficit <= 512;
        std::printf("%s iters=%-9llu emitted insns/blk=%.3f  state=%s  blocks exp=%llu got=%llu%s"
                    "  insns exp=%llu got=%llu (bootstrap deficit %llu)%s\n",
                    p.name.c_str(), (unsigned long long)p.iters, p.insns_per_block,
                    state_ok ? "OK" : "MISMATCH",
                    (unsigned long long)exp_b, (unsigned long long)got_b,
                    exp_b == got_b ? "" : " <-- DIFFERS",
                    (unsigned long long)exp_i, (unsigned long long)got_i,
                    (unsigned long long)ideficit,
                    insns_ok ? "" : " <-- DIFFERS");
        if (!state_ok || exp_b != got_b || !insns_ok) failures++;
    }

    std::printf("\n=== insn/s (median of %d, min in brackets) ===\n", reps);
    std::printf("%-18s %8s %11s %11s %11s %11s\n", "workload", "insn/blk", kConfigName[B0],
                kConfigName[B2], kConfigName[BP], kConfigName[BX]);
    for (size_t pi = 0; pi < progs.size(); ++pi) {
        const Program& p = progs[pi];
        std::printf("%-18s %8.3f", p.name.c_str(), p.insns_per_block);
        for (int c = 0; c < NCONFIG; ++c) {
            if (secs[pi][c].empty()) { std::printf(" %14s", "FAIL"); continue; }
            double med = median(secs[pi][c]);
            std::printf(" %10.0fM", double(p.expected_insns()) / med / 1e6);
        }
        std::printf("\n");
    }

    std::printf("\n=== ns per BLOCK ENTRY (median), and the deltas that decompose it ===\n");
    std::printf("%-18s %8s %8s %8s %8s %8s %8s %8s %8s\n", "workload", "insn/blk", "B0", "B2",
                "BP", "BX", "B2-B0", "BP-B2", "BP-BX");
    for (size_t pi = 0; pi < progs.size(); ++pi) {
        const Program& p = progs[pi];
        if (secs[pi][B0].empty() || secs[pi][B2].empty() || secs[pi][BP].empty() ||
            secs[pi][BX].empty())
            continue;
        const double blocks = double(p.expected_blocks());
        double n0 = median(secs[pi][B0]) / blocks * 1e9;
        double n2 = median(secs[pi][B2]) / blocks * 1e9;
        double np = median(secs[pi][BP]) / blocks * 1e9;
        double nx = median(secs[pi][BX]) / blocks * 1e9;
        std::printf("%-18s %8.3f %8.3f %8.3f %8.3f %8.3f %8.3f %8.3f %8.3f\n", p.name.c_str(),
                    p.insns_per_block, n0, n2, np, nx, n2 - n0, np - n2, np - nx);
    }
    std::printf("BP-BX is the ceiling of removing the (pc,size)->icount LOOKUP from the hook\n"
                "body -- i.e. of having each block's instruction count arrive WITH the block\n"
                "instead of being hashed for. At these block sizes consecutive blocks always\n"
                "differ, so the one-entry inline cache misses on essentially every entry,\n"
                "exactly as it must in production at 3.172 insns/block.\n");

    std::printf("\n=== median-to-min spread (contamination detector, section 2 rule 4) ===\n");
    for (size_t pi = 0; pi < progs.size(); ++pi)
        for (int c = 0; c < NCONFIG; ++c) {
            if (secs[pi][c].empty()) continue;
            double med = median(secs[pi][c]);
            double mn = *std::min_element(secs[pi][c].begin(), secs[pi][c].end());
            if (med > 0 && (med - mn) / med > 0.10)
                std::printf("  %s %s: median %.4f s min %.4f s (%.1f%% spread)\n",
                            progs[pi].name.c_str(), kConfigName[c], med, mn,
                            100.0 * (med - mn) / med);
        }
    std::printf("  (rows printed only above 10%%)\n");

    std::printf("\nfailures=%d\n", failures);
    return failures ? 1 : 0;
}
