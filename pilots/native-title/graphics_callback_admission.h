#ifndef PILOT_GRAPHICS_CALLBACK_ADMISSION_H
#define PILOT_GRAPHICS_CALLBACK_ADMISSION_H
#if defined(__has_include)
#if __has_include("pilot_gpu_callback_effects.h")
#include "pilot_gpu_callback_effects.h"
#endif
#endif
#ifndef PILOT_GPU_CALLBACK_EFFECTS_VERSION
#define PILOT_GPU_CALLBACK_EFFECTS_VERSION 0
#endif
/* This exact original closure includes RECOMP_TRACE_ENTER/checkpoints and
 * xbox_ReadTimeStampCounter. Other scheduler/device/clock branches have broader
 * effects and retain the original dispatch path, even with a debug override. */
#if PILOT_GPU_CALLBACK_EFFECTS_VERSION == 1 && defined(_WIN32) && defined(PILOT_MEMORY_WRITE_WATCH) && !defined(PILOT_WRITE_WATCH_PORTABLE) && !defined(PILOT_SCHEDULING_LLE) && !defined(PILOT_AUDIO_REFERENCE) && !defined(PILOT_AUDIO_DEVICE_LLE) && !defined(PILOT_KERNEL_CLOCK_NATIVE) && !defined(PILOT_USB_LLE) && !defined(PILOT_IO_DEVICE_LLE) && !defined(PILOT_MEMORY_LLE) && !defined(PILOT_FILES_LLE)
#define PILOT_GPU_SCALAR_PREPARATION_AVAILABLE 1
#else
#define PILOT_GPU_SCALAR_PREPARATION_AVAILABLE 0
#endif
#endif
