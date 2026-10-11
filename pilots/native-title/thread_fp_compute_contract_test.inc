/* Independent authored x86 producers: live architectural FP registers survive
 * an actual IRQ0 switch to a different FP owner while computing without calls.
 * x87/SSE and MMX are separate phases because MMX aliases the x87 registers. */
#define THREAD_FP_COMPUTE 0x13e00u
#define THREAD_FP_PREEMPTOR 0x15000u
#define THREAD_FP_PHASE 0x1c000u
struct ThreadFpCode { uint8_t bytes[512]; unsigned size; };
static void thread_fp_emit(struct ThreadFpCode *code,const uint8_t *bytes,unsigned size)
{
    CHECK(code->size+size<=sizeof(code->bytes));
    memcpy(code->bytes+code->size,bytes,size);code->size+=size;
}
static void thread_fp_u32(struct ThreadFpCode *code,uint32_t value)
{
    const uint8_t bytes[]={value,value>>8,value>>16,value>>24};
    thread_fp_emit(code,bytes,sizeof(bytes));
}
#define THREAD_FP_OP(...) do { const uint8_t op[]={__VA_ARGS__}; \
    thread_fp_emit(code,op,sizeof(op)); } while(0)
static void thread_fp_save(struct ThreadFpCode *code,uint32_t offset)
{
    THREAD_FP_OP(0x0f,0xae,0x87);thread_fp_u32(code,offset); /* FXSAVE [EDI+disp32] */
}
static void thread_fp_wait(struct ThreadFpCode *code,uint32_t result)
{
    /* LEA EAX,[EDI+8]; PUSH EAX; PUSH 0; PUSH [EDI+4];
     * MOV EAX,private NtWait233; CALL EAX; MOV [EDI+result],EAX. */
    THREAD_FP_OP(0x8d,0x47,8,0x50,0x6a,0,0xff,0x77,4,0xb8);
    thread_fp_u32(code,0xfe0003a4u);THREAD_FP_OP(0xff,0xd0,0x89,0x87);
    thread_fp_u32(code,result);
}
static void thread_fp_spin(struct ThreadFpCode *code,unsigned phase,uint32_t counter)
{
    unsigned start=code->size;
    THREAD_FP_OP(0xff,0x87);thread_fp_u32(code,counter); /* INC owned counter */
    THREAD_FP_OP(0x83,0x3d);thread_fp_u32(code,THREAD_FP_PHASE);
    THREAD_FP_OP(phase,0x72); /* CMP [shared],phase; JB start */
    int displacement=(int)start-(int)(code->size+1);
    CHECK(displacement>=-128 && displacement<=127);
    THREAD_FP_OP((uint8_t)displacement);
}
static unsigned thread_fp_program(uint32_t address,int peer)
{
    struct ThreadFpCode buffer={0},*code=&buffer;
    THREAD_FP_OP(0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14);
    THREAD_FP_OP(0x0f,0x20,0xe0,0x89,0x87);thread_fp_u32(code,0xb20);
    for(unsigned leaf=0;leaf<2;++leaf) {
        THREAD_FP_OP(0xb8);thread_fp_u32(code,leaf);THREAD_FP_OP(0x31,0xc9,0x0f,0xa2);
        THREAD_FP_OP(0x89,0x87);thread_fp_u32(code,0xb00+leaf*16);
        THREAD_FP_OP(0x89,0x9f);thread_fp_u32(code,0xb04+leaf*16);
        THREAD_FP_OP(0x89,0x8f);thread_fp_u32(code,0xb08+leaf*16);
        THREAD_FP_OP(0x89,0x97);thread_fp_u32(code,0xb0c+leaf*16);
    }
    /* Distinct control words and all eight x87/XMM registers. */
    THREAD_FP_OP(0xd9,0xaf);thread_fp_u32(code,0x82);
    THREAD_FP_OP(0x0f,0xae,0x97);thread_fp_u32(code,0x84);
    for(unsigned i=0;i<8;++i) {
        THREAD_FP_OP(0xdd,0x87);thread_fp_u32(code,0x90+i*8);
        THREAD_FP_OP(0x0f,0x10,(uint8_t)(0x87+i*8));thread_fp_u32(code,0x100+i*16);
    }
    thread_fp_save(code,0x200);
    if(peer)thread_fp_wait(code,0xa10);else thread_fp_spin(code,1,0xa00);
    thread_fp_save(code,0x400);
    if(peer) { THREAD_FP_OP(0xc7,0x05);thread_fp_u32(code,THREAD_FP_PHASE);thread_fp_u32(code,1); }
    for(unsigned i=0;i<8;++i) {
        THREAD_FP_OP(0x0f,0x6f,(uint8_t)(0x87+i*8));thread_fp_u32(code,0x180+i*8);
    }
    thread_fp_save(code,0x600);
    if(peer)thread_fp_wait(code,0xa14);else thread_fp_spin(code,2,0xa04);
    thread_fp_save(code,0x800);
    if(peer) { THREAD_FP_OP(0xc7,0x05);thread_fp_u32(code,THREAD_FP_PHASE);thread_fp_u32(code,2); }
    THREAD_FP_OP(0x0f,0x77,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    memcpy(memory+address,code->bytes,code->size);return code->size;
}
#undef THREAD_FP_OP
static void thread_fp_state_equal(uint32_t before,uint32_t after)
{
    /* Defined fields only: padding in the FXSAVE destination is not state. */
    CHECK(!memcmp(memory+before,memory+after,5));
    CHECK(!memcmp(memory+before+6,memory+after+6,8));
    CHECK(!memcmp(memory+before+16,memory+after+16,6));
    CHECK(MEM32(before+24)==MEM32(after+24));
    for(unsigned i=0;i<8;++i)CHECK(!memcmp(memory+before+32+i*16,memory+after+32+i*16,10));
    CHECK(!memcmp(memory+before+160,memory+after+160,128));
}
static void test_thread_fp_compute_lle(void)
{
    unsigned bytes[2]={thread_fp_program(THREAD_FP_COMPUTE,0),thread_fp_program(THREAD_FP_PREEMPTOR,1)};
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    for(unsigned lifetime=0;lifetime<3;++lifetime) {
        MEM32(THREAD_FP_PHASE)=0;
        uint32_t out=0x19800,event_args[]={out,0,1,0};
        CHECK(!event_lle_call(189,event_args));uint32_t event=MEM32(out),handles[2],objects[2];
        uint32_t duration=130000+lifetime*50000;
        for(unsigned id=0;id<2;++id) {
            uint32_t context=0x1a000+id*0x1000;memset(memory+context,0,0x1000);
            MEM32(context+4)=event;MEM64(context+8)=(uint64_t)-(int64_t)duration;
            MEM32(context+64)=id?THREAD_FP_PREEMPTOR:THREAD_FP_COMPUTE;
            MEM16(context+0x82)=id?0x077f:0x027f;MEM32(context+0x84)=id?0x3f80:0x1f80;
            for(unsigned i=0;i<8;++i) {
                double value=1.5+id*16+lifetime*8+i;memcpy(memory+context+0x90+i*8,&value,8);
                for(unsigned lane=0;lane<4;++lane) {
                    float value=(float)(1+id*128+lifetime*32+i*4+lane);
                    memcpy(memory+context+0x100+i*16+lane*4,&value,4);
                }
                MEM64(context+0x180+i*8)=0x0123456789abcdefull^(0x1111111111111111ull*(1+id*8+i));
            }
            uint32_t create[]={out,0,MEM32(context+64),context,0};
            CHECK(!event_lle_call(254,create));handles[id]=MEM32(out);
            uint32_t ref[]={handles[id],0,out+4};CHECK(!event_lle_call(246,ref));objects[id]=MEM32(out+4);
            uint32_t priority[]={objects[id],id?30:29};CHECK(event_lle_call(148,priority)==8);
        }
        uint64_t next=0;CHECK(!pilot_usb_pit_clock(&thread_idle_clock.base,&next));
        thread_idle_clock.origin=dispatch_monotonic();thread_idle_clock.samples=0;
        uint64_t before=0;CHECK(!pilot_io_device_interrupt_time(memory,&before));
        CHECK(!pilot_io_device_clock_bind(test_thread_idle_clock_input,&thread_idle_clock));
        for(unsigned id=0;id<2;++id) {
            uint32_t join[]={objects[id],0,0,0,0};CHECK(!event_lle_call(159,join));
            CHECK(!event_lle_call(187,handles+id));CHECK(event_lle_call(187,handles+id)==0xc0000008u);
            event_lle_call(250,objects+id);
        }
        uint64_t after=0;CHECK(!pilot_io_device_interrupt_time(memory,&after));
        uint64_t elapsed=after-before;
        CHECK(!pilot_io_device_clock_bind(NULL,NULL));CHECK(!event_lle_call(187,&event));
        CHECK(event_lle_call(187,&event)==0xc0000008u);
        const char *directory=getenv("PILOT_FP_PREEMPTION_EVIDENCE");
        if(directory) {
            char path[MAX_PATH];CHECK(snprintf(path,sizeof(path),"%s/live-fp-%u.aot-private.bin",directory,lifetime)<sizeof(path));
            FILE *file=fopen(path,"wb");CHECK(file);
            uint32_t header[]={bytes[0],bytes[1],lifetime,MEM32(THREAD_FP_PHASE)};
            CHECK(fwrite(header,1,sizeof(header),file)==sizeof(header));
            CHECK(fwrite(memory+THREAD_FP_COMPUTE,1,bytes[0],file)==bytes[0]);
            CHECK(fwrite(memory+THREAD_FP_PREEMPTOR,1,bytes[1],file)==bytes[1]);
            CHECK(fwrite(memory+0x1a000,1,0x2000,file)==0x2000);CHECK(!fclose(file));
        }
        CHECK(elapsed>=2*duration && MEM32(THREAD_FP_PHASE)==2);
        CHECK(MEM32(0x1aa00) && MEM32(0x1aa04));
        CHECK(MEM32(0x1ba10)==0x102 && MEM32(0x1ba14)==0x102);
        for(unsigned id=0;id<2;++id) {
            uint32_t context=0x1a000+id*0x1000;
            thread_fp_state_equal(context+0x200,context+0x400);
            thread_fp_state_equal(context+0x600,context+0x800);
            CHECK(MEM16(context+0x200)==(id?0x077f:0x027f));
            CHECK(MEM16(context+0x206)==0x587); /* actual last FLD qword [EDI+disp32] */
            CHECK(MEM32(context+0x210)==context+0xc8 && MEM16(context+0x214)==16);
            const uint32_t cpuid0[]={2,0x756e6547,0x6c65746e,0x49656e69};
            const uint32_t cpuid1[]={0x68a,0,0,0x0383f9ff};
            CHECK(!memcmp(memory+context+0xb00,cpuid0,sizeof(cpuid0)));
            CHECK(!memcmp(memory+context+0xb10,cpuid1,sizeof(cpuid1)));
            CHECK((MEM32(context+0xb20)&(1u<<9)) && !(MEM32(context+0xb20)&(1u<<18)));
            CHECK(MEM8(context+0x204)==0xff && MEM32(context+0x218)==(id?0x3f80:0x1f80));
            CHECK(!memcmp(memory+context+0x100,memory+context+0x2a0,128));
            for(unsigned i=0;i<8;++i) {
                /* Exact binary64-to-extended conversion for authored positive,
                 * normal half-integers; FXSAVE stores logical ST0..ST7. */
                uint64_t input=MEM64(context+0x90+(7-i)*8);
                uint64_t significand=((input&0xfffffffffffffull)|0x10000000000000ull)<<11;
                uint16_t exponent=(uint16_t)(((input>>52)&0x7ff)-1023+16383);
                CHECK(MEM64(context+0x220+i*16)==significand);
                CHECK(MEM16(context+0x228+i*16)==exponent);
            }
            for(unsigned i=0;i<8;++i)CHECK(MEM64(context+0x620+i*16)==MEM64(context+0x180+i*8));
        }
        uint32_t live=0;CHECK(!pilot_io_device_next_thread(&live) && !live);
        MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
        fprintf(stderr,"PASS: live FP compute lifetime=%u bytes=%u/%u loops=%u/%u elapsed=%llu; IRQ0 original switch retains both owners' x87, all eight XMM/MMX registers and controls\n",
            lifetime,bytes[0],bytes[1],MEM32(0x1aa00),MEM32(0x1aa04),(unsigned long long)elapsed);
    }
}
