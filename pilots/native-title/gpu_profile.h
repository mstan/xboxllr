#ifndef PILOT_GPU_PROFILE_H
#define PILOT_GPU_PROFILE_H
#include <stdint.h>
/* Private R09 instrumentation prototype, optional game adapter wiring prepared.
 * One graphics owner/immediate context; no locks, waiting or flushes here. */
enum { PILOT_GPU_PROFILE_PASSES=16, PILOT_GPU_PROFILE_FRAMES=8 };
enum {
    PILOT_GPU_PROFILE_DISJOINT=1, PILOT_GPU_PROFILE_DEVICE_ERROR=2,
    PILOT_GPU_PROFILE_OVERFLOW=4, PILOT_GPU_PROFILE_ORDER_ERROR=8,
    PILOT_GPU_PROFILE_UPLOADS_UNKNOWN=16
};
typedef struct PilotGpuProfile PilotGpuProfile;
typedef struct PilotGpuProfileSample {
    uint64_t source_frame,frequency,upload_bytes,vertex_bytes,texture_bytes;
    uint32_t native_width,native_height,render_width,render_height;
    uint32_t flags,passes,pass_id[PILOT_GPU_PROFILE_PASSES];
    uint64_t begin[PILOT_GPU_PROFILE_PASSES],end[PILOT_GPU_PROFILE_PASSES];
} PilotGpuProfileSample;
/* Device and immediate_context must belong to the same D3D11 device.
 * Returns NULL on unsupported platforms/devices or query creation failure. */
PilotGpuProfile *pilot_gpu_profile_create(void *device,void *immediate_context);
void pilot_gpu_profile_destroy(PilotGpuProfile *profile);
int pilot_gpu_profile_begin_frame(PilotGpuProfile *profile,uint64_t source_frame,
    uint32_t native_width,uint32_t native_height,uint32_t render_width,uint32_t render_height);
int pilot_gpu_profile_begin_pass(PilotGpuProfile *profile,uint32_t pass_id);
int pilot_gpu_profile_end_pass(PilotGpuProfile *profile);
/* Actual source-byte transfers only; kind0=other,1=vertex,2=texture.
 * Backend must separately certify complete counter coverage at end_frame. */
int pilot_gpu_profile_upload(PilotGpuProfile *profile,uint32_t kind,uint64_t bytes);
int pilot_gpu_profile_end_frame(PilotGpuProfile *profile,int uploads_complete);
/* 1=one sample,0=not ready/none,-1=device/query failure; never waits/flushes. */
int pilot_gpu_profile_poll(PilotGpuProfile *profile,PilotGpuProfileSample *sample);
unsigned pilot_gpu_profile_pending(const PilotGpuProfile *profile);
uint64_t pilot_gpu_profile_dropped(const PilotGpuProfile *profile);
#endif
