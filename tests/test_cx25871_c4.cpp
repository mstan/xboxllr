#include "machine/cx25871_control.hpp"
#include <cstdio>
#include <string>

int main() {
    int failures=0;
    auto check=[&](bool ok,const char* label) {
        if(!ok) { std::printf("FAIL: %s\n",label); ++failures; }
    };
    std::string fault;
    xlle::Cx25871Control encoder([&](const std::string& why){fault=why;});
    uint8_t readback=0x55;
    check(encoder.write_byte(0x6c,0x46),"enable standard readback");
    check(!encoder.read_byte(0xc4,readback) && readback==0x55,
          "warm C4 cannot be inferred from zero initialization");
    for(unsigned selector=0;selector<4;++selector) {
        for(unsigned gate:{0u,8u}) for(unsigned enable:{0u,1u}) {
            const auto value=uint8_t((selector<<6)|gate|enable);
            check(encoder.write_byte(0xc4,value),"disabled-caption write");
            check(encoder.read_byte(0xc4,readback) && readback==value,
                  "standard readback preserves selectors and inactive gate");
            for(unsigned unsupported:{2u,4u,0x10u,0x20u,0x30u}) {
                check(!encoder.write_byte(0xc4,uint8_t(value|unsupported)),
                      "unqualified video/caption controls reject");
                check(encoder.read_byte(0xc4,readback) && readback==value,
                      "rejected write does not change C4 latch");
            }
        }
    }
    check(encoder.write_byte(0xba,0x80),"software reset");
    check(encoder.write_byte(0x6c,0x46),"restore standard readback");
    check(!encoder.read_byte(0xc4,readback),"reset does not invent ESTATUS default");
    check(encoder.write_byte(0xba,0) && encoder.write_byte(0xc6,0x9c) &&
          encoder.write_byte(0xc4,1) && encoder.write_byte(0x6c,0xc6) &&
          encoder.advance_timing(17),"establish real encoder timing");
    xlle::Cx25871Timing::State before{},after{};
    check(encoder.timing_state(before),"capture timing phase");
    check(encoder.write_byte(0xc4,0xc9),"set selector and inactive gate");
    check(encoder.timing_state(after) && before.clocks==after.clocks &&
          before.subcarrier_phase==after.subcarrier_phase &&
          before.input_x==after.input_x && before.output_x==after.output_x &&
          before.input_y==after.input_y && before.output_y==after.output_y,
          "selector and disabled-caption gate preserve timing");
    check(!encoder.receive_byte(readback),"legacy readback remains guarded");
    std::printf("CX25871 C4: %d failures\n",failures);
    return failures?1:0;
}
