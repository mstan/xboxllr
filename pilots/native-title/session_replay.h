/* Native session record/replay transport. The runtime, not this transport,
 * owns guest scheduling and the boundaries at which devices publish state.
 * Files contain no host pointers, handles, native stacks or executable code. */
#ifndef PILOT_SESSION_REPLAY_H
#define PILOT_SESSION_REPLAY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { PILOT_SESSION_OFF, PILOT_SESSION_RECORD, PILOT_SESSION_REPLAY };
#define PILOT_SESSION_ACTOR_NONE UINT32_C(0xffffffff)
enum PilotSessionBoundary {
    PILOT_SESSION_BOOT = 1, PILOT_SESSION_PREEMPT, PILOT_SESSION_BLOCK,
    PILOT_SESSION_RETIRE, PILOT_SESSION_DEVICE, PILOT_SESSION_TERMINAL
};
enum PilotSessionValue {
    PILOT_SESSION_MONOTONIC = 1, PILOT_SESSION_WALL, PILOT_SESSION_THREAD_ID,
    PILOT_SESSION_ENTROPY, PILOT_SESSION_INPUT, PILOT_SESSION_CAPABILITIES,
    PILOT_SESSION_RUMBLE, PILOT_SESSION_WAIT, PILOT_SESSION_GPU_BUSY,
    PILOT_SESSION_AUDIO_STATE, PILOT_SESSION_WINDOW, PILOT_SESSION_FRAME,
    PILOT_SESSION_FILE_RESULT, PILOT_SESSION_SERVICE, PILOT_SESSION_FAULT,
    PILOT_SESSION_AUDIO_MIX, PILOT_SESSION_AUDIO_OUTPUT
};
typedef struct PilotSessionTurn {
    uint32_t actor, boundary;
    uint64_t checkpoint, digest;
} PilotSessionTurn;

/* Call once, before creating simulation workers. The new record directory
 * must exist and be empty. Identity is SHA-256 of the actual executable.
 * A replay mismatch terminates with an explicit divergence (exit 3); capture
 * failures stop recording and report an incomplete session without stopping
 * gameplay. No export may throw across the C ABI. */
int pilot_session_open(const char *record, const char *replay, const uint8_t identity[32]);
int pilot_session_mode(void);
int pilot_session_healthy(void);
void pilot_session_actor(uint32_t actor);
uint32_t pilot_session_actor_id(void);
/* Replace an external observation during replay; compare simulation evidence
 * with verify instead. Both record the exact tag, argument, size and sequence. */
void pilot_session_value(uint32_t tag, uint64_t argument, void *value, size_t bytes);
void pilot_session_verify(uint32_t tag, uint64_t argument, const void *value, size_t bytes);
uint64_t pilot_session_value64(uint32_t tag, uint64_t argument, uint64_t value);
/* A turn is one uninterrupted owner of the guest CPU or a device publication.
 * Replay's next turn is visible before admission so the scheduler can wait for
 * the recorded owner without holding another owner's state locks. */
int pilot_session_next_turn(PilotSessionTurn *turn);
void pilot_session_turn(const PilotSessionTurn *turn);
/* Monotonic completed-turn generation for progress-based stall detection. */
uint64_t pilot_session_progress(void);
/* Final drains are for an orderly boundary only, never an SEH/VEH observer. */
int pilot_session_finish(void);
void pilot_session_incomplete(const char *reason);
void pilot_session_diverge(const char *reason, uint64_t sequence);
void pilot_session_describe_activity(void);
typedef int (*PilotSessionDeviceEnter)(uint32_t actor);
typedef void (*PilotSessionDeviceLeave)(int token);
void pilot_session_bind_devices(PilotSessionDeviceEnter enter, PilotSessionDeviceLeave leave);
void pilot_session_bind_device_try(PilotSessionDeviceEnter try_enter);
int pilot_session_device_enter(uint32_t actor);
/* Physical presentation callbacks must not wait for a future simulation turn:
 * engine teardown can be waiting for that same callback to return. */
int pilot_session_device_try_enter(uint32_t actor);
void pilot_session_device_leave(int token);
typedef int (*PilotSessionHostWaitBegin)(void);
typedef void (*PilotSessionHostWaitEnd)(int token);
void pilot_session_bind_host_wait(PilotSessionHostWaitBegin begin, PilotSessionHostWaitEnd end);
/* Drop CPU/device ownership around a genuine host wait. Nesting is inert.
 * End restores the same logical owner using the normal recorded admission. */
int pilot_session_host_wait_begin(void);
void pilot_session_host_wait_end(int token);
typedef void (*PilotSessionTerminal)(const char *reason,uint32_t code);
void pilot_session_bind_terminal(PilotSessionTerminal terminal);
void pilot_session_report_stop(const char *reason,uint32_t code);
/* Logical owner-lock IDs are source-authored constants, never host addresses.
 * Wait before acquiring the native lock; publish after acquiring it. This
 * retains lock ordering for blocking services that run outside a CPU turn. */
void pilot_session_lock_enter(uint32_t lock_id);
void pilot_session_lock_acquired(uint32_t lock_id);
/* Establish the intentional process-exit cut after obtaining the CPU permit.
 * Nonterminal recorders park instead of adding post-exit observations. */
void pilot_session_terminal_begin(void);
#ifdef __cplusplus
}
#endif
#endif
