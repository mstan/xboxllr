// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef XBOXLLR_BOOT_WINDOW_API_H
#define XBOXLLR_BOOT_WINDOW_API_H
#include <stdint.h>
#include <wchar.h>
#if defined(_WIN32)
#define XBOXLLR_BOOT_CALL __cdecl
#else
#define XBOXLLR_BOOT_CALL
#endif

/* A borrowed window must belong to the calling process AND calling thread.
 * The module never creates, moves, resizes or destroys that window. All of its
 * GPU/audio objects are released before returning it to the title renderer. */
typedef struct XboxllrBootWindowV1 {
    uint32_t size;
    uint32_t volume;
    uint32_t flags;
    void *window;
    const wchar_t *pack;
    const wchar_t *title_logo;
    const wchar_t *report;
} XboxllrBootWindowV1;
enum { XBOXLLR_BOOT_WINDOW_MUTE = 1, XBOXLLR_BOOT_WINDOW_HIDDEN = 2 };
enum { XBOXLLR_BOOT_WINDOW_OK = 0, XBOXLLR_BOOT_WINDOW_FAILED = 2, XBOXLLR_BOOT_WINDOW_CLOSED = 3 };
typedef int (XBOXLLR_BOOT_CALL *XboxllrBootRunWindowV1)(const XboxllrBootWindowV1 *);
#endif
