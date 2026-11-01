#ifndef PILOT_DSP_DMA_H
#define PILOT_DSP_DMA_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct PilotDspDmaView {
 uint32_t *x,*y,*mix; /*4096,2048,1024words respectively.*/
 uint8_t *scratch;uint32_t scratch_bytes;
 uint8_t *owned,*dirty;uint32_t owned_bytes,dirty_bytes;
} PilotDspDmaView;
/* One validated payload, with explicit little-endian ring semantics. Native
 * view/offset storage and dirty bitmap must not alias payload data. Banks and
 * selected scratch ring are checked disjoint for the payload spans. Callers exclude concurrent data
 * mutation, retain descriptor/IRQ/node ownership, and publish after completion.
 * Returns0 without writing for unsupported spans/invalid native shapes, so the
 * original operation can retain its exact validation/failure behavior. */
int pilot_dsp_dma_payload(const PilotDspDmaView *view,unsigned to_scratch,
 uint32_t address,uint32_t count,uint32_t ring_base,uint32_t ring_size,
 uint32_t offset,uint32_t *next_offset);
#ifdef __cplusplus
}
#endif
#endif
