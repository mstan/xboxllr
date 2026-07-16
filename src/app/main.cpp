// xlle — A0 entry point.
//
// Deliverable (ROADMAP A0): the host runs and the chosen CPU floor executes instruction
// fixtures under ring instrumentation, with NO Xbox devices attached. This wires the
// observability skeleton (rings + JSONL + TCP debug server) to the Unicorn CPU floor and
// runs the fixture spike, fault-stopping loudly on any mismatch.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "cpu/unicorn_floor.hpp"
#include "host/sdl_shell.hpp"
#include "machine/machine.hpp"
#include "obs/debug_server.hpp"
#include "spike/fixtures.hpp"
#include "xlle/jsonl.hpp"
#include "xlle/ring.hpp"

using namespace xlle;

namespace {

Ring<RetiredInsn> g_retired(1u << 16);
Ring<MemEvent> g_mem(1u << 16);

uint32_t reg_field(const X86Regs& r, int i) {
    switch (i) {
        case 0: return r.eax; case 1: return r.ecx; case 2: return r.edx; case 3: return r.ebx;
        case 4: return r.esp; case 5: return r.ebp; case 6: return r.esi; case 7: return r.edi;
        case 8: return r.eip; default: return 0;
    }
}
const char* reg_name(int i) {
    static const char* n[] = {"eax","ecx","edx","ebx","esp","ebp","esi","edi","eip"};
    return n[i];
}

bool compare(const Fixture& f, const X86Regs& got, std::string& diff) {
    bool ok = true;
    for (int i = 0; i < 9; ++i) {
        if (!(f.reg_mask & (1u << i))) continue;
        uint32_t g = reg_field(got, i), e = reg_field(f.expect, i);
        if (g != e) {
            char b[96];
            std::snprintf(b, sizeof(b), " %s=0x%08x(exp 0x%08x)", reg_name(i), g, e);
            diff += b;
            ok = false;
        }
    }
    if (f.eflags_mask) {
        uint32_t d = (got.eflags ^ f.expect.eflags) & f.eflags_mask;
        if (d) {
            char b[96];
            std::snprintf(b, sizeof(b), " eflags-diff(masked)=0x%08x", d);
            diff += b;
            ok = false;
        }
    }
    return ok;
}

// Run one fixture on a fresh CPU floor; feed the global rings via the floor's callbacks.
bool run_fixture(const Fixture& f) {
    UnicornFloor uf;
    if (!uf.ok()) {
        std::printf("FAIL %-14s : could not open unicorn x86-32\n", f.name);
        return false;
    }
    uf.on_retired([](const RetiredInsn& e) { g_retired.push(e); });
    uf.on_mem([](const MemEvent& e) { g_mem.push(e); });

    if (!uf.map_mem(kCodeBase, kMapSize) ||
        !uf.write_mem(kCodeBase, f.code.data(), f.code.size())) {
        std::printf("FAIL %-14s : map/write failed\n", f.name);
        return false;
    }
    uf.set_regs(f.init);
    for (const auto& rb : f.ext_init) uf.write_reg(rb.reg, rb.bytes.data(), rb.bytes.size());

    std::string err;
    uint64_t n = uf.run(kCodeBase, f.insns, err);
    if (!err.empty()) {
        std::printf("FAIL %-14s : %s\n", f.name, err.c_str());
        return false;
    }

    X86Regs got = uf.get_regs();
    std::string diff;
    bool ok = compare(f, got, diff);

    if (ok && f.has_mem) {
        uint32_t v = 0;
        uf.read_mem(f.mem_addr, &v, sizeof(v));
        if (v != f.mem_expect) {
            char b[96];
            std::snprintf(b, sizeof(b), " mem[0x%x]=0x%08x(exp 0x%08x)", f.mem_addr, v,
                          f.mem_expect);
            diff += b;
            ok = false;
        }
    }

    for (const auto& rb : f.ext_check) {
        // Read into a max-width (XMM = 16B) buffer so Unicorn can never overflow us,
        // whatever size it writes for a given register; compare only the expected span.
        uint8_t raw[16] = {0};
        if (!uf.read_reg(rb.reg, raw, sizeof(raw))) {
            diff += " ext-reg-read-failed";
            ok = false;
            continue;
        }
        std::vector<uint8_t> got_b(raw, raw + rb.bytes.size());
        if (got_b != rb.bytes) {
            diff += " ext-reg";
            char h[8];
            diff += "(got ";
            for (auto x : got_b) { std::snprintf(h, sizeof(h), "%02x", x); diff += h; }
            diff += ")";
            ok = false;
        }
    }

    std::printf("%s %-14s : %llu insn%s%s\n", ok ? "PASS" : "FAIL", f.name,
                (unsigned long long)n, n == 1 ? " " : "s", ok ? "" : diff.c_str());
    return ok;
}

std::string dump_retired(size_t n) {
    std::vector<RetiredInsn> w;
    g_retired.window(n, w);
    std::string out;
    for (auto& e : w)
        out += JsonLine().str("ring", "retired").u("seq", e.seq).hex("eip", e.eip)
                   .u("size", e.size).line();
    return out;
}
std::string dump_mem(size_t n) {
    std::vector<MemEvent> w;
    g_mem.window(n, w);
    std::string out;
    for (auto& e : w)
        out += JsonLine().str("ring", "mem").u("seq", e.seq).b("write", e.write)
                   .hex("addr", e.addr).u("size", e.size).hex("value", e.value).line();
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    bool serve = false, window = false;
    const char* boot_flash = nullptr;
    const char* run2bl = nullptr;
    uint64_t max_insns = 128ull * 1000 * 1000;  // 2BL kernel LZX decompress needs tens of M
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--serve")) serve = true;
        else if (!std::strcmp(argv[i], "--window")) window = true;
        else if (!std::strcmp(argv[i], "--boot") && i + 1 < argc) boot_flash = argv[++i];
        else if (!std::strcmp(argv[i], "--run2bl") && i + 1 < argc) run2bl = argv[++i];
        else if (!std::strcmp(argv[i], "--max-insns") && i + 1 < argc)
            max_insns = std::strtoull(argv[++i], nullptr, 0);
    }

    // --boot <flash.bin>: stand up the machine (RAM + flash) and smoke-test the map.
    if (boot_flash) {
        XboxMachine mach;
        if (!mach.ok()) { std::fprintf(stderr, "[machine] unicorn init failed\n"); return 1; }
        if (!mach.init_ram()) { std::fprintf(stderr, "[machine] RAM map failed\n"); return 1; }
        if (!mach.load_flash(boot_flash)) {
            std::fprintf(stderr, "[machine] flash load failed: %s\n", boot_flash);
            return 1;
        }
        std::printf("[machine] 64MB RAM @0x0 + flash (%zu bytes, mirrored to 0xFFFFFFFF) mapped\n",
                    mach.flash_size());
        uint8_t hdr[8] = {0}, rst[4] = {0};
        mach.read(0xFF000000, hdr, 8);
        mach.read(0xFFFFFFF0, rst, 4);
        std::printf("[machine] flash @0xFF000000: %02x%02x%02x%02x%02x%02x%02x%02x "
                    "(NV2A-ptr header)\n", hdr[0], hdr[1], hdr[2], hdr[3], hdr[4], hdr[5],
                    hdr[6], hdr[7]);
        std::printf("[machine] reset @0xFFFFFFF0: %02x %02x %02x %02x (expect eb c6 = jmp short)\n",
                    rst[0], rst[1], rst[2], rst[3]);
        if (run2bl) {
            // Load the decrypted 2BL at its RAM base (0x90000, entry 0x901ee) and run it.
            // MCPX handoff state approximated (protected mode, flat segs, a scratch stack);
            // the prologue is CR0/CR3/MTRR setup with no stack use, so this suffices to start.
            if (!mach.load_blob(0x90000, run2bl)) {
                std::fprintf(stderr, "[2bl] load failed: %s\n", run2bl);
                return 1;
            }
            // Stand up the I/O fabric (PCI host, SMBus+SMC, NV2A) the 2BL platform-init drives.
            mach.init_devices();
            std::vector<IoEvent> io_log;
            mach.on_io_event([&](const IoEvent& e) {
                io_log.push_back(e);
                if (io_log.size() > 64) io_log.erase(io_log.begin());
            });
            std::vector<MemEvent> mmio_log;
            mach.on_mmio_event([&](const MemEvent& e) {
                mmio_log.push_back(e);
                if (mmio_log.size() > 64) mmio_log.erase(mmio_log.begin());
            });
            // Keep the last 16 PCs in a fixed circular buffer (cheap on the per-insn hot path;
            // this fires for every one of the tens of millions of decompression instructions).
            uint32_t trace[16] = {0};
            uint64_t trace_n = 0;
            mach.cpu().on_retired([&](const RetiredInsn& e) { trace[trace_n++ & 15] = e.eip; });
            X86Regs r{};
            r.eip = 0x901ee;
            r.esp = 0x00400000;  // scratch stack in RAM (2BL sets its own later)
            r.eflags = 0x2;
            mach.cpu().set_regs(r);
            mach.cpu().setup_flat_segments(0x1000);  // MCPX handoff GDT (code=0x08, data=0x10)
            std::printf("[2bl] entering decrypted 2BL at 0x901ee (budget %llu insns) ...\n",
                        (unsigned long long)max_insns);
            std::string err2;
            uint64_t n = mach.run(0x901ee, max_insns, err2);
            X86Regs f = mach.cpu().get_regs();
            std::printf("[2bl] retired %llu instructions; final eip=0x%08x eax=0x%08x ecx=0x%08x\n",
                        (unsigned long long)n, f.eip, f.eax, f.ecx);
            if (!err2.empty()) std::printf("[2bl] STOPPED: %s\n", err2.c_str());
            else std::printf("[2bl] ran to instruction budget without fault\n");
            std::printf("[2bl] last PCs:");
            uint64_t first = trace_n > 16 ? trace_n - 16 : 0;
            for (uint64_t i = first; i < trace_n; ++i) std::printf(" %05x", trace[i & 15]);
            std::printf("\n");
            std::printf("[2bl] MCPX ROM overlay disabled by firmware: %s\n",
                        mach.pci() && mach.pci()->mcpx_rom_disabled() ? "yes" : "no");
            std::printf("[2bl] last %zu I/O ops:\n", io_log.size());
            for (const auto& e : io_log)
                std::printf("      %-3s port=0x%04x size=%u value=0x%08x\n", e.out ? "OUT" : "IN",
                            e.port, e.size, e.value);
            std::printf("[2bl] last %zu MMIO ops:\n", mmio_log.size());
            for (const auto& e : mmio_log)
                std::printf("      %-3s addr=0x%08x size=%u value=0x%08x\n",
                            e.write ? "WR" : "RD", e.addr, e.size, e.value);
            return err2.empty() ? 0 : 2;
        }
        std::printf("[machine] harness OK. NEXT: MCPX X-code repro + 2BL RC4-decrypt to boot.\n");
        return 0;
    }

    DebugServer dbg;
    dbg.register_ring("retired", dump_retired);
    dbg.register_ring("mem", dump_mem);
    if (!dbg.start(4370))
        std::fprintf(stderr, "[obs] debug server: %s\n", dbg.last_error().c_str());
    else
        std::printf("[obs] debug server on tcp/%u (ping | rings | query <ring> <n>)\n",
                    dbg.port());

    std::printf("[spike] CPU floor = unicorn (x86-32); %zu fixtures\n", corpus().size());
    int pass = 0, fail = 0;
    for (const auto& f : corpus()) (run_fixture(f) ? pass : fail)++;
    std::printf("[spike] %d passed, %d failed; rings: retired=%llu mem=%llu\n", pass, fail,
                (unsigned long long)g_retired.total(), (unsigned long long)g_mem.total());

    if (window) run_window("xlle A0 host", 640, 480);

    if (serve) {
        std::printf("[obs] serving; connect to tcp/4370 (Ctrl-C to exit)\n");
        for (;;) std::this_thread::sleep_for(std::chrono::seconds(3600));
    }
    dbg.stop();
    return fail == 0 ? 0 : 1;
}
