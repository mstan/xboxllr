#pragma once
#include <cstdint>

// Authored independent x87 expectations, all exceptions masked and PC=64.
// FMUL ST0,ST3 or FSUBP ST3,ST0. Intel SDM vol.1 sections 8.2.2 and 8.5.
struct X87ArithmeticCase {
    bool subtract;
    uint64_t a_low,b_low,result_low;
    uint16_t a_high,b_high,result_high,status;
};
inline constexpr X87ArithmeticCase x87_arithmetic_cases[]={
    {false,1,0x8000000000000000ull,1,0,0x3fff,0,2}, // smallest extended denormal * 1
    {false,0x8000000000000000ull,0x8000000000000000ull,0x8000000000000000ull,
     0,0x3fff,1,2}, // pseudo-denormal * 1 normalizes, still reports DE
    {true,1,1,0,0,0,0,2}, // equal denormals subtract to exact +0
    {true,0x8000000000000000ull,0x8000000000000000ull,0,0,0,0,2},
    {false,0,0x8000000000000000ull,0xc000000000000000ull,0,0x7fff,0xffff,1},
    {true,0x8000000000000000ull,0x8000000000000000ull,0xc000000000000000ull,
     0x7fff,0x7fff,0xffff,1}, // infinity - infinity: invalid
    {false,1,0xa000000000000000ull,0xe000000000000000ull,0,0x7fff,0x7fff,1},
    {false,1,0xe000000000000000ull,0xe000000000000000ull,0,0x7fff,0x7fff,0},
    {false,1,1,0xc000000000000000ull,0,1,0xffff,1}, // unsupported encoding wins over DE
    {false,1,0x8000000000000000ull,0x8000000000000000ull,0,0x7fff,0x7fff,2},
};

struct X87MultiplyMemoryCase {
    uint64_t st_low; uint16_t st_high;
    uint64_t memory32,memory64,result_low;
    uint16_t result_high32,result_high64,status;
};
inline constexpr X87MultiplyMemoryCase x87_multiply_memory_cases[]={
    {0xe000000000000123ull,0x7fff,1,1,0xe000000000000123ull,0x7fff,0x7fff,0},
    {0xa000000000000123ull,0x7fff,1,1,0xe000000000000123ull,0x7fff,0x7fff,1},
    {0x8000000000000000ull,0x3fff,1,1,0x8000000000000000ull,0x3f6a,0x3bcd,2},
    // A quiet register NaN wins over a signaling memory NaN even when the
    // latter has a larger payload; invalid is still reported.
    {0xc000000000000001ull,0x7fff,0x7fa00000,0x7ff4000000000000ull,
     0xc000000000000001ull,0x7fff,0x7fff,1},
};
