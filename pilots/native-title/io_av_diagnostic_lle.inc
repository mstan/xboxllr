/* Explicitly opt-in, bounded, read-only observations at EXISTING raw blocks.
 * No instruction hook, block split, device read, admission or clock advance.
 * Missing observations and truncation are diagnostic gaps, never readiness. */
static struct {
    int selected;
    unsigned events,sequence;
    struct {
        uint32_t id,thread,ordinal,sp,caller,args[6],seen[128];
        unsigned seen_count,block_gap;
    } frames[32];
} av_diagnostic;

static int av_diagnostic_word(uint32_t va,uint32_t *value)
{
    uint32_t pa;
    return diagnostic_translation(va,&pa) && diagnostic_ram_read(pa,value,4);
}
static void av_diagnostic_gap(const char *reason,uint32_t id)
{
    if(av_diagnostic.events<4096) {
        ++av_diagnostic.events;
        fprintf(stderr,"[AV OBS] gap=%s id=%u\n",reason,id);
    } else if(av_diagnostic.events==4096) {
        ++av_diagnostic.events;
        fprintf(stderr,"[AV OBS] truncated=1 capacity=4096\n");
    }
}
static void av_diagnostic_emit(uc_engine *engine,const char *kind,uint32_t pc,
    uint32_t id,uint32_t ordinal,uint32_t caller,const uint32_t *args)
{
    if(av_diagnostic.events>=4096) {
        if(av_diagnostic.events++==4096)fprintf(stderr,"[AV OBS] truncated=1 capacity=4096\n");
        return;
    }
    ++av_diagnostic.events;
    uint32_t thread=0,encoder=0,mode=0,regs[8]={0};
    unsigned known=8u|(av_diagnostic_word(0x8003da64,&thread)?1u:0u);
    if(av_diagnostic_word(0x8004ff8c,&encoder))known|=2;
    if(av_diagnostic_word(0x8004ff90,&mode))known|=4;
    for(unsigned i=0;i<8;++i)if(uc_reg_read(engine,general_regs[i],regs+i)!=UC_ERR_OK)known&=~8u;
    fprintf(stderr,"[AV OBS] kind=%s seq=%u id=%u ordinal=%u pc=%08x thread=%08x known=%x encoder_word=%08x mode=%08x caller=%08x eax=%08x ebx=%08x esi=%08x edi=%08x esp=%08x ebp=%08x args=%08x,%08x,%08x,%08x,%08x,%08x\n",
        kind,av_diagnostic.events,id,ordinal,pc,thread,known,encoder,mode,caller,
        regs[0],regs[3],regs[6],regs[7],regs[4],regs[5],args[0],args[1],args[2],args[3],args[4],args[5]);
}
static void av_diagnostic_block(uc_engine *engine,uint32_t pc)
{
    if(!av_diagnostic.selected) {
        const char *value=getenv("XLLE_PILOT_AV_DIAGNOSTIC");
        av_diagnostic.selected=value && !strcmp(value,"1")?1:-1;
    }
    if(av_diagnostic.selected<0 || av_diagnostic.events>4096)return;
    const unsigned ordinal=pc==0x80034667?2:pc==0x80034ba6?3:0;
    /* Fast reject: ordinary blocks need only compare active return targets. */
    int relevant=ordinal || (pc>=0x80034ba6 && pc<=0x8003510a);
    for(unsigned i=0;!relevant && i<32;++i)
        relevant=av_diagnostic.frames[i].id && av_diagnostic.frames[i].caller==pc;
    if(!relevant)return;
    uint32_t thread=0,sp=0;
    if(!av_diagnostic_word(0x8003da64,&thread) || uc_reg_read(engine,UC_X86_REG_ESP,&sp)!=UC_ERR_OK)return;
    for(unsigned i=0;i<32;++i) {
        if(!av_diagnostic.frames[i].id || av_diagnostic.frames[i].thread!=thread)continue;
        if(pc==av_diagnostic.frames[i].caller && sp==av_diagnostic.frames[i].sp+4+
            (av_diagnostic.frames[i].ordinal==3?24:16)) {
            av_diagnostic_emit(engine,"return",pc,av_diagnostic.frames[i].id,
                av_diagnostic.frames[i].ordinal,pc,av_diagnostic.frames[i].args);
            av_diagnostic.frames[i].id=0;
        }
    }
    if(ordinal) {
        unsigned slot=32;
        for(unsigned i=0;i<32;++i) {
            if(av_diagnostic.frames[i].id && av_diagnostic.frames[i].thread==thread &&
               av_diagnostic.frames[i].sp==sp && av_diagnostic.frames[i].ordinal==ordinal)return;
            if(!av_diagnostic.frames[i].id && slot==32)slot=i;
        }
        uint32_t stack[7]={0};
        /* Direct services use the existing native RAM stack mapping; original
         * thread services use page-walked RAM. Reuse the diagnostic API's plain
         * backing checks for both, never uc_mem_read/device callbacks. */
        const unsigned saved_borrow=original_diagnostic_borrow;
        original_diagnostic_borrow=1;
        const unsigned bytes=(ordinal==3?7:5)*4;
        int readable=pilot_io_device_diagnostic_read(sp,stack,bytes,0);
        original_diagnostic_borrow=saved_borrow;
        /* Service frames have a separately mapped, guarded backing and are
         * intentionally absent from generic diagnostic RAM translations. */
        if(!readable && execution_depth && execution_depth<=PILOT_IO_SERVICE_FRAMES) {
            const unsigned index=execution_depth-1;
            const uint64_t base=(uint64_t)PILOT_IO_SERVICE_BASE+index*PILOT_IO_SERVICE_STRIDE;
            if(service_stack_owners[index]==thread && sp>=base+4096 &&
               (uint64_t)sp+bytes<=base+PILOT_IO_SERVICE_STRIDE) {
                memcpy(stack,guest_memory+sp,bytes);readable=1;
            }
        }
        if(slot==32 || !readable) {
            av_diagnostic_gap(slot==32?"entry_capacity":"entry_stack",0);
            return;
        }
        memset(av_diagnostic.frames+slot,0,sizeof(av_diagnostic.frames[slot]));
        av_diagnostic.frames[slot].id=++av_diagnostic.sequence;
        av_diagnostic.frames[slot].thread=thread;av_diagnostic.frames[slot].sp=sp;
        av_diagnostic.frames[slot].ordinal=ordinal;av_diagnostic.frames[slot].caller=stack[0];
        memcpy(av_diagnostic.frames[slot].args,stack+1,(ordinal==3?6:4)*4);
        av_diagnostic_emit(engine,"entry",pc,av_diagnostic.frames[slot].id,ordinal,stack[0],av_diagnostic.frames[slot].args);
    }
    /* Each reached AV3 block is sampled once per invocation, avoiding loop
     * logging. The PC is the actual existing block start, NOT every instr PC. */
    if(pc>=0x80034ba6 && pc<=0x8003510a)for(unsigned i=0;i<32;++i) {
        if(!av_diagnostic.frames[i].id || av_diagnostic.frames[i].ordinal!=3 || av_diagnostic.frames[i].thread!=thread)continue;
        unsigned n=0;for(;n<av_diagnostic.frames[i].seen_count;++n)if(av_diagnostic.frames[i].seen[n]==pc)break;
        if(n==av_diagnostic.frames[i].seen_count && n<128) {
            av_diagnostic.frames[i].seen[n]=pc;++av_diagnostic.frames[i].seen_count;
            av_diagnostic_emit(engine,"block",pc,av_diagnostic.frames[i].id,3,av_diagnostic.frames[i].caller,av_diagnostic.frames[i].args);
        } else if(n==128 && !av_diagnostic.frames[i].block_gap) {
            av_diagnostic.frames[i].block_gap=1;
            av_diagnostic_gap("block_capacity",av_diagnostic.frames[i].id);
        }
    }
}
