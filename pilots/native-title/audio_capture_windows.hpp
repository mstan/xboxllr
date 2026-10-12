/* Bounded opt-in observation schedule; it never advances device time. */
#pragma once
#include <cstdint>
struct AudioCaptureWindow {uint32_t start,seconds;};
struct AudioCaptureWindows {AudioCaptureWindow items[3]{};unsigned count=0;bool explicit_windows=false;};
static bool audio_capture_number(const char *&p,uint32_t &n)
{
    if(*p<'0' || *p>'9')return false;
    n=0;
    do {unsigned digit=(unsigned)(*p++-'0');if(n>(1200-digit)/10)return false;n=n*10+digit;}
    while(*p>='0' && *p<='9');
    return true;
}
static bool audio_capture_windows_parse(const char *text,unsigned seconds,AudioCaptureWindows &out)
{
    AudioCaptureWindows parsed;
    if(!text) {
        if(!seconds || seconds>300)return false;
        parsed.items[parsed.count++]={0,seconds};out=parsed;return true;
    }
    parsed.explicit_windows=true;
    const char *p=text;
    for(;;) {
        uint32_t start,duration;
        if(parsed.count==3 || !audio_capture_number(p,start) || *p++!=':' ||
           !audio_capture_number(p,duration) || !duration || duration>300 ||
           start>1200-duration || (parsed.count &&
           start<=parsed.items[parsed.count-1].start+parsed.items[parsed.count-1].seconds))return false;
        parsed.items[parsed.count++]={start,duration};
        if(!*p)break;
        if(*p++!=',')return false;
    }
    out=parsed;return true;
}
/* Ceiling to milliseconds preserves an absolute QPC deadline. The caller
 * rechecks QPC after every wait rather than accumulating nominal sleeps. */
static uint32_t audio_capture_remaining_ms(uint64_t now,uint64_t deadline)
{return now>=deadline?0:(uint32_t)((deadline-now+9999)/10000);}
