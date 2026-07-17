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
    const char* hdd = nullptr;
    const char* eeprom = nullptr;
    uint64_t max_insns = 128ull * 1000 * 1000;  // 2BL kernel LZX decompress needs tens of M
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--serve")) serve = true;
        else if (!std::strcmp(argv[i], "--window")) window = true;
        else if (!std::strcmp(argv[i], "--boot") && i + 1 < argc) boot_flash = argv[++i];
        else if (!std::strcmp(argv[i], "--run2bl") && i + 1 < argc) run2bl = argv[++i];
        else if (!std::strcmp(argv[i], "--hdd") && i + 1 < argc) hdd = argv[++i];
        else if (!std::strcmp(argv[i], "--eeprom") && i + 1 < argc) eeprom = argv[++i];
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
            if (hdd) {
                if (!mach.attach_hdd(hdd)) {
                    std::fprintf(stderr, "[hdd] attach failed: %s\n", hdd);
                    return 1;
                }
                std::printf("[hdd] attached %s (IDE drive 0, security LOCKED)\n", hdd);
            } else {
                std::printf("[hdd] no --hdd image: IDE drive 0 reports absent\n");
            }
            if (eeprom) {
                if (!mach.attach_eeprom(eeprom)) {
                    std::fprintf(stderr, "[eeprom] attach failed (need 256-byte dump): %s\n",
                                 eeprom);
                    return 1;
                }
                std::printf("[eeprom] attached %s (SMBus 0x54)\n", eeprom);
            } else {
                std::printf("[eeprom] no --eeprom image: SMBus 0x54 reports absent\n");
            }
            std::vector<IoEvent> io_log;
            uint64_t ide_io_count = 0;  // any access to 0x1F0-0x1F7 / 0x3F6 / bus-master ports
            mach.on_io_event([&](const IoEvent& e) {
                if ((e.port >= 0x1F0 && e.port <= 0x1F7) || e.port == 0x3F6 ||
                    (e.port >= 0xFF60 && e.port <= 0xFF6F))
                    ide_io_count++;
                io_log.push_back(e);
                if (io_log.size() > 64) io_log.erase(io_log.begin());
            });
            std::vector<MemEvent> mmio_log;
            mach.on_mmio_event([&](const MemEvent& e) {
                mmio_log.push_back(e);
                if (mmio_log.size() > 64) mmio_log.erase(mmio_log.begin());
            });
            // Keep the last 64 PCs in a fixed circular buffer (cheap on the per-insn hot path;
            // this fires for every one of the tens of millions of decompression instructions).
            uint32_t trace[64] = {0};
            uint64_t trace_n = 0;
            // Snapshot the PC history at each transition INTO the idle loop, so the final snapshot
            // shows the boot thread's last actions before it blocked and yielded to idle.
            uint32_t pre_idle[64] = {0};
            uint64_t pre_idle_n = 0;
            bool was_idle = false;
            auto in_idle = [](uint32_t pc) { return pc >= 0x8001e3ce && pc <= 0x8001e3ff; };
            bool idle_latched = false;
            // Thread attribution (kernel 4983 layout): KPCR at 0x8003da3c, PrcbData.CurrentThread
            // at KPCR+0x28, idle thread 0x8003d8f0. The pointer is polled every 64 insns (cheap);
            // every retired PC is recorded to the non-idle ring while a non-idle thread is
            // current, so we see exactly what phase-1 (or any worker) did last.
            constexpr uint32_t kCurThreadPhys = 0x0003da64;
            constexpr uint32_t kIdleThread = 0x8003d8f0;
            uint32_t cur_thread = 0;
            struct ThreadCount { uint32_t thread; uint64_t insns; };
            std::vector<ThreadCount> thread_counts;
            auto bump_thread = [&](uint32_t t, uint64_t n) {
                for (auto& tc : thread_counts)
                    if (tc.thread == t) { tc.insns += n; return; }
                thread_counts.push_back({t, n});
            };
            std::vector<uint32_t> nonidle_ring(512, 0);
            uint64_t nonidle_n = 0;
            mach.cpu().on_retired([&](const RetiredInsn& e) {
                trace[trace_n++ & 63] = e.eip;
                if ((trace_n & 63) == 0) {
                    uint32_t t = 0;
                    if (mach.read(kCurThreadPhys, &t, 4) && (t & 0x80000000u)) {
                        cur_thread = t;
                        bump_thread(t, 64);
                    }
                }
                if (cur_thread && cur_thread != kIdleThread)
                    nonidle_ring[nonidle_n++ & 511] = e.eip;
                bool now_idle = in_idle(e.eip);
                // Latch the FIRST entry into the idle loop — that's the boot thread's original
                // block (it never runs again after this), so the history is its blocking path.
                if (now_idle && !was_idle && !idle_latched) {
                    for (int i = 0; i < 64; ++i) pre_idle[i] = trace[i];
                    pre_idle_n = trace_n;
                    idle_latched = true;
                }
                was_idle = now_idle;
            });
            // Guest-vector observability: log every guest-raised vector (capped), and decode
            // the kernel debug service (INT 0x2D, eax=1 -> DbgPrint with ecx=buffer) so the
            // kernel's own diagnostics reach our console.
            int vec_logged = 0;
            auto read_va_str = [&](uint32_t va, char* out, size_t n) {
                uint32_t pa = 0;
                if (!mach.cpu().translate(va, pa) || !mach.read(pa, out, n - 1)) return false;
                out[n - 1] = 0;
                for (char* c = out; *c; ++c)
                    if (*c < 0x20 || *c > 0x7e) *c = *c == '\n' ? ' ' : '.';
                return true;
            };
            mach.cpu().on_interrupt([&](uint32_t vec) {
                X86Regs ir = mach.cpu().get_regs();
                if (vec == 0x2d) {
                    // NT debug services: eax=1 print (ecx=buffer, edx=len), 2 prompt,
                    // 3 load symbols (ecx=STRING image name, edx=info), 4 unload symbols.
                    char msg[160] = {0};
                    if (ir.eax == 1) {
                        if (read_va_str(ir.ecx, msg, sizeof(msg)))
                            std::printf("[dbg] DbgPrint: %s\n", msg);
                        return;
                    }
                    if (ir.eax == 3 || ir.eax == 4) {
                        // ecx -> ANSI_STRING {USHORT len; USHORT max; PCHAR buf}
                        uint32_t pa = 0;
                        uint8_t s[8] = {0};
                        uint32_t buf_va = 0;
                        uint16_t len = 0;
                        if (mach.cpu().translate(ir.ecx, pa) && mach.read(pa, s, 8)) {
                            len = static_cast<uint16_t>(s[0] | (s[1] << 8));
                            buf_va = s[4] | (s[5] << 8) | (s[6] << 16) |
                                     (static_cast<uint32_t>(s[7]) << 24);
                        }
                        if (buf_va && len && len < sizeof(msg) &&
                            read_va_str(buf_va, msg, len + 1))
                            std::printf("[dbg] %s symbols: %s\n",
                                        ir.eax == 3 ? "LOAD" : "UNLOAD", msg);
                        else
                            std::printf("[dbg] %s symbols (name unreadable, ecx=0x%08x)\n",
                                        ir.eax == 3 ? "LOAD" : "UNLOAD", ir.ecx);
                        return;
                    }
                    static int svc_logged = 0;
                    if (svc_logged < 16) {
                        uint8_t raw[16] = {0};
                        uint32_t pa = 0;
                        if (mach.cpu().translate(ir.ecx, pa)) mach.read(pa, raw, 16);
                        std::printf("[dbg] debug service eax=%u ecx=0x%08x edx=0x%08x "
                                    "[ecx]=%02x%02x%02x%02x %02x%02x%02x%02x\n", ir.eax, ir.ecx,
                                    ir.edx, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5],
                                    raw[6], raw[7]);
                        ++svc_logged;
                    }
                    return;
                }
                if (vec_logged < 32) {
                    std::printf("[dbg] guest vector 0x%02x raised at eip=0x%08x eax=0x%08x\n", vec,
                                ir.eip, ir.eax);
                    if (++vec_logged == 32) std::printf("[dbg] (vector log capped)\n");
                }
            });
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
            // Dump kernel code bytes around the final EIP (virtual 0x8xxxxxxx -> physical
            // & 0x07FFFFFF) so the faulting instruction can be disassembled off-line.
            {
                uint32_t peip = f.eip & 0x07FFFFFF;
                uint8_t code[48] = {0};
                if (peip >= 24 && mach.read(peip - 24, code, sizeof(code))) {
                    std::printf("[2bl] code @0x%08x-0x%08x:", peip - 24, peip + 24);
                    for (int i = 0; i < 48; ++i) std::printf(" %02x", code[i]);
                    std::printf("\n      (EIP is the 25th byte above)\n");
                }
            }
            std::printf("[2bl] last PCs:");
            uint64_t first = trace_n > 16 ? trace_n - 16 : 0;
            for (uint64_t i = first; i < trace_n; ++i) std::printf(" %05x", trace[i & 15]);
            std::printf("\n");
            std::printf("[2bl] regs: eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x "
                        "ebp=%08x esp=%08x\n", f.eax, f.ebx, f.ecx, f.edx, f.esi, f.edi, f.ebp,
                        f.esp);
            // Dump kernel scheduler state the idle loop watches (ebx≈PRCB, ebp≈DPC list head).
            // Kernel VAs are identity-mapped 0x80000000->0x0, so physical = VA & 0x07FFFFFF.
            auto kdump = [&](const char* tag, uint32_t va, int n) {
                if (!(va & 0x80000000u)) return;
                uint8_t buf[64] = {0};
                if (n > 64) n = 64;
                if (mach.read(va & 0x07FFFFFFu, buf, n)) {
                    std::printf("[2bl] %s @%08x:", tag, va);
                    for (int i = 0; i < n; ++i) std::printf(" %02x", buf[i]);
                    std::printf("\n");
                }
            };
            kdump("PRCB(ebx)", f.ebx, 64);
            kdump("DPCq(ebp)", f.ebp, 16);
            // Scan kernel RAM for KTHREAD dispatcher headers (Type=6, Size=0x44 -> "06 00 44 00")
            // to enumerate every thread and its State (+0x2c) / Priority (+0x32). This tells us
            // whether a phase-1/dashboard work thread exists besides the running idle thread.
            {
                const uint32_t base = 0x1000, len = 0x3FF0000;  // ~64 MB (all of RAM)
                std::vector<uint8_t> km(len);
                mach.read(base, km.data(), km.size());
                std::printf("[2bl] KTHREAD scan (State 1=Ready 2=Running 5=Waiting):\n");
                int found = 0;
                std::vector<uint32_t> waiters;
                for (size_t o = 0; o + 0x60 < km.size(); o += 4) {
                    // KTHREAD dispatcher header: Type byte = 6 (ThreadObject), Size byte = 0x44.
                    if (km[o] == 0x06 && km[o + 2] == 0x44 && km[o + 1] == 0 && km[o + 3] == 0) {
                        uint32_t va = 0x80000000u + base + static_cast<uint32_t>(o);
                        uint8_t state = km[o + 0x2c];
                        std::printf("[2bl]   KTHREAD=%08x State=%u Priority=%d WaitReason=%u\n", va,
                                    state, static_cast<int8_t>(km[o + 0x32]), km[o + 0x57]);
                        if (state == 5) waiters.push_back(va);  // Waiting
                        if (++found >= 16) break;
                    }
                }
                std::printf("[2bl] total KTHREADs found: %d\n", found);
                // Follow every waiting thread's wait block to the object it blocks on.
                for (uint32_t waiter : waiters) {
                    for (int r = 0; r < 128; r += 32) {  // raw KTHREAD dump for offset validation
                        uint8_t b[32] = {0};
                        mach.read((waiter + r) & 0x07FFFFFFu, b, 32);
                        std::printf("[2bl]   KT+%02x:", r);
                        for (int i = 0; i < 32; ++i) std::printf(" %02x", b[i]);
                        std::printf("\n");
                    }
                    // The wait block lives on the thread's kernel stack (0xD0000000 region), a
                    // non-identity mapping — translate through the page tables to read it.
                    auto read_va = [&](uint32_t va, void* buf, size_t n) {
                        uint32_t pa = 0;
                        return mach.cpu().translate(va, pa) && mach.read(pa, buf, n);
                    };
                    uint32_t wbl = 0, obj = 0, nextwb = 0;
                    mach.read((waiter + 0x58) & 0x07FFFFFFu, &wbl, 4);      // WaitBlockList
                    if (wbl & 0x80000000u) {
                        read_va(wbl + 0x0c, &obj, 4);     // KWAIT_BLOCK.Object (+0x08 is Thread)
                        read_va(wbl + 0x10, &nextwb, 4);  // NextWaitBlock
                    }
                    std::printf("[2bl] waiter %08x WaitBlockList=%08x Object=%08x nextWB=%08x\n",
                                waiter, wbl, obj, nextwb);
                    // Dump the waiter's kernel stack (saved ESP = KernelStack @+0x24) as dwords,
                    // to reconstruct its call chain — return addresses are kernel code pointers.
                    uint32_t ksp = 0;
                    mach.read((waiter + 0x24) & 0x07FFFFFFu, &ksp, 4);
                    for (int r = 0; r < 0x120; r += 32) {
                        uint8_t b[32] = {0};
                        if (!read_va(ksp + r, b, 32)) break;
                        std::printf("[2bl] stk %08x:", ksp + r);
                        for (int i = 0; i < 32; i += 4)
                            std::printf(" %02x%02x%02x%02x", b[i + 3], b[i + 2], b[i + 1], b[i]);
                        std::printf("\n");
                    }
                    if (obj & 0x80000000u) {
                        uint8_t hdr[16] = {0};
                        read_va(obj, hdr, 16);
                        int32_t sig = hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | (hdr[7] << 24);
                        std::printf("[2bl] wait object %08x: Type=%u Size=%u SignalState=%d "
                                    "(Type 6=Thread -> waiting for it to terminate)\n", obj,
                                    hdr[0], hdr[2], sig);
                        uint32_t po = 0, pi = 0, pp = 0;
                        mach.cpu().translate(obj, po);
                        mach.cpu().translate(0x8003d8f0, pi);
                        mach.cpu().translate(0x80001bf8, pp);
                        std::printf("[2bl] phys: waitobj=%08x idleThr(8003d8f0)=%08x "
                                    "phase1(80001bf8)=%08x\n", po, pi, pp);
                    }
                }
            }
            // The boot thread's last PCs before its final block-and-yield-to-idle.
            std::printf("[2bl] pre-idle PCs (last real work before blocking):");
            uint64_t pf = pre_idle_n > 64 ? pre_idle_n - 64 : 0;
            for (uint64_t i = pf; i < pre_idle_n; ++i) std::printf(" %08x", pre_idle[i & 63]);
            std::printf("\n");
            // Per-thread attribution + the non-idle threads' final instructions.
            std::printf("[2bl] insns by thread (sampled/64):");
            for (const auto& tc : thread_counts)
                std::printf(" %08x=%llu", tc.thread, (unsigned long long)tc.insns);
            std::printf("\n[2bl] last %llu non-idle PCs:",
                        (unsigned long long)(nonidle_n < 96 ? nonidle_n : 96));
            {
                uint64_t nf = nonidle_n > 96 ? nonidle_n - 96 : 0;
                for (uint64_t i = nf; i < nonidle_n; ++i)
                    std::printf(" %08x", nonidle_ring[i & 511]);
            }
            std::printf("\n");
            if (mach.nv2a())
                std::printf("[2bl] NV2A: vblanks=%llu pmc_intr_en=0x%08x pcrtc_intr_en=0x%08x\n",
                            (unsigned long long)mach.nv2a()->vblanks(),
                            mach.nv2a()->pmc_intr_en(), mach.nv2a()->pcrtc_intr_en());
            std::printf("[2bl] IDE: port accesses=%llu irqs=%llu sectors rd/wr=%llu/%llu "
                        "bm_base=0x%04x\n", (unsigned long long)ide_io_count,
                        (unsigned long long)(mach.ide() ? mach.ide()->irq_pulses() : 0),
                        (unsigned long long)(mach.ide() ? mach.ide()->sectors_read() : 0),
                        (unsigned long long)(mach.ide() ? mach.ide()->sectors_written() : 0),
                        mach.ide() ? mach.ide()->bm_base() : 0);
            std::printf("[2bl] MCPX ROM overlay disabled by firmware: %s\n",
                        mach.pci() && mach.pci()->mcpx_rom_disabled() ? "yes" : "no");
            std::printf("[2bl] IRQ0 pulses=%llu, pending-boundaries=%llu, delivered=%llu\n",
                        (unsigned long long)mach.irq0_pulses(),
                        (unsigned long long)mach.pending_boundaries(),
                        (unsigned long long)mach.injected_interrupts());
            if (mach.pic_dbg()) {
                auto& m = mach.pic_dbg()->master();
                auto& s = mach.pic_dbg()->slave();
                std::printf("[2bl] PIC master: imr=0x%02x irr=0x%02x isr=0x%02x base=0x%02x "
                            "init=%u | slave: imr=0x%02x irr=0x%02x base=0x%02x\n",
                            m.imr, m.irr, m.isr, m.irq_base, m.init_state, s.imr, s.irr,
                            s.irq_base);
            }
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
