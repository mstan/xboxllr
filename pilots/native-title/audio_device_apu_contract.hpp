// Shared admission for the original native device and source-execution transport.
#pragma once
#include "machine/apu_startup_config.hpp"
#include "machine/apu_dsp_host.hpp"

inline bool pilot_original_apu_admitted(uint32_t off,unsigned size,int write,uint32_t value) {
    if(size!=4 || (off&3))return false;
    if(xlle::ApuDspHostRegisters::contains(off))return !write || xlle::ApuDspHostRegisters::permits(off,value);
    if(!write && xlle::ApuStartupMethods::register_index(off)>=0)return true;
    if(xlle::ApuStartupMethods::touches(off,int(size)))return write;
    if(xlle::ApuStartupMappings::touches(off,int(size)))return true;
    if(xlle::ApuStartupConfiguration::touches(off,int(size)))return true; // Device owns value/context admission.
    if(off==0x200c && write)return value==0; // Device also requires mode0.
    if(off==0x3fffc || off==0x5fffc)return !write || value==1 || value==3 || (off==0x3fffc && value==0);
    if(!write) {
        if(off>=0x3024 && off<=0x3054 && (off&15)==4)return true;
        if(off>=0x2054 && off<=0x2074)return true;
        switch(off) {
        case 0x2000:case 0x200c:case 0x1000:case 0x1004:case 0x1100:case 0x1110:case 0x1300:case 0x1304:
        case 0x1510:case 0x20010:return true;
        default:return false;
        }
    }
    // Original startup composition already qualifies these physical
    // scratch/SG/FIFO mappings. Mapping storage never releases held DSPs.
    if(((off>=0x3024 && off<=0x307c) || (off>=0x4024 && off<=0x407c)) &&
       (off&15))return true; // lane +4/+8/+C only; +0 is not a register
    if(off>=0x20200 && off<=0x2027c)return true;
    switch(off) {
    case 0x2040:case 0x2044:case 0x2048:case 0x204c:
    case 0x20d4:case 0x20d8:case 0x20dc:case 0x20e0:
    case 0x202c:case 0x2030:case 0x2034:case 0x115c:case 0x1000:case 0x1004:
    case 0x1100:case 0x1504:case 0x1510:case 0x2000:
    case 0x2054:case 0x2060:case 0x206c:
    case 0x2058:case 0x205c:case 0x2064:case 0x2068:case 0x2070:case 0x2074:
    case 0x20120:case 0x20124:case 0x20128:case 0x20140:
    case 0x202f8:case 0x202fc:
    case 0x20300:case 0x20304:case 0x20308:case 0x2030c:
    case 0x20310:case 0x20314:case 0x20318:
    case 0x20360:case 0x20364:case 0x20368:case 0x2036c:
    case 0x20374:case 0x20378:case 0x2037c:
    case 0x203a0:case 0x203a4:case 0x203d8:case 0x203dc:return true;
    default:return false;
    }
}
