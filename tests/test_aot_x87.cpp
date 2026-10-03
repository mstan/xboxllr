#include "aot_x87_probe.h"
#include "../src/cpu/unicorn_metrics.h"
#include <unicorn/unicorn.h>
#include <unicorn/x86.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

static void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "x87 qualification: %s\n", message); std::exit(1); }
}
static void uc_ok(uc_err err) { require(err == UC_ERR_OK, uc_strerror(err)); }
static uc_engine* create(uint32_t control) {
    uc_engine* uc = nullptr;
    uc_ok(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));
    uc_ok(uc_mem_map(uc, 0x1000, 0x2000, UC_PROT_ALL));
    uint32_t tags = 0xffff, status = 0;
    uc_ok(uc_reg_write(uc, UC_X86_REG_FPTAG, &tags));
    uc_ok(uc_reg_write(uc, UC_X86_REG_FPSW, &status));
    uc_ok(uc_reg_write(uc, UC_X86_REG_FPCW, &control));
    return uc;
}
static xlle_x87_snapshot snapshot(uc_engine* uc) {
    xlle_x87_snapshot result{}; xlle_probe_x87_snapshot(uc, &result); return result;
}
static void equal(const xlle_x87_snapshot& a, const xlle_x87_snapshot& b) {
    require(std::memcmp(&a, &b, sizeof(a)) == 0, "FPU state differs");
}
int main() {
    xlle_guest_tsc_ticks = 0; // Link and use the runtime's real deterministic clock.
    // fld qword [2000]; fadd qword [2008]; fsub qword [2000]; fstp qword [2010]
    const uint8_t code[] = {0xdd,5,0,0x20,0,0, 0xdc,5,8,0x20,0,0,
                            0xdc,0x25,0,0x20,0,0, 0xdd,0x1d,0x10,0x20,0,0};
    struct Case { uint32_t control; uint64_t a, b, result; };
    // (2^53 + 1) - 2^53 proves an intermediate wider than host double.
    const Case cases[] = {
        {0x37f, 0x4340000000000000ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull},
        {0x27f, 0x4340000000000000ull, 0x3ff0000000000000ull, 0},
        {0x07f, 0x4170000000000000ull, 0x3ff0000000000000ull, 0},
        {0x87f, 0x4170000000000000ull, 0x3ff0000000000000ull, 0x4000000000000000ull},
        {0x37f, 0x3ff8000000000000ull, 0x4002000000000000ull, 0x4002000000000000ull},
    };
    for (const auto& c : cases) {
        auto* native = create(c.control); auto* reference = create(c.control);
        uc_ok(uc_mem_write(reference, 0x1000, code, sizeof(code)));
        uc_ok(uc_mem_write(reference, 0x2000, &c.a, 8));
        uc_ok(uc_mem_write(reference, 0x2008, &c.b, 8));
        for (unsigned step = 0; step < 4; ++step) {
            const auto before = snapshot(native);
            uint64_t output = 0;
            const uint32_t address[] = {0x2000, 0x2008, 0x2000, 0x2010};
            const uint64_t input = step == 1 ? c.b : c.a;
            require(xlle_probe_x87_step(native, step, input, 0x1000 + 6 * step,
                                        address[step], 0, &output), "stage helper");
            equal(before, snapshot(native)); // fault/event before commit has no effects
            require(xlle_probe_x87_step(native, step, input, 0x1000 + 6 * step,
                                        address[step], 1, &output), "commit helper");
            uc_ok(uc_emu_start(reference, 0x1000 + 6 * step, 0x1018, 0, 1));
            equal(snapshot(native), snapshot(reference));
            if (step == 3) {
                uint64_t expected = 0; uc_ok(uc_mem_read(reference, 0x2010, &expected, 8));
                require(output == expected && output == c.result, "independent arithmetic result");
            }
        }
        uc_ok(uc_close(native)); uc_ok(uc_close(reference));
    }
    for (uint32_t control : {0x37eu, 0x37fu}) {
        auto* uc = create(control); const auto before = snapshot(uc); uint64_t out = 123;
        require(!xlle_probe_x87_step(uc, control == 0x37f ? XLLE_PROBE_FADD64 : XLLE_PROBE_FLD64,
                                    0, 0x1000, 0x2000, 1, &out), "unmasked/empty-stack declines");
        equal(before, snapshot(uc)); require(out == 123, "decline leaves result untouched");
        uc_ok(uc_close(uc));
    }
    std::puts("x87 helpers: extended precision, PC/rounding, every boundary and discarded proposals pass; no guest JIT on candidate");
}
