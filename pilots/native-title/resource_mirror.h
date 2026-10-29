#ifndef PILOT_RESOURCE_MIRROR_H
#define PILOT_RESOURCE_MIRROR_H
#include "memory_write_watch.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Host mirror ownership, under the existing serialized graphics owner.
 * Source is the canonical guest allocation; identity names native storage,
 * including its generation and exact interpretation. No pointer-only hit.
 * Unsupported/unqualified observations return false for the byte fallback. */
typedef struct PilotResourceIdentity {
    uint64_t generation;
    uint32_t kind,subresource,format,width,height,levels;
} PilotResourceIdentity;
typedef struct PilotResourceWhole {
    PilotWriteSnapshot snapshot;
    uint64_t gpu_revision;
    PilotResourceIdentity identity;
} PilotResourceWhole;
typedef struct PilotResourcePage PilotResourcePage;
typedef struct PilotResourcePages {
    const void *source;
    size_t bytes,count,metadata_bytes;
    PilotResourceIdentity identity;
    uint64_t reset_epoch;
    PilotResourcePage *pages;
} PilotResourcePages;
typedef struct PilotResourceMirrorStats {
    uint64_t lookups,hits,lookup_bytes,capture_attempts,captured_pages;
    uint64_t captured_bytes,rejected_pages,unavailable_pages,compared_bytes;
    uint64_t metadata_bytes,metadata_peak,metadata_refusals;
    uint64_t whole_lookups,whole_hits,whole_captures;
} PilotResourceMirrorStats;
typedef struct PilotResourceTransferStats {
    uint64_t vertex_full_calls,vertex_full_compares,vertex_full_compare_bytes;
    uint64_t vertex_full_updates,vertex_full_bytes,vertex_range_calls;
    uint64_t vertex_range_compares,vertex_range_compare_bytes;
    uint64_t vertex_range_updates,vertex_range_bytes,vertex_refetches;
    uint64_t texture_sample_calls,texture_snapshot_hits,texture_snapshot_misses;
    uint64_t texture_upload_calls,texture_compare_spans,texture_compare_bytes;
    uint64_t texture_payload_updates,texture_payload_bytes;
} PilotResourceTransferStats;
extern int pilot_resource_mirror_diagnostics;
extern PilotResourceMirrorStats pilot_resource_mirror_stats;
extern PilotResourceTransferStats pilot_resource_transfer_stats;
/* Byte counts are comparison/upload span arguments, not measured bus traffic
 * or the number of bytes libc inspected before an early mismatch. */
#define PILOT_RESOURCE_COUNT(field,amount) do {if(pilot_resource_mirror_diagnostics)pilot_resource_transfer_stats.field+=(uint64_t)(amount);} while(0)
uint64_t pilot_resource_new_identity(void);
int pilot_resource_whole_reusable(PilotResourceWhole *,const PilotResourceIdentity *,const void *,size_t,uint64_t);
int pilot_resource_whole_begin(PilotResourceWhole *,const PilotResourceIdentity *,const void *,size_t);
void pilot_resource_whole_end(PilotResourceWhole *,uint64_t);
void pilot_resource_pages_clear(PilotResourcePages *);
void pilot_resource_pages_invalidate(PilotResourcePages *,size_t,size_t);
void pilot_resource_pages_invalidate_all(PilotResourcePages *);
int pilot_resource_pages_reusable(PilotResourcePages *,const PilotResourceIdentity *,const void *,size_t,size_t,size_t);
/* Mirror contains actual native CPU snapshot bytes, not a desired upload.
 * Certify guest-page intersections only after full-span equality under the
 * write observer. A successful consumed-subset comparison is insufficient. */
void pilot_resource_pages_capture(PilotResourcePages *,const PilotResourceIdentity *,const void *,const void *,size_t,size_t,size_t);
#ifdef __cplusplus
}
#endif
#endif
