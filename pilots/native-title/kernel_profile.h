#pragma once
/* Native services identify their own exported API profile. This does not map
 * or execute an original kernel image in a native title process. */
#define PILOT_NATIVE_KERNEL_BUILD 5838u
#if defined(PILOT_STARTUP_NATIVE)
#if defined(PILOT_SCHEDULING_LLE) || defined(PILOT_USB_LLE) || defined(PILOT_IO_DEVICE_LLE)
#error Native startup compatibility requires the native export-only kernel
#endif
#define PILOT_NATIVE_KERNEL_EXPORT_ONLY 1
#else
#define PILOT_NATIVE_KERNEL_EXPORT_ONLY 0
#endif
