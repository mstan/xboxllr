#pragma once
#include <vector>
#include <map>
#include "xlle/aot_execution.hpp"

namespace xlle {
class UnicornFloor;

// Owns only immutable compiled-region references and dispatch bookkeeping.
// Unicorn retains all CPU state. Generated integer code temporarily borrows
// GPR/EIP/EFLAGS. Fixed MMX services update engine-owned x87 aliases in place;
// extended/control/hidden state never migrates into a shadow CPU model.
class AotExecutor {
public:
    explicit AotExecutor(UnicornFloor& cpu) : cpu_(cpu) {}
    bool add_region(const xlle_aot_region& region);
    bool add_replacement(const AotReplacement& hook);
    void set_profile(uint32_t profile) { profile_ = profile; }
    bool contains(uint32_t pc) const;
    uint64_t execute(uint32_t pc, uint64_t budget);
    const char* decline_reason() const { return decline_reason_; }
    AotExecutionStats stats{};
    std::vector<AotRegionStats> region_stats;
private:
    size_t region_index(uint32_t pc) const;
    void* ram(uint32_t address, size_t size, unsigned access);
    static int read32(void* opaque, uint32_t address, uint32_t* value);
    static int write32(void* opaque, uint32_t address, uint32_t value);
    void commit_ram(uint8_t* host, uint64_t value, size_t size);
    static xlle_aot_exit mmx_load64(void* opaque, unsigned reg, uint32_t address);
    static xlle_aot_exit mmx_store64(void* opaque, unsigned reg, uint32_t address, unsigned non_temporal);
    static xlle_aot_exit mmx_move(void* opaque, unsigned destination, unsigned source);
    static xlle_aot_exit mmx_emms(void* opaque);
    static xlle_aot_exit prefetch(void* opaque, uint32_t address, unsigned locality);
    static xlle_aot_exit x87_memory(void*, unsigned, uint32_t, uint32_t, unsigned, unsigned);
    static xlle_aot_exit x87_register(void*, unsigned, unsigned, uint32_t, unsigned);
    UnicornFloor& cpu_;
    std::vector<const xlle_aot_region*> regions_;
    // Address order is independent of stable registration/statistics order.
    std::map<uint32_t, size_t> region_by_base_;
    std::vector<AotReplacement> hooks_;
    uint32_t profile_ = 0;
    const char* decline_reason_ = "no-native-entry";
    struct RamAccess {
        uint32_t address = 0;
        uint8_t* host = nullptr;
    };
    // One exact four-byte access per direction, valid only for a single native
    // invocation. This subset cannot change MMU/segments or run devices.
    RamAccess read_access_{}, write_access_{};
};
}  // namespace xlle
