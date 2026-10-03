#include "machine/machine.hpp"
#include "cpu/unicorn_floor.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace xlle;
static void require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "idle fast-forward: %s\n", why); std::exit(1); }
}
static constexpr uint32_t head = 0x8001e3ce;
static constexpr uint32_t pcs[] = {head, head+1, head+2, head+3, head+4, head+7, head+21, head+25};
// Existing pinned instruction shape. Only the eight idle instructions execute;
// the non-idle branches remain real guest branches and must prevent a skip.
static const uint8_t loop[] = {0xfb,0x90,0x90,0xfa,0x3b,0x6d,0,0x74,0x0c,
    0xb1,2,0xe8,0xde,0x8d,0xff,0xff,0xe8,0x4c,0,0,0,0x83,0x7b,0x2c,0,0x74,0xe5};
static void word(XboxMachine& m, uint32_t p, uint32_t v) {
    require(m.cpu().write_mem(p, &v, 4), "write fixture word");
}
static uint32_t word(XboxMachine& m, uint32_t p) {
    uint32_t v = 0; require(m.cpu().read_mem(p, &v, 4), "read fixture word"); return v;
}
static void exact(XboxMachine& m, uint64_t n) {
    std::string error;
    const auto retired = m.run(m.cpu().get_regs().eip, n, error);
    if (retired != n || !error.empty()) std::fprintf(stderr, "n=%llu retired=%llu %s\n",
        (unsigned long long)n, (unsigned long long)retired, error.c_str());
    require(retired == n && error.empty(), "exact nonfaulting run");
}
static void prepare(XboxMachine& m, unsigned phase, bool split = false) {
    require(m.ok() && m.init_ram() && m.cpu().setup_flat_segments(0x8000), "flat machine");
    require(m.cpu().write_mem(0x1e3ce, loop, sizeof(loop)), "load idle fixture");
    std::array<uint32_t,1024> pd{}, pt{};
    for (unsigned i=0; i<1024; ++i) pt[i] = i*4096+3;
    pd[0] = pd[512] = 0x19003;
    require(m.cpu().write_mem(0x18000, pd.data(), sizeof(pd)) &&
            m.cpu().write_mem(0x19000, pt.data(), sizeof(pt)), "paged kernel alias");
    const uint32_t ebp = split ? 0x80009ffe : 0x80009000;
    word(m, ebp & 0x7fffffff, ebp); word(m, 0xa02c, 0);
    uint32_t cr3 = 0x18000, cr0 = 0;
    require(m.cpu().read_reg(XReg::CR0, &cr0, 4), "CR0"); cr0 |= 0x80010001;
    require(m.cpu().write_reg(XReg::CR3, &cr3, 4) &&
            m.cpu().write_reg(XReg::CR0, &cr0, 4), "enable 4 KiB paging");
    X86Regs r{}; r.eip = head; r.eflags = 0x446; r.ebp = ebp; r.ebx = 0x8000a000;
    r.esp = 0x8000bff0; r.eax = 0x12345678; r.ecx = 0x87654321;
    r.edx = 0xabcdef01; r.esi = 0x11112222; r.edi = 0x33334444;
    m.cpu().set_regs(r);
    // Real execution establishes A bits, cached permissions, and the STI shadow.
    exact(m, 8+phase);
    require(m.cpu().get_regs().eip == pcs[phase], "independent phase PC");
}
static void state(XboxMachine& m, unsigned phase) {
    const auto r = m.cpu().get_regs();
    const bool enabled = phase == 1 || phase == 2 || phase == 3;
    require(r.eip == pcs[phase] && r.eflags == (0x446u | (enabled ? 0x200u : 0)),
            "phase PC and all EFLAGS, including preserved DF");
    require(m.cpu().interrupts_enabled() == (phase == 2 || phase == 3), "exact STI shadow");
    require(r.eax == 0x12345678 && r.ecx == 0x87654321 && r.edx == 0xabcdef01 &&
            r.ebx == 0x8000a000 && r.esp == 0x8000bff0 && r.esi == 0x11112222 &&
            r.edi == 0x33334444, "all untouched integer registers");
    require(word(m, r.ebp & 0x7fffffff) == r.ebp && word(m, 0xa02c) == 0, "idle RAM unchanged");
}
static void phase_tails() {
    for (bool skip : {false,true}) for (bool split : {false,true}) {
        for (unsigned phase=0; phase<8; ++phase) {
            XboxMachine m; prepare(m, phase, split); m.set_idle_fast_forward(skip);
            unsigned at = phase;
            for (unsigned tail=0; tail<8; ++tail) {
                const auto before = m.total_insns();
                const auto skipped = m.idle_census().ff_insns;
                exact(m, 8*513+tail); at = (at+tail)%8;
                state(m, at);
                require(m.total_insns() == before+8*513+tail, "clock includes exact tail");
                require((m.idle_census().ff_insns > skipped) == skip, "both execution arms exercised");
            }
            const auto& floor = static_cast<UnicornFloor&>(m.cpu());
            const auto stats = floor.block_stats();
            require(stats.tb_insns+stats.skipped_insns == m.total_insns(), "disjoint clock accounting");
            require(stats.insn_mode_runs == 0, "normal skip uses no instruction observer");
            if (skip) require(m.idle_census().ff_phase[phase] > 0, "requested entry phase actually skipped");
            // RDTSC after the skip reads the exact retired clock, not elapsed host time.
            const uint8_t tsc[] = {0x0f,0x31};
            require(m.cpu().write_mem(0x11000, tsc, sizeof(tsc)), "TSC probe");
            auto r = m.cpu().get_regs(); r.eip = 0x80011000; m.cpu().set_regs(r);
            const auto clock = m.total_insns(); exact(m, 1);
            r = m.cpu().get_regs();
            require(r.eax == clock && r.edx == 0, "guest TSC after skipped iterations");
        }
    }
}
static void guards() {
    // Per-instruction and memory observers must receive the executed effects.
    for (bool instructions : {false,true}) {
        XboxMachine m;
        unsigned events = 0;
        if (instructions) m.cpu().on_retired([&](const RetiredInsn&) { ++events; });
        else m.cpu().on_mem([&](const MemEvent&) { ++events; });
        // Configure observers before any translation, as in normal diagnostics.
        // Activating memory hooks on cached TBs is tracked separately in .1.29.
        prepare(m,0); m.set_idle_fast_forward(true); events = 0;
        exact(m,80);
        if (m.idle_census().ff_insns || events != (instructions ? 80u : 20u))
            std::fprintf(stderr,"observer instructions=%d events=%u skipped=%llu\n", instructions, events,
                (unsigned long long)m.idle_census().ff_insns);
        require(m.idle_census().ff_insns == 0 && events == (instructions ? 80u : 20u),
                "observers retain every idle instruction/read");
    }
    {
        XboxMachine m; prepare(m,0); m.set_idle_fast_forward(true);
        m.cpu().request_stop("fixture stop"); std::string error;
        require(m.run(head,80,error) == 0 && error.find("fixture stop") != std::string::npos &&
                m.idle_census().ff_insns == 0, "skip cannot consume a pending stop");
    }
    {
        XboxMachine m; prepare(m,0); m.set_idle_fast_forward(true);
        auto r = m.cpu().get_regs(); r.eflags |= 1; m.cpu().set_regs(r); exact(m,80);
        require(m.idle_census().ff_insns == 0 && m.idle_census().ff_rej_flags > 0,
                "non-fixed flags execute before qualifying"); state(m,0);
    }
    {
        XboxMachine m; prepare(m,0);
        auto r = m.cpu().get_regs(); r.eip = pcs[1]; r.eflags |= 0x200; m.cpu().set_regs(r);
        require(!m.cpu().idle_skip_context_ok(true), "phase 1 requires actual STI shadow");
        r.eip = head; r.eflags |= 0x10000; m.cpu().set_regs(r);
        require(!m.cpu().idle_skip_context_ok(false), "RF debug state excluded");
        r.eflags = 0x446; m.cpu().set_regs(r);
        uint32_t dr7 = 0x401; require(m.cpu().write_reg(XReg::DR7, &dr7,4), "debug breakpoint");
        require(!m.cpu().idle_skip_context_ok(false), "hardware breakpoint excluded");
    }
    for (bool thread : {false,true}) {
        XboxMachine m; prepare(m,0); m.set_idle_fast_forward(true);
        word(m,thread ? 0xa02c : 0x9000,1); std::string error;
        m.run(head,8,error);
        require(m.idle_census().ff_insns == 0 && m.idle_census().ff_rej_state > 0,
                "queued work executes the non-idle branch");
    }
    {
        XboxMachine m; prepare(m,0); m.set_idle_fast_forward(true);
        const uint8_t inc = 0x40; require(m.cpu().write_mem(0x1e3d0,&inc,1), "changed loop code");
        exact(m,80);
        require(m.idle_census().ff_insns == 0 && m.idle_census().ff_rej_bytes == 1 &&
                m.cpu().get_regs().eax == 0x12345682, "code mismatch executes changed instruction");
    }
}
static void cached_memory() {
    XboxMachine m; prepare(m,0);
    uint32_t value = 0;
    require(m.cpu().read_cached_ram(0x80009000,&value,4,false) && value == 0x80009000,
            "warm read access certified");
    // A changed PTE without INVLPG/CR3 does not replace an already-cached mapping.
    word(m,0xc000,1); word(m,0x19000+9*4,0xc003);
    require(m.cpu().read_cached_ram(0x80009000,&value,4,false) && value == 0x80009000,
            "guard sees cached translation rather than new PTE");
    m.set_idle_fast_forward(true); exact(m,80);
    require(m.idle_census().ff_insns == 80, "unchanged cached loop still eligible");
    uint32_t cr3 = 0x18000; require(m.cpu().write_reg(XReg::CR3,&cr3,4), "flush translations");
    require(!m.cpu().read_cached_ram(0x80009000,&value,4,false) &&
            word(m,0x19000+9*4) == 0xc003, "cold guard neither refills nor sets accessed bit");
    require(!m.cpu().read_cached_ram(0xfffffffE,&value,4,false), "wrapping span rejected");
    unsigned reads = 0;
    require(m.cpu().map_mmio(0x0e000000,4096,[&](uint32_t,int) { ++reads; return 0u; },
            [](uint32_t,int,uint32_t) {}), "MMIO fixture mapping");
    word(m,0x19000+9*4,0x0e000003);
    require(m.cpu().write_reg(XReg::CR3,&cr3,4), "flush MMIO remap");
    require(!m.cpu().read_cached_ram(0x80009000,&value,4,false) && reads == 0,
            "guard never services an MMIO register");
    const uint8_t load[] = {0xa1,0,0x90,0,0x80};
    require(m.cpu().write_mem(0x11000,load,sizeof(load)), "real MMIO load");
    auto r=m.cpu().get_regs(); r.eip=0x80011000; m.cpu().set_regs(r); exact(m,1);
    require(reads == 1 && !m.cpu().read_cached_ram(0x80009000,&value,4,false) && reads == 1,
            "cached MMIO entry cannot become a RAM certificate or repeat the read");
    // After a flush, warm only the first page of an unaligned four-byte span.
    word(m,0x19000+9*4,0x9003); word(m,0x19000+10*4,0xa003);
    require(m.cpu().write_reg(XReg::CR3,&cr3,4), "flush split operand");
    const uint8_t byte_load[] = {0x0f,0xb6,0x05,0xfe,0x9f,0,0x80};
    require(m.cpu().write_mem(0x11000,byte_load,sizeof(byte_load)), "warm first page only");
    r=m.cpu().get_regs(); r.eip=0x80011000; m.cpu().set_regs(r); exact(m,1);
    require(m.cpu().read_cached_ram(0x80009ffe,&value,1,false) &&
            !m.cpu().read_cached_ram(0x80009ffe,&value,4,false) && word(m,0x19000+10*4)==0xa003,
            "second cold page declines without accessed-bit effects");
}
struct Poll { uint64_t clock; uint32_t pc, flags; bool enabled; };
static std::vector<Poll> deadline_run(bool skip, unsigned phase) {
    XboxMachine::Config options{}; options.cpu_chunk_insns = 4096;
    XboxMachine m(options); prepare(m,phase); m.set_idle_fast_forward(skip);
    std::vector<Poll> polls;
    m.on_host_poll([&]() {
        const auto r = m.cpu().get_regs();
        polls.push_back({m.total_insns(),r.eip,r.eflags,m.cpu().interrupts_enabled()});
    });
    exact(m,1466680-m.total_insns());
    require(polls.size() == 3 && polls[0].clock == 8+phase &&
            polls[1].clock == 733334 && polls[2].clock == 1466667,
            "independently calculated host deadlines reached exactly");
    state(m,1466680%8);
    if (skip) require(m.idle_census().ff_insns > 1400000, "deadline test exercises long skips");
    return polls;
}
static void deadlines() {
    for (unsigned phase=0; phase<8; ++phase) {
        const auto a = deadline_run(false,phase), b = deadline_run(true,phase);
        for (size_t i=0; i<a.size(); ++i)
            require(a[i].clock == b[i].clock && a[i].pc == b[i].pc &&
                    a[i].flags == b[i].flags && a[i].enabled == b[i].enabled,
                    "executed/skip arms agree at every host deadline");
    }
}
static void pending_irq() {
    for (bool skip : {false,true}) for (unsigned phase=0; phase<8; ++phase) {
        XboxMachine m; prepare(m,phase); m.set_idle_fast_forward(skip);
        // First skip/execute whole iterations, then make the IRQ pending. The
        // floor must stop at the first actual open window, including phase 1's
        // inhibited NOP and phase 3's pre-CLI boundary.
        exact(m,80);
        m.cpu().set_irq_pending(true); std::string error;
        const uint64_t expected = phase == 3 ? 0 : (10-phase)%8;
        const auto n = m.cpu().run(pcs[phase],8,error);
        require(error.empty() && n == expected && m.cpu().stopped_for_irq_window(),
                "pending IRQ executes exactly through the STI shadow");
        require(m.cpu().get_regs().eip == (phase == 3 ? pcs[3] : pcs[2]),
                "first interruptible instruction boundary");
    }
}
static std::array<uint64_t,8> delivered_irq(bool skip) {
    XboxMachine m; prepare(m,0); m.init_devices(); m.set_idle_fast_forward(skip);
    // One real PIC IRQ, with a ring-0 IDT gate and executed guest IRET. The
    // handler records the saved PC/flags and RDTSC after four instructions.
    const uint8_t handler[] = {0x8b,0x04,0x24,0xa3,0,0xc0,0,0x80,
        0x8b,0x44,0x24,8,0xa3,4,0xc0,0,0x80,0x0f,0x31,0xa3,8,0xc0,0,0x80,0xcf};
    require(m.cpu().write_mem(0x11000,handler,sizeof(handler)), "IRQ recording handler");
    const uint64_t gate = 0x80018e0000081000ull;
    require(m.cpu().write_mem(0x12000+0x23*8,&gate,8), "interrupt gate");
    X86DescriptorTable idt{}; idt.base=0x80012000; idt.limit=0x7ff;
    require(m.cpu().write_reg(XReg::IDTR,&idt,sizeof(idt)), "IDTR");
    auto* pic=m.pic_dbg(); pic->master().irq_base=0x20;
    pic->master().imr=0xf7; pic->slave().imr=0xff;
    m.on_host_poll([&]() {
        if (m.total_insns() == 733334) { pic->raise(3,true); pic->raise(3,false); }
    });
    exact(m,1466680-m.total_insns());
    require(m.injected_interrupts() == 1, "PIC interrupt delivered exactly once");
    require(word(m,0xc000) == pcs[2] && word(m,0xc004) == 0x646 && word(m,0xc008) == 733342,
            "IRQ frame and guest TSC prove exact delivery after inhibited NOP");
    const auto r=m.cpu().get_regs();
    require(r.eip == pcs[1] && r.eflags == 0x646 && !m.cpu().interrupts_enabled(),
            "IRET and remaining loop preserve final STI shadow");
    return {r.eip,r.eflags,r.esp,word(m,0xc008),m.dev_digest(),m.dev_events(),m.dma_digest(),m.dma_writes()};
}
int main() {
    phase_tails(); guards(); cached_memory(); deadlines(); pending_irq();
    require(delivered_irq(false) == delivered_irq(true), "executed/skip PIC and DMA/device history");
    std::puts("idle fast-forward: all eight phases, exact tails, context/MMU guards and deadlines passed");
}
