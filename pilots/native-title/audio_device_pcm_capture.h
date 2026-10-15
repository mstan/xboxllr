#ifndef PILOT_AUDIO_DEVICE_PCM_CAPTURE_H
#define PILOT_AUDIO_DEVICE_PCM_CAPTURE_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct PilotPcmCapture PilotPcmCapture;
/* Ordinary 600-second stereo48k PCM is115200000 bytes and112500 codec
 * frames. Tiny descriptors can reach the explicit stamp bound earlier. */
#define PILOT_PCM_CAPTURE_BYTES (128ull*1024*1024)
#define PILOT_PCM_CAPTURE_STAMPS 262144ull
typedef struct PilotPcmCaptureHeader {
    char magic[8];
    uint32_t version,header_bytes,rate,channels,bits,status;
    uint64_t lifetime_id,epoch_ticks,pcm_bytes,stamp_count,byte_capacity,stamp_capacity;
} PilotPcmCaptureHeader;
typedef struct PilotPcmCaptureStamp {
    uint64_t lifetime_id,epoch_ticks,observed_pit_ticks,codec_frame,byte_offset;
    uint32_t bytes,reserved; /* zero for PCM; programmed format for XLLESPD1 */
} PilotPcmCaptureStamp;
typedef struct PilotPcmCaptureFooter {
    char magic[8];uint64_t pcm_bytes,stamp_count;uint32_t status,reserved;
} PilotPcmCaptureFooter;
/* Zero limits choose bounded defaults; nonzero limits cannot exceed them.
 * Exclusive <destination>.partial is retained on failure; final destination
 * is non-replacing and published only after successful flush AND close. */
const char *pilot_pcm_capture_create(PilotPcmCapture **out,const char *destination,uint64_t bytes,uint64_t stamps);
/* Uses the same bounded ownership/persistence machinery, but a distinct
 * XLLESPD1 header and per-stamp raw format. No PCM decoding or playback.
 * The common bind/finish/destroy/view APIs below handle either stream kind. */
const char *pilot_spdif_capture_create(PilotPcmCapture **out,const char *destination,uint64_t bytes,uint64_t stamps);
const char *pilot_pcm_capture_bind(PilotPcmCapture *capture);
int pilot_pcm_capture_consume(void *context,const uint8_t *bytes,size_t count);
int pilot_spdif_capture_consume(void *context,const uint8_t *bytes,size_t count,uint32_t format);
const char *pilot_pcm_capture_finish(PilotPcmCapture *capture,int complete);
const char *pilot_pcm_capture_destroy(PilotPcmCapture *capture);
const char *pilot_pcm_capture_error(const PilotPcmCapture *capture);
/* Passive borrowed accepted state, including after rejection/finalization. */
void pilot_pcm_capture_view(const PilotPcmCapture *capture,const uint8_t **bytes,uint64_t *count,
    const PilotPcmCaptureStamp **stamps,uint64_t *stamp_count);
#ifdef PILOT_PCM_CAPTURE_TEST
/* Fixture-only pre-accept failure and finalization I/O injection. */
void pilot_pcm_capture_test_fail(PilotPcmCapture *capture,unsigned phase);
#endif
#ifdef __cplusplus
}
#endif
#endif
