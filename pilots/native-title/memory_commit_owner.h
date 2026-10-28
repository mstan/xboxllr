#ifndef PILOT_MEMORY_COMMIT_OWNER_H
#define PILOT_MEMORY_COMMIT_OWNER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Admit a wholly reserved, uncommitted address space before publishing any
 * workers. All mapping writers must use the owned Win32/NT entry points.
 * Other profiles/hosts retain the existing authoritative host query. */
int pilot_commit_owner_initialize(void *base,size_t bytes);
void pilot_commit_owner_finish(void);
void pilot_commit_owner_publish(const void *data,size_t bytes,int committed);
void pilot_commit_owner_release(const void *data,size_t bytes,int reservation);
int pilot_commit_owner_query(uint32_t base,uint64_t bytes,uint64_t *committed);
enum PilotMemoryPermission {
    PILOT_MEMORY_READ=1,PILOT_MEMORY_WRITE=2,PILOT_MEMORY_DATA_WRITE=4
};
/* Intersection across the whole span. False means the caller must use its
 * existing host-query path; an output of zero is a proved inaccessible span.
 * DATA_WRITE retains the title registry's stricter non-executable write rule.
 * One-shot PAGE_GUARD ranges deliberately require the host-query fallback. */
int pilot_commit_owner_permissions(uint32_t base,uint64_t bytes,unsigned *permissions);
#ifdef __cplusplus
}
#endif
#endif
