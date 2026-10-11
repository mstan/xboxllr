/* A wait-only phase of one passive original component. The caller must have
 * released every native host lock before a slice; GPU/device work and the
 * original dispatcher remain the only completion/readiness producers. Use a
 * fresh local context, reuse slices, then close before returning. Successful
 * close releases its manual stack lease, permitting a later wait/VM phase in
 * this same component. An outstanding VM workspace cannot be co-leased. */
struct ComponentWait {uint32_t frame,event,thread;};
enum { COMPONENT_WAIT_BYTES=32, COMPONENT_WAIT_MAX_100NS=10000000 };
static const char *component_wait_context(void)
{
    if(!pilot_io_device_thread_component_active())return "component timed wait outside original component";
    if(dispatch_in_dpc || pilot_io_device_irql())return "component timed wait outside passive callback-free context";
    return NULL;
}
static const char *component_wait_owned(const struct ComponentWait *wait)
{
    const char *error=component_wait_context();if(error)return error;
    if(!wait || !wait->event || !wait->frame || wait->thread!=MEM32(0x8003da64u))
        return "component timed wait outside its original event/thread lifetime";
    uint32_t bottom=MEM32(wait->thread+0x20),top=MEM32(wait->thread+0x1c);
    if(wait->frame<bottom || (uint64_t)wait->frame+COMPONENT_WAIT_BYTES>top ||
       MEM32(wait->frame)!=wait->event || MEM32(wait->frame+4)!=0x13579bdf ||
       MEM32(wait->frame+24)!=0x2468ace0 || MEM32(wait->frame+28)!=0x89abcdef)
        return "component timed wait lost original stack ownership";
    return NULL;
}
static const char *component_wait_query(struct ComponentWait *wait)
{
    const char *error=component_wait_owned(wait);if(error)return error;
    uint32_t args[]={wait->event,wait->frame+16},result=0xbad0cafe;
    error=pilot_io_device_thread_component_call(209,args,&result);if(error)return error;
    if(result || MEM32(wait->frame+16)!=1 || MEM32(wait->frame+20))
        return "component timed wait private synchronization event changed";
    return component_wait_owned(wait);
}
static const char *component_wait_open(struct ComponentWait *wait)
{
    const char *error=component_wait_context();if(error)return error;
    if(!wait || wait->frame || wait->event || wait->thread)
        return "component timed wait requires a fresh local owner";
    if(!pilot_io_device_clock_bound())return "component timed wait requires the bound original clock owner";
    uint64_t ticks=0,next=0;
    error=pilot_usb_pit_clock(&ticks,&next);if(error)return error;
    if(!next)return "component timed wait requires the programmed original clock";
    uint32_t frame=0;
    error=pilot_io_device_thread_component_workspace(COMPONENT_WAIT_BYTES,&frame);if(error)return error;
    wait->frame=frame;wait->thread=MEM32(0x8003da64u);
    memset(memory+frame,0,COMPONENT_WAIT_BYTES);
    MEM32(frame+4)=0x13579bdf;MEM32(frame+24)=0x2468ace0;MEM32(frame+28)=0x89abcdef;
    uint32_t create[]={frame,0,1,0},result=0xbad0cafe;
    error=pilot_io_device_thread_component_call(189,create,&result);if(error)return error;
    wait->event=MEM32(frame);
    if(result || !wait->event)return "component timed wait original event creation failed";
    return component_wait_query(wait);
}
static const char *component_wait_slice(struct ComponentWait *wait,uint32_t duration_100ns,uint32_t *status)
{
    if(!status || !duration_100ns || duration_100ns>COMPONENT_WAIT_MAX_100NS)
        return "component timed wait requires a finite positive relative duration";
    const char *error=component_wait_query(wait);if(error)return error;
    MEM64(wait->frame+8)=(uint64_t)-(int64_t)duration_100ns;
    uint32_t args[]={wait->event,0,wait->frame+8},result=0xbad0cafe;
    error=pilot_io_device_thread_component_call(233,args,&result);if(error)return error;
    *status=result; /* Preserve the actual original result, including errors. */
    if(result!=0x102u)return "component timed wait did not return original STATUS_TIMEOUT";
    return component_wait_query(wait);
}
static const char *component_wait_close(struct ComponentWait *wait)
{
    const char *error=component_wait_query(wait);if(error)return error;
    uint32_t result=0xbad0cafe;
    error=pilot_io_device_thread_component_call(187,&wait->event,&result);if(error)return error;
    if(result)return "component timed wait original event close failed";
    error=component_wait_owned(wait);if(error)return error;
    memset(memory+wait->frame,0,COMPONENT_WAIT_BYTES);
    error=pilot_io_device_thread_component_workspace_release(wait->frame,COMPONENT_WAIT_BYTES);if(error)return error;
    memset(wait,0,sizeof(*wait));return NULL;
}
