/* Actual original 124/143/144 operate on referenced, ready KTHREADs. The
 * actor keeps its original priority30, so clamp-to1 observations cannot let
 * an inherited title worker consume unrelated captured resources. Two real
 * targets then run in the priority order produced by 143, not birth order. */
#include <stddef.h>
#define THREAD_PRIORITY_CPU 0x1e000u
#define THREAD_PRIORITY_TARGET 0x1e400u
#define THREAD_PRIORITY_CONTEXT 0x1d000u
#define THREAD_PRIORITY_TARGET_CONTEXT 0x1d400u
#define PRIORITY_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
struct ThreadPriorityRow {
    int32_t requested,previous,query;
    uint32_t state_priority,base_control,process,row_address,ebx;
};
struct ThreadBoostRow {uint32_t requested,previous,observed;};
struct ThreadPriorityContext {
    uint32_t handles[2],objects[2],reference_status[2],self;
    uint32_t sp_before,sp_after,flags_before,flags_after,irql_before,irql_after,done;
    uint32_t canary_before,padding,entry,order;
    int32_t other_previous[2],other_query,initial_query;
    uint32_t ready[2],nonvolatile[3],wait_status[2],events[2],canary_middle;
    int64_t timeout;
    double fp_input,fp_output;
    uint16_t fp_control_requested,fp_control_before,fp_control_after,fp_padding;
    uint32_t mxcsr_requested,mxcsr_before,mxcsr_after;
    uint8_t xmm_input[16],xmm_output[16];
    uint32_t row_remaining,boost_remaining,process_base[2];
    struct ThreadPriorityRow rows[8];
    struct ThreadBoostRow boosts[4];
    uint32_t canary_after;
};
struct ThreadPriorityTargetContext {
    uint32_t done,event,actor,self,order_address,order,actor_state,current;
    uint32_t sp_before,sp_after,flags_before,flags_after;
    double fp_input,fp_output;
    uint32_t entry,nonvolatile[3];
    uint8_t xmm_input[16],xmm_output[16];
    uint32_t signal_status,signal_previous,priority,base_priority;
    uint16_t fp_control_requested,fp_control_before,fp_control_after,fp_padding;
    uint32_t mxcsr_requested,mxcsr_before,mxcsr_after,canary;
};
#define PRIORITY_OFFSET(f) ((uint32_t)offsetof(struct ThreadPriorityContext,f))
#define PRIORITY_TARGET_OFFSET(f) ((uint32_t)offsetof(struct ThreadPriorityTargetContext,f))
static void priority_store(struct ThreadChildCode *c,unsigned reg,uint32_t offset)
{PRIORITY_BYTES(c,0x89,0x87|(reg<<3));child_word(c,offset);}
static void priority_load(struct ThreadChildCode *c,unsigned reg,uint32_t offset)
{PRIORITY_BYTES(c,0x8b,0x87|(reg<<3));child_word(c,offset);}
static void priority_push(struct ThreadChildCode *c,uint32_t offset)
{PRIORITY_BYTES(c,0xff,0xb7);child_word(c,offset);}
static void priority_pointer(struct ThreadChildCode *c,uint32_t offset)
{PRIORITY_BYTES(c,0x8d,0x87);child_word(c,offset);PRIORITY_BYTES(c,0x50);}
static void priority_mov(struct ThreadChildCode *c,uint32_t offset,uint32_t value)
{PRIORITY_BYTES(c,0xc7,0x87);child_word(c,offset);child_word(c,value);}
static void priority_flags(struct ThreadChildCode *c,uint32_t offset)
{PRIORITY_BYTES(c,0x9c,0x58);priority_store(c,0,offset);}
static void priority_irql(struct ThreadChildCode *c,uint32_t offset)
{PRIORITY_BYTES(c,0x64,0x0f,0xb6,0x05,0x24,0,0,0);priority_store(c,0,offset);}
static void priority_loop(struct ThreadChildCode *c,uint32_t remaining,unsigned target)
{
    PRIORITY_BYTES(c,0xff,0x8f);child_word(c,remaining);
    PRIORITY_BYTES(c,0x0f,0x85);child_word(c,(uint32_t)((int32_t)target-(int32_t)(c->size+4)));
}
static void priority_fp_begin(struct ThreadChildCode *c,uint32_t fp,uint32_t xmm,
    uint32_t control,uint32_t control_before,uint32_t mxcsr,uint32_t mxcsr_before)
{
    PRIORITY_BYTES(c,0xd9,0xaf);child_word(c,control); /* fldcw */
    PRIORITY_BYTES(c,0x0f,0xae,0x97);child_word(c,mxcsr); /* ldmxcsr */
    PRIORITY_BYTES(c,0xd9,0xbf);child_word(c,control_before);
    PRIORITY_BYTES(c,0x0f,0xae,0x9f);child_word(c,mxcsr_before);
    PRIORITY_BYTES(c,0xdd,0x87);child_word(c,fp);
    PRIORITY_BYTES(c,0x0f,0x10,0x87);child_word(c,xmm);
}
static void priority_fp_end(struct ThreadChildCode *c,uint32_t fp,uint32_t xmm,
    uint32_t control,uint32_t mxcsr)
{
    PRIORITY_BYTES(c,0xdd,0x9f);child_word(c,fp);
    PRIORITY_BYTES(c,0x0f,0x11,0x87);child_word(c,xmm);
    PRIORITY_BYTES(c,0xd9,0xbf);child_word(c,control);
    PRIORITY_BYTES(c,0x0f,0xae,0x9f);child_word(c,mxcsr);
}
static void priority_cpu_code(void)
{
    struct ThreadChildCode code={0},*c=&code;
    PRIORITY_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14);
    priority_store(c,4,PRIORITY_OFFSET(sp_before));
    priority_flags(c,PRIORITY_OFFSET(flags_before));priority_irql(c,PRIORITY_OFFSET(irql_before));
    priority_fp_begin(c,PRIORITY_OFFSET(fp_input),PRIORITY_OFFSET(xmm_input),
        PRIORITY_OFFSET(fp_control_requested),PRIORITY_OFFSET(fp_control_before),
        PRIORITY_OFFSET(mxcsr_requested),PRIORITY_OFFSET(mxcsr_before));
    for(unsigned i=0;i<2;++i) {
        priority_pointer(c,PRIORITY_OFFSET(objects)+i*4);child_push(c,0);
        priority_push(c,PRIORITY_OFFSET(handles)+i*4);child_call(c,246);
        priority_store(c,0,PRIORITY_OFFSET(reference_status)+i*4);
        priority_load(c,2,PRIORITY_OFFSET(objects)+i*4);
        PRIORITY_BYTES(c,0x8b,0x42,0x44,0x0f,0xbe,0x40,0x18);
        priority_store(c,0,PRIORITY_OFFSET(process_base)+i*4);
        /* Actual base8 domain before any priority write. Original143 has a
         * separate realtime-process16..31 branch; admitting that domain here
         * could preempt actor30 and invalidate the controlled ready proof. */
        PRIORITY_BYTES(c,0x83,0xf8,8,0x74,1,0xcc);
    }
    priority_push(c,PRIORITY_OFFSET(objects));child_call(c,124);
    priority_store(c,0,PRIORITY_OFFSET(initial_query));
    PRIORITY_BYTES(c,0xbb);child_word(c,0xfedcba98u);
    PRIORITY_BYTES(c,0xbd);child_word(c,0x87654321u);
    PRIORITY_BYTES(c,0x8d,0xb7);child_word(c,PRIORITY_OFFSET(rows));
    unsigned row_loop=c->size;
    PRIORITY_BYTES(c,0xff,0x36);priority_push(c,PRIORITY_OFFSET(objects));child_call(c,143);
    PRIORITY_BYTES(c,0x89,0x46,4);priority_push(c,PRIORITY_OFFSET(objects));child_call(c,124);
    PRIORITY_BYTES(c,0x89,0x46,8);priority_load(c,2,PRIORITY_OFFSET(objects));
    PRIORITY_BYTES(c,0x8b,0x42,0x30,0x89,0x46,12,0x8b,0x42,0x70,0x89,0x46,16,
        0x8b,0x42,0x44,0x89,0x46,20,0x89,0x76,24,0x89,0x5e,28,0x83,0xc6,32);
    priority_loop(c,PRIORITY_OFFSET(row_remaining),row_loop);
    PRIORITY_BYTES(c,0x8d,0xb7);child_word(c,PRIORITY_OFFSET(boosts));
    unsigned boost_loop=c->size;
    PRIORITY_BYTES(c,0xff,0x36);priority_push(c,PRIORITY_OFFSET(objects));child_call(c,144);
    PRIORITY_BYTES(c,0x89,0x46,4);priority_load(c,2,PRIORITY_OFFSET(objects));
    PRIORITY_BYTES(c,0x0f,0xb6,0x42,0x73,0x89,0x46,8,0x83,0xc6,12);
    priority_loop(c,PRIORITY_OFFSET(boost_remaining),boost_loop);
    /* Saturation first removes the target's previous dynamic/base offset;
     * the ordinary +6 then genuinely produces effective14, not 20->15. */
    for(unsigned i=0;i<2;++i) {
        child_push(c,i?6:16);priority_push(c,PRIORITY_OFFSET(objects)+4);child_call(c,143);
        priority_store(c,0,PRIORITY_OFFSET(other_previous)+i*4);
    }
    priority_push(c,PRIORITY_OFFSET(objects)+4);child_call(c,124);
    priority_store(c,0,PRIORITY_OFFSET(other_query));
    for(unsigned i=0;i<2;++i) {
        priority_load(c,2,PRIORITY_OFFSET(objects)+i*4);
        PRIORITY_BYTES(c,0x0f,0xb6,0x42,0x2c);priority_store(c,0,PRIORITY_OFFSET(ready)+i*4);
        priority_load(c,1,PRIORITY_OFFSET(objects)+i*4);child_call(c,250);
    }
    for(unsigned i=0;i<2;++i) {
        priority_pointer(c,PRIORITY_OFFSET(timeout));child_push(c,0);
        priority_push(c,PRIORITY_OFFSET(events)+i*4);child_call(c,233);
        priority_store(c,0,PRIORITY_OFFSET(wait_status)+i*4);
    }
    priority_store(c,3,PRIORITY_OFFSET(nonvolatile));priority_store(c,5,PRIORITY_OFFSET(nonvolatile)+4);
    priority_store(c,6,PRIORITY_OFFSET(nonvolatile)+8);priority_store(c,4,PRIORITY_OFFSET(sp_after));
    priority_flags(c,PRIORITY_OFFSET(flags_after));priority_irql(c,PRIORITY_OFFSET(irql_after));
    priority_fp_end(c,PRIORITY_OFFSET(fp_output),PRIORITY_OFFSET(xmm_output),
        PRIORITY_OFFSET(fp_control_after),PRIORITY_OFFSET(mxcsr_after));
    priority_mov(c,PRIORITY_OFFSET(done),1);
    PRIORITY_BYTES(c,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(c->size<THREAD_PRIORITY_TARGET-THREAD_PRIORITY_CPU);
    memcpy(memory+THREAD_PRIORITY_CPU,c->bytes,c->size);
}
static void priority_target_code(void)
{
    struct ThreadChildCode code={0},*c=&code;
    PRIORITY_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14);
    priority_store(c,4,PRIORITY_TARGET_OFFSET(sp_before));priority_flags(c,PRIORITY_TARGET_OFFSET(flags_before));
    priority_fp_begin(c,PRIORITY_TARGET_OFFSET(fp_input),PRIORITY_TARGET_OFFSET(xmm_input),
        PRIORITY_TARGET_OFFSET(fp_control_requested),PRIORITY_TARGET_OFFSET(fp_control_before),
        PRIORITY_TARGET_OFFSET(mxcsr_requested),PRIORITY_TARGET_OFFSET(mxcsr_before));
    PRIORITY_BYTES(c,0xbb);child_word(c,0xfedcba98u);
    PRIORITY_BYTES(c,0xbd);child_word(c,0x87654321u);
    PRIORITY_BYTES(c,0xbe);child_word(c,0x12345678u);
    priority_load(c,0,PRIORITY_TARGET_OFFSET(actor));PRIORITY_BYTES(c,0x0f,0xb6,0x40,0x2c);
    priority_store(c,0,PRIORITY_TARGET_OFFSET(actor_state));
    PRIORITY_BYTES(c,0x64,0xa1,0x28,0,0,0);priority_store(c,0,PRIORITY_TARGET_OFFSET(current));
    PRIORITY_BYTES(c,0x0f,0xbe,0x50,0x32);priority_store(c,2,PRIORITY_TARGET_OFFSET(priority));
    PRIORITY_BYTES(c,0x0f,0xbe,0x50,0x70);priority_store(c,2,PRIORITY_TARGET_OFFSET(base_priority));
    priority_load(c,1,PRIORITY_TARGET_OFFSET(order_address));
    PRIORITY_BYTES(c,0xff,0x01,0x8b,0x01);priority_store(c,0,PRIORITY_TARGET_OFFSET(order));
    priority_pointer(c,PRIORITY_TARGET_OFFSET(signal_previous));priority_push(c,PRIORITY_TARGET_OFFSET(event));
    child_call(c,225);priority_store(c,0,PRIORITY_TARGET_OFFSET(signal_status));
    priority_store(c,3,PRIORITY_TARGET_OFFSET(nonvolatile));priority_store(c,5,PRIORITY_TARGET_OFFSET(nonvolatile)+4);
    priority_store(c,6,PRIORITY_TARGET_OFFSET(nonvolatile)+8);priority_store(c,4,PRIORITY_TARGET_OFFSET(sp_after));
    priority_flags(c,PRIORITY_TARGET_OFFSET(flags_after));
    priority_fp_end(c,PRIORITY_TARGET_OFFSET(fp_output),PRIORITY_TARGET_OFFSET(xmm_output),
        PRIORITY_TARGET_OFFSET(fp_control_after),PRIORITY_TARGET_OFFSET(mxcsr_after));
    priority_mov(c,PRIORITY_TARGET_OFFSET(done),1);
    PRIORITY_BYTES(c,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(c->size<0x400);memcpy(memory+THREAD_PRIORITY_TARGET,c->bytes,c->size);
}
static void test_thread_priority_cpu_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));
    CHECK(PRIORITY_OFFSET(entry)==64 && PRIORITY_TARGET_OFFSET(entry)==64);
    CHECK(sizeof(struct ThreadPriorityContext)<0x400 && sizeof(struct ThreadPriorityTargetContext)<0x200);
    priority_cpu_code();priority_target_code();CHECK(!pilot_io_device_clock_start());
    CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    uint64_t next=0;CHECK(!pilot_usb_pit_clock(&thread_idle_clock.base,&next));
    thread_idle_clock.origin=dispatch_monotonic();thread_idle_clock.samples=0;
    CHECK(!pilot_io_device_clock_bind(test_thread_idle_clock_input,&thread_idle_clock));
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t base_threads=MEM32(stats+4),base_handles=MEM32(stats+8),before[9],after[9];test_memory_snapshot(before);
    const int32_t requested[]={17,-17,8,-8,3,-3,0,7};
    const int32_t queries[]={16,-16,7,-7,3,-3,0,7};
    const uint8_t priorities[]={15,1,15,1,11,5,8,15};
    const uint8_t saturation[]={1,255,0,0,0,0,0,0};
    for(unsigned life=0;life<3;++life) {
        uint32_t out=0x19800,handles[3],objects[3],events[2];
        struct ThreadPriorityContext *p=(struct ThreadPriorityContext*)(memory+THREAD_PRIORITY_CONTEXT);
        struct ThreadPriorityTargetContext *targets[2];memset(p,0,sizeof(*p));
        p->entry=THREAD_PRIORITY_CPU;p->row_remaining=8;p->boost_remaining=4;p->timeout=-500000;
        p->fp_input=123.625+life;p->fp_control_requested=0x077f;p->mxcsr_requested=0x3f80;
        p->canary_before=0x13579bdf;p->canary_middle=0x2468ace0;p->canary_after=0x89abcdef;
        for(unsigned i=0;i<16;++i)p->xmm_input[i]=(uint8_t)(0x35+i*7+life);
        for(unsigned i=0;i<8;++i)p->rows[i].requested=requested[i];
        for(unsigned i=0;i<4;++i)p->boosts[i].requested=i<2;
        for(unsigned i=0;i<2;++i) {
            uint32_t birth[]={out,0,1,0};CHECK(!event_lle_call(189,birth));events[i]=MEM32(out);p->events[i]=events[i];
            uint32_t query[]={events[i],out+16};MEM32(out+16)=MEM32(out+20)=0xcccccccc;
            CHECK(!event_lle_call(209,query) && MEM32(out+16)==1 && !MEM32(out+20));
            targets[i]=(struct ThreadPriorityTargetContext*)(memory+THREAD_PRIORITY_TARGET_CONTEXT+i*0x200);
            memset(targets[i],0,sizeof(*targets[i]));targets[i]->entry=THREAD_PRIORITY_TARGET;
            targets[i]->event=events[i];targets[i]->order_address=THREAD_PRIORITY_CONTEXT+PRIORITY_OFFSET(order);
            targets[i]->fp_input=-42.25-life-i;targets[i]->fp_control_requested=0x027f;targets[i]->mxcsr_requested=0x1f80;
            targets[i]->canary=0xabcdef01+i;for(unsigned x=0;x<16;++x)targets[i]->xmm_input[x]=(uint8_t)(0x81+x*3+i);
        }
        /* Birth B before A; the actor's real base-priority writes must move
         * them into priority14/15 queues and reverse that ready selection. */
        for(unsigned born=0;born<3;++born) {
            unsigned id=born==0?1:born==1?0:2;
            uint32_t entry=id==2?THREAD_PRIORITY_CPU:THREAD_PRIORITY_TARGET;
            uint32_t context=id==2?THREAD_PRIORITY_CONTEXT:THREAD_PRIORITY_TARGET_CONTEXT+id*0x200;
            uint32_t birth[]={out,0,entry,context,0};CHECK(!event_lle_call(254,birth));handles[id]=MEM32(out);
            uint32_t ref[]={handles[id],0,out+4};CHECK(!event_lle_call(246,ref));objects[id]=MEM32(out+4);
            unsigned char original[0x140];CHECK(component_wait_thread_snapshot(objects[id],original));
            CHECK(original[0x70]==8 && !original[0x31] && !original[0x73]);
            uint32_t priority[]={objects[id],id==2?30u:id?29u:28u};CHECK(event_lle_call(148,priority)==8);
        }
        p->self=objects[2];for(unsigned i=0;i<2;++i){p->handles[i]=handles[i];targets[i]->self=objects[i];targets[i]->actor=objects[2];}
        uint32_t join[]={objects[2],0,0,0,0};CHECK(!event_lle_call(159,join));
        CHECK(p->done && p->objects[0]==objects[0] && p->objects[1]==objects[1]);
        CHECK(!p->reference_status[0] && !p->reference_status[1] && !p->initial_query);
        CHECK(p->process_base[0]==8 && p->process_base[1]==8 && p->ready[0]==1 && p->ready[1]==1);
        CHECK(!p->row_remaining && !p->boost_remaining && p->other_previous[0]==0 && p->other_previous[1]==16 && p->other_query==6);
        for(unsigned i=0;i<8;++i) {
            struct ThreadPriorityRow *r=p->rows+i;
            CHECK(r->requested==requested[i] && r->previous==(i?queries[i-1]:0) && r->query==queries[i]);
            CHECK(((r->state_priority>>8)&255)==saturation[i] && ((r->state_priority>>16)&255)==priorities[i]);
            CHECK(r->base_control==priorities[i] && r->row_address==THREAD_PRIORITY_CONTEXT+PRIORITY_OFFSET(rows)+i*sizeof(*r) && r->ebx==0xfedcba98);
            CHECK(r->process==p->rows[0].process && r->process);
        }
        const unsigned boost_previous[]={0,1,1,0};
        for(unsigned i=0;i<4;++i)CHECK(p->boosts[i].requested==(i<2) && p->boosts[i].previous==boost_previous[i] && p->boosts[i].observed==(i<2));
        CHECK(!p->wait_status[0] && !p->wait_status[1] && p->order==2 && p->sp_before==p->sp_after);
        CHECK(p->nonvolatile[0]==0xfedcba98 && p->nonvolatile[1]==0x87654321 &&
            p->nonvolatile[2]==THREAD_PRIORITY_CONTEXT+PRIORITY_OFFSET(boosts)+sizeof(p->boosts));
        /* Condition flags are volatile across original CALLs. Observe the
         * actual preserved interrupt/direction contract, without inventing
         * equality for arithmetic flags that the original callee changes. */
        CHECK((p->flags_before&0x600)==0x200 && (p->flags_after&0x600)==0x200 && !p->irql_before && !p->irql_after);
        CHECK(!memcmp(&p->fp_input,&p->fp_output,8) && !memcmp(p->xmm_input,p->xmm_output,16));
        CHECK(p->fp_control_before==p->fp_control_requested && p->fp_control_after==p->fp_control_requested &&
            p->mxcsr_before==p->mxcsr_requested && p->mxcsr_after==p->mxcsr_requested);
        CHECK(p->canary_before==0x13579bdf && p->canary_middle==0x2468ace0 && p->canary_after==0x89abcdef);
        for(unsigned i=0;i<2;++i) {
            join[0]=objects[i];CHECK(!event_lle_call(159,join));struct ThreadPriorityTargetContext *t=targets[i];
            CHECK(t->done && t->order==i+1 && t->actor_state==5 && t->current==objects[i]);
            CHECK(t->priority==15-i && t->base_priority==15-i && !t->signal_status && !t->signal_previous);
            CHECK(t->sp_before==t->sp_after && (t->flags_before&0x600)==0x200 && (t->flags_after&0x600)==0x200);
            CHECK(t->nonvolatile[0]==0xfedcba98 && t->nonvolatile[1]==0x87654321 && t->nonvolatile[2]==0x12345678);
            CHECK(!memcmp(&t->fp_input,&t->fp_output,8) && !memcmp(t->xmm_input,t->xmm_output,16));
            CHECK(t->fp_control_before==t->fp_control_requested && t->fp_control_after==t->fp_control_requested &&
                t->mxcsr_before==t->mxcsr_requested && t->mxcsr_after==t->mxcsr_requested && t->canary==0xabcdef01+i);
        }
        for(unsigned i=0;i<3;++i) {
            CHECK(!event_lle_call(187,handles+i));event_lle_call(250,objects+i);
            CHECK(event_lle_call(187,handles+i)==0xc0000008u);
        }
        for(unsigned i=0;i<2;++i){CHECK(!event_lle_call(187,events+i));CHECK(event_lle_call(187,events+i)==0xc0000008u);}
        CHECK(!event_lle_call(256,&stats) && MEM32(stats+4)==base_threads && MEM32(stats+8)==base_handles);
        test_memory_snapshot(after);CHECK(!memcmp(before,after,sizeof(before)));
        fprintf(stderr,"[RAW THREAD PRIORITY] life=%u process_base=8 base/saturation/clamp/previous=exact boost1/0=exact readyA15/B14 birthBfirst runA/B actorWaiting5 FP/ABI/canaries/9memory/threadhandles=exact\n",life);
    }
    CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    fprintf(stderr,"PASS: three actual original124/143/144 lifetimes, reference/ready queue/block/wake ownership, base8 signed delta/clamp/saturation and boost previous byte, exact FP/ABI/resource retirement; silicon boost/quantum precision remains deferred\n");
}
#undef PRIORITY_OFFSET
#undef PRIORITY_TARGET_OFFSET
#undef PRIORITY_BYTES
