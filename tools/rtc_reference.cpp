// Private original-code qualification bridge. Only adapter/ownership here;
// every register operation uses the maintained RTC. Not installed/shipped.
#include "machine/rtc.hpp"
#include <cstdint>
#ifdef _WIN32
#define RTC_API extern "C" __declspec(dllexport)
#else
#define RTC_API extern "C" __attribute__((visibility("default")))
#endif
RTC_API void* xlle_rtc_create() { return new xlle::Mc146818Rtc; }
RTC_API void xlle_rtc_destroy(void* opaque) { delete static_cast<xlle::Mc146818Rtc*>(opaque); }
RTC_API int xlle_rtc_access(void* opaque,uint32_t port,int size,int write,uint32_t* value) {
    auto* rtc=static_cast<xlle::Mc146818Rtc*>(opaque);
    if(!rtc || !value || port>0xffff || !rtc->owns((uint16_t)port) || size!=1 ||
       (write && *value>255))return 0;
    if(write)rtc->io_write((uint16_t)port,size,*value);
    else *value=rtc->io_read((uint16_t)port,size);
    return 1;
}
