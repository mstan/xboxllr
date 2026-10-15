/* Optional API observations share the existing block hook. No new instruction
 * hook, block splitting, CPU admission or guest-clock opportunity is added. */
static struct {
    PilotIoCallDiagnostic receiver;
    PilotIoDiagnosticCall calls[32];
    PilotIoCallDiagnosticEvent frames[128];
    unsigned count;
    uint64_t sequence;
    unsigned short targets[1024];
} original_call_diagnostic;
static unsigned diagnostic_call_bucket(uint32_t pc){return ((pc>>2)^(pc>>12))&1023;}
static const char *diagnostic_call_emit(const PilotIoCallDiagnosticEvent *event)
{
    original_diagnostic_borrow=1;device_bus_borrow=1;device_bus_dma_lease=0;
    const char *error=original_call_diagnostic.receiver(event);
    device_bus_borrow=0;device_bus_dma_lease=0;original_diagnostic_borrow=0;
    return error;
}
static void diagnostic_calls_block(uc_engine *engine,uint32_t address)
{
    if(!original_call_diagnostic.targets[diagnostic_call_bucket(address)])return;
    PilotIoDiagnosticEvent context;
    if(!diagnostic_context(engine,&context)) {forbidden(engine,"call observation context unavailable");return;}
    struct OriginalThread *thread=thread_object(context.thread);
    if(!thread || !thread->in_cpu)return;
    for(unsigned i=0;i<128;++i) {
        PilotIoCallDiagnosticEvent *frame=original_call_diagnostic.frames+i;
        if(!frame->id || frame->thread!=context.thread || frame->caller!=address ||
           (uint64_t)frame->entry_sp+4+original_call_diagnostic.calls[frame->index].stack_bytes!=context.regs[4])continue;
        PilotIoCallDiagnosticEvent event=*frame;event.kind=PILOT_IO_DIAGNOSTIC_RETURN;
        memcpy(event.regs,context.regs,sizeof(event.regs));
        --original_call_diagnostic.targets[diagnostic_call_bucket(address)];frame->id=0;
        const char *error=diagnostic_call_emit(&event);
        if(error){forbidden(engine,error);return;}
        break;
    }
    for(unsigned i=0;i<original_call_diagnostic.count;++i) {
        const PilotIoDiagnosticCall *call=original_call_diagnostic.calls+i;
        if(call->entry!=address)continue;
        unsigned free_slot=128;
        for(unsigned slot=0;slot<128;++slot) {
            const PilotIoCallDiagnosticEvent *frame=original_call_diagnostic.frames+slot;
            // A stopped block can re-enter before its first instruction runs.
            if(frame->id && frame->index==i && frame->thread==context.thread && frame->entry_sp==context.regs[4])return;
            if(!frame->id && free_slot==128)free_slot=slot;
        }
        if(free_slot==128){forbidden(engine,"call observation active-frame bound exhausted");return;}
        if(context.regs[4]<thread->stack ||
           (uint64_t)context.regs[4]+4+call->stack_bytes>(uint64_t)thread->stack+thread->stack_bytes) {
            forbidden(engine,"call observation arguments outside original stack");return;
        }
        uint32_t stack[9]={0};original_diagnostic_borrow=1;
        int readable=pilot_io_device_diagnostic_read(context.regs[4],stack,4+call->stack_bytes,0);
        original_diagnostic_borrow=0;
        if(!readable){forbidden(engine,"call observation stack unavailable");return;}
        PilotIoCallDiagnosticEvent *frame=original_call_diagnostic.frames+free_slot;
        memset(frame,0,sizeof(*frame));frame->id=++original_call_diagnostic.sequence;
        frame->index=i;frame->kind=PILOT_IO_DIAGNOSTIC_ENTRY;frame->thread=context.thread;
        memcpy(frame->regs,context.regs,sizeof(frame->regs));memcpy(frame->args,stack+1,call->stack_bytes);
        frame->caller=stack[0];frame->entry_sp=context.regs[4];
        ++original_call_diagnostic.targets[diagnostic_call_bucket(frame->caller)];
        const char *error=diagnostic_call_emit(frame);if(error)forbidden(engine,error);
        return;
    }
}
const char *pilot_io_device_diagnostic_calls_bind(const PilotIoDiagnosticCall *calls,
    unsigned count,PilotIoCallDiagnostic receiver)
{
    if(device_bus_borrow || original_diagnostic_borrow)return "call diagnostic bind during borrowed owner";
    AcquireSRWLockExclusive(&lock);const char *error=NULL;
    if(!cpu || execution_depth)error="call diagnostic binding requires idle original CPU";
    else if(!receiver)memset(&original_call_diagnostic,0,sizeof(original_call_diagnostic));
    else if(!calls || !count || count>32 || original_call_diagnostic.receiver)
        error="call diagnostic binding requires a fresh bounded table";
    else {
        for(unsigned i=0;!error && i<count;++i) {
            const PilotIoDiagnosticCall *call=calls+i;
            if(!call->sha256 || strlen(call->sha256)!=64 || !call->bytes || call->bytes>16384 ||
               call->entry<0x10000 || (uint64_t)call->entry+call->bytes>0x80000000ull ||
               call->stack_bytes>32 || call->stack_bytes%4) {error="call diagnostic source boundary invalid";break;}
            for(unsigned j=0;j<i;++j)if(calls[j].entry==call->entry)error="duplicate call diagnostic entry";
            for(uint64_t page=call->entry&~4095u;!error && page<(uint64_t)call->entry+call->bytes;page+=4096)
                if(!guest_page((uint32_t)page))error="call diagnostic source is not resident";
            unsigned char digest[32];char actual[65];
            if(!error && BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,guest_memory+call->entry,call->bytes,digest,32)<0)
                error="call diagnostic source hash failed";
            if(!error) {
                for(unsigned j=0;j<32;++j)sprintf(actual+j*2,"%02x",digest[j]);
                if(strcmp(actual,call->sha256))error="call diagnostic source differs from reviewed bytes";
            }
        }
        if(!error) {
            memcpy(original_call_diagnostic.calls,calls,count*sizeof(*calls));
            original_call_diagnostic.count=count;original_call_diagnostic.receiver=receiver;
            for(unsigned i=0;i<count;++i)++original_call_diagnostic.targets[diagnostic_call_bucket(calls[i].entry)];
        }
    }
    ReleaseSRWLockExclusive(&lock);return error;
}
