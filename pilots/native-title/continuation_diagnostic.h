/* Diagnostic AOT execution records. No host addresses or host stack bytes. */
#ifndef PILOT_CONTINUATION_DIAGNOSTIC_H
#define PILOT_CONTINUATION_DIAGNOSTIC_H
#include <stddef.h>
#include <stdint.h>
#ifdef RECOMP_ABI_CHECK
#error Diagnostic continuation replay has not qualified donor native-call ABI probes
#endif

#define PILOT_CONT_MAX_FRAMES 32u
#define PILOT_CONT_VERSION 1u
#define PILOT_CONT_WIRE_BOUND (48u + PILOT_CONT_MAX_FRAMES * 44u)

typedef struct PilotContFrame {
    uint32_t function, token, callee;
    uint32_t fa, fb;
    int32_t fas, fbs, flags;
    uint32_t ebp, icall_esp, icall_target;
} PilotContFrame;
typedef struct PilotContSite {
    uint32_t function, token, guest_address, is_call, direct_target;
} PilotContSite;
typedef struct PilotContSchema {
    uint8_t identity[32];
    const PilotContSite *sites;
    size_t site_count;
} PilotContSchema;

/* Single guest execution context; no concurrent callers. Bind before entry. */
void pilot_cont_bind(const PilotContSchema *schema);
PilotContFrame *pilot_cont_enter(uint32_t function);
void pilot_cont_leave(void);
void pilot_cont_validate_call(const PilotContFrame *frame);
void pilot_cont_at_label(PilotContFrame *frame,uint32_t token,uint32_t guest_address);
unsigned pilot_cont_depth(void);
/* Called only at generated labels, outside host services. Harness owns policy. */
void pilot_cont_checkpoint(uint32_t function, uint32_t guest_address);
/* Fault hook must terminate execution; continuing cannot preserve this ABI. */
_Noreturn void pilot_cont_fault(const char *reason);

/* Save only during checkpoint callback. Load only with no active C frames.
 * Load checks the complete wire/schema/chain before changing any execution
 * records. The caller must stage/validate its CPU, RAM and service state too. */
size_t pilot_cont_save(void *output, size_t capacity);
int pilot_cont_load(const void *input, size_t size);
#endif
