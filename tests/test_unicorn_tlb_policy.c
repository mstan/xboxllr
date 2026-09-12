// Exercise the actual pinned implementation, including its private resize
// function, without adding test mutation hooks to the shipping CPU bridge.
// CMake derives this source with the existing fault-restoration patch and
// retains the upstream license. The test object supplies the TLB definitions
// instead of pulling the equivalent object from the static Unicorn archive.
#ifdef XLLE_TEST_HOST_TLB
#include "unicorn/cputlb-host-timed.c"
#else
#include "unicorn/cputlb.c"
#endif
#if defined(XLLE_TEST_EXPECT_FLUSH_CLOCK) && !defined(XLLE_TLB_RESIZE_FLUSH_WINDOW)
#error The policy fixture must exercise the generated flush-clock implementation
#endif
#if !defined(XLLE_TEST_EXPECT_FLUSH_CLOCK) && defined(XLLE_TLB_RESIZE_FLUSH_WINDOW)
#error The legacy fixture must exercise the generated host-clock implementation
#endif
#include "unicorn_aot_bridge.h"
#include "unicorn_metrics.h"

static void require(int ok, const char* why) {
    if (!ok) { fprintf(stderr, "TLB policy failure: %s\n", why); exit(1); }
}

typedef struct Result {
    size_t entries;
    int code_resident;
    uint32_t eax, eip, eflags;
} Result;

static Result probe(int64_t elapsed_ns, unsigned flushes, unsigned warm_pages) {
    uc_engine* engine = NULL;
    require(uc_open(UC_ARCH_X86, UC_MODE_32, &engine) == UC_ERR_OK, "open engine");
    require(uc_mem_map(engine, 0, 4 * 1024 * 1024, UC_PROT_ALL) == UC_ERR_OK, "map RAM");
    const uint8_t program[] = {0xb8, 0x78, 0x56, 0x34, 0x12, 0x90};
    require(uc_mem_write(engine, 0x1000, program, sizeof(program)) == UC_ERR_OK, "load authored program");
    CPUState* cpu = engine->cpu;
    CPUArchState* env = cpu->env_ptr;
    int index = cpu_mmu_index(env, true);
    CPUTLBDesc* desc = &env_tlb(env)->d[index];
    CPUTLBDescFast* fast = &env_tlb(env)->f[index];
    require(tlb_n_entries(fast) == 256, "pinned initial TLB capacity");

    // Identical translation use and explicit flush. Only elapsed time differs;
    // call the real policy with an injected timestamp rather than sleeping.
    tlb_window_reset(desc, 0, 0);
    for (unsigned flush = 0; flush < flushes; ++flush) {
        for (unsigned page = 0; page < warm_pages; ++page) {
            uint32_t address = 0x1000 + page * 4096;
            tlb_set_page(cpu, address, address, PAGE_READ | PAGE_WRITE | PAGE_EXEC, index, 4096);
        }
        tlb_flush_one_mmuidx_locked(env, index, elapsed_ns);
    }
    Result result = {0};
    result.entries = tlb_n_entries(fast);

    // Ten code pages collide in a 64-entry table; a 256-entry table spreads
    // them over four slots. Include the real eight-entry victim cache.
    for (unsigned i = 0; i < 10; ++i) {
        uint32_t address = 0x1000 + i * 0x40000;
        tlb_set_page(cpu, address, address, PAGE_READ | PAGE_WRITE | PAGE_EXEC, index, 4096);
    }
    result.code_resident = xlle_unicorn_aot_ram_ptr(engine, 0x1000, sizeof(program),
                                                  XLLE_UNICORN_AOT_ACCESS_FETCH) != NULL;
    // A cache miss must affect only eligibility. Unicorn still executes the
    // same authored guest instructions with the same architectural result.
    require(uc_emu_start(engine, 0x1000, 0x1000 + sizeof(program), 0, 0) == UC_ERR_OK, "execute authored program");
    require(uc_reg_read(engine, UC_X86_REG_EAX, &result.eax) == UC_ERR_OK, "read EAX");
    require(uc_reg_read(engine, UC_X86_REG_EIP, &result.eip) == UC_ERR_OK, "read EIP");
    require(uc_reg_read(engine, UC_X86_REG_EFLAGS, &result.eflags) == UC_ERR_OK, "read EFLAGS");
    require(uc_close(engine) == UC_ERR_OK, "close engine");
    return result;
}

#ifdef XLLE_TLB_RESIZE_FLUSH_WINDOW
static void mode_windows(void) {
    uc_engine* engine = NULL;
    require(uc_open(UC_ARCH_X86, UC_MODE_32, &engine) == UC_ERR_OK, "open mode fixture");
    require(uc_mem_map(engine, 0, 4 * 1024 * 1024, UC_PROT_ALL) == UC_ERR_OK, "map mode fixture");
    CPUState* cpu = engine->cpu;
    CPUArchState* env = cpu->env_ptr;
    int first = cpu_mmu_index(env, true), second = (first + 1) % NB_MMU_MODES;
    require(first != second, "two independent MMU modes");
    CPUTLBDesc* a = &env_tlb(env)->d[first];
    CPUTLBDesc* b = &env_tlb(env)->d[second];
    for (unsigned i = 1; i <= 250; ++i) {
        tlb_set_page(cpu, 0x1000, 0x1000, PAGE_READ | PAGE_WRITE | PAGE_EXEC, first, 4096);
        tlb_flush_one_mmuidx_locked(env, first, 0);
        require(a->window_begin_ns == i % 100, "window counter resets and remains bounded");
        require(b->window_begin_ns == 0 && tlb_n_entries(&env_tlb(env)->f[second]) == 256,
                "one mode does not age another");
    }
    require(tlb_n_entries(&env_tlb(env)->f[first]) == 64, "minimum size stable across repeated windows");
    for (unsigned i = 1; i <= 100; ++i) {
        tlb_set_page(cpu, 0x1000, 0x1000, PAGE_READ | PAGE_WRITE | PAGE_EXEC, second, 4096);
        tlb_flush_one_mmuidx_locked(env, second, 0);
    }
    require(a->window_begin_ns == 50 && b->window_begin_ns == 0 &&
            tlb_n_entries(&env_tlb(env)->f[second]) == 64, "independent second-mode shrink/reset");

    // A large-page invalidation flushes this entire mode, so it is a resize
    // opportunity. An unrelated ordinary-page invalidation does not age it.
    tlb_set_page(cpu, 0, 0, PAGE_READ | PAGE_WRITE | PAGE_EXEC, first, 2 * 1024 * 1024);
    tlb_flush_page_locked(env, first, 0x300000);
    require(a->window_begin_ns == 50, "ordinary page invalidation must not age window");
    tlb_flush_page_locked(env, first, 0x1000);
    require(a->window_begin_ns == 51 && b->window_begin_ns == 0,
            "large-page mode flush ages only its own window");
    require(uc_close(engine) == UC_ERR_OK, "close mode fixture");
}
#endif

int main(void) {
    xlle_guest_tsc_deterministic = 1;
    Result fast = probe(50000000, 1, 1), slow = probe(150000000, 1, 1);
#ifdef XLLE_TLB_RESIZE_FLUSH_WINDOW
    require(fast.entries == 256 && slow.entries == 256, "host delay cannot change cache capacity");
    require(fast.code_resident && slow.code_resident, "host delay cannot change code eligibility");
    Result before = probe(0, 99, 1), expired = probe(0, 100, 1);
    require(before.entries == 256 && expired.entries == 64, "shrink at logical per-mode resize window");
    require(before.code_resident && !expired.code_resident, "real shrink changes residency");
    Result grown = probe(0, 1, 192);
    require(grown.entries == 512 && grown.code_resident, "high occupancy retains adaptive growth");
    mode_windows();
#else
    require(fast.entries == 256 && slow.entries == 64, "host time changes cache capacity");
    require(fast.code_resident && !slow.code_resident, "capacity changes native code eligibility");
#endif
    require(fast.eax == 0x12345678 && fast.eip == 0x1006, "independent guest result");
    require(fast.eax == slow.eax && fast.eip == slow.eip && fast.eflags == slow.eflags,
            "guest result independent of cache policy");
#ifdef XLLE_TLB_RESIZE_FLUSH_WINDOW
    puts("TLB policy: host-time independent; shrink after 100 per-mode resize opportunities; adaptive growth; guest state equal");
#else
    puts("TLB policy: 50ms=256 entries/resident; 150ms=64 entries/decline; guest state equal");
#endif
    return 0;
}
