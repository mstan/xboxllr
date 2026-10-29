#ifndef PILOT_MEMORY_WRITE_WATCH_H
#define PILOT_MEMORY_WRITE_WATCH_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Host-only acceleration. Unsupported hosts/ranges return false; callers must
 * retain ordinary byte validation. A snapshot is valid only after begin, the
 * original consumer's copy, and end. No guest protection/dirty bit is changed. */
typedef struct PilotWriteSnapshot {
    const void *data;
    size_t bytes;
    uint64_t owner,version,checked;
    unsigned valid;
} PilotWriteSnapshot;
int pilot_write_watch_initialize(void *base,size_t bytes);
void pilot_write_watch_finish(void); /* after all guest/device writers retire */
int pilot_write_watch_begin(const void *data,size_t bytes,PilotWriteSnapshot *snapshot);
void pilot_write_watch_end(PilotWriteSnapshot *snapshot);
int pilot_write_watch_reusable(PilotWriteSnapshot *snapshot);
int pilot_write_watch_invalidate(const void *data,size_t bytes);
/* OS/kernel writes do not necessarily execute a user-mode store instruction.
 * Hold a native writer lease from before submission through async completion. */
int pilot_write_watch_native_begin(const void *data,size_t bytes);
void pilot_write_watch_native_end(const void *data,size_t bytes);
/* Short CPU-owner input captures may share the native-writer ledger without
 * installing read-only pages or depending on their hot-page policy. A true
 * begin holds the observer lock until end; no native writer may start then.
 * Caller must separately exclude guest writers and validate logical access.
 * Unsupported/uninitialized observers and outstanding writers refuse. */
int pilot_write_watch_native_read_begin(const void *data,size_t bytes);
void pilot_write_watch_native_read_end(void); /* only after a true begin */
uint64_t pilot_write_watch_faults(void);
#ifdef __cplusplus
}
#endif
#endif
