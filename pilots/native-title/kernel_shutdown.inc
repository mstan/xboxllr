/* Native registration owner only. Callback delivery and firmware transitions
 * retain a separate explicit guard; no physical power operation occurs here. */
#include "kernel_shutdown.h"
static SRWLOCK kernel_shutdown_lock=SRWLOCK_INIT;
static PilotShutdownRegistration kernel_shutdown_entries[PILOT_SHUTDOWN_LIMIT];
static unsigned kernel_shutdown_count;
static uint32_t kernel_shutdown_head;

static int kernel_shutdown_ranges_overlap(uint32_t left,uint32_t left_bytes,uint32_t right,uint32_t right_bytes)
{
    return (uint64_t)left<(uint64_t)right+right_bytes && (uint64_t)right<(uint64_t)left+left_bytes;
}
static void kernel_shutdown_validate(void)
{
    if(!kernel_shutdown_head)stop("native shutdown registration owner uninitialized",47);
    uint32_t first=kernel_shutdown_count?kernel_shutdown_entries[0].address+8:kernel_shutdown_head;
    uint32_t last=kernel_shutdown_count?kernel_shutdown_entries[kernel_shutdown_count-1].address+8:kernel_shutdown_head;
    if(MEM32(kernel_shutdown_head)!=first || MEM32(kernel_shutdown_head+4)!=last)
        stop("native shutdown registration head modified",47);
    for(unsigned i=0;i<kernel_shutdown_count;++i) {
        PilotShutdownRegistration *entry=&kernel_shutdown_entries[i];
        uint32_t next=i+1<kernel_shutdown_count?kernel_shutdown_entries[i+1].address+8:kernel_shutdown_head;
        uint32_t previous=i?kernel_shutdown_entries[i-1].address+8:kernel_shutdown_head;
        uint32_t priority;memcpy(&priority,&entry->priority,4);
        if(MEM32(entry->address)!=entry->routine || MEM32(entry->address+4)!=priority ||
           MEM32(entry->address+8)!=next || MEM32(entry->address+12)!=previous)
            stop("native shutdown registration modified while owned",entry->address);
    }
}
static void kernel_shutdown_initialize(uint32_t head)
{
    if(!head || (head&3u) || head>UINT32_MAX-7)
        stop("invalid native shutdown list backing",head);
    AcquireSRWLockExclusive(&kernel_shutdown_lock);
    if(kernel_shutdown_count || (kernel_shutdown_head && kernel_shutdown_head!=head))
        stop("reinitializing active native shutdown owner",47);
    if(kernel_shutdown_head)kernel_shutdown_validate();
    kernel_shutdown_head=head;MEM32(head)=head;MEM32(head+4)=head;
    ReleaseSRWLockExclusive(&kernel_shutdown_lock);
}
/* Integration may use this before freeing/unmapping guest storage. The caller
 * retains its own allocator lock ordering; no guest callback runs here. */
static int kernel_shutdown_owns_range(uint32_t address,uint32_t bytes)
{
    if(!bytes)return 0;
    int overlaps=0;AcquireSRWLockShared(&kernel_shutdown_lock);
    if(kernel_shutdown_head && kernel_shutdown_ranges_overlap(address,bytes,kernel_shutdown_head,8))overlaps=1;
    for(unsigned i=0;i<kernel_shutdown_count && !overlaps;++i)
        overlaps=kernel_shutdown_ranges_overlap(address,bytes,kernel_shutdown_entries[i].address,16);
    ReleaseSRWLockShared(&kernel_shutdown_lock);return overlaps;
}
static int kernel_shutdown_dispatch(unsigned ordinal)
{
    if(ordinal!=47)return 0;
    if((uint64_t)g_esp+12>UINT64_C(0x100000000))stop("invalid shutdown registration frame",g_esp);
    uint32_t registration=MEM32(g_esp+4);
    uint8_t registering=MEM8(g_esp+8); /* BOOLEAN observes only its low byte. */
    AcquireSRWLockExclusive(&kernel_shutdown_lock);kernel_shutdown_validate();
    unsigned index;
    for(index=0;index<kernel_shutdown_count;++index)
        if(kernel_shutdown_entries[index].address==registration)break;
    if(!registering) {
        if(index<kernel_shutdown_count) {
            uint32_t next=MEM32(registration+8),previous=MEM32(registration+12);
            MEM32(previous)=next;MEM32(next+4)=previous;
            /* Original RemoveEntryList leaves the removed node's links alone. */
            memmove(kernel_shutdown_entries+index,kernel_shutdown_entries+index+1,
                (kernel_shutdown_count-index-1)*sizeof(kernel_shutdown_entries[0]));
            --kernel_shutdown_count;
        }
    } else {
        if(index<kernel_shutdown_count)stop("duplicate native shutdown registration",registration);
        if(!registration || (registration&3u) || registration>UINT32_MAX-15)
            stop("invalid native shutdown registration",registration);
        if(kernel_shutdown_count==PILOT_SHUTDOWN_LIMIT)stop("native shutdown registration capacity exhausted",47);
        if(kernel_shutdown_ranges_overlap(registration,16,kernel_shutdown_head,8))
            stop("shutdown registration overlaps owner head",registration);
        for(unsigned i=0;i<kernel_shutdown_count;++i)
            if(kernel_shutdown_ranges_overlap(registration,16,kernel_shutdown_entries[i].address,16))
                stop("shutdown registration overlaps active object",registration);
        uint32_t routine=MEM32(registration);int32_t priority;
        memcpy(&priority,memory+registration+4,4);
        if(!routine || !recomp_lookup(routine))stop("shutdown callback is not generated",routine);
        /* Signed descending priority; equality passes existing entries so
         * registrations with equal priority retain FIFO order. */
        unsigned position=0;
        while(position<kernel_shutdown_count && kernel_shutdown_entries[position].priority>=priority)++position;
        uint32_t previous=position?kernel_shutdown_entries[position-1].address+8:kernel_shutdown_head;
        uint32_t next=position<kernel_shutdown_count?kernel_shutdown_entries[position].address+8:kernel_shutdown_head;
        memmove(kernel_shutdown_entries+position+1,kernel_shutdown_entries+position,
            (kernel_shutdown_count-position)*sizeof(kernel_shutdown_entries[0]));
        kernel_shutdown_entries[position].address=registration;
        kernel_shutdown_entries[position].routine=routine;
        kernel_shutdown_entries[position].priority=priority;++kernel_shutdown_count;
        MEM32(registration+8)=next;MEM32(registration+12)=previous;
        MEM32(previous)=registration+8;MEM32(next+4)=registration+8;
    }
    ReleaseSRWLockExclusive(&kernel_shutdown_lock);
    g_esp+=12;return 1; /* VOID/NTAPI: retain EAX and all other registers. */
}
static void kernel_shutdown_delivery_guard(void)
{
    AcquireSRWLockShared(&kernel_shutdown_lock);
    unsigned pending=kernel_shutdown_count;
    ReleaseSRWLockShared(&kernel_shutdown_lock);
    stop("native shutdown callback delivery and firmware transition unqualified",pending);
}
