// unicorn_hook_bench — raw Unicorn 2.1.1 (QEMU TCG, x86-32) throughput under different
// hook configurations. Standalone; touches no emulator source.
//
// Purpose (tracker beads-xbox.1.14.2): decide xboxlle's CPU-tier strategy by measuring what
// the whole-address-space UC_HOOK_CODE (src/cpu/unicorn_floor.cpp:31) plus count=4096 slicing
// (src/machine/machine.cpp:333-413) actually costs versus a bare engine.
//
// Workloads terminate by reaching an exit address passed as uc_emu_start's `until` argument.
// Never by `count` (except H1/H3 where the count hook is the thing under test, and there the
// count is slack/slice-sized), and never by `timeout` (which spawns a host-clock thread).
//
// Build (MinGW g++ from msys64, same compiler/flags as build-release):
//   see BUILD_CMD below / tools/bench/build.sh

#include <unicorn/unicorn.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// ---------------------------------------------------------------------------
// Guest memory map (one contiguous 4 MB mapping, flat 32-bit protected mode)
// ---------------------------------------------------------------------------
static const uint32_t kMapBase = 0x00100000u;
static const size_t   kMapSize = 0x00400000u;  // 4 MB

static const uint32_t kCodeBase  = 0x00100000u;
static const uint32_t kDataBase  = 0x00200000u;  // 64 KB working buffer lives here
static const uint32_t kTableAddr = 0x00220000u;  // indirect-jump table (4 dwords)
static const uint32_t kScratch   = 0x00230000u;  // final register dump (7 dwords)
static const uint32_t kIdleCell  = 0x00240000u;  // target of `cmp ebp,[ebp]`
static const uint32_t kStackTop  = 0x00300000u;  // esp

// ---------------------------------------------------------------------------
// Tiny x86-32 emitter
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

    // OP r/m32, r32   (01 add, 09 or, 21 and, 29 sub, 31 xor, 39 cmp)
    void alu_rr(uint8_t op, Reg dst, Reg src) { b(op); b((uint8_t)(0xC0 | (src << 3) | dst)); }
    // OP r/m32, imm8 sign-extended (83 /n)
    void alu_ri8(uint8_t ext, Reg dst, int8_t imm) {
        b(0x83);
        b((uint8_t)(0xC0 | (ext << 3) | dst));
        b((uint8_t)imm);
    }
    // AND r/m32, imm32 (81 /4)
    void and_ri32(Reg dst, uint32_t imm) {
        b(0x81);
        b((uint8_t)(0xC0 | (4 << 3) | dst));
        d32(imm);
    }
    // mov [ebx + disp32], r32   (89 /r, mod=10 rm=011)
    void mov_mem_ebx_r(uint32_t disp, Reg src) {
        b(0x89);
        b((uint8_t)(0x83 | (src << 3)));
        d32(disp);
    }
    // mov r32, [ebx + disp32]   (8B /r, mod=10 rm=011)
    void mov_r_mem_ebx(Reg dst, uint32_t disp) {
        b(0x8B);
        b((uint8_t)(0x83 | (dst << 3)));
        d32(disp);
    }
    // mov [disp32], r32         (89 /r, mod=00 rm=101)
    void mov_abs_r(uint32_t addr, Reg src) {
        b(0x89);
        b((uint8_t)(0x05 | (src << 3)));
        d32(addr);
    }
    // mov r32, [disp32 + idx*4] (8B /r, mod=00 rm=100 SIB scale=4)
    void mov_r_table(Reg dst, uint32_t table, Reg idx) {
        b(0x8B);
        b((uint8_t)(0x04 | (dst << 3)));
        b((uint8_t)(0x80 | (idx << 3) | 0x05));
        d32(table);
    }
    // cmp r32, [ebp+0]          (3B /r, mod=01 rm=101 disp8=0)
    void cmp_r_mem_ebp(Reg dst) {
        b(0x3B);
        b((uint8_t)(0x40 | (dst << 3) | 0x05));
        b(0x00);
    }
    void dec_r(Reg r) { b((uint8_t)(0x48 | r)); }
    void nop() { b(0x90); }
    void sti() { b(0xFB); }
    void cli() { b(0xFA); }
    void ret() { b(0xC3); }
    void jmp_r(Reg r) { b(0xFF); b((uint8_t)(0xC0 | (4 << 3) | r)); }

    // Relative branches: emit, then patch.
    size_t jcc32(uint8_t cc) {  // 0F 8x rel32
        b(0x0F);
        b((uint8_t)(0x80 | cc));
        size_t at = buf.size();
        d32(0);
        return at;
    }
    size_t jmp32() {  // E9 rel32
        b(0xE9);
        size_t at = buf.size();
        d32(0);
        return at;
    }
    size_t call32() {  // E8 rel32
        b(0xE8);
        size_t at = buf.size();
        d32(0);
        return at;
    }
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
// Workload description
// ---------------------------------------------------------------------------
struct Program {
    const char* name;
    std::vector<uint8_t> code;
    uint32_t code_base;
    uint32_t entry;       // start EIP
    uint32_t exit_addr;   // uc_emu_start `until`
    uint64_t iters;
    uint64_t insns_per_iter;
    uint64_t epilogue_insns;
    uint32_t table[4];    // indirect-jump table (MIXED only; zeros otherwise)
    bool     has_table;
    uint32_t ebp_init;    // MIXED uses ebp as a 0..3 table index; IDLE points it at kIdleCell
    uint64_t expected_insns() const { return iters * insns_per_iter + epilogue_insns; }
};

// Store the final architectural state we care about, so every config can be byte-compared.
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

// (a) TIGHT: 100 straight-line register-only ALU insns + dec ecx + jnz = 102 / iteration.
static Program build_tight(uint64_t iters) {
    Emitter e;
    Program p{};
    p.name = "TIGHT";
    p.code_base = kCodeBase;
    p.entry = e.here();

    uint32_t top = e.here();
    // 10 groups of 10 ALU ops over {eax, edx, ebx, esi, edi} (ecx is the loop counter).
    for (int g = 0; g < 10; ++g) {
        e.alu_rr(0x01, EAX, EBX);  // add eax, ebx
        e.alu_rr(0x29, EDX, ESI);  // sub edx, esi
        e.alu_rr(0x31, EDI, EAX);  // xor edi, eax
        e.alu_rr(0x01, ESI, EDX);  // add esi, edx
        e.alu_rr(0x39, EAX, EDX);  // cmp eax, edx
        e.alu_rr(0x29, EAX, EDI);  // sub eax, edi
        e.alu_rr(0x31, EDX, EBX);  // xor edx, ebx
        e.alu_rr(0x01, EDI, ESI);  // add edi, esi
        e.alu_rr(0x21, EBX, EAX);  // and ebx, eax
        e.alu_rr(0x09, EBX, EDX);  // or  ebx, edx
    }
    e.dec_r(ECX);
    size_t back = e.jcc32(0x05);  // jnz top
    e.patch_rel32(back, top);

    p.insns_per_iter = 100 + 1 + 1;
    p.epilogue_insns = emit_epilogue(e);
    p.exit_addr = e.here();
    e.b(0xF4);  // hlt — never executed; `until` stops us first
    p.code = e.buf;
    p.iters = iters;
    p.has_table = false;
    p.ebp_init = 0;
    return p;
}

// (b) MIXED: 20 x 8-insn memory blocks + call/ret + indirect jmp through a 4-entry table
//     + index update + loop control = 170 / iteration.
static Program build_mixed(uint64_t iters) {
    Emitter e;
    Program p{};
    p.name = "MIXED";
    p.code_base = kCodeBase;

    // Subroutine first, so the exit address stays at the end of the linear program.
    uint32_t func = e.here();
    e.ret();

    p.entry = e.here();
    uint32_t top = e.here();
    for (int blk = 0; blk < 20; ++blk) {
        e.mov_mem_ebx_r(kDataBase + 0x00, EAX);  // mov [ebx+BUF+0x00], eax
        e.mov_r_mem_ebx(EAX, kDataBase + 0x10);  // mov eax, [ebx+BUF+0x10]
        e.alu_rr(0x01, EAX, EDX);                // add eax, edx
        e.mov_mem_ebx_r(kDataBase + 0x20, ESI);  // mov [ebx+BUF+0x20], esi
        e.mov_r_mem_ebx(ESI, kDataBase + 0x30);  // mov esi, [ebx+BUF+0x30]
        e.alu_rr(0x31, ESI, EAX);                // xor esi, eax
        e.alu_ri8(0, EBX, 16);                   // add ebx, 16
        e.and_ri32(EBX, 0x0000FFC0u);            // and ebx, 0xFFC0
    }
    size_t call_at = e.call32();
    e.patch_rel32(call_at, func);

    e.mov_r_table(EDI, kTableAddr, EBP);  // mov edi, [TABLE + ebp*4]
    e.jmp_r(EDI);                         // jmp edi

    uint32_t pad[4];
    size_t pad_jmp[4];
    for (int i = 0; i < 4; ++i) {
        pad[i] = e.here();
        e.nop();
        pad_jmp[i] = e.jmp32();
    }
    uint32_t cont = e.here();
    for (int i = 0; i < 4; ++i) e.patch_rel32(pad_jmp[i], cont);

    e.alu_ri8(0, EBP, 1);  // add ebp, 1
    e.alu_ri8(4, EBP, 3);  // and ebp, 3
    e.dec_r(ECX);
    size_t back = e.jcc32(0x05);  // jnz top
    e.patch_rel32(back, top);

    // 160 block + call + ret + mov edi + jmp edi + (nop + jmp) + add ebp + and ebp + dec + jnz
    p.insns_per_iter = 160 + 1 + 1 + 1 + 1 + 2 + 2 + 2;  // = 170
    p.epilogue_insns = emit_epilogue(e);
    p.exit_addr = e.here();
    e.b(0xF4);
    p.code = e.buf;
    p.iters = iters;
    p.has_table = true;
    p.ebp_init = 0;
    for (int i = 0; i < 4; ++i) p.table[i] = pad[i];
    return p;
}

// (c) IDLE: the Xbox idle-loop shape. sti; nop; nop; cli; cmp ebp,[ebp]; jne(never taken);
//     plus dec ecx / jnz so the loop terminates = 8 / iteration.
static Program build_idle(uint64_t iters) {
    Emitter e;
    Program p{};
    p.name = "IDLE";
    p.code_base = kCodeBase;
    p.entry = e.here();

    uint32_t top = e.here();
    e.sti();
    e.nop();
    e.nop();
    e.cli();
    e.cmp_r_mem_ebp(EBP);         // cmp ebp, [ebp]  -> ZF set iff [ebp] == ebp
    size_t never = e.jcc32(0x05); // jne <never taken>
    e.dec_r(ECX);
    size_t back = e.jcc32(0x05);  // jnz top
    e.patch_rel32(back, top);

    p.insns_per_iter = 8;
    p.epilogue_insns = emit_epilogue(e);
    uint32_t after_epi = e.here();
    e.patch_rel32(never, after_epi);  // land on the epilogue's end if it ever fires (it must not)
    p.exit_addr = after_epi;
    e.b(0xF4);
    p.code = e.buf;
    p.iters = iters;
    p.has_table = false;
    p.ebp_init = kIdleCell;
    return p;
}

// ---------------------------------------------------------------------------
// Hook callbacks
// ---------------------------------------------------------------------------
static uint64_t g_code_hits = 0;
static uint64_t g_block_hits = 0;

static void cb_code(uc_engine*, uint64_t, uint32_t, void*) { g_code_hits++; }
static void cb_block(uc_engine*, uint64_t, uint32_t, void*) { g_block_hits++; }

// ---------------------------------------------------------------------------
// Run one (workload, config) cell
// ---------------------------------------------------------------------------
enum Config { H0 = 0, H1, H2, H3, H4, H5, NCONFIG };
static const char* kConfigName[NCONFIG] = {
    "H0 none",        // no hooks, count=0
    "H1 count",       // count hook only (Unicorn-installed), count = total+slack -> inline path
    "H2 block",       // UC_HOOK_BLOCK only, count=0 -> inline path, once per TB
    "H3 xlle",        // UC_HOOK_CODE + count=4096 slicing (what xboxlle does today)
    "H4 code",        // UC_HOOK_CODE only, count=0 -> inline path
    "H5 code+cnt",    // UC_HOOK_CODE + count hook, ONE uc_emu_start (2 hooks, no slicing)
};

struct RunResult {
    bool ok = false;
    std::string err;
    double seconds = 0.0;
    uint64_t code_hits = 0;
    uint64_t block_hits = 0;
    uint64_t emu_start_calls = 0;
    uint32_t final_eip = 0;
    uint32_t scratch[7] = {0, 0, 0, 0, 0, 0, 0};
};

static double now_s(LARGE_INTEGER freq, LARGE_INTEGER a, LARGE_INTEGER b) {
    return double(b.QuadPart - a.QuadPart) / double(freq.QuadPart);
}

static RunResult run_cell(const Program& p, Config cfg, uint64_t known_insns) {
    RunResult r;
    uc_engine* uc = nullptr;
    uc_err e = uc_open(UC_ARCH_X86, UC_MODE_32, &uc);
    if (e != UC_ERR_OK) { r.err = std::string("uc_open: ") + uc_strerror(e); return r; }

    e = uc_mem_map(uc, kMapBase, kMapSize, UC_PROT_ALL);
    if (e != UC_ERR_OK) { r.err = std::string("uc_mem_map: ") + uc_strerror(e); uc_close(uc); return r; }

    // Zero the whole mapping so every run starts from identical guest state.
    {
        std::vector<uint8_t> zeros(kMapSize, 0);
        uc_mem_write(uc, kMapBase, zeros.data(), zeros.size());
    }
    uc_mem_write(uc, p.code_base, p.code.data(), p.code.size());
    if (p.has_table) uc_mem_write(uc, kTableAddr, p.table, sizeof(p.table));
    // `cmp ebp,[ebp]` compares ebp against the dword at kIdleCell; store ebp there so ZF=1.
    { uint32_t v = kIdleCell; uc_mem_write(uc, kIdleCell, &v, 4); }

    uint32_t r_eax = 0x12345678, r_edx = 0x9ABCDEF0, r_ebx = 0, r_esi = 0x0F0F0F0F,
             r_edi = 0xDEADBEEF, r_ebp = p.ebp_init, r_esp = kStackTop;
    uint32_t r_ecx = (uint32_t)p.iters;
    uc_reg_write(uc, UC_X86_REG_EAX, &r_eax);
    uc_reg_write(uc, UC_X86_REG_ECX, &r_ecx);
    uc_reg_write(uc, UC_X86_REG_EDX, &r_edx);
    uc_reg_write(uc, UC_X86_REG_EBX, &r_ebx);
    uc_reg_write(uc, UC_X86_REG_ESI, &r_esi);
    uc_reg_write(uc, UC_X86_REG_EDI, &r_edi);
    uc_reg_write(uc, UC_X86_REG_EBP, &r_ebp);
    uc_reg_write(uc, UC_X86_REG_ESP, &r_esp);

    g_code_hits = 0;
    g_block_hits = 0;
    uc_hook h = 0;
    if (cfg == H3 || cfg == H4 || cfg == H5) {
        // Exactly what src/cpu/unicorn_floor.cpp:31 installs: whole address space (begin=1,end=0).
        e = uc_hook_add(uc, &h, UC_HOOK_CODE, (void*)cb_code, nullptr, 1, 0);
        if (e != UC_ERR_OK) { r.err = "uc_hook_add CODE"; uc_close(uc); return r; }
    } else if (cfg == H2) {
        e = uc_hook_add(uc, &h, UC_HOOK_BLOCK, (void*)cb_block, nullptr, 1, 0);
        if (e != UC_ERR_OK) { r.err = "uc_hook_add BLOCK"; uc_close(uc); return r; }
    }

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);

    uc_err run_err = UC_ERR_OK;
    if (cfg == H3) {
        // machine.cpp:333-413 shape: re-enter uc_emu_start from the current EIP, 4096 at a time.
        uint32_t eip = p.entry;
        uint64_t calls = 0;
        QueryPerformanceCounter(&t0);
        for (;;) {
            run_err = uc_emu_start(uc, eip, p.exit_addr, /*timeout=*/0, /*count=*/4096);
            calls++;
            if (run_err != UC_ERR_OK) break;
            uc_reg_read(uc, UC_X86_REG_EIP, &eip);
            if (eip == p.exit_addr) break;
        }
        QueryPerformanceCounter(&t1);
        r.emu_start_calls = calls;
    } else {
        // H1 gets the count hook; give it enough slack that `until` is what terminates us.
        uint64_t count = (cfg == H1 || cfg == H5) ? (known_insns + 4096) : 0;
        QueryPerformanceCounter(&t0);
        run_err = uc_emu_start(uc, p.entry, p.exit_addr, /*timeout=*/0, /*count=*/count);
        QueryPerformanceCounter(&t1);
        r.emu_start_calls = 1;
    }
    r.seconds = now_s(freq, t0, t1);

    uc_reg_read(uc, UC_X86_REG_EIP, &r.final_eip);
    uc_mem_read(uc, kScratch, r.scratch, sizeof(r.scratch));
    r.code_hits = g_code_hits;
    r.block_hits = g_block_hits;

    if (run_err != UC_ERR_OK) {
        r.err = std::string("uc_emu_start: ") + uc_strerror(run_err);
    } else if (r.final_eip != p.exit_addr) {
        char b2[128];
        std::snprintf(b2, sizeof(b2), "eip=0x%08x != exit 0x%08x", r.final_eip, p.exit_addr);
        r.err = b2;
    } else {
        r.ok = true;
    }
    uc_close(uc);
    return r;
}

// ---------------------------------------------------------------------------
static void print_cpu_model() {
    HKEY k;
    char name[256] = {0};
    DWORD sz = sizeof(name);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ,
                      &k) == ERROR_SUCCESS) {
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
    if (n == 0) return 0.0;
    return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

int main(int argc, char** argv) {
    int reps = 5;
    uint64_t scale_num = 1, scale_den = 1;
    for (int i = 1; i < argc; ++i) {
        if (!std::strncmp(argv[i], "--reps=", 7)) reps = std::atoi(argv[i] + 7);
        if (!std::strncmp(argv[i], "--div=", 6)) scale_den = (uint64_t)std::atoll(argv[i] + 6);
        if (!std::strncmp(argv[i], "--mul=", 6)) scale_num = (uint64_t)std::atoll(argv[i] + 6);
    }
    if (scale_den < 1) scale_den = 1;
    if (scale_num < 1) scale_num = 1;

    print_cpu_model();
    std::printf("unicorn %d.%d.%d\n", UC_VERSION_MAJOR, UC_VERSION_MINOR, UC_VERSION_EXTRA);
    std::printf("reps=%d  workload divisor=%llu\n\n", reps, (unsigned long long)scale_den);

    std::vector<Program> progs;
    auto scale = [&](uint64_t n) {
        uint64_t v = n * scale_num / scale_den;
        return v < 1 ? 1ull : v;
    };
    progs.push_back(build_tight(scale(1960784ull)));
    progs.push_back(build_mixed(scale(1176471ull)));
    progs.push_back(build_idle(scale(25000000ull)));

    // ---- calibration: one UC_HOOK_CODE run per workload gives the exact retired count ----
    std::vector<uint64_t> known(progs.size(), 0);
    std::vector<std::vector<uint32_t>> ref_scratch(progs.size());
    for (size_t i = 0; i < progs.size(); ++i) {
        RunResult c = run_cell(progs[i], H4, progs[i].expected_insns());
        if (!c.ok) {
            std::printf("CALIBRATION FAILED (%s): %s\n", progs[i].name, c.err.c_str());
            return 1;
        }
        known[i] = c.code_hits;
        ref_scratch[i].assign(c.scratch, c.scratch + 7);
        std::printf(
            "%-6s code bytes=%zu entry=0x%08x exit=0x%08x iters=%llu\n"
            "       arithmetic insns = %llu   hook-counted insns = %llu   %s\n"
            "       final: eax=%08x ecx=%08x edx=%08x ebx=%08x esi=%08x edi=%08x ebp=%08x\n",
            progs[i].name, progs[i].code.size(), progs[i].entry, progs[i].exit_addr,
            (unsigned long long)progs[i].iters,
            (unsigned long long)progs[i].expected_insns(), (unsigned long long)known[i],
            progs[i].expected_insns() == known[i] ? "MATCH" : "*** MISMATCH ***",
            c.scratch[0], c.scratch[1], c.scratch[2], c.scratch[3], c.scratch[4], c.scratch[5],
            c.scratch[6]);
    }
    std::printf("\n");

    // ---- interleaved measurement: cycle all (workload, config) cells, then repeat ----
    std::vector<std::vector<std::vector<double>>> rate(  // [prog][config][rep]
        progs.size(), std::vector<std::vector<double>>(NCONFIG));
    std::vector<std::vector<uint64_t>> calls(progs.size(), std::vector<uint64_t>(NCONFIG, 0));
    int anomalies = 0;

    for (int rep = 0; rep < reps; ++rep) {
        for (size_t i = 0; i < progs.size(); ++i) {
            for (int c = 0; c < NCONFIG; ++c) {
                RunResult rr = run_cell(progs[i], (Config)c, known[i]);
                if (!rr.ok) {
                    std::printf("[rep %d] %-6s %-8s FAILED: %s\n", rep, progs[i].name,
                                kConfigName[c], rr.err.c_str());
                    anomalies++;
                    continue;
                }
                bool state_ok =
                    std::equal(rr.scratch, rr.scratch + 7, ref_scratch[i].begin());
                bool hits_ok = true;
                if (c == H3 || c == H4 || c == H5) hits_ok = (rr.code_hits == known[i]);
                if (!state_ok || !hits_ok) {
                    std::printf("[rep %d] %-6s %-8s SANITY: state_ok=%d hits=%llu expect=%llu\n",
                                rep, progs[i].name, kConfigName[c], (int)state_ok,
                                (unsigned long long)rr.code_hits, (unsigned long long)known[i]);
                    anomalies++;
                }
                double ips = double(known[i]) / rr.seconds;
                rate[i][c].push_back(ips);
                calls[i][c] = rr.emu_start_calls;
                std::printf("[rep %d] %-6s %-8s %8.3f s  %10.2f M insn/s  calls=%llu%s\n", rep,
                            progs[i].name, kConfigName[c], rr.seconds, ips / 1e6,
                            (unsigned long long)rr.emu_start_calls, "");
                if (c == H2) {
                    std::printf("         (block hook fired %llu times = %.1f insns/TB)\n",
                                (unsigned long long)rr.block_hits,
                                rr.block_hits ? double(known[i]) / double(rr.block_hits) : 0.0);
                }
                std::fflush(stdout);
            }
        }
    }

    // ---- summary ----
    std::printf("\n================ SUMMARY (insn/s) ================\n");
    std::printf("%-6s %-9s %14s %14s %8s %8s\n", "load", "config", "min", "median", "min/H0",
                "med/H0");
    for (size_t i = 0; i < progs.size(); ++i) {
        double h0_min = 0, h0_med = 0;
        if (!rate[i][H0].empty()) {
            h0_min = *std::min_element(rate[i][H0].begin(), rate[i][H0].end());
            h0_med = median(rate[i][H0]);
        }
        for (int c = 0; c < NCONFIG; ++c) {
            if (rate[i][c].empty()) continue;
            double mn = *std::min_element(rate[i][c].begin(), rate[i][c].end());
            double md = median(rate[i][c]);
            std::printf("%-6s %-9s %14.0f %14.0f %8.4f %8.4f\n", progs[i].name, kConfigName[c],
                        mn, md, h0_min ? mn / h0_min : 0.0, h0_med ? md / h0_med : 0.0);
        }
        std::printf("       H3 uc_emu_start calls = %llu ; retired insns = %llu\n",
                    (unsigned long long)calls[i][H3], (unsigned long long)known[i]);
        if (!rate[i][H3].empty() && !rate[i][H2].empty()) {
            double h3 = median(rate[i][H3]), h2 = median(rate[i][H2]);
            std::printf("       H3 -> H2 speedup (median) = %.2fx ; H3 -> H0 = %.2fx\n", h2 / h3,
                        h0_med / h3);
        }
        std::printf("\n");
    }
    std::printf("anomalies: %d\n", anomalies);
    return 0;
}
