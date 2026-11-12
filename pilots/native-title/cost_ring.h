#ifndef PILOT_COST_RING_H
#define PILOT_COST_RING_H
#include <stdint.h>
/* One source-swap ring. Hot counters are thread-local; querying never touches
 * guest memory, renderer state or the title thread's accumulator. */
#ifndef PILOT_COST_RING
#define PILOT_COST_RING 1
#endif
#if defined(_MSC_VER)
#define PILOT_COST_TLS __declspec(thread)
#else
#define PILOT_COST_TLS __thread
#endif
#ifdef __cplusplus
extern "C" {
#endif
enum PilotCostRole { COST_TITLE,COST_XAUDIO,COST_DISPATCHER,COST_INTERPOLATION,COST_FEEDER,COST_ROLES };
enum PilotCostWait { COST_PACING,COST_POLL,COST_SLEEP,COST_PRESENT_RETIRE,COST_COMPLETION,
    COST_FLUSH,COST_MAP_READ,COST_GRAPHICS_LOCK,COST_AUDIO_LOCK,COST_CALLBACK,COST_I3DL2,COST_WAITS };
enum PilotCostWork { COST_D3D,COST_OTHER,COST_COMPANION,COST_REPLAY,COST_WORKS };
enum PilotCostCounter { COST_GUEST_DRAWS,COST_HOST_DRAWS,COST_COMPANION_DRAWS,COST_PACKETS,
    COST_GROUP_EXHAUSTION,COST_VERTEX_BYTES,COST_TEXTURE_BYTES,COST_VISIBILITY,COST_READBACK,
    COST_TARGET_LOCK,COST_CRITICAL_ENTER,COST_KERNEL_CALL,COST_I3DL2_COMMIT,COST_COUNTERS };
enum PilotCostEpoch { COST_EPOCH_PRESENT,COST_EPOCH_CLEAR,COST_EPOCH_TARGET,COST_EPOCH_UPLOAD,
    COST_EPOCH_READBACK,COST_EPOCH_COPY,COST_EPOCH_VISIBILITY,COST_EPOCH_CAPACITY,
    COST_EPOCH_ADMISSION,COST_EPOCH_OTHER,COST_EPOCH_REASONS };
struct PilotCostTotals {
    uint64_t waits[COST_WAITS],wait_cycles[COST_WAITS],work[COST_WORKS],samples[COST_WORKS];
    uint64_t counters[COST_COUNTERS],epochs[COST_EPOCH_REASONS];
    uint64_t strip_capacity,strip_boundary;
};
struct PilotCostLocal {struct PilotCostTotals total;uint64_t probe_cycles;unsigned random,service_depth,wait_depth,companion_depth;};
struct PilotCostSpan {uint64_t wall,cycles;unsigned outer;};
#if PILOT_COST_RING
extern PILOT_COST_TLS struct PilotCostLocal pilot_cost_local;
uint64_t pilot_cost_now(void);
uint64_t pilot_cost_cycles(void);
void pilot_cost_register(unsigned role);
void pilot_cost_swap(uint64_t source_swap,uint64_t write_faults);
static inline void pilot_cost_add(unsigned counter,uint64_t amount)
{pilot_cost_local.total.counters[counter]+=amount;}
static inline void pilot_cost_epoch(unsigned reason)
{++pilot_cost_local.total.epochs[reason];}
static inline void pilot_cost_strip(int capacity)
{if(capacity)++pilot_cost_local.total.strip_capacity;else ++pilot_cost_local.total.strip_boundary;}
static inline int pilot_cost_sample(void)
{
    unsigned r=pilot_cost_local.random;if(!r)r=1;
    r^=r<<13;r^=r>>17;r^=r<<5;pilot_cost_local.random=r;return !(r&63);
}
static inline uint64_t pilot_cost_work_begin(unsigned kind)
{(void)kind;return pilot_cost_sample()?pilot_cost_cycles():0;}
static inline void pilot_cost_work_end(unsigned kind,uint64_t begin)
{if(begin){uint64_t elapsed=pilot_cost_cycles()-begin,probe=pilot_cost_local.probe_cycles;
    if(elapsed>probe)pilot_cost_local.total.work[kind]+=64*(elapsed-probe);
    ++pilot_cost_local.total.samples[kind];}}
static inline uint64_t pilot_cost_service_begin(unsigned kind)
{return pilot_cost_local.service_depth++?0:pilot_cost_work_begin(kind);}
static inline void pilot_cost_service_end(unsigned kind,uint64_t begin)
{pilot_cost_work_end(kind,begin);--pilot_cost_local.service_depth;}
static inline uint64_t pilot_cost_companion_begin(void)
{++pilot_cost_local.companion_depth;return pilot_cost_work_begin(COST_COMPANION);}
static inline void pilot_cost_companion_end(uint64_t begin)
{pilot_cost_work_end(COST_COMPANION,begin);--pilot_cost_local.companion_depth;}
static inline uint64_t pilot_cost_packet_begin(void)
{return pilot_cost_local.companion_depth?0:pilot_cost_work_begin(COST_REPLAY);}
static inline struct PilotCostSpan pilot_cost_wait_begin(void)
{
    struct PilotCostSpan s={0,0,0};
    s.outer=pilot_cost_local.wait_depth++==0;
    if(s.outer){s.cycles=pilot_cost_cycles();s.wall=pilot_cost_now();}return s;
}
static inline void pilot_cost_wait_end_weight(unsigned kind,struct PilotCostSpan s,unsigned weight)
{
    if(s.outer){uint64_t w=pilot_cost_now(),c=pilot_cost_cycles();
        pilot_cost_local.total.waits[kind]+=weight*(w-s.wall);
        pilot_cost_local.total.wait_cycles[kind]+=weight*(c-s.cycles);}
    --pilot_cost_local.wait_depth;
}
static inline void pilot_cost_wait_end(unsigned kind,struct PilotCostSpan s)
{pilot_cost_wait_end_weight(kind,s,1);}
#else
static inline uint64_t pilot_cost_now(void){return 0;}
static inline uint64_t pilot_cost_cycles(void){return 0;}
static inline void pilot_cost_register(unsigned role){(void)role;}
static inline void pilot_cost_swap(uint64_t s,uint64_t f){(void)s;(void)f;}
static inline void pilot_cost_add(unsigned c,uint64_t a){(void)c;(void)a;}
static inline void pilot_cost_epoch(unsigned r){(void)r;}
static inline void pilot_cost_strip(int c){(void)c;}
static inline int pilot_cost_sample(void){return 0;}
static inline uint64_t pilot_cost_work_begin(unsigned k){(void)k;return 0;}
static inline void pilot_cost_work_end(unsigned k,uint64_t b){(void)k;(void)b;}
static inline uint64_t pilot_cost_service_begin(unsigned k){(void)k;return 0;}
static inline void pilot_cost_service_end(unsigned k,uint64_t b){(void)k;(void)b;}
static inline uint64_t pilot_cost_companion_begin(void){return 0;}
static inline void pilot_cost_companion_end(uint64_t b){(void)b;}
static inline uint64_t pilot_cost_packet_begin(void){return 0;}
static inline struct PilotCostSpan pilot_cost_wait_begin(void){struct PilotCostSpan s={0,0,0};return s;}
static inline void pilot_cost_wait_end(unsigned k,struct PilotCostSpan s){(void)k;(void)s;}
static inline void pilot_cost_wait_end_weight(unsigned k,struct PilotCostSpan s,unsigned w){(void)k;(void)s;(void)w;}
#endif
#ifdef __cplusplus
}
#include <string>
std::string pilot_cost_query(const std::string& args);
#endif
#endif
