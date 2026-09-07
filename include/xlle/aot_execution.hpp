#pragma once
#include "xlle/aot_runtime.h"

namespace xlle {

struct AotExecutionStats {
    uint64_t attempts = 0, invocations = 0, retired = 0;
    uint64_t fallback_runs = 0, fallback_insns = 0, handoffs = 0;
    uint64_t context_declines = 0, code_tlb_declines = 0;
    uint64_t memory_reads = 0, memory_writes = 0, memory_declines = 0;
    uint64_t hook_calls = 0, hook_accepts = 0, hook_declines = 0;
    uint64_t hook_ineligible = 0;
    uint64_t exits[XLLE_AOT_EXTERNAL + 1]{};
};

// Version 1 modifications are register-state transactions. The callback receives
// no machine/bus pointer; it calls an independent model and proposes a new state.
// False discards the proposal. Reviewed callbacks must have no other side effects;
// a C++ function pointer is not a sandbox. Memory/device replacements need a later
// explicitly transactional service interface.
struct AotReplacement {
    uint32_t version = 1;
    uint64_t identity = 0;
    uint32_t profile = 0;  // zero is faithful/default and never enables a hook
    const xlle_aot_region* region = nullptr;
    bool (*propose)(const xlle_aot_state& input, xlle_aot_state& output,
                    uint64_t budget) = nullptr;
};

}  // namespace xlle
