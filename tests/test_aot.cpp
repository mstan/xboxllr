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
            "unsupported BSWAP returns to fallback");
    require(s.retired == 1 && s.eip == 0x1005, "one native instruction precedes fallback");
    auto native_retired = s.retired;
    auto fallback = oracle(region, s, 1, m.bytes);
    s = fallback;
    s.retired = native_retired + fallback.retired;
    require(region.run(&region, &s, &m.bus, 100) == XLLE_AOT_COMPLETE,
            "explicit entry resumes native after BSWAP");
    require(s.retired == 3 && s.gpr[0] == 0x04030202, "native fallback native result");
    auto expected = oracle(region, initial, 3, original_memory);
    equal_state(s, expected, 0xffffffffu, "fallback round trip");
}

int main() {
    independent_fixtures();
    guards_and_exits();
    cs_limit_synthetic_guards();
    memory_addressing_fixtures();
    sub83_boundary_fixtures();
    fallback_round_trip();
    const xlle_aot_region* cases[] = {&xlle_aot_add_eax_ebx, &xlle_aot_sub_eax_ebx,
        &xlle_aot_xor_eax_eax, &xlle_aot_inc_eax, &xlle_aot_dec_eax,
        &xlle_aot_branch, &xlle_aot_bytes, &xlle_aot_memory, &xlle_aot_imm_ops,
        &xlle_aot_register_moves, &xlle_aot_and_regs, &xlle_aot_xor_regs,
        &xlle_aot_add_imm, &xlle_aot_sub_imm, &xlle_aot_xor_imm,
        &xlle_aot_branch_ne, &xlle_aot_jump_near, &xlle_aot_load_memory,
        &xlle_aot_sub83_zero, &xlle_aot_sub83_7f, &xlle_aot_sub83_80,
        &xlle_aot_sub83_ff};
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
                region == &xlle_aot_xor_imm;
            differential(*region, s, undefined_af ? ~0x10u : 0xffffffffu);
            ++compared;
        }
    }
    std::cout << "AOT: 9 independent fixtures, memory addressing and SUB imm8 boundaries, "
              << compared
              << " differential cases, guards and native/Unicorn/native continuation passed\n";
}
