// Original xboxlle AOT runtime contract. Version 2 carries the integer state
// touched by generated IA-32 code. The machine adapter certifies its execution
// context; untouched extended/hidden state remains owned by the CPU floor.
#ifndef XLLE_AOT_RUNTIME_H
#define XLLE_AOT_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XLLE_AOT_ABI_VERSION 2u
#define XLLE_AOT_CONTEXT_FLAT32_RAM 1u
// Certified by the machine adapter from cached CPU segments and live MMU state.
// IF, DF, IOPL and ID are preserved by this instruction subset, never modified.
#define XLLE_AOT_CONTEXT_MACHINE_FLAT32_RAM 2u

typedef enum xlle_aot_exit {
    XLLE_AOT_CONTINUE = 0,
    XLLE_AOT_COMPLETE,
    XLLE_AOT_BUDGET,
    XLLE_AOT_UNSUPPORTED,
    XLLE_AOT_CODE_CHANGED,
    XLLE_AOT_MEMORY,
    XLLE_AOT_INVALID_ENTRY,
    XLLE_AOT_CONTEXT,
    XLLE_AOT_EVENT,
    XLLE_AOT_EXTERNAL
} xlle_aot_exit;

typedef struct xlle_aot_state {
    // Encoding order EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI.
    uint32_t gpr[8];
    uint32_t eip;
    uint32_t eflags;
    uint64_t retired;
} xlle_aot_state;

typedef struct xlle_aot_bus {
    uint32_t context;
    uint32_t code_base;
    const uint8_t* code;
    size_t code_size;
    // Cached CS limit, inclusive, for MACHINE_FLAT32_RAM. CS base is zero;
    // DS/SS remain full-range. Synthetic FLAT32_RAM uses a full code range.
    uint32_t code_limit;
    // Stable for an invocation, or changed synchronously by memory callbacks.
    // The machine scheduler bounds each invocation by its next device deadline.
    // No asynchronous guest-device mutation is allowed during an invocation.
    uint32_t event_pending;
    void* opaque;
    // Return 1 on complete success, 0 with NO guest-visible side effects.
    // The adapter must reject MMIO, code-read faults and unsupported mappings.
    // Successful writes to executable bytes must be visible via code immediately.
    int (*read32)(void* opaque, uint32_t address, uint32_t* value);
    int (*write32)(void* opaque, uint32_t address, uint32_t value);
} xlle_aot_bus;

struct xlle_aot_region;
typedef xlle_aot_exit (*xlle_aot_run_fn)(const struct xlle_aot_region*,
    xlle_aot_state*, const xlle_aot_bus*, uint64_t);

typedef struct xlle_aot_region {
    uint32_t abi_version;
    uint32_t base;
    size_t code_size;
    const uint8_t* code;
    xlle_aot_run_fn run;
} xlle_aot_region;

static inline xlle_aot_exit xlle_aot_check(const xlle_aot_region* region,
    const xlle_aot_state* state, const xlle_aot_bus* bus,
    uint64_t budget, uint64_t start)
{
    // The adapter certifies flat 32-bit code/data, mapped RAM and no hidden state
    // effects. VM86 and single-step are unsupported; the certified machine
    // context permits preserved IF/DF/IOPL/ID flags.
    const uint32_t allowed_flags = bus &&
        bus->context == XLLE_AOT_CONTEXT_MACHINE_FLAT32_RAM
        ? UINT32_C(0x00203ed7) : UINT32_C(0x000008d7);
    if (!region || !state || !bus || region->abi_version != XLLE_AOT_ABI_VERSION ||
        (bus->context != XLLE_AOT_CONTEXT_FLAT32_RAM &&
         bus->context != XLLE_AOT_CONTEXT_MACHINE_FLAT32_RAM) ||
        (state->eflags & ~allowed_flags) != 0 || (state->eflags & 2u) == 0 ||
        state->retired < start || state->retired == UINT64_MAX)
        return XLLE_AOT_CONTEXT;
    if (bus->context == XLLE_AOT_CONTEXT_MACHINE_FLAT32_RAM &&
        (region->code_size == 0 ||
         (uint64_t)region->base + region->code_size - 1 > bus->code_limit))
        return XLLE_AOT_CONTEXT;
    if (!region->code || !bus->code || region->code_size == 0 ||
        bus->code_base != region->base || bus->code_size != region->code_size ||
        (uint64_t)region->base + region->code_size > UINT64_C(0xffffffff) ||
        memcmp(region->code, bus->code, region->code_size) != 0)
        return XLLE_AOT_CODE_CHANGED;
    if (bus->event_pending) return XLLE_AOT_EVENT;
    // Completion is checked before budget: the instruction that reaches the end
    // may consume the final unit. An invalid entry must never appear as success.
    if ((uint64_t)state->eip == (uint64_t)region->base + region->code_size)
        return XLLE_AOT_COMPLETE;
    if (state->eip < region->base ||
        (uint64_t)state->eip > (uint64_t)region->base + region->code_size)
        return XLLE_AOT_EXTERNAL;
    if (state->retired - start >= budget) return XLLE_AOT_BUDGET;
    return XLLE_AOT_CONTINUE;
}

// A taken near branch checks CS before retiring. Decline at the original EIP
// so Unicorn delivers any precise #GP; sequential fallthrough faults on fetch.
static inline int xlle_aot_branch_allowed(const xlle_aot_bus* bus, uint32_t target)
{
    return bus->context != XLLE_AOT_CONTEXT_MACHINE_FLAT32_RAM || target <= bus->code_limit;
}

static inline uint32_t xlle_aot_szp(uint32_t value)
{
    uint32_t byte = value & 0xffu;
    byte ^= byte >> 4;
    byte ^= byte >> 2;
    byte ^= byte >> 1;
    return (value == 0 ? 0x40u : 0u) | ((value >> 24) & 0x80u) |
        ((byte & 1u) == 0 ? 4u : 0u);
}

static inline uint32_t xlle_aot_add(xlle_aot_state* state, uint32_t a, uint32_t b)
{
    uint32_t result = a + b;
    uint32_t flags = xlle_aot_szp(result) | (result < a ? 1u : 0u) |
        ((a ^ b ^ result) & 0x10u) |
        (((~(a ^ b) & (a ^ result)) >> 20) & 0x800u);
    state->eflags = (state->eflags & ~UINT32_C(0x8d5)) | flags;
    return result;
}

static inline uint32_t xlle_aot_sub(xlle_aot_state* state, uint32_t a, uint32_t b)
{
    uint32_t result = a - b;
    uint32_t flags = xlle_aot_szp(result) | (a < b ? 1u : 0u) |
        ((a ^ b ^ result) & 0x10u) |
        ((((a ^ b) & (a ^ result)) >> 20) & 0x800u);
    state->eflags = (state->eflags & ~UINT32_C(0x8d5)) | flags;
    return result;
}

static inline uint32_t xlle_aot_logic(xlle_aot_state* state, uint32_t result)
{
    // AF is architecturally undefined for this subset's logical operations.
    // Preserve it explicitly; comparisons must mask AF after these operations.
    state->eflags = (state->eflags & ~UINT32_C(0x8c5)) | xlle_aot_szp(result);
    return result;
}

static inline uint32_t xlle_aot_inc(xlle_aot_state* state, uint32_t value)
{
    uint32_t cf = state->eflags & 1u;
    uint32_t result = xlle_aot_add(state, value, 1u);
    state->eflags = (state->eflags & ~1u) | cf;
    return result;
}

static inline uint32_t xlle_aot_dec(xlle_aot_state* state, uint32_t value)
{
    uint32_t cf = state->eflags & 1u;
    uint32_t result = xlle_aot_sub(state, value, 1u);
    state->eflags = (state->eflags & ~1u) | cf;
    return result;
}

static inline uint8_t xlle_aot_get8(const xlle_aot_state* state, unsigned reg)
{
    return (uint8_t)(state->gpr[reg & 3u] >> ((reg & 4u) ? 8 : 0));
}

static inline void xlle_aot_set8(xlle_aot_state* state, unsigned reg, uint8_t value)
{
    unsigned shift = (reg & 4u) ? 8 : 0;
    uint32_t mask = UINT32_C(0xff) << shift;
    state->gpr[reg & 3u] = (state->gpr[reg & 3u] & ~mask) | ((uint32_t)value << shift);
}

static inline int xlle_aot_read32(const xlle_aot_bus* bus, uint32_t addr, uint32_t* value)
{
    return bus->read32 && bus->read32(bus->opaque, addr, value) == 1;
}

static inline int xlle_aot_write32(const xlle_aot_bus* bus, uint32_t addr, uint32_t value)
{
    return bus->write32 && bus->write32(bus->opaque, addr, value) == 1;
}

#ifdef __cplusplus
}
#endif
#endif
