/* Optional bounded owner-side observations. Sample the maintained shared
 * backing while its sole engine lock is held; raw captured RAM may be stale
 * after publication. This never executes a service or changes readiness. */
static void original_usb_instruction(uc_engine *engine,uint64_t address,uint32_t size,void *data)
{
    (void)size;(void)data;
    static unsigned samples;
    if(samples>=128 || (address!=0x187f6du && address!=0x187fa6u && address!=0x1880a5u &&
       address!=0x188247u && address!=0x188260u && address!=0x188275u &&
       address!=0x18827au && address!=0x18827du && address!=0x188565u && address!=0x18859fu))return;
    uint32_t regs[8]={0},flags=0,words[4]={0};unsigned char code[8]={0};
    for(unsigned i=0;i<8;++i)uc_reg_read(engine,general_regs[i],regs+i);
    uc_reg_read(engine,UC_X86_REG_EFLAGS,&flags);
    uc_mem_read(engine,address,code,sizeof(code));
    uint32_t object=address>=0x188247u && address<=0x188284u?regs[7]:regs[1];
    uc_err read=UC_ERR_READ_UNMAPPED;
    if((object>=0x10000u && object<0x80000000u) ||
       (object>=0x80000000u && object<0x84000000u) ||
       (object>=0xd0000000u && object<0xd4000000u))
        read=uc_mem_read(engine,object,words,sizeof(words));
    fprintf(stderr,"[ORIGINAL USB INSTRUCTION] sample=%u pc=%08x thread=%08x eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x flags=%08x object=%08x read=%u words=%08x/%08x/%08x/%08x code=",
        ++samples,(uint32_t)address,kernel_word(0x8003da64u),regs[0],regs[1],regs[2],regs[3],regs[4],regs[5],regs[6],regs[7],flags,
        object,(unsigned)read,words[0],words[1],words[2],words[3]);
    for(unsigned i=0;i<sizeof(code);++i)fprintf(stderr,"%02x",code[i]);
    fprintf(stderr,"\n");
}
static void original_progress(uint32_t entry)
{
    static int configured,enabled;
    static unsigned samples;
    static ULONGLONG next;
    if(!configured) {
        const char *value=getenv("XLLE_PILOT_ORIGINAL_PROGRESS");
        enabled=value && !strcmp(value,"1");configured=1;
    }
    if(!enabled || samples>=80)return;
    ULONGLONG now=GetTickCount64();
    if(now<next)return;
    next=now+5000;++samples;
    uint32_t pc=0,flags=0;
    if(uc_reg_read(cpu,UC_X86_REG_EIP,&pc)!=UC_ERR_OK ||
       uc_reg_read(cpu,UC_X86_REG_EFLAGS,&flags)!=UC_ERR_OK)return;
    uint32_t current=kernel_word(0x8003da64u),idle=kernel_word(0x8003da6cu);
    uint32_t time=original_export(120);
    uint32_t low=kernel_word(time),high=kernel_word(time+4);
    uint64_t ticks=0,irq=0;int vector=-1,inhibited=-1;
#ifdef PILOT_USB_LLE
    if(pilot_usb_pit_clock(&ticks,&irq))return;
    vector=pilot_usb_pic_vector(0);
#endif
#ifdef PILOT_UNICORN_IRQ_BOUNDARY
    inhibited=xlle_unicorn_irq_inhibited(cpu);
#endif
    fprintf(stderr,"[ORIGINAL PROGRESS] sample=%u host_ms=%llu entry=%08x pc=%08x flags=%08x inhibited=%d current=%08x idle=%08x irql=%u ticks=%llu next_irq=%llu vector=%d itime=%llu dpc_head=%08x next_thread=%08x aliases=%u/%u retired=%u\n",
        samples,(unsigned long long)now,entry,pc,flags,inhibited,current,idle,
        kernel_word(0x8003da60u)&255,(unsigned long long)ticks,(unsigned long long)irq,vector,
        (unsigned long long)(((uint64_t)high<<32)|low),kernel_word(0x8003da8cu),
        kernel_word(0x8003da68u),mapped_count,mapped_capacity,alias_evictions);
#ifdef PILOT_USB_LLE
    pilot_usb_lle_progress(samples);
#endif
    for(unsigned i=0;i<64;++i) {
        const struct OriginalThread *thread=original_threads+i;
        if(!thread->object || !thread->started || !thread->published)continue;
        uint32_t object=thread->object;
        uint32_t state=kernel_word(object+0x2cu),priority=kernel_word(object+0x30u);
        uint32_t wait=kernel_word(object+0x54u),control=kernel_word(object+0x74u);
        fprintf(stderr,"[ORIGINAL PROGRESS THREAD] object=%08x handle=%08x state=%u priority=%d switches=%u wait=%08x reason=%u suspend=%u block=%08x stack=%08x timer=%08x/%08x cpu=%d kernel=%d component=%d ordinal=%u request=%u\n",
            object,thread->handle,state&255,(int)(int8_t)(priority>>16),kernel_word(object+0x4cu),
            kernel_word(object+0x50u),wait>>24,(control>>8)&255,kernel_word(object+0x58u),
            kernel_word(object+0x24u),kernel_word(object+0x98u),kernel_word(object+0x9cu),
            thread->in_cpu,thread->in_kernel,thread->in_component,thread->call_ordinal,thread->request);
    }
}
