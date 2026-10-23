/* Authored report vectors: all byte pressures under both source policies. */
#include "input_report.h"
#include <stdio.h>
#include <string.h>
int main(void)
{
    unsigned char surrounded[20];
    for(unsigned pressure=0;pressure<256;++pressure) {
        memset(surrounded,0xa5,sizeof(surrounded));
        unsigned char *report=surrounded+1;
        report[0]=0x39;report[1]=0x12;
        for(unsigned i=2;i<10;++i)report[i]=(unsigned char)pressure;
        for(unsigned i=10;i<18;++i)report[i]=(unsigned char)(i*7);
        pilot_input_report_filter(report);
        if(surrounded[0]!=0xa5 || surrounded[19]!=0xa5 || report[0]!=0x39 || report[1]!=0x12)return 1;
        unsigned expected=pressure;
#if PILOT_INPUT_ANALOG_BUTTON_FLOOR != 0
        if(pressure<PILOT_INPUT_ANALOG_BUTTON_FLOOR)expected=0;
#endif
        for(unsigned i=2;i<10;++i)
            if(report[i]!=expected)return 2;
        for(unsigned i=10;i<18;++i)if(report[i]!=(unsigned char)(i*7))return 3;
    }
    printf("PASS: all analog pressures, digital/stick preservation and canaries; floor=%u\n",(unsigned)PILOT_INPUT_ANALOG_BUTTON_FLOOR);
    return 0;
}
