#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Bounded GP effects executor. Input/output are planar 24-bit words, 32 frames.
 * The caller owns zeroed, aligned storage for the entire graph lifetime.
 * No audio device, guest address, asset key or title binding is implicit. */
typedef struct PilotDspImage PilotDspImage;
typedef struct PilotDsp PilotDsp;
PilotDspImage *pilot_dsp_decode(const void *image,uint32_t bytes,uint64_t seed,const char **error);
void pilot_dsp_image_destroy(PilotDspImage *image);
uint32_t pilot_dsp_storage_bytes(const PilotDspImage *image);
uint32_t pilot_dsp_descriptor_offset(const PilotDspImage *image);
PilotDsp *pilot_dsp_create(const PilotDspImage *image,void *storage,uint32_t guest_base);
void pilot_dsp_destroy(PilotDsp *dsp);
void pilot_dsp_process(PilotDsp *dsp,const uint32_t mix[1024],uint32_t output[192]);
uint64_t pilot_dsp_blocks(const PilotDsp *dsp);
uint64_t pilot_dsp_dma_nodes(const PilotDsp *dsp);
#ifdef __cplusplus
}
#endif
