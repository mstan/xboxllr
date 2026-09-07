#pragma once
#include <vector>
#include "xlle/aot_execution.hpp"

namespace xlle {
class UnicornFloor;

// Owns only immutable compiled-region references and dispatch bookkeeping.
// Unicorn retains all CPU state. Generated integer code temporarily borrows
// GPR/EIP/EFLAGS; x87, SSE, control and hidden state never leave the engine.
class AotExecutor {
public:
    explicit AotExecutor(UnicornFloor& cpu) : cpu_(cpu) {}
    bool add_region(const xlle_aot_region& region);
    bool add_replacement(const AotReplacement& hook);
    void set_profile(uint32_t profile) { profile_ = profile; }
    bool contains(uint32_t pc) const;
    uint64_t execute(uint32_t pc, uint64_t budget);
    AotExecutionStats stats{};
private:
    void* ram(uint32_t address, size_t size, unsigned access);
    static int read32(void* opaque, uint32_t address, uint32_t* value);
    static int write32(void* opaque, uint32_t address, uint32_t value);
    UnicornFloor& cpu_;
    std::vector<const xlle_aot_region*> regions_;
    std::vector<AotReplacement> hooks_;
    uint32_t profile_ = 0;
    xlle_aot_bus* active_bus_ = nullptr;
};
}  // namespace xlle
