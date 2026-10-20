#ifndef PILOT_NATIVE_DEBUG_H
#define PILOT_NATIVE_DEBUG_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Elective localhost automation, shared by native titles. No listener exists
 * without an explicit port. Renderer calls remain on the graphics owner. */
int pilot_debug_start(const char *port);
int pilot_debug_active(void);
int pilot_debug_input(uint8_t pad[18]);
void pilot_debug_frame(void);
uint64_t pilot_debug_capture_requested(void);
void pilot_debug_capture_complete(uint64_t request,const unsigned char *rgba,
                                  unsigned width,unsigned height,int status);
#ifdef __cplusplus
}
#endif
#endif
