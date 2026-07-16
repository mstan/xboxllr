// xlle — UnicornFloor implementation.
//
// A0 note on fidelity (PRINCIPLES §6): UC_HOOK_CODE fires once per instruction *before*
// execution, so `seq_` here counts dispatch, not true architectural retirement. That is
// adequate for the A0 fixture spike; precise retirement / restartable-fault boundaries are
// a later contract we validate against hardware. This is called out, not papered over.
#include "cpu/unicorn_floor.hpp"

#include <unicorn/unicorn.h>

#include <cstring>

namespace xlle {

static uint64_t to_hook(void* p) { return reinterpret_cast<uint64_t>(p); }
static void* from_hook(uc_hook h) { return reinterpret_cast<void*>(static_cast<uintptr_t>(h)); }

UnicornFloor::UnicornFloor() {
    uc_engine* uc = nullptr;
    if (uc_open(UC_ARCH_X86, UC_MODE_32, &uc) != UC_ERR_OK) {
        uc_ = nullptr;
        return;
    }
    uc_ = reinterpret_cast<uc_struct*>(uc);

    uc_hook hc = 0, hm = 0;
    uc_hook_add(uc, &hc, UC_HOOK_CODE, reinterpret_cast<void*>(&UnicornFloor::code_trampoline),
                this, 1, 0);
    uc_hook_add(uc, &hm, UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE,
                reinterpret_cast<void*>(&UnicornFloor::mem_trampoline), this, 1, 0);
    hook_code_ = from_hook(hc);
    hook_mem_ = from_hook(hm);
}

UnicornFloor::~UnicornFloor() {
    if (uc_) uc_close(reinterpret_cast<uc_engine*>(uc_));
}

bool UnicornFloor::map_mem(uint32_t addr, size_t size) {
    return uc_mem_map(reinterpret_cast<uc_engine*>(uc_), addr, size, UC_PROT_ALL) == UC_ERR_OK;
}
bool UnicornFloor::write_mem(uint32_t addr, const void* data, size_t size) {
    return uc_mem_write(reinterpret_cast<uc_engine*>(uc_), addr, data, size) == UC_ERR_OK;
}
bool UnicornFloor::read_mem(uint32_t addr, void* data, size_t size) {
    return uc_mem_read(reinterpret_cast<uc_engine*>(uc_), addr, data, size) == UC_ERR_OK;
}

void UnicornFloor::set_regs(const X86Regs& r) {
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    struct { int id; uint32_t v; } regs[] = {
        {UC_X86_REG_EAX, r.eax}, {UC_X86_REG_ECX, r.ecx}, {UC_X86_REG_EDX, r.edx},
        {UC_X86_REG_EBX, r.ebx}, {UC_X86_REG_ESP, r.esp}, {UC_X86_REG_EBP, r.ebp},
        {UC_X86_REG_ESI, r.esi}, {UC_X86_REG_EDI, r.edi}, {UC_X86_REG_EIP, r.eip},
        {UC_X86_REG_EFLAGS, r.eflags},
    };
    for (auto& reg : regs) {
        uint32_t v = reg.v;
        uc_reg_write(uc, reg.id, &v);
    }
}

X86Regs UnicornFloor::get_regs() {
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    X86Regs r;
    struct { int id; uint32_t* out; } regs[] = {
        {UC_X86_REG_EAX, &r.eax}, {UC_X86_REG_ECX, &r.ecx}, {UC_X86_REG_EDX, &r.edx},
        {UC_X86_REG_EBX, &r.ebx}, {UC_X86_REG_ESP, &r.esp}, {UC_X86_REG_EBP, &r.ebp},
        {UC_X86_REG_ESI, &r.esi}, {UC_X86_REG_EDI, &r.edi}, {UC_X86_REG_EIP, &r.eip},
        {UC_X86_REG_EFLAGS, &r.eflags},
    };
    for (auto& reg : regs) uc_reg_read(uc, reg.id, reg.out);
    return r;
}

uint64_t UnicornFloor::run(uint32_t start_eip, uint64_t max_insns, std::string& err) {
    uc_engine* uc = reinterpret_cast<uc_engine*>(uc_);
    const uint64_t before = seq_;
    uc_err e = uc_emu_start(uc, start_eip, 0, /*timeout=*/0, /*count=*/max_insns);
    if (e != UC_ERR_OK) {
        X86Regs r = get_regs();
        char buf[256];
        std::snprintf(buf, sizeof(buf), "unicorn fault: %s (eip=0x%08x, after %llu insns)",
                      uc_strerror(e), r.eip,
                      static_cast<unsigned long long>(seq_ - before));
        err = buf;
    }
    return seq_ - before;
}

void UnicornFloor::code_trampoline(uc_struct*, uint64_t addr, uint32_t size, void* user) {
    auto* self = static_cast<UnicornFloor*>(user);
    RetiredInsn ev{self->seq_++, static_cast<uint32_t>(addr), size};
    if (self->retired_cb_) self->retired_cb_(ev);
}

void UnicornFloor::mem_trampoline(uc_struct*, int type, uint64_t addr, int size, int64_t value,
                                  void* user) {
    auto* self = static_cast<UnicornFloor*>(user);
    MemEvent ev{};
    ev.seq = self->seq_;
    ev.write = (type == UC_MEM_WRITE) ? 1 : 0;
    ev.addr = static_cast<uint32_t>(addr);
    ev.size = static_cast<uint8_t>(size);
    ev.value = static_cast<uint64_t>(value);
    if (self->mem_cb_) self->mem_cb_(ev);
}

// Silence unused-parameter warnings for the hook handles we retain for lifetime clarity.
static_assert(sizeof(void*) >= sizeof(uc_hook), "uc_hook must fit in void* storage");

}  // namespace xlle
