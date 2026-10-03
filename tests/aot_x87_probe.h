#ifndef XLLE_TEST_AOT_X87_PROBE_H
#define XLLE_TEST_AOT_X87_PROBE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { XLLE_PROBE_FLD64, XLLE_PROBE_FADD64, XLLE_PROBE_FSUB64, XLLE_PROBE_FSTP64 };
typedef struct xlle_x87_snapshot {
    uint64_t significand[8], ip, dp;
    uint16_t exponent[8], control, status, cs, ds, opcode;
    uint8_t tags[8], top;
    int arithmetic_exceptions;
} xlle_x87_snapshot;
void xlle_probe_x87_snapshot(void* engine, xlle_x87_snapshot* out);
// Qualification only. Stage on a copy; commit=0 models declining a later memory
// access. This is not a production instruction executor or a guest JIT entry.
int xlle_probe_x87_step(void* engine, unsigned operation, uint64_t input,
                       uint32_t pc, uint32_t address, int commit, uint64_t* output);
#ifdef __cplusplus
}
#endif
#endif
