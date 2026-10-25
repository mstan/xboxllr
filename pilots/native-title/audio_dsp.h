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
/* Isolated execution keeps live DSP stores off guest RAM. Begin/publication
 * run under the runtime CPU permit; the real graph runs between them without
 * that permit. CPU writes during execution survive unless the DSP writes the
 * same word, in which case its later publication wins. */
PilotDsp *pilot_dsp_create_isolated(const PilotDspImage *image,void *storage,uint32_t guest_base);
void pilot_dsp_begin(PilotDsp *dsp);
void pilot_dsp_publish(PilotDsp *dsp);
void pilot_dsp_destroy(PilotDsp *dsp);
void pilot_dsp_process(PilotDsp *dsp,const uint32_t mix[1024],uint32_t output[192]);
uint64_t pilot_dsp_fingerprint(const PilotDsp *dsp);
uint64_t pilot_dsp_blocks(const PilotDsp *dsp);
uint64_t pilot_dsp_dma_nodes(const PilotDsp *dsp);
/* CPU shadow state is separate from live GP execution. A patch batch validates
 * every range before changing either owner. The caller must exclude Process. */
struct PilotDspPatch {uint32_t offset,bytes;const void *data;};
int pilot_dsp_effect_read(const PilotDsp *dsp,unsigned effect,uint32_t offset,void *data,uint32_t bytes);
int pilot_dsp_effect_patch(PilotDsp *dsp,unsigned effect,unsigned count,const struct PilotDspPatch *patches);
#ifdef __cplusplus
}
#endif
