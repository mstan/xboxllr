#include "aot_programs.h"
#include "machine/machine.hpp"
#include "cpu/unicorn_floor.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <array>
#include <vector>
#include <cstring>
#include <algorithm>

using namespace xlle;
void test_aot_machine_state();
void test_aot_irq_shadow();
void test_aot_store_fault();
static void require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "AOT machine failure: %s\n", why); std::exit(1); }
}

static void prepare(XboxMachine& machine, const xlle_aot_region& region) {
    require(machine.ok() && machine.init_ram(), "machine RAM and floor");
    require(machine.cpu().setup_flat_segments(0x8000), "flat protected segments");
    require(machine.cpu().write_mem(region.base, region.code, region.code_size), "code load");
    // Keep an executed fallback entry before the native self-loop so both
    // test arms exercise the same initial guest instruction.
    const uint8_t entry[] = {0xe9, 0xfb, 0, 0, 0};
    require(machine.cpu().write_mem(region.base - 0x100, entry, sizeof(entry)), "entry jump");
    X86Regs r{};
    r.eip = region.base - 0x100;
    r.esp = 0x9000;
    r.eflags = 2;
    machine.cpu().set_regs(r);
}

static void loop(bool native) {
    XboxMachine machine;
    prepare(machine, xlle_aot_loop);
    if (native) require(machine.register_aot_region(xlle_aot_loop), "register native region");
    std::string err;
    require(machine.run(0xf00, 101, err) == 101, "exact loop instruction budget");
    require(err.empty(), err.c_str());
    const auto regs = machine.cpu().get_regs();
    require(regs.eax == 50 && regs.eip == 0x1000, "independent loop state");
    require(machine.total_insns() == 101, "single machine clock");
    const auto stats = machine.aot_stats();
    if (native) {
        require(stats.retired > 0 && stats.fallback_insns > 0, "both native and fallback execute");
        require(stats.retired + stats.fallback_insns == 101, "disjoint retirement accounting");
        std::printf("AOT machine: native=%llu fallback=%llu handoffs=%llu\n",
            static_cast<unsigned long long>(stats.retired),
            static_cast<unsigned long long>(stats.fallback_insns),
            static_cast<unsigned long long>(stats.handoffs));
    }
}

static uint32_t read_word(XboxMachine& m, uint32_t address) {
    uint32_t result = 0;
    require(m.cpu().read_mem(address, &result, 4), "read physical test word");
    return result;
}

static void write_word(XboxMachine& m, uint32_t address, uint32_t value) {
    require(m.cpu().write_mem(address, &value, 4), "write physical test word");
}

static void run_exact(XboxMachine& m, uint32_t pc, uint64_t budget) {
    std::string err;
    const auto n = m.run(pc, budget, err);
    require(err.empty(), err.c_str());
    require(n == budget, "exact independent workload budget");
}

static void continuation(bool native, bool tsc) {
    XboxMachine m;
    prepare(m, xlle_aot_machine_mixed);
    // BSWAP (or RDTSC) lives outside registered native code. Its jump
    // returns to a native block, repeatedly, within the same machine slice.
    uint8_t fallback[] = {0x0f, uint8_t(tsc ? 0x31 : 0xc8), 0xe9, 0xe9, 0xff, 0xff, 0xff};
    require(m.cpu().write_mem(0x1010, fallback, sizeof(fallback)), "fallback block");
    if (native) require(m.register_aot_region(xlle_aot_machine_mixed), "mixed region");
    run_exact(m, 0xf00, 81);
    uint32_t expected = 0;
    for (unsigned i = 0; i < 20; ++i) {
        ++expected;
        expected = (expected >> 24) | ((expected >> 8) & 0xff00) |
                   ((expected << 8) & 0xff0000) | (expected << 24);
    }
    if (tsc) expected = 79;  // last fallback TB starts after 1 + 19*4 + 2
    const auto r = m.cpu().get_regs();
    require(r.eax == expected && r.eip == 0x1000, "continuation independent state/TSC");
    if (tsc) require(r.edx == 0, "guest TSC high half");
    if (native) {
        const auto s = m.aot_stats();
        require(s.retired == 40 && s.fallback_insns == 41, "repeated tier accounting");
        require(s.handoffs >= 20, "fallback reenters native in same slice");
    }
}

// Nonidentity 4 KiB paging: code/data map to physical 0x21000/0x22000.
// Two virtual ranges alias the same PTEs. A/D bits start clear so real Unicorn
// execution must establish access/dirty semantics before direct native access.
static void paging(XboxMachine& m, const xlle_aot_region& region, bool readonly = false) {
    std::array<uint32_t, 1024> pt{}, pd{};
    for (uint32_t i = 0; i < 1024; ++i) pt[i] = i * 4096 + 3;
    pt[1] = 0x21003;
    pt[2] = readonly ? 0x22001 : 0x22003;
    pd[0] = 0x18003;
    pd[16] = 0x18003;
    require(m.cpu().write_mem(0x21000, region.code, region.code_size), "paged code");
    require(m.cpu().write_mem(0x18000, pt.data(), sizeof(pt)), "page table");
    require(m.cpu().write_mem(0x19000, pd.data(), sizeof(pd)), "page directory");
    uint32_t cr3 = 0x19000, cr0 = 0;
    require(m.cpu().read_reg(XReg::CR0, &cr0, 4), "CR0");
    cr0 |= 0x80010001u;  // paging + supervisor write protection + protected mode
    require(m.cpu().write_reg(XReg::CR3, &cr3, 4), "load CR3");
    require(m.cpu().write_reg(XReg::CR0, &cr0, 4), "enable paging");
}

static void memory(bool native, bool alias, bool paged) {
    XboxMachine m;
    const auto& region = alias ? xlle_aot_machine_alias : xlle_aot_machine_memory;
    prepare(m, region);
    if (paged) paging(m, region);
    const uint32_t physical = paged ? 0x22000 : 0x2000;
    write_word(m, physical, 0x100);
    if (native) require(m.register_aot_region(region), "memory region");
    run_exact(m, 0xf00, 101);
    const auto r = m.cpu().get_regs();
    require(r.eax == 0x119 && r.eip == 0x1000, "memory loop independent registers");
    require(read_word(m, physical) == 0x119, "physical RAM receives native writes");
    require(read_word(m, physical + XboxMachine::kRamSize) == 0x119, "physical RAM alias");
    if (paged) {
        require((read_word(m, 0x18004) & 0x20) != 0, "code PTE accessed");
        require((read_word(m, 0x18008) & 0x60) == 0x60, "data PTE accessed and dirty");
    }
    if (native) {
        const auto s = m.aot_stats();
        require(s.retired > 0 && s.memory_reads > 0 && s.memory_writes > 0,
                "native memory path actually executed");
        require(s.retired + s.fallback_insns == 101, "memory retirement partition");
    }
}

static void stale_translation(bool native) {
    XboxMachine m;
    prepare(m, xlle_aot_machine_memory);
    paging(m, xlle_aot_machine_memory);
    write_word(m, 0x22000, 100);
    if (native) require(m.register_aot_region(xlle_aot_machine_memory), "TLB region");
    run_exact(m, 0xf00, 41);
    require(read_word(m, 0x22000) == 110, "warm paged loop");
    std::vector<uint8_t> changed(xlle_aot_machine_memory.code,
                               xlle_aot_machine_memory.code + xlle_aot_machine_memory.code_size);
    changed[5] = 0x48;  // DEC instead of INC
    require(m.cpu().write_mem(0x23000, changed.data(), changed.size()), "alternate code page");
    write_word(m, 0x18004, 0x23003);
    run_exact(m, 0x1000, 40);
    require(read_word(m, 0x22000) == 120, "PTE change alone retains cached translation");
    const auto before = m.aot_stats().retired;
    uint32_t cr3 = 0x19000;
    require(m.cpu().write_reg(XReg::CR3, &cr3, 4), "explicit TLB invalidation");
    run_exact(m, 0x1000, 40);
    require(read_word(m, 0x22000) == 110, "reloaded translation executes changed bytes");
    if (native) {
        require(m.aot_stats().retired == before, "stale generated code declined after remap");
        require(m.aot_stats().exits[XLLE_AOT_CODE_CHANGED] > 0, "remapped byte mismatch reported");
    }
}

static void alias_code_write(bool native) {
    XboxMachine m;
    prepare(m, xlle_aot_loop);
    if (native) require(m.register_aot_region(xlle_aot_loop), "alias code region");
    run_exact(m, 0xf00, 101);
    const uint8_t dec = 0x48;
    require(m.cpu().write_mem(0x04001000, &dec, 1), "code write through physical alias");
    const auto before = m.aot_stats().retired;
    run_exact(m, 0x1000, 100);
    require(m.cpu().get_regs().eax == 0, "fallback observes alias code modification");
    require(m.aot_stats().retired == before, "changed generated code never reused");
    if (native) require(m.aot_stats().exits[XLLE_AOT_CODE_CHANGED] > 0, "alias mismatch reported");
}

static void mmio_or_fault(bool native, bool mapped) {
    XboxMachine m;
    prepare(m, xlle_aot_machine_mmio);
    unsigned reads = 0;
    if (mapped) require(m.cpu().map_mmio(0x0e000000, 4096,
        [&](uint32_t addr, int size) {
            ++reads;
            require(addr == 0x0e000000 && size == 4, "device access identity");
            require(m.cpu().get_regs().ebx == reads, "native register effect precedes MMIO");
            return 11u * reads;
        }, [](uint32_t, int, uint32_t) { require(false, "no unexpected MMIO write"); }), "map device");
    if (native) require(m.register_aot_region(xlle_aot_machine_mmio), "MMIO region");
    std::string err;
    auto n = m.run(0xf00, 41, err);
    const auto r = m.cpu().get_regs();
    if (mapped) {
        require(err.empty(), err.c_str());
        require(n == 41 && reads == 10 && r.eax == 111 && r.ebx == 10,
                "MMIO executes exactly once per iteration");
    } else {
        if (err.empty() || r.eip != 0x1001)
            std::fprintf(stderr, "fault arm native=%d n=%llu eip=%08x eax=%08x ebx=%08x error=%s\n",
                native, static_cast<unsigned long long>(n), r.eip, r.eax, r.ebx, err.c_str());
        require(!err.empty() && reads == 0 && r.eip == 0x1001,
                "unmapped access faults at original instruction");
        require(r.eax == 0 && r.ebx == 1, "fault preserves only preceding effects");
        require(n == 2 && m.total_insns() == 2, "faulting load and TB suffix do not retire");
    }
    if (native) require(m.aot_stats().retired > 0 && m.aot_stats().memory_declines > 0,
                        "native prefix and no-effect memory decline exercised");
}

static void write_protection(bool native) {
    XboxMachine m;
    prepare(m, xlle_aot_machine_memory);
    paging(m, xlle_aot_machine_memory, true);
    write_word(m, 0x22000, 50);
    // Populate read permission but not write permission before native entry.
    const uint8_t entry[] = {0xa1, 0, 0x20, 0, 0, 0xe9, 0xf6, 0, 0, 0};
    require(m.cpu().write_mem(0xf00, entry, sizeof(entry)), "read-only bootstrap");
    if (native) require(m.register_aot_region(xlle_aot_machine_memory), "read-only region");
    std::string err;
    const auto n = m.run(0xf00, 100, err);
    const auto r = m.cpu().get_regs();
    require(!err.empty() && r.eip == 0x1006, "write protection fault EIP");
    require(r.eax == 51 && read_word(m, 0x22000) == 50, "faulting write has no effects");
    require(n == 4 && m.total_insns() == 4, "faulting store and TB suffix do not retire");
    require((read_word(m, 0x18008) & 0x40) == 0, "faulting write leaves dirty clear");
    if (native) require(m.aot_stats().retired == 2 && m.aot_stats().memory_declines > 0,
                        "native read/inc then precise write decline");
}

static void stale_write_permission_after_native_tlb() {
    XboxMachine m;
    prepare(m, xlle_aot_machine_memory);
    paging(m, xlle_aot_machine_memory);
    write_word(m, 0x22000, 50);
    run_exact(m, 0xf00, 5);
    require(read_word(m, 0x22000) == 51, "writable warm-up store commits");
    write_word(m, 0x22000, 50);
    auto regs = m.cpu().get_regs();
    regs.eip = xlle_aot_machine_memory.base;
    regs.eax = 0;
    m.cpu().set_regs(regs);
    require(m.register_aot_region(xlle_aot_machine_memory), "stale permission native region");

    run_exact(m, xlle_aot_machine_memory.base, 4);
    require(read_word(m, 0x22000) == 51, "initial writable native store commits");
    auto before = m.aot_stats();
    const uint64_t before_total = m.total_insns();
    require(before.memory_reads == 1 && before.memory_writes == 1 && before.retired == 4,
            "initial write actually used native store path");

    // Revoke write permission after the writable TLB path has been exercised,
    // then reload CR3 so a direct host write must not rely on stale permission.
    write_word(m, 0x18008, 0x22001);
    uint32_t cr3 = 0x19000;
    require(m.cpu().write_reg(XReg::CR3, &cr3, 4), "reload CR3 after revoking write permission");

    // Stay on the same mapped code page and warm FETCH plus READ under Unicorn,
    // but never WRITE. A stale invocation-local write pointer would skip the
    // post-CR3 permission check; a correct cache reset reaches native load/inc
    // and then declines specifically at the store.
    const uint8_t bootstrap[] = {
        0x8b, 0x15, 0x00, 0x20, 0x00, 0x00,  // MOV EDX,[0x2000]
        0xe9, 0xf5, 0xfe, 0xff, 0xff         // JMP 0x1000 from 0x1106
    };
    require(m.cpu().write_mem(0x21100, bootstrap, sizeof(bootstrap)),
            "same-page post-revoke bootstrap");

    std::string err;
    const auto n = m.run(0x1100, 6, err);
    const auto r = m.cpu().get_regs();
    require(!err.empty(), "revoked native store faults loudly");
    require(n == 4 && m.total_insns() == before_total + 4,
            "revoked store retires bootstrap and native prefix only");
    require(r.eip == 0x1006 && r.eax == 52, "revoked store preserves precise prefix state");
    require(read_word(m, 0x22000) == 51, "revoked native store has no memory effect");
    require((read_word(m, 0x18008) & 0x40) == 0, "revoked native store leaves dirty clear");
    const auto after = m.aot_stats();
    require(after.memory_writes == before.memory_writes,
            "declined revoked write does not count as native write");
    require(after.memory_reads > before.memory_reads &&
            after.memory_declines > before.memory_declines &&
            after.retired == before.retired + 2,
            "revoked write reaches native read/inc then declines current write permission");
}

static void unaligned_same_page_store_bytes(bool native) {
    XboxMachine m;
    prepare(m, xlle_aot_machine_unaligned_store);
    const std::array<uint8_t, 8> initial{{0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7}};
    require(m.cpu().write_mem(0x2000, initial.data(), initial.size()), "unaligned initial bytes");
    uint32_t entry = 0xf00;
    uint64_t budget = 5;
    if (native) {
        run_exact(m, 0xf00, 5);
        require(m.cpu().write_mem(0x2000, initial.data(), initial.size()),
                "reset warmed unaligned bytes");
        auto warm = m.cpu().get_regs();
        warm.eip = xlle_aot_machine_unaligned_store.base;
        warm.eax = 0;
        m.cpu().set_regs(warm);
        require(m.register_aot_region(xlle_aot_machine_unaligned_store),
                "unaligned native store region");
        entry = xlle_aot_machine_unaligned_store.base;
        budget = 4;
    }

    std::string err;
    const auto n = m.run(entry, budget, err);
    require(err.empty(), err.c_str());
    const auto r = m.cpu().get_regs();
    require(n == budget && r.eip == 0x1000 && r.eax == 0xa4a3a2a2,
            "unaligned load/inc/store executes exactly one loop body");
    std::array<uint8_t, 8> bytes{};
    require(m.cpu().read_mem(0x2000, bytes.data(), bytes.size()), "unaligned result bytes");
    const std::array<uint8_t, 8> expected{{0xa0, 0xa2, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7}};
    require(bytes == expected, "unaligned store writes exact little-endian bytes only");
    if (native) {
        const auto s = m.aot_stats();
        require(s.memory_reads == 1 && s.memory_writes == 1 && s.retired == 4,
                "unaligned read/write actually use native memory path");
    }
}

static void cross_page(bool native, bool contiguous, bool readonly) {
    XboxMachine m;
    prepare(m, xlle_aot_machine_crosspage);
    paging(m, xlle_aot_machine_crosspage);
    const uint32_t second = contiguous ? 0x23000 : 0x24000;
    write_word(m, 0x1800c, second | (readonly ? 1 : 3));
    const uint16_t lo = 0xffff, hi = 0x1234;
    require(m.cpu().write_mem(0x22ffe, &lo, 2) &&
            m.cpu().write_mem(second, &hi, 2), "cross-page initial data");
    // Warm both read TLB entries before the native candidate, with no stores.
    const uint8_t entry[] = {0xa1, 0xfe, 0x2f, 0, 0, 0xe9, 0xf6, 0, 0, 0};
    require(m.cpu().write_mem(0xf00, entry, sizeof(entry)), "cross-page bootstrap");
    if (native) require(m.register_aot_region(xlle_aot_machine_crosspage), "cross-page region");
    std::string err;
    const auto n = m.run(0xf00, 42, err);
    uint16_t after_lo = 0, after_hi = 0;
    require(m.cpu().read_mem(0x22ffe, &after_lo, 2) &&
            m.cpu().read_mem(second, &after_hi, 2), "cross-page result data");
    if (readonly) {
        require(!err.empty() && m.cpu().get_regs().eip == 0x1006, "second-page write fault");
        require(after_lo == lo && after_hi == hi, "faulting split store has no partial effects");
        require(n == 4 && m.cpu().get_regs().eax == 0x12350000, "split fault prefix accounting");
    } else {
        require(err.empty() && n == 42, "cross-page workload completes");
        require(after_lo == 9 && after_hi == 0x1235, "cross-page independent result");
    }
    if (native) {
        const auto s = m.aot_stats();
        require(s.invocations > 0, "cross-page test reaches generated code");
        if (!contiguous) require(s.retired == 0, "noncontiguous span declines before native effects");
        if (contiguous) require(s.memory_reads > 0, "contiguous native split load");
        if (contiguous && !readonly) require(s.memory_writes > 0, "contiguous native split store");
        if (!contiguous || readonly) require(s.memory_declines > 0, "split access safely declines");
        require(s.retired + s.fallback_insns == n, "split access tier accounting");
    }
}

static void native_alias_self_modify(bool native) {
    XboxMachine m;
    prepare(m, xlle_aot_machine_smc_alias);
    // Prime the alias write TLB without changing code, then load the new bytes.
    const uint8_t entry[] = {0xb8, 0xb8, 0x44, 0x33, 0x22,
        0xa3, 0x05, 0x10, 0x00, 0x04,
        0xb8, 0xb8, 0xef, 0xbe, 0xad, 0xe9, 0xec, 0, 0, 0};
    require(m.cpu().write_mem(0xf00, entry, sizeof(entry)), "native SMC bootstrap");
    if (native) require(m.register_aot_region(xlle_aot_machine_smc_alias), "native SMC region");
    run_exact(m, 0xf00, 7);
    if (m.cpu().get_regs().eax != 0x11adbeef || m.cpu().get_regs().eip != 0x1000)
        std::fprintf(stderr, "SMC native=%d eax=%08x eip=%08x native_insns=%llu native_writes=%llu\n",
            native, m.cpu().get_regs().eax, m.cpu().get_regs().eip,
            static_cast<unsigned long long>(m.aot_stats().retired),
            static_cast<unsigned long long>(m.aot_stats().memory_writes));
    require(m.cpu().get_regs().eax == 0x11adbeef && m.cpu().get_regs().eip == 0x1000,
            "alias SMC executes new immediate, not generated stale bytes");
    require(read_word(m, 0x1005) == 0xadbeefb8, "native code write visible through base alias");
    if (native) {
        const auto s = m.aot_stats();
        require(s.memory_writes == 1 && s.retired == 1, "SMC write actually executes natively");
        require(s.exits[XLLE_AOT_CODE_CHANGED] > 0, "same invocation detects native alias write");
    }
}

static void precise_tsc_variants() {
    for (bool observer : {false, true}) for (bool msr : {false, true}) {
        XboxMachine m;
        prepare(m, xlle_aot_machine_tsc_prefix);
        const uint8_t opcode = msr ? 0x32 : 0x31;
        require(m.cpu().write_mem(0x1002, &opcode, 1), "TSC opcode variant");
        auto r = m.cpu().get_regs();
        r.ecx = 0x10;  // IA32_TSC for RDMSR
        m.cpu().set_regs(r);
        require(m.register_aot_region(xlle_aot_inc_eax), "TSC native prefix only");
        if (observer) m.cpu().on_retired([](const RetiredInsn&) {});
        run_exact(m, 0xf00, 61);
        r = m.cpu().get_regs();
        if (r.eax != 59 || r.edx != 0) std::fprintf(stderr,
            "TSC variant observer=%d msr=%d eax=%u edx=%u\n", observer, msr, r.eax, r.edx);
        require(r.eax == 59 && r.edx == 0, "RDTSC/RDMSR and instruction clock agree");
        require((m.aot_stats().retired > 0) != observer, "observer preserves diagnostic execution");
    }
    XboxMachine m;
    prepare(m, xlle_aot_loop);
    const uint8_t entry[] = {0x40, 0x0f, 0x31, 0xe9, 0xf8, 0, 0, 0};
    require(m.cpu().write_mem(0xf00, entry, sizeof(entry)), "first-TB TSC prefix");
    run_exact(m, 0xf00, 3);
    require(m.cpu().get_regs().eax == 1, "bootstrap TSC has exact prefix and fresh machine base");
    for (bool native : {false, true}) {
        XboxMachine w;
        prepare(w, xlle_aot_loop);
        // MOV ECX,10; MOV EDX,deadbeef; MOV EAX,1000; WRMSR; JMP loop.
        const uint8_t entry[] = {0xb9, 0x10, 0, 0, 0, 0xba, 0xef, 0xbe, 0xad, 0xde,
            0xb8, 0xe8, 3, 0, 0, 0x0f, 0x30, 0xe9, 0xea, 0, 0, 0};
        const uint8_t body[] = {0x40, 0x0f, 0x31, 0xeb, 0xfb};
        require(w.cpu().write_mem(0xf00, entry, sizeof(entry)) &&
                w.cpu().write_mem(0x1000, body, sizeof(body)), "P6 TSC write workload");
        if (native) require(w.register_aot_region(xlle_aot_inc_eax), "TSC write native continuation");
        run_exact(w, 0xf00, 8);
        const auto r = w.cpu().get_regs();
        if (r.eax != 1003 || r.edx != 0) std::fprintf(stderr,
            "TSC write native=%d eax=%u edx=%u\n", native, r.eax, r.edx);
        require(r.eax == 1003 && r.edx == 0,
                "P6 WRMSR clears high half and TSC offset survives native continuation");
    }
}

static void native_device_deadlines(bool native) {
    XboxMachine::Config options{};
    options.cpu_chunk_insns = 10000000;  // force device deadlines to bind
    XboxMachine m(options);
    prepare(m, xlle_aot_loop);
    m.init_devices();
    if (native) require(m.register_aot_region(xlle_aot_loop), "deadline region");
    std::vector<uint64_t> polls;
    m.on_host_poll([&]() {
        polls.push_back(m.total_insns());
        require(m.cpu().get_regs().eax == m.total_insns() / 2,
                "host service observes all CPU effects through deadline");
    });
    // ceil(N * 733333333 / 1000), calculated independently of scheduler code.
    const uint64_t first = 733334, second = 1466667;
    run_exact(m, 0xf00, 2 * first + 1);
    require(std::find(polls.begin(), polls.end(), first) != polls.end() &&
            std::find(polls.begin(), polls.end(), second) != polls.end(),
            "both CPU tiers stop exactly at successive device/host deadlines");
    if (native) require(m.aot_stats().retired == 2 * first && m.aot_stats().fallback_insns == 1,
            "deadline test executes natively after bootstrap");
}

static void observed_entry() {
    XboxMachine machine;
    prepare(machine, xlle_aot_loop);
    require(machine.register_aot_region(xlle_aot_loop), "observer native region");
    auto& cpu = static_cast<UnicornFloor&>(machine.cpu());
    unsigned calls = 0;
    require(cpu.observe_entry(0x1000, [&] { ++calls; }), "install entry observer");
    std::string err;
    require(machine.run(0xf00, 101, err) == 101 && err.empty(), "observed original executes");
    require(cpu.get_regs().eax == 50 && calls == 50, "observer does not replace guest body");
    require(machine.aot_stats().retired == 0, "observed entry cannot be bypassed by AOT");
}

int main() {
    observed_entry();
    loop(false);
    loop(true);
    for (bool native : {false, true}) {
        continuation(native, false);
        continuation(native, true);
        for (bool alias : {false, true})
            for (bool paged : {false, true}) memory(native, alias, paged);
        stale_translation(native);
        mmio_or_fault(native, true);
        mmio_or_fault(native, false);
        write_protection(native);
        unaligned_same_page_store_bytes(native);
        for (bool contiguous : {false, true})
            for (bool readonly : {false, true}) cross_page(native, contiguous, readonly);
        native_alias_self_modify(native);
    }
    stale_write_permission_after_native_tlb();
    alias_code_write(false);
    alias_code_write(true);
    test_aot_machine_state();
    test_aot_irq_shadow();
    test_aot_store_fault();
    precise_tsc_variants();
    native_device_deadlines(false);
    native_device_deadlines(true);
    for (bool native : {false, true}) {
        XboxMachine m;
        prepare(m, xlle_aot_machine_tsc_prefix);
        if (native) require(m.register_aot_region(xlle_aot_machine_tsc_prefix), "TSC prefix region");
        run_exact(m, 0xf00, 61);
        const auto r = m.cpu().get_regs();
        if (r.eax != 59) std::fprintf(stderr, "TSC prefix native=%d actual=%u expected=59\n", native, r.eax);
        require(r.eax == 59 && r.edx == 0 && r.eip == 0x1000,
                "RDTSC uses exact instruction clock independent of tier boundaries");
        if (native) require(m.aot_stats().retired > 0, "TSC prefix actually executes native");
    }
    std::puts("test_aot_machine: machine integration checks passed");
}
