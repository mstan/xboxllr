/* SPDX-License-Identifier: GPL-3.0-or-later
 * GDI's optional diagnostic text overlay is unavailable on native Linux.
 * Frame statistics remain available through the native debug interface/log.
 */
#include <stdio.h>
void d3d8_overlay_draw(const char *text)
{
    static int reported;
    if (text && *text && !reported) {
        fputs("[OVERLAY] GDI debug overlay unavailable on Linux; use native debug statistics.\n", stderr);
        reported=1;
    }
}
void d3d8_overlay_shutdown(void) {}
