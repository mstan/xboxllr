// xlle — UnicornFloor: CPU floor backed by the harvested QEMU x86 core (via Unicorn).
#pragma once
#include "xlle/cpu_floor.hpp"

struct uc_struct;  // unicorn's uc_engine (opaque)

namespace xlle {

class UnicornFloor : public CpuFloor {
public:
    UnicornFloor();
    ~UnicornFloor() override;

    const char* name() const override { return "unicorn"; }

    bool map_mem(uint32_t addr, size_t size) override;
    bool write_mem(uint32_t addr, const void* data, size_t size) override;
    bool read_mem(uint32_t addr, void* data, size_t size) override;

    void set_regs(const X86Regs& r) override;
    X86Regs get_regs() override;

    bool read_reg(XReg r, void* buf, size_t n) override;
    bool write_reg(XReg r, const void* buf, size_t n) override;
    bool setup_flat_segments(uint32_t gdt_addr) override;

    void on_retired(RetiredCb cb) override { retired_cb_ = std::move(cb); }
    void on_mem(MemCb cb) override { mem_cb_ = std::move(cb); }

    uint64_t run(uint32_t start_eip, uint64_t max_insns, std::string& err) override;

    bool ok() const { return uc_ != nullptr; }

private:
    // C-ABI trampolines Unicorn calls; they recover `this` from user_data.
    static void code_trampoline(uc_struct*, uint64_t addr, uint32_t size, void* user);
    static void mem_trampoline(uc_struct*, int type, uint64_t addr, int size, int64_t value,
                               void* user);

    uc_struct* uc_ = nullptr;
    void* hook_code_ = nullptr;  // uc_hook is uintptr-sized; stored type-erased
    void* hook_mem_ = nullptr;
    RetiredCb retired_cb_;
    MemCb mem_cb_;
    uint64_t seq_ = 0;  // global retirement/dispatch index feeding the rings
};

}  // namespace xlle
