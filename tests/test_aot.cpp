#include "aot_programs.h"
#include "cpu/unicorn_floor.hpp"
#include "spike/fixtures.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace xlle;

static void require(bool value, const char* why) {
    if (!value) { std::cerr << "AOT failure: " << why << '\n'; std::exit(1); }
}

struct Access {
    uint32_t address, size;
    bool write;
    bool operator==(const Access& other) const {
        return address == other.address && size == other.size && write == other.write;
    }
};

struct Memory {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x3000, 0);
    std::vector<Access> accesses;
    xlle_aot_bus bus{};
    bool reject = false;
    bool reject_reads = false;
    bool reject_writes = false;
    bool arm_event_after_write = false;

    explicit Memory(const xlle_aot_region& region) {
        require(region.base >= 0x1000 && region.base + region.code_size <= 0x4000,
                "test region fits mapped memory");
        std::copy(region.code, region.code + region.code_size,
                  bytes.begin() + region.base - 0x1000);
        bus.context = XLLE_AOT_CONTEXT_FLAT32_RAM;
        bus.code_base = region.base;
        bus.code = bytes.data() + region.base - 0x1000;
        bus.code_size = region.code_size;
        bus.opaque = this;
        bus.read32 = read;
        bus.write32 = write;
    }

    static bool range(Memory& m, uint32_t address) {
        return !m.reject && address >= 0x1000 && (uint64_t)address + 4 <= 0x4000;
    }
    static int read(void* opaque, uint32_t address, uint32_t* value) {
        auto& m = *static_cast<Memory*>(opaque);
        if (m.reject_reads) return 0;
        if (!range(m, address)) return 0;
        *value = 0;
        for (unsigned i = 0; i < 4; ++i)
            *value |= (uint32_t)m.bytes[address - 0x1000 + i] << (i * 8);
        m.accesses.push_back({address, 4, false});
        return 1;
    }
    static int write(void* opaque, uint32_t address, uint32_t value) {
        auto& m = *static_cast<Memory*>(opaque);
        if (m.reject_writes) return 0;
        if (!range(m, address)) return 0;
        for (unsigned i = 0; i < 4; ++i)
            m.bytes[address - 0x1000 + i] = (uint8_t)(value >> (i * 8));
        m.accesses.push_back({address, 4, true});
        if (m.arm_event_after_write) m.bus.event_pending = 1;
        return 1;
    }
};

struct CodeOnlyBus {
    std::vector<uint8_t> code;
    xlle_aot_bus bus{};

    CodeOnlyBus(const xlle_aot_region& region, uint32_t context, uint32_t code_limit) {
        code.assign(region.code, region.code + region.code_size);
        bus.context = context;
        bus.code_base = region.base;
        bus.code = code.data();
        bus.code_size = code.size();
        bus.code_limit = code_limit;
    }
};

static void poke32(std::vector<uint8_t>& bytes, uint32_t address, uint32_t value) {
    require(address >= 0x1000 && (uint64_t)address + 4 <= 0x4000, "poke address in mapped RAM");
    for (unsigned i = 0; i < 4; ++i)
        bytes[address - 0x1000 + i] = (uint8_t)(value >> (i * 8));
}

static uint32_t peek32(const std::vector<uint8_t>& bytes, uint32_t address) {
    require(address >= 0x1000 && (uint64_t)address + 4 <= 0x4000, "peek address in mapped RAM");
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= (uint32_t)bytes[address - 0x1000 + i] << (i * 8);
    return value;
}

static xlle_aot_state from_regs(const X86Regs& r) {
    return {{r.eax, r.ecx, r.edx, r.ebx, r.esp, r.ebp, r.esi, r.edi},
            r.eip, r.eflags, 0};
}

static X86Regs to_regs(const xlle_aot_state& s) {
    X86Regs r;
    r.eax = s.gpr[0]; r.ecx = s.gpr[1]; r.edx = s.gpr[2]; r.ebx = s.gpr[3];
    r.esp = s.gpr[4]; r.ebp = s.gpr[5]; r.esi = s.gpr[6]; r.edi = s.gpr[7];
    r.eip = s.eip; r.eflags = s.eflags;
    return r;
}

static void equal_state(const xlle_aot_state& a, const xlle_aot_state& b,
                        uint32_t flags, const char* label) {
    if (!std::equal(a.gpr, a.gpr + 8, b.gpr) || a.eip != b.eip ||
        ((a.eflags ^ b.eflags) & flags) != 0) {
        std::cerr << label << " EIP=" << std::hex << a.eip << '/' << b.eip
                  << " FLAGS=" << a.eflags << '/' << b.eflags << std::dec << '\n';
        require(false, "CPU state differs");
    }
}

static xlle_aot_state oracle(const xlle_aot_region& region,
                             const xlle_aot_state& initial, uint64_t instructions,
                             const std::vector<uint8_t>& memory,
                             std::vector<uint8_t>* result = nullptr,
                             std::vector<Access>* accesses = nullptr) {
    UnicornFloor cpu;
    require(cpu.ok(), "Unicorn opens");
    // Fresh instance, instruction scheduling throughout. Never change count modes.
    cpu.on_retired([](const RetiredInsn&) {});
    if (accesses) cpu.on_mem([&](const MemEvent& e) {
        accesses->push_back({e.addr, e.size, e.write != 0});
    });
    require(cpu.map_mem(0x1000, memory.size()), "oracle map");
    require(cpu.write_mem(0x1000, memory.data(), memory.size()), "oracle memory");
    cpu.set_regs(to_regs(initial));
    std::string error;
    auto retired = cpu.run(initial.eip, instructions, error);
    require(error.empty(), error.c_str());
    require(retired == instructions, "oracle exact synthetic budget");
    auto state = from_regs(cpu.get_regs());
    state.retired = retired;
    if (result) {
        result->resize(memory.size());
        require(cpu.read_mem(0x1000, result->data(), result->size()), "oracle memory result");
    }
    (void)region;
    return state;
}

static void differential_with_memory(const xlle_aot_region& region, xlle_aot_state initial,
                                     uint32_t flags, const std::vector<uint8_t>* seeded_memory) {
    initial.eip = region.base;
    Memory memory(region);
    if (seeded_memory) {
        require(seeded_memory->size() == memory.bytes.size(), "seeded memory size");
        memory.bytes = *seeded_memory;
        memory.bus.code = memory.bytes.data() + region.base - 0x1000;
    }
    const auto before = memory.bytes;
    auto native = initial;
    auto exit = region.run(&region, &native, &memory.bus, 100);
    require(exit == XLLE_AOT_COMPLETE, "generated program completes");
    require(native.retired > 0, "AOT actually executes instructions");
    std::vector<uint8_t> expected_memory;
    std::vector<Access> expected_accesses;
    auto expected = oracle(region, initial, native.retired, before,
                           &expected_memory, &expected_accesses);
    equal_state(native, expected, flags, "differential");
    require(memory.bytes == expected_memory, "memory contents match Unicorn");
    require(memory.accesses == expected_accesses, "memory event ordering matches Unicorn");
}

static void differential(const xlle_aot_region& region, xlle_aot_state initial,
                         uint32_t flags) {
    differential_with_memory(region, initial, flags, nullptr);
}

static void independent_fixtures() {
    const xlle_aot_region* regions[] = {
        &xlle_aot_mov_eax_imm, &xlle_aot_add_eax_ebx, &xlle_aot_sub_eax_ebx,
        &xlle_aot_xor_eax_eax, &xlle_aot_inc_eax, &xlle_aot_mov_add_two,
        &xlle_aot_mov_store_mem, &xlle_aot_and_eax_imm, &xlle_aot_cmp_je_taken
    };
    unsigned tested = 0;
    for (const auto& f : corpus()) {
        for (const auto* r : regions) {
            if (f.code.size() != r->code_size ||
                !std::equal(f.code.begin(), f.code.end(), r->code)) continue;
            Memory memory(*r);
            auto s = from_regs(f.init);
            require(r->run(r, &s, &memory.bus, f.insns) == XLLE_AOT_COMPLETE,
                    "hand-derived fixture completes");
            require(s.retired == f.insns, "hand-derived fixture instruction count");
            auto expected = from_regs(f.expect);
            for (unsigned reg = 0; reg < 8; ++reg)
                if (f.reg_mask & (1u << reg)) require(s.gpr[reg] == expected.gpr[reg], "fixture GPR");
            if (f.reg_mask & (1u << 8)) require(s.eip == expected.eip, "fixture EIP");
            require(((s.eflags ^ expected.eflags) & f.eflags_mask) == 0, "fixture defined flags");
            if (f.has_mem) {
                uint32_t v = 0;
                require(Memory::read(&memory, f.mem_addr, &v) && v == f.mem_expect, "fixture RAM");
            }
            ++tested;
        }
    }
    require(tested == 9, "all selected independent fixtures ran");
}

static void guards_and_exits() {
    const auto& r = xlle_aot_mov_eax_imm;
    Memory m(r);
    xlle_aot_state initial{}; initial.eip = r.base; initial.eflags = 2;
    auto s = initial;
    require(r.run(&r, &s, &m.bus, 0) == XLLE_AOT_BUDGET && s.retired == 0, "zero budget");
    m.bytes[1] ^= 1;
    require(r.run(&r, &s, &m.bus, 10) == XLLE_AOT_CODE_CHANGED && s.retired == 0, "changed bytes");
    m.bytes[1] ^= 1;
    m.bus.context = 0;
    require(r.run(&r, &s, &m.bus, 10) == XLLE_AOT_CONTEXT, "unsupported context");
    m.bus.context = XLLE_AOT_CONTEXT_FLAT32_RAM;
    s.eflags |= 0x100;
    require(r.run(&r, &s, &m.bus, 10) == XLLE_AOT_CONTEXT, "single step cannot silently execute");
    s = initial; s.eip++;
    require(r.run(&r, &s, &m.bus, 10) == XLLE_AOT_INVALID_ENTRY, "entry inside immediate");
    s = initial; m.bus.event_pending = 1;
    require(r.run(&r, &s, &m.bus, 10) == XLLE_AOT_EVENT && s.retired == 0, "pending event");
    m.bus.event_pending = 0;
    auto wrong_abi = r; wrong_abi.abi_version++;
    require(r.run(&wrong_abi, &s, &m.bus, 10) == XLLE_AOT_CONTEXT, "ABI mismatch");
    // All variants share a guest base. Calling one compiled body with another
    // descriptor must not validate different bytes and execute the wrong body.
    Memory other(xlle_aot_inc_eax);
    require(r.run(&xlle_aot_inc_eax, &s, &other.bus, 10) == XLLE_AOT_CONTEXT && s.retired == 0,
            "generated body is bound to its exact artifact");
    require(r.run(&r, nullptr, &m.bus, 10) == XLLE_AOT_CONTEXT, "null state");

    const auto& loop = xlle_aot_loop;
    Memory lm(loop); s = initial;
    require(loop.run(&loop, &s, &lm.bus, 9) == XLLE_AOT_BUDGET, "loop stops at budget");
    require(s.retired == 9 && s.gpr[0] == 5 && s.eip == 0x1001, "loop instruction accounting");

    const auto& store = xlle_aot_mov_store_mem;
    Memory sm(store); s = initial; s.gpr[0] = 0x12345678;
    auto before = sm.bytes; sm.reject = true;
    require(store.run(&store, &s, &sm.bus, 10) == XLLE_AOT_MEMORY, "declined memory access");
    require(s.retired == 0 && s.eip == initial.eip && sm.bytes == before && sm.accesses.empty(),
            "declining memory has no partial effects");
    const auto& load = xlle_aot_load_memory;
    Memory read_fail(load); read_fail.reject = true;
    s = initial; s.gpr[0] = 0xdeadbeef;
    require(load.run(&load, &s, &read_fail.bus, 10) == XLLE_AOT_MEMORY &&
            s.gpr[0] == 0xdeadbeef && s.eip == initial.eip && s.retired == 0,
            "failed read preserves destination and continuation");

    const auto& memory = xlle_aot_memory;
    Memory em(memory); em.arm_event_after_write = true; s = initial; s.gpr[0] = 42;
    require(memory.run(&memory, &s, &em.bus, 10) == XLLE_AOT_EVENT && s.retired == 1 && s.eip == 0x1005,
            "synchronous device event exits after completed write");

    const auto& changed = xlle_aot_self_modify;
    Memory cm(changed); s = initial; s.gpr[0] = 0x90909090;
    require(changed.run(&changed, &s, &cm.bus, 10) == XLLE_AOT_CODE_CHANGED,
            "self modification invalidates before next instruction");
    require(s.retired == 1 && s.eip == 0x1005 && s.gpr[0] == 0x90909090, "stale instruction never runs");

    const auto& outside = xlle_aot_external;
    Memory om(outside); s = initial;
    require(outside.run(&outside, &s, &om.bus, 10) == XLLE_AOT_EXTERNAL && s.eip == 0x1012 && s.retired == 1,
            "external control flow preserves continuation");
}

static void cs_limit_synthetic_guards() {
    constexpr uint32_t limit = 0xbfffffffu;
    {
        const auto& r = xlle_aot_cs_limit_jmp_within;
        CodeOnlyBus b(r, XLLE_AOT_CONTEXT_MACHINE_FLAT32_RAM, limit);
        xlle_aot_state s{}; s.eip = r.base; s.eflags = 2;
        require(r.run(&r, &s, &b.bus, 100) == XLLE_AOT_COMPLETE,
                "machine CS limit allows branch target at limit");
        require(s.retired == 2 && s.eip == 0xc0000000u,
                "branch-to-limit retires then completes at end");
    }
    {
        const auto& r = xlle_aot_cs_limit_jmp_beyond;
        CodeOnlyBus b(r, XLLE_AOT_CONTEXT_MACHINE_FLAT32_RAM, limit);
        xlle_aot_state s{}; s.eip = r.base; s.eflags = 2; s.gpr[0] = 0x11111111u;
        require(r.run(&r, &s, &b.bus, 100) == XLLE_AOT_CONTEXT,
                "machine CS limit rejects taken jump beyond limit");
        require(s.retired == 0 && s.eip == r.base && s.gpr[0] == 0x11111111u,
                "over-limit jump has no branch-instruction effects");
    }
    {
        const auto& r = xlle_aot_cs_limit_inc_jmp_beyond;
        CodeOnlyBus b(r, XLLE_AOT_CONTEXT_MACHINE_FLAT32_RAM, limit);
        xlle_aot_state s{}; s.eip = r.base; s.eflags = 2; s.gpr[0] = 41;
        require(r.run(&r, &s, &b.bus, 100) == XLLE_AOT_CONTEXT,
                "prior instruction retires before following over-limit jump declines");
        require(s.retired == 1 && s.eip == r.base + 1 && s.gpr[0] == 42,
                "over-limit jump preserves precise continuation after previous instruction");
    }
    {
        const auto& r = xlle_aot_cs_limit_jne_beyond_not_taken;
        CodeOnlyBus b(r, XLLE_AOT_CONTEXT_MACHINE_FLAT32_RAM, limit);
        xlle_aot_state s{}; s.eip = r.base; s.eflags = 2 | 0x40u;
        require(r.run(&r, &s, &b.bus, 100) == XLLE_AOT_COMPLETE,
                "untaken branch with out-of-limit target still runs");
        require(s.retired == 15 && s.eip == 0xc0000000u,
                "untaken out-of-limit branch only follows in-limit fallthrough");
    }
    {
        const auto& r = xlle_aot_cs_limit_span_cross;
        CodeOnlyBus b(r, XLLE_AOT_CONTEXT_MACHINE_FLAT32_RAM, limit);
        xlle_aot_state s{}; s.eip = r.base; s.eflags = 2; s.gpr[0] = 0x55555555u;
        require(r.run(&r, &s, &b.bus, 100) == XLLE_AOT_CONTEXT,
                "region byte span crossing CS limit is rejected before execution");
        require(s.retired == 0 && s.eip == r.base && s.gpr[0] == 0x55555555u,
                "span-crossing region has no effects");
    }
    {
        const auto& r = xlle_aot_cs_limit_span_cross;
        CodeOnlyBus b(r, XLLE_AOT_CONTEXT_FLAT32_RAM, 0);
        xlle_aot_state s{}; s.eip = r.base; s.eflags = 2; s.gpr[0] = 0x55555555u;
        require(r.run(&r, &s, &b.bus, 100) == XLLE_AOT_COMPLETE,
                "synthetic flat context ignores machine CS limit field");
        require(s.retired == 13 && s.eip == r.base + r.code_size && s.gpr[0] == 0x11223344u,
                "flat synthetic context preserves legacy full-region execution");
    }
}

static xlle_aot_state expect_complete_with_seed(const xlle_aot_region& region, xlle_aot_state initial,
                                                Memory& memory, uint64_t expected_retired) {
    initial.eip = region.base;
    const auto before = memory.bytes;
    auto native = initial;
    auto exit = region.run(&region, &native, &memory.bus, 100);
    require(exit == XLLE_AOT_COMPLETE, "addressing fixture completes");
    require(native.retired == expected_retired, "addressing fixture retired count");
    std::vector<uint8_t> expected_memory;
    std::vector<Access> expected_accesses;
    auto expected = oracle(region, initial, native.retired, before,
                           &expected_memory, &expected_accesses);
    equal_state(native, expected, 0xffffffffu, "addressing fixture");
    require(memory.bytes == expected_memory, "addressing memory matches Unicorn");
    require(memory.accesses == expected_accesses, "addressing access order matches Unicorn");
    return native;
}

static void memory_addressing_fixtures() {
    {
        const auto& r = xlle_aot_mov_mem_base_index_scale;
        Memory m(r);
        xlle_aot_state s{}; s.eflags = 2;
        s.gpr[0] = 0x13579bdf; s.gpr[3] = 0x2000; s.gpr[6] = 3;
        auto native = expect_complete_with_seed(r, s, m, 2);
        require(peek32(m.bytes, 0x202c) == 0x13579bdf && native.gpr[2] == 0x13579bdf,
                "base+index*scale fixture wrote expected address");
        s.eip = r.base;
        differential(r, s, 0xffffffffu);
    }
    {
        const auto& r = xlle_aot_mov_mem_abs_sib_index;
        Memory m(r);
        xlle_aot_state s{}; s.eflags = 2;
        s.gpr[0] = 4; s.gpr[1] = 0x2468ace0;
        auto native = s; native.eip = r.base;
        require(r.run(&r, &native, &m.bus, 100) == XLLE_AOT_COMPLETE, "absolute SIB completes");
        require(native.gpr[2] == 0x2468ace0 && peek32(m.bytes, 0x2110) == 0x2468ace0,
                "absolute SIB omits base and uses scaled index");
        s.eip = r.base;
        differential(r, s, 0xffffffffu);
    }
    {
        const auto& r = xlle_aot_mov_mem_disp8_ebp;
        Memory m(r);
        xlle_aot_state s{}; s.eflags = 2;
        s.gpr[0] = 0xfeedface; s.gpr[5] = 0x2204;
        auto native = s; native.eip = r.base;
        require(r.run(&r, &native, &m.bus, 100) == XLLE_AOT_COMPLETE, "negative disp8 EBP completes");
        require(native.gpr[3] == 0xfeedface && peek32(m.bytes, 0x2200) == 0xfeedface,
                "negative disp8 is sign-extended before address add");
        s.eip = r.base;
        differential(r, s, 0xffffffffu);
    }
    {
        const auto& r = xlle_aot_mov_mem_disp32_esp;
        Memory m(r);
        xlle_aot_state s{}; s.eflags = 2;
        s.gpr[1] = 0x11223344; s.gpr[4] = 0x2300;
        auto native = s; native.eip = r.base;
        require(r.run(&r, &native, &m.bus, 100) == XLLE_AOT_COMPLETE, "disp32 ESP completes");
        require(native.gpr[7] == 0x11223344 && peek32(m.bytes, 0x2330) == 0x11223344,
                "ESP SIB no-index addressing");
        s.eip = r.base;
        differential(r, s, 0xffffffffu);
    }
    {
        const auto& r = xlle_aot_mov_mem_wrap_disp32;
        Memory m(r);
        xlle_aot_state s{}; s.eflags = 2;
        s.gpr[0] = 0x3000; s.gpr[3] = 0xa5a55a5a;
        auto native = s; native.eip = r.base;
        require(r.run(&r, &native, &m.bus, 100) == XLLE_AOT_COMPLETE, "wrapped disp32 completes");
        require(native.gpr[1] == 0xa5a55a5a && peek32(m.bytes, 0x2010) == 0xa5a55a5a,
                "effective address wraps as uint32");
        s.eip = r.base;
        differential(r, s, 0xffffffffu);
    }
    {
        const auto& r = xlle_aot_mov_load_dest_equals_base;
        Memory m(r);
        poke32(m.bytes, 0x2420, 0x2001);
        const auto seeded = m.bytes;
        xlle_aot_state s{}; s.eflags = 2; s.gpr[0] = 0x2400;
        auto native = s; native.eip = r.base;
        require(r.run(&r, &native, &m.bus, 100) == XLLE_AOT_COMPLETE, "load dest equals base completes");
        require(native.gpr[0] == 0x2001, "load computes address before overwriting base register");
        differential_with_memory(r, s, 0xffffffffu, &seeded);
    }
    {
        const auto& r = xlle_aot_mov_store_source_equals_base;
        Memory m(r);
        xlle_aot_state s{}; s.eflags = 2; s.gpr[0] = 0x2500;
        auto native = s; native.eip = r.base;
        require(r.run(&r, &native, &m.bus, 100) == XLLE_AOT_COMPLETE, "store source equals base completes");
        require(peek32(m.bytes, 0x2520) == 0x2500, "store computes address and value from original register");
        s.eip = r.base;
        differential(r, s, 0xffffffffu);
    }

    const auto& pair = xlle_aot_mov_mem_base_index_scale;
    {
        Memory denied(pair);
        xlle_aot_state s{}; s.eip = pair.base; s.eflags = 2;
        s.gpr[0] = 0x13579bdf; s.gpr[3] = 0x2000; s.gpr[6] = 3;
        const auto before = denied.bytes;
        denied.reject_writes = true;
        require(pair.run(&pair, &s, &denied.bus, 100) == XLLE_AOT_MEMORY,
                "denied ModRM write exits as memory");
        require(s.retired == 0 && s.eip == pair.base && s.gpr[2] == 0 &&
                denied.bytes == before && denied.accesses.empty(),
                "denied ModRM write has no guest-visible effects");
    }
    {
        const auto& load = xlle_aot_mov_load_dest_equals_base;
        Memory denied(load);
        poke32(denied.bytes, 0x2420, 0x2001);
        xlle_aot_state s{}; s.eip = load.base; s.eflags = 2; s.gpr[0] = 0x2400;
        denied.reject_reads = true;
        require(load.run(&load, &s, &denied.bus, 100) == XLLE_AOT_MEMORY,
                "denied ModRM read exits as memory");
        require(s.retired == 0 && s.eip == load.base && s.gpr[0] == 0x2400 &&
                denied.accesses.empty(),
                "denied ModRM read preserves destination and continuation");
    }
    {
        Memory event(pair);
        event.arm_event_after_write = true;
        xlle_aot_state s{}; s.eip = pair.base; s.eflags = 2;
        s.gpr[0] = 0x13579bdf; s.gpr[3] = 0x2000; s.gpr[6] = 3; s.gpr[2] = 0x77777777;
        require(pair.run(&pair, &s, &event.bus, 100) == XLLE_AOT_EVENT,
                "event between ModRM memory operations exits before load");
        require(s.retired == 1 && s.eip == pair.base + 4 && s.gpr[2] == 0x77777777 &&
                peek32(event.bytes, 0x202c) == 0x13579bdf,
                "event boundary preserves completed store only");
    }
    {
        Memory budget(pair);
        xlle_aot_state s{}; s.eip = pair.base; s.eflags = 2;
        s.gpr[0] = 0x13579bdf; s.gpr[3] = 0x2000; s.gpr[6] = 3; s.gpr[2] = 0x77777777;
        require(pair.run(&pair, &s, &budget.bus, 1) == XLLE_AOT_BUDGET,
                "budget boundary between ModRM memory operations");
        require(s.retired == 1 && s.eip == pair.base + 4 && s.gpr[2] == 0x77777777 &&
                peek32(budget.bytes, 0x202c) == 0x13579bdf,
                "budget boundary preserves completed store only");
    }
}

static void expect_sub83(const xlle_aot_region& r, unsigned reg, uint32_t initial_value,
                         uint32_t expected_value, uint32_t must_set, uint32_t must_clear) {
    Memory m(r);
    xlle_aot_state s{}; s.eip = r.base; s.eflags = 2u;
    s.gpr[reg] = initial_value;
    require(r.run(&r, &s, &m.bus, 100) == XLLE_AOT_COMPLETE, "SUB imm8 completes");
    require(s.retired == 1 && s.gpr[reg] == expected_value, "SUB imm8 result");
    require((s.eflags & must_set) == must_set, "SUB imm8 required flags set");
    require((s.eflags & must_clear) == 0, "SUB imm8 required flags clear");
    xlle_aot_state diff{}; diff.eflags = 2u; diff.gpr[reg] = initial_value;
    differential(r, diff, 0xffffffffu);
}

static void sub83_boundary_fixtures() {
    constexpr uint32_t CF = 0x001u, ZF = 0x040u, SF = 0x080u, OF = 0x800u;
    expect_sub83(xlle_aot_sub83_zero, 0, 0x80000000u, 0x80000000u, SF, CF | ZF | OF);
    expect_sub83(xlle_aot_sub83_7f, 3, 0x80000000u, 0x7fffff81u, OF, CF | ZF | SF);
    expect_sub83(xlle_aot_sub83_80, 0, 0x7fffffffu, 0x8000007fu, CF | SF | OF, ZF);
    expect_sub83(xlle_aot_sub83_ff, 7, 0xffffffffu, 0x00000000u, ZF, CF | SF | OF);
}

static void fallback_round_trip() {
    const auto& region = xlle_aot_mixed;
    Memory m(region);
    xlle_aot_state s{}; s.eip = region.base; s.eflags = 2;
    const auto initial = s;
    const auto original_memory = m.bytes;
    require(region.run(&region, &s, &m.bus, 100) == XLLE_AOT_UNSUPPORTED,
            "unsupported NEG AL returns to fallback");
    require(s.retired == 1 && s.eip == 0x1005, "one native instruction precedes fallback");
    auto native_retired = s.retired;
    auto fallback = oracle(region, s, 1, m.bytes);
    s = fallback;
    s.retired = native_retired + fallback.retired;
    require(region.run(&region, &s, &m.bus, 100) == XLLE_AOT_COMPLETE,
            "explicit entry resumes native after NEG AL");
    require(s.retired == 3 && s.gpr[0] == 0x010203fdu, "native fallback native result");
    auto expected = oracle(region, initial, 3, original_memory);
    equal_state(s, expected, 0xffffffffu, "fallback round trip");
}

static void lea_fixtures() {
    struct Case { const xlle_aot_region* region; unsigned destination; uint32_t result; };
    // Hand-derived effective offsets, including unmapped addresses, wraparound,
    // absent SIB operands and destination aliasing the old base/index/ESP.
    const Case cases[] = {
        {&xlle_aot_lea_base_index, 0, 0x14},
        {&xlle_aot_lea_abs_index, 0, 0x6100},
        {&xlle_aot_lea_negative_ebp, 5, 0xfffffffe},
        {&xlle_aot_lea_esp, 4, 0x28},
        {&xlle_aot_lea_wrap, 0, 0x10},
        {&xlle_aot_lea_absolute, 7, 0xdeadbeef},
        {&xlle_aot_lea_same_base_index, 6, 0x40000005},
        {&xlle_aot_lea_no_base_index, 3, 0x80000000},
    };
    for (const auto& c : cases) {
        const auto& r = *c.region;
        for (uint32_t flags : {2u, 0x8d7u}) {
            xlle_aot_state initial{{0x1000, 3, 0xabcdef01, 0xfffffff0,
                                    0xfffffff8, 2, 0x40000001, 0x12345678},
                                   r.base, flags, 0};
            Memory m(r);
            const auto before = m.bytes;
            // No memory implementation is needed to compute an address.
            m.bus.read32 = nullptr; m.bus.write32 = nullptr;
            auto s = initial;
            require(r.run(&r, &s, &m.bus, 0) == XLLE_AOT_BUDGET && !s.retired,
                    "LEA respects instruction budget");
            require(r.run(&r, &s, &m.bus, 1) == XLLE_AOT_COMPLETE && s.retired == 1,
                    "LEA computes an offset without memory callbacks");
            auto expected = initial;
            expected.gpr[c.destination] = c.result;
            expected.eip = r.base + static_cast<uint32_t>(r.code_size);
            equal_state(s, expected, 0xffffffffu, "independent LEA result and all flags");
            require(m.bytes == before && m.accesses.empty(), "LEA has no memory effects");
            differential(r, initial, 0xffffffffu);
        }
    }
    const auto& invalid = xlle_aot_lea_invalid_register;
    Memory m(invalid);
    xlle_aot_state s{}; s.eip = invalid.base; s.eflags = 0x8d7;
    const auto initial = s;
    require(invalid.run(&invalid, &s, &m.bus, 1) == XLLE_AOT_UNSUPPORTED && !s.retired,
            "invalid LEA register form declines before effects");
    equal_state(s, initial, 0xffffffffu, "invalid LEA state");
}

static void stack_and_call_fixtures() {
    for (const auto* region : {&xlle_aot_call_flags, &xlle_aot_stack_regs,
                              &xlle_aot_call_reg}) {
        xlle_aot_state initial{};
        initial.eip = region->base; initial.eflags = 2;
        initial.gpr[4] = 0x3000; initial.gpr[0] = 0x1005; initial.gpr[3] = 0x12345678;
        differential(*region, initial, ~0x10u);
        // Every observable prefix must agree, including inside the callee.
        for (uint64_t budget = 1; budget <= 12; ++budget) {
            Memory m(*region);
            auto before = m.bytes;
            auto s = initial;
            auto exit = region->run(region, &s, &m.bus, budget);
            require(exit == XLLE_AOT_BUDGET || exit == XLLE_AOT_COMPLETE, "call prefix exit");
            std::vector<uint8_t> expected_memory;
            auto expected = oracle(*region, initial, s.retired, before, &expected_memory);
            equal_state(s, expected, ~0x10u, "stack/call prefix");
            require(m.bytes == expected_memory, "stack/call prefix memory");
            if (exit == XLLE_AOT_COMPLETE) break;
        }
    }
    for (const auto* r : {&xlle_aot_ret_plain, &xlle_aot_ret_adjust,
                         &xlle_aot_pop_esp, &xlle_aot_push_esp,
                         &xlle_aot_call_esp, &xlle_aot_jump_reg}) {
        Memory m(*r);
        xlle_aot_state initial{}; initial.eip = r->base; initial.eflags = 0x8d7;
        initial.gpr[4] = 0x3000; initial.gpr[0] = 0x3100;
        poke32(m.bytes, 0x3000, 0x3100);
        const auto before = m.bytes;
        auto s = initial;
        auto exit = r->run(r, &s, &m.bus, 1);
        require(exit == XLLE_AOT_COMPLETE || exit == XLLE_AOT_EXTERNAL, "stack/control single exit");
        require(s.retired == 1, "single stack/control retirement");
        std::vector<uint8_t> expected_memory;
        auto expected = oracle(*r, initial, 1, before, &expected_memory);
        equal_state(s, expected, 0xffffffffu, "stack/control operand alias");
        require(m.bytes == expected_memory, "stack/control operand alias memory");
        if (r == &xlle_aot_call_esp)
            require(s.eip == 0x3000 && peek32(m.bytes, 0x2ffc) == 0x1002,
                    "CALL ESP uses old ESP and writes actual return PC");
        if (r == &xlle_aot_push_esp)
            require(peek32(m.bytes, 0x2ffc) == 0x3000, "PUSH ESP stores original ESP");
        if (r == &xlle_aot_pop_esp) require(s.gpr[4] == 0x3100, "POP ESP replaces increment");
        if (r == &xlle_aot_ret_adjust) require(s.gpr[4] == 0x300c, "RET immediate cleanup");
    }
    for (const auto* r : {&xlle_aot_push_esp, &xlle_aot_pop_esp,
                         &xlle_aot_ret_plain, &xlle_aot_call_esp}) {
        Memory m(*r);
        xlle_aot_state initial{}; initial.eip = r->base; initial.eflags = 2;
        initial.gpr[4] = 0x3000;
        const auto before = m.bytes;
        m.reject = true;
        auto s = initial;
        require(r->run(r, &s, &m.bus, 5) == XLLE_AOT_MEMORY, "stack decline");
        equal_state(s, initial, 0xffffffffu, "stack decline preserves state");
        require(s.retired == 0 && m.bytes == before && m.accesses.empty(), "stack decline has no effects");
    }
    {
        const auto& r = xlle_aot_call_flags;
        Memory m(r); m.arm_event_after_write = true;
        xlle_aot_state s{}; s.eip = r.base; s.eflags = 2; s.gpr[4] = 0x3000;
        require(r.run(&r, &s, &m.bus, 100) == XLLE_AOT_EVENT, "event after CALL stack write");
        require(s.retired == 2 && s.eip == 0x1012 && s.gpr[4] == 0x2ffc &&
                peek32(m.bytes, 0x2ffc) == 0x100a, "CALL completes before event delivery");
        m.bus.event_pending = 0; m.arm_event_after_write = false;
        require(r.run(&r, &s, &m.bus, 100) == XLLE_AOT_COMPLETE && s.gpr[0] == 0 &&
                s.gpr[4] == 0x3000, "callee flags reach caller after resumed CALL");
    }
    for (const auto* r : {&xlle_aot_call_esp, &xlle_aot_ret_plain}) {
        Memory m(*r);
        m.bus.context = XLLE_AOT_CONTEXT_MACHINE_FLAT32_RAM; m.bus.code_limit = 0x1fff;
        xlle_aot_state s{}; s.eip = r->base; s.eflags = 2; s.gpr[4] = 0x3000;
        poke32(m.bytes, 0x3000, 0x3000);
        const auto before = m.bytes; const auto initial = s;
        require(r->run(r, &s, &m.bus, 5) == XLLE_AOT_CONTEXT, "invalid stack transfer target");
        equal_state(s, initial, 0xffffffffu, "invalid target state");
        require(s.retired == 0 && m.bytes == before, "invalid target must not change stack");
    }
}

static void test32_fixtures() {
    struct Case { uint32_t a,b,both_flags,self_flags; };
    const Case cases[]={{0,0xffffffff,0x44,0x44},{0x80000000,0xffffffff,0x84,0x84},
        {1,0xffffffff,0,0},{3,1,0,4},{0xff,0,0x44,4},
        {0xffffffff,0xffffffff,0x84,0x84},{0x100,0xffffffff,4,4}};
    for(const auto* r:{&xlle_aot_test_regs,&xlle_aot_test_same}) for(const auto& c:cases) {
        Memory m(*r); xlle_aot_state state{}; state.eip=r->base; state.eflags=0x8d7;
        state.gpr[0]=c.a; state.gpr[3]=c.b;
        for(unsigned i=1;i<8;++i) if(i!=3) state.gpr[i]=0x10203040+i;
        const auto before=state; const auto bytes=m.bytes;
        require(r->run(r,&state,&m.bus,1)==XLLE_AOT_COMPLETE,"TEST completes exactly at region end");
        auto expected=before; expected.eip+=2;
        expected.eflags=(before.eflags&~0x8c5u)|(r==&xlle_aot_test_regs?c.both_flags:c.self_flags);
        equal_state(state,expected,0xffffffffu,"TEST independent flags and untouched registers");
        require(state.retired==1 && m.bytes==bytes && m.accesses.empty(),"TEST changes no destination or memory");
    }
}

static void rotate_case(const xlle_aot_region& region, unsigned destination,
                        bool right, unsigned count, bool cl, uint32_t value,
                        uint32_t flags, bool compare_floor) {
    Memory memory(region);
    xlle_aot_state state{};
    for (unsigned i=0;i<8;++i) state.gpr[i]=0x12345678u + 0x1020304u*i;
    state.eip=region.base; state.eflags=flags; state.retired=19;
    state.gpr[destination]=value;
    if (cl) state.gpr[1]=(state.gpr[1]&0xffffff00u)|count;
    const auto before=state;
    const auto bytes=memory.bytes;
    // Independent bit permutation: assign each input bit its destination.
    // In particular, ECX as destination must consume its original CL.
    const uint32_t operand=before.gpr[destination];
    const unsigned shift=count%32;
    uint32_t expected_value=0;
    for (unsigned bit=0;bit<32;++bit) if (operand & (1u<<bit))
        expected_value |= 1u<<((bit+(right?32-shift:shift))%32);
    auto expected=before;
    expected.eip+=static_cast<uint32_t>(region.code_size);
    expected.gpr[destination]=expected_value;
    uint32_t defined=0xffffffffu;
    if (shift) {
        const unsigned carry_bit=right?shift-1:32-shift;
        expected.eflags=(flags&~1u)|((operand>>carry_bit)&1u);
        if (shift==1) {
            // ROL overflow is the old top two bits; ROR is old MSB XOR LSB.
            const unsigned overflow=((operand>>31)^(operand>>(right?0:30)))&1u;
            expected.eflags=(expected.eflags&~0x800u)|(overflow<<11);
        } else defined &= ~0x800u;
    }
    require(region.run(&region,&state,&memory.bus,1)==XLLE_AOT_COMPLETE,"rotate retires exactly once");
    equal_state(state,expected,defined,"rotate independent bit mapping/defined flags");
    require(state.retired==20 && memory.bytes==bytes && memory.accesses.empty(),"rotate has no memory effects");
    if (compare_floor) {
        const auto floor=oracle(region,before,1,bytes);
        // Includes the explicitly chosen floor-compatible undefined OF policy.
        equal_state(state,floor,0xffffffffu,"rotate floor including deterministic undefined OF");
    }
}

static void rotate32_fixtures() {
    const xlle_aot_region* cl_regions[2][8]={
        {&xlle_aot_rol_eax_cl,&xlle_aot_rol_ecx_cl,&xlle_aot_rol_edx_cl,&xlle_aot_rol_ebx_cl,
         &xlle_aot_rol_esp_cl,&xlle_aot_rol_ebp_cl,&xlle_aot_rol_esi_cl,&xlle_aot_rol_edi_cl},
        {&xlle_aot_ror_eax_cl,&xlle_aot_ror_ecx_cl,&xlle_aot_ror_edx_cl,&xlle_aot_ror_ebx_cl,
         &xlle_aot_ror_esp_cl,&xlle_aot_ror_ebp_cl,&xlle_aot_ror_esi_cl,&xlle_aot_ror_edi_cl}};
    const uint32_t values[]={0,1,0x80000000,0xffffffff,0x12345678,0xa5a55a5a};
    for (unsigned right=0;right<2;++right) for(unsigned reg=0;reg<8;++reg)
        for(unsigned count=0;count<256;++count) for(auto value:values)
            for(auto flags:{2u,0x8d7u}) {
                const bool floor_check=value==0x12345678 && flags==0x8d7 &&
                    (count<=2 || count==31 || count==32 || count==33 || count==63 ||
                     count==64 || count==127 || count==128 || count==255);
                rotate_case(*cl_regions[right][reg],reg,right!=0,count,true,value,flags,floor_check);
            }
    struct Immediate {const xlle_aot_region* left;const xlle_aot_region* right;unsigned count;};
    const Immediate immediate[]={
        {&xlle_aot_rol_eax_one,&xlle_aot_ror_eax_one,1},
        {&xlle_aot_rol_eax_i0,&xlle_aot_ror_eax_i0,0},
        {&xlle_aot_rol_eax_i1,&xlle_aot_ror_eax_i1,1},
        {&xlle_aot_rol_eax_i2,&xlle_aot_ror_eax_i2,2},
        {&xlle_aot_rol_eax_i31,&xlle_aot_ror_eax_i31,31},
        {&xlle_aot_rol_eax_i32,&xlle_aot_ror_eax_i32,32},
        {&xlle_aot_rol_eax_i33,&xlle_aot_ror_eax_i33,33},
        {&xlle_aot_rol_eax_i63,&xlle_aot_ror_eax_i63,63},
        {&xlle_aot_rol_eax_i64,&xlle_aot_ror_eax_i64,64},
        {&xlle_aot_rol_eax_i255,&xlle_aot_ror_eax_i255,255}};
    for (const auto& c:immediate) for(auto value:values) for(auto flags:{2u,0x8d7u})
        for(unsigned right=0;right<2;++right)
            rotate_case(*(right?c.right:c.left),0,right!=0,c.count,false,value,flags,true);
}

static void bswap32_fixtures() {
    const xlle_aot_region* regions[]={&xlle_aot_bswap_eax,&xlle_aot_bswap_ecx,
        &xlle_aot_bswap_edx,&xlle_aot_bswap_ebx,&xlle_aot_bswap_esp,&xlle_aot_bswap_ebp,
        &xlle_aot_bswap_esi,&xlle_aot_bswap_edi};
    const uint32_t cases[][2]={{0,0},{1,0x01000000},{0x12345678,0x78563412},
        {0x80000000,0x80},{0xffffffff,0xffffffff},{0x00ff55aa,0xaa55ff00}};
    for(unsigned reg=0;reg<8;++reg) for(const auto& c:cases) for(auto flags:{2u,0x8d7u}) {
        const auto& r=*regions[reg]; Memory m(r); const auto bytes=m.bytes;
        xlle_aot_state state{};state.eip=r.base;state.eflags=flags;
        for(unsigned i=0;i<8;++i) state.gpr[i]=0x10203040+i;
        state.gpr[reg]=c[0]; const auto initial=state;
        auto expected=state;expected.eip+=2;expected.gpr[reg]=c[1];
        require(r.run(&r,&state,&m.bus,1)==XLLE_AOT_COMPLETE,"BSWAP completes");
        equal_state(state,expected,0xffffffffu,"BSWAP independent bytes and preserved flags");
        require(state.retired==1 && m.bytes==bytes && m.accesses.empty(),"BSWAP has no memory effects");
        equal_state(state,oracle(r,initial,1,bytes),0xffffffffu,"BSWAP floor");
    }
}

static void or32_fixtures() {
    struct Case {uint32_t a,b,result,flags;};
    const Case cases[]={{0,0,0,0x44},{1,2,3,4},{0x80000000,0,0x80000000,0x84},
        {0x12340000,0x5678,0x12345678,4},{0xaaaaaaaa,0x55555555,0xffffffff,0x84},
        {0xff,0,0xff,4},{0,0x100,0x100,4}};
    for(const auto* r:{&xlle_aot_or_regs,&xlle_aot_or_reverse}) for(const auto& c:cases) {
        Memory m(*r);xlle_aot_state state{};state.eip=r->base;state.eflags=0x8d7;
        state.gpr[0]=c.a;state.gpr[3]=c.b;const auto initial=state;
        auto expected=initial;expected.eip+=2;expected.gpr[0]=c.result;
        expected.eflags=(initial.eflags&~0x8c5u)|c.flags;
        require(r->run(r,&state,&m.bus,1)==XLLE_AOT_COMPLETE,"OR completes");
        equal_state(state,expected,0xffffffffu,"OR independent result/flags");
        require(state.retired==1 && m.accesses.empty(),"OR does no memory work");
    }
    struct Immediate {const xlle_aot_region* region;unsigned reg;uint32_t initial,result,flags;};
    const Immediate immediates[]={
        {&xlle_aot_or_eax_imm,0,0,0x80000000,0x84},
        {&xlle_aot_or81_edi,7,0x80000000,0x92345678,0x84},
        {&xlle_aot_or83_eax_zero,0,0,0,0x44},
        {&xlle_aot_or83_ebx_pos,3,0,0x7f,0},
        {&xlle_aot_or83_eax_neg,0,1,0xffffff81,0x84},
        {&xlle_aot_or83_edi_all,7,0,0xffffffff,0x84}};
    for(const auto& c:immediates) {
        const auto& r=*c.region;Memory m(r);xlle_aot_state state{};
        state.eip=r.base;state.eflags=0x8d7;state.gpr[c.reg]=c.initial;
        auto expected=state;expected.eip+=static_cast<uint32_t>(r.code_size);
        expected.gpr[c.reg]=c.result;expected.eflags=(state.eflags&~0x8c5u)|c.flags;
        require(r.run(&r,&state,&m.bus,1)==XLLE_AOT_COMPLETE,"OR immediate completes");
        equal_state(state,expected,0xffffffffu,"OR independent immediate/sign extension");
    }
}

static void integer_cycle_boundaries() {
    const auto& r=xlle_aot_integer_cycle;Memory m(r);xlle_aot_state initial{};
    initial.eip=r.base;initial.eflags=2;
    for(unsigned i=0;i<8;++i) initial.gpr[i]=0x12345678u+0x01020304u*i;
    for(unsigned budget=1;budget<=33;++budget) {
        auto state=initial;
        require(r.run(&r,&state,&m.bus,budget)==XLLE_AOT_BUDGET,"integer cycle exact budget");
        require(state.retired==budget,"integer cycle exact retirement");
        equal_state(state,oracle(r,initial,budget,m.bytes),~0x10u,"integer cycle every boundary");
        m.bus.event_pending=1;const auto before=state;
        require(r.run(&r,&state,&m.bus,1)==XLLE_AOT_EVENT,"integer cycle pending event");
        equal_state(state,before,0xffffffffu,"integer cycle event preserves state");
        require(state.retired==before.retired,"event retires no work");
        m.bus.event_pending=0;
    }
}

int main() {
    independent_fixtures();
    guards_and_exits();
    cs_limit_synthetic_guards();
    memory_addressing_fixtures();
    sub83_boundary_fixtures();
    lea_fixtures();
    fallback_round_trip();
    stack_and_call_fixtures();
    test32_fixtures();
    rotate32_fixtures();
    bswap32_fixtures();
    or32_fixtures();
    integer_cycle_boundaries();
    const xlle_aot_region* cases[] = {&xlle_aot_add_eax_ebx, &xlle_aot_sub_eax_ebx,
        &xlle_aot_xor_eax_eax, &xlle_aot_inc_eax, &xlle_aot_dec_eax,
        &xlle_aot_branch, &xlle_aot_bytes, &xlle_aot_memory, &xlle_aot_imm_ops,
        &xlle_aot_register_moves, &xlle_aot_and_regs, &xlle_aot_xor_regs,
        &xlle_aot_add_imm, &xlle_aot_sub_imm, &xlle_aot_xor_imm,
        &xlle_aot_branch_ne, &xlle_aot_jump_near, &xlle_aot_load_memory,
        &xlle_aot_sub83_zero, &xlle_aot_sub83_7f, &xlle_aot_sub83_80,
        &xlle_aot_sub83_ff, &xlle_aot_add83_80, &xlle_aot_add81_max,
        &xlle_aot_sub81_min, &xlle_aot_cmp83_ff, &xlle_aot_cmp81_max,
        &xlle_aot_add_reverse, &xlle_aot_sub_reverse, &xlle_aot_xor_reverse,
        &xlle_aot_and_reverse, &xlle_aot_cmp_reverse,
        &xlle_aot_lea_base_index, &xlle_aot_lea_abs_index, &xlle_aot_lea_negative_ebp,
        &xlle_aot_lea_esp, &xlle_aot_lea_wrap, &xlle_aot_lea_absolute,
        &xlle_aot_lea_same_base_index, &xlle_aot_lea_no_base_index,
        &xlle_aot_test_regs,&xlle_aot_test_same,&xlle_aot_test_eax_imm,&xlle_aot_test_edi_imm,
        &xlle_aot_or_regs,&xlle_aot_or_reverse,&xlle_aot_or_same,&xlle_aot_or_eax_imm,
        &xlle_aot_or81_edi,&xlle_aot_or83_eax_zero,&xlle_aot_or83_ebx_pos,
        &xlle_aot_or83_eax_neg,&xlle_aot_or83_edi_all};
    const uint32_t edges[] = {0, 1, 15, 16, 0x7fffffff, 0x80000000, 0xffffffff};
    uint32_t random = 0x58424f58;
    unsigned compared = 0;
    for (const auto* region : cases) {
        for (unsigned i = 0; i < 24; ++i) {
            xlle_aot_state s{}; s.eip = region->base;
            for (auto& reg : s.gpr) { random ^= random << 13; random ^= random >> 17; random ^= random << 5; reg = random; }
            s.gpr[0] = i < 7 ? edges[i] : s.gpr[0];
            s.gpr[3] = i < 7 ? edges[6 - i] : s.gpr[3];
            if (i == 7) s.gpr[3] = s.gpr[0]; // taken branch
            s.eflags = 2u | ((i & 1u) ? 0x8d5u : 0u);
            const bool undefined_af = region == &xlle_aot_xor_eax_eax ||
                region == &xlle_aot_and_regs || region == &xlle_aot_xor_regs ||
                region == &xlle_aot_xor_imm || region == &xlle_aot_xor_reverse ||
                region == &xlle_aot_and_reverse || region == &xlle_aot_test_regs ||
                region == &xlle_aot_test_same || region == &xlle_aot_test_eax_imm || region == &xlle_aot_test_edi_imm ||
                region == &xlle_aot_or_regs || region == &xlle_aot_or_reverse || region == &xlle_aot_or_same ||
                region == &xlle_aot_or_eax_imm || region == &xlle_aot_or81_edi ||
                region == &xlle_aot_or83_eax_zero || region == &xlle_aot_or83_ebx_pos ||
                region == &xlle_aot_or83_eax_neg || region == &xlle_aot_or83_edi_all;
            differential(*region, s, undefined_af ? ~0x10u : 0xffffffffu);
            ++compared;
        }
    }
    std::cout << "AOT: 9 independent fixtures, LEA offsets, memory addressing and SUB imm8 boundaries, "
              << compared
              << " differential cases, guards and native/Unicorn/native continuation passed\n";
}
