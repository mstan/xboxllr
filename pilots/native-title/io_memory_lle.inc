#include "io_vm_publication_lle.inc"
/* Original virtual-memory service transport. The captured kernel owns VADs,
 * PTEs/PFNs, allocation, zeroing, protection, queries and release. Native CPU
 * pages expose those same bytes; this is not a second allocator implementation.
 * Caller serializes the guest CPU and runtime ownership ledger. */
const char *pilot_io_device_vm(unsigned ordinal,unsigned char *guest,
    const uint32_t inputs[5],uint32_t outputs[7],uint32_t *status)
{
    AcquireSRWLockExclusive(&lock);
    if(!guest_memory)initialize(guest);
    if(!failure && (guest_memory!=guest || execution_depth))failure="memory LLE invalid caller context";
    if(!failure && ordinal!=184 && ordinal!=199 && ordinal!=204 && ordinal!=217)
        failure="memory LLE unsupported VM ordinal";
    if(failure)goto done;
    ++execution_depth;
    const uint32_t slots=SCRATCH+0x2000;
    uint32_t words[]={inputs[0],inputs[1],0xdeadbeef},args[5],count;
    uc_err error=uc_mem_write(cpu,slots,words,sizeof(words));
    if(error){failure=uc_strerror(error);goto finish;}
    if(ordinal==184) {
        args[0]=slots;args[1]=inputs[4];args[2]=slots+4;args[3]=inputs[2];args[4]=inputs[3];count=5;
    } else if(ordinal==199) {
        args[0]=slots;args[1]=slots+4;args[2]=inputs[2];count=3;
    } else if(ordinal==204) {
        args[0]=slots;args[1]=slots+4;args[2]=inputs[3];args[3]=slots+8;count=4;
    } else {args[0]=inputs[0];args[1]=slots+32;count=2;}
    (void)count; /* execute owns the independently checked stdcall arity. */
    struct VmCallerAccess access={0};
    vm_caller_prepare(ordinal,args,&access);
    if(failure)goto restore;
    execute(ordinal,args,status);
    if(!failure && *status)fprintf(stderr,"[MEMORY LLE RESULT] ordinal=%u base=%08x bytes=%u type=%x status=%08x\n",
        ordinal,inputs[0],inputs[1],inputs[2],*status);
    if(!failure) {
        error=uc_mem_read(cpu,ordinal==217?slots+32:slots,outputs,ordinal==217?28:12);
        if(error)failure=uc_strerror(error);
    }
    if(!failure && *status==0xc0000017u) {
        /* Record real original counters on allocation failure, after saving
         * caller outputs. This diagnostic cannot change the returned status. */
        uint32_t statistics[9]={36},address=slots+64,queried=0;
        error=uc_mem_write(cpu,address,statistics,sizeof(statistics));
        if(error)failure=uc_strerror(error);
        else execute(181,&address,&queried);
        if(!failure && !queried) {
            error=uc_mem_read(cpu,address,statistics,sizeof(statistics));
            if(error)failure=uc_strerror(error);
            else fprintf(stderr,"[MEMORY LLE EXHAUSTED] available=%u committed=%u reserved=%u cache=%u pool=%u stack=%u image=%u published=%u\n",
                statistics[2],statistics[3],statistics[4],statistics[5],statistics[6],statistics[7],statistics[8],published_count);
        }
    }
restore:
    vm_caller_complete(ordinal,args,failure?0:*status,&access);
finish:
    --execution_depth;
done:
    const char *diagnostic=failure;ReleaseSRWLockExclusive(&lock);return diagnostic;
}
