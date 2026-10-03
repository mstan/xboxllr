/*
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; see LICENSES/LGPL-2.1.txt.
 *
 * xlle corrections included in the hash-pinned, derived QEMU FPU helper file.
 * Arithmetic remains in the existing portable extended-precision SoftFloat.
 * Exception merging follows the newer upstream QEMU/xemu implementation in
 * target/i386/tcg/fpu_helper.c (xemu 482088490c2d5983ed5e1eafbd09906157c1eca0).
 * This extension is distributed under LGPL-2.1-or-later, matching that donor;
 * the original derived file retains its upstream copyright/license header.
 */
static int xlle_fp_begin(CPUX86State *env)
{
    int old = get_float_exception_flags(&env->fp_status);
    set_float_exception_flags(0, &env->fp_status);
    env->fpus &= ~0x200; /* C1 is defined for these arithmetic/transfer forms. */
    return old;
}

static void xlle_fp_end(CPUX86State *env, int old)
{
    int flags = get_float_exception_flags(&env->fp_status);
    fpu_set_exception(env, (flags & 0x3d) |
                          ((flags & float_flag_input_denormal) ? FPUS_DE : 0));
    set_float_exception_flags(flags | old, &env->fp_status);
}

static bool xlle_fp_larger_magnitude(floatx80 a, floatx80 b)
{
    unsigned ae = a.high & 0x7fff, be = b.high & 0x7fff;
    return ae > be || (ae == be && a.low > b.low);
}

typedef floatx80 (*xlle_fp_binary_fn)(floatx80, floatx80, float_status *);
static bool xlle_fp_denormal(floatx80 value)
{
    /* Pseudo-denormals (explicit integer bit set with exponent zero) also
     * report DE on x87; the existing arithmetic normalizes their value.
     */
    return !(value.high & 0x7fff) && value.low;
}
static floatx80 xlle_fp_binary_input(CPUX86State *env, floatx80 a, floatx80 b,
                                    xlle_fp_binary_fn operation, int input_flags)
{
    int old = xlle_fp_begin(env);
    float_status chopped = env->fp_status;
    floatx80 result = operation(a, b, &env->fp_status);
    float_raise(input_flags & ~float_flag_input_denormal, &env->fp_status);
    /* This pinned SoftFloat normalizes 80-bit denormal inputs but does not
     * publish the x87 DE flag. Respect the higher-priority invalid, NaN and
     * divide-by-zero cases instead of setting DE merely from operand bits.
     */
    if ((xlle_fp_denormal(a) || xlle_fp_denormal(b) || (input_flags & float_flag_input_denormal)) &&
        !floatx80_is_any_nan(a) && !floatx80_is_any_nan(b) &&
        !(get_float_exception_flags(&env->fp_status) &
          (float_flag_invalid | float_flag_divbyzero))) {
        float_raise(float_flag_input_denormal, &env->fp_status);
    }
    if (get_float_exception_flags(&env->fp_status) & float_flag_inexact) {
        /* C1 describes rounding away from zero, independent of the sign.
         * Re-evaluate only inexact operations at the same precision with
         * truncation; the comparison never substitutes host double math.
         */
        set_float_rounding_mode(float_round_to_zero, &chopped);
        if (xlle_fp_larger_magnitude(result, operation(a, b, &chopped))) env->fpus |= 0x200;
    }
    xlle_fp_end(env, old);
    return result;
}
static floatx80 xlle_fp_binary(CPUX86State *env, floatx80 a, floatx80 b,
                              xlle_fp_binary_fn operation)
{
    return xlle_fp_binary_input(env, a, b, operation, 0);
}

#define XLLE_BINARY_HELPER(name, destination, a, b, operation, parameters) \
void helper_##name parameters { \
    destination = xlle_fp_binary(env, a, b, operation); \
}
XLLE_BINARY_HELPER(fadd_ST0_FT0, ST0, ST0, FT0, floatx80_add, (CPUX86State *env))
XLLE_BINARY_HELPER(fmul_ST0_FT0, ST0, ST0, FT0, floatx80_mul, (CPUX86State *env))
XLLE_BINARY_HELPER(fsub_ST0_FT0, ST0, ST0, FT0, floatx80_sub, (CPUX86State *env))
XLLE_BINARY_HELPER(fsubr_ST0_FT0, ST0, FT0, ST0, floatx80_sub, (CPUX86State *env))
XLLE_BINARY_HELPER(fdiv_ST0_FT0, ST0, ST0, FT0, floatx80_div, (CPUX86State *env))
XLLE_BINARY_HELPER(fdivr_ST0_FT0, ST0, FT0, ST0, floatx80_div, (CPUX86State *env))
XLLE_BINARY_HELPER(fadd_STN_ST0, ST(st_index), ST(st_index), ST0, floatx80_add, (CPUX86State *env, int st_index))
XLLE_BINARY_HELPER(fmul_STN_ST0, ST(st_index), ST(st_index), ST0, floatx80_mul, (CPUX86State *env, int st_index))
XLLE_BINARY_HELPER(fsub_STN_ST0, ST(st_index), ST(st_index), ST0, floatx80_sub, (CPUX86State *env, int st_index))
XLLE_BINARY_HELPER(fsubr_STN_ST0, ST(st_index), ST0, ST(st_index), floatx80_sub, (CPUX86State *env, int st_index))
XLLE_BINARY_HELPER(fdiv_STN_ST0, ST(st_index), ST(st_index), ST0, floatx80_div, (CPUX86State *env, int st_index))
XLLE_BINARY_HELPER(fdivr_STN_ST0, ST(st_index), ST0, ST(st_index), floatx80_div, (CPUX86State *env, int st_index))
#undef XLLE_BINARY_HELPER

static bool xlle_fp_subnormal32(uint32_t value)
{
    return !(value & 0x7f800000u) && (value & 0x7fffffu);
}
static bool xlle_fp_subnormal64(uint64_t value)
{
    return !(value & UINT64_C(0x7ff0000000000000)) && (value & UINT64_C(0xfffffffffffff));
}

/* A memory operand conversion and its arithmetic belong to one instruction.
 * Publish their flags together so a denormal input cannot preempt a higher
 * priority NaN/invalid result. Standalone FLD retains its own load semantics.
 */
#define XLLE_MEMORY_MULTIPLY(name, type, convert, subnormal) \
void helper_##name(CPUX86State *env, type value) { \
    float_status conversion = env->fp_status; \
    set_float_exception_flags(0, &conversion); \
    FT0 = convert(value, &conversion); \
    int flags = get_float_exception_flags(&conversion); \
    if (subnormal(value)) flags |= float_flag_input_denormal; \
    ST0 = xlle_fp_binary_input(env, ST0, FT0, floatx80_mul, flags); \
}
XLLE_MEMORY_MULTIPLY(xlle_fmul32, uint32_t, float32_to_floatx80, xlle_fp_subnormal32)
XLLE_MEMORY_MULTIPLY(xlle_fmul64, uint64_t, float64_to_floatx80, xlle_fp_subnormal64)
#undef XLLE_MEMORY_MULTIPLY

#define XLLE_LOAD_HELPER(name, type, subnormal) \
void helper_##name(CPUX86State *env, type value) { \
    int old = xlle_fp_begin(env); \
    xlle_raw_##name(env, value); \
    if (subnormal(value)) float_raise(float_flag_input_denormal, &env->fp_status); \
    xlle_fp_end(env, old); \
}
XLLE_LOAD_HELPER(flds_FT0, uint32_t, xlle_fp_subnormal32)
XLLE_LOAD_HELPER(fldl_FT0, uint64_t, xlle_fp_subnormal64)
XLLE_LOAD_HELPER(flds_ST0, uint32_t, xlle_fp_subnormal32)
XLLE_LOAD_HELPER(fldl_ST0, uint64_t, xlle_fp_subnormal64)
#undef XLLE_LOAD_HELPER

uint32_t helper_fsts_ST0(CPUX86State *env)
{
    int old = xlle_fp_begin(env);
    float_status chopped = env->fp_status;
    uint32_t result = xlle_raw_fsts_ST0(env);
    if (get_float_exception_flags(&env->fp_status) & float_flag_inexact) {
        set_float_rounding_mode(float_round_to_zero, &chopped);
        if ((result & 0x7fffffffu) > (floatx80_to_float32(ST0, &chopped) & 0x7fffffffu))
            env->fpus |= 0x200;
    }
    xlle_fp_end(env, old);
    return result;
}

uint64_t helper_fstl_ST0(CPUX86State *env)
{
    int old = xlle_fp_begin(env);
    float_status chopped = env->fp_status;
    uint64_t result = xlle_raw_fstl_ST0(env);
    if (get_float_exception_flags(&env->fp_status) & float_flag_inexact) {
        set_float_rounding_mode(float_round_to_zero, &chopped);
        if ((result & UINT64_C(0x7fffffffffffffff)) >
            (floatx80_to_float64(ST0, &chopped) & UINT64_C(0x7fffffffffffffff))) env->fpus |= 0x200;
    }
    xlle_fp_end(env, old);
    return result;
}

void helper_fxchg_ST0_STN(CPUX86State *env, int st_index)
{
    env->fpus &= ~0x200;
    xlle_raw_fxchg_ST0_STN(env, st_index);
}
void helper_fmov_ST0_STN(CPUX86State *env, int st_index)
{
    env->fpus &= ~0x200;
    xlle_raw_fmov_ST0_STN(env, st_index);
}
void helper_fmov_STN_ST0(CPUX86State *env, int st_index)
{
    env->fpus &= ~0x200;
    xlle_raw_fmov_STN_ST0(env, st_index);
    env->fptags[(env->fpstt + st_index) & 7] = 0;
}

/* A conversion may change SoftFloat/FPSW even when the eventual guest store
 * faults. Keep its proposal private until the real memory operation succeeds.
 * flags: bit 0 pops after success; bit 1 selects a 64-bit rather than 32-bit store.
 */
void helper_xlle_fst(CPUX86State *env, target_ulong address, uint32_t flags)
{
    const uintptr_t retaddr = GETPC();
    const float_status before_status = env->fp_status;
    const uint16_t before_sw = env->fpus;
    uint64_t value = flags & 2 ? helper_fstl_ST0(env) : helper_fsts_ST0(env);
    const float_status after_status = env->fp_status;
    const uint16_t after_sw = env->fpus;
    env->fp_status = before_status;
    env->fpus = before_sw;
    /* The real store helper checks both guest pages before split writes,
     * including A/D bits, watchpoints and SMC. Do not precede it with
     * probe_access: that pinned Unicorn path incorrectly looks up a virtual
     * address as a physical mapping when invalidating translated code.
     */
    if (flags & 2) cpu_stq_data_ra(env, address, value, retaddr);
    else cpu_stl_data_ra(env, address, (uint32_t)value, retaddr);
    env->fp_status = after_status;
    env->fpus = after_sw;
    if (flags & 1) fpop(env);
}
