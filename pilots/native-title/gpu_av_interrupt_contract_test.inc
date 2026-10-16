/* Authored miniport consumer, real original ISR/DPC/AV3 and one GPU owner.
 * Object layout/producers: retained pilot_display_bindings.h and existing
 * display_lle_open's109/98/107/108 setup. No external frame/FIELD drive.
 * Empty flip queue and NULL user callback are explicit fixture inputs; this
 * does not import a ready driver/device or claim title/pixel qualification. */
#if defined(PILOT_GPU_FILES_WORKER) && defined(PILOT_AUDIO_REFERENCE) && defined(PILOT_GPU_AV_TEST_CALLBACKS)
#ifndef PILOT_SCHEDULING_LLE
#error The GPU AV integration fixture requires the original callback kernel selection
#endif
#include "pilot_display_bindings.h"
static unsigned gpu_av_interrupt_isrs,gpu_av_interrupt_dpcs,gpu_av_interrupt_samples;
static uint32_t gpu_av_interrupt_context;
static void gpu_av_interrupt_observe(const char *kind,uint32_t entry)
{
    if(gpu_av_interrupt_samples>=64)return;
    uint64_t ticks=0,next=0;
    CHECK(!pilot_usb_pit_clock(&ticks,&next));
    fprintf(stderr,"[GPU AV INTERRUPT] sample=%u kind=%s entry=%08x ticks=%llu irql=%u frame=%u pending=%u\n",
        ++gpu_av_interrupt_samples,kind,entry,(unsigned long long)ticks,
        pilot_io_device_irql(),
        MEM32(gpu_av_interrupt_context+PILOT_DISPLAY_FRAME_OFFSET),
        MEM8(gpu_av_interrupt_context+PILOT_DISPLAY_DPC_OBJECT_OFFSET+2));
}
static const char *gpu_av_interrupt_isr(uint32_t entry,uint32_t regs[8])
{
    int selected=entry==PILOT_DISPLAY_ISR;
    if(selected) {
        CHECK(pilot_io_device_irql()==23);
        ++gpu_av_interrupt_isrs;gpu_av_interrupt_observe("isr-enter",entry);
    }
    const char *error=usb_original_isr(entry,regs);
    if(!error && selected)gpu_av_interrupt_observe("isr-return",entry);
    return error;
}
static const char *gpu_av_interrupt_dpc(uint32_t entry,uint32_t regs[8])
{
    int selected=entry==PILOT_DISPLAY_DPC;
    if(selected) {
        CHECK(pilot_io_device_irql()==2);
        ++gpu_av_interrupt_dpcs;gpu_av_interrupt_observe("dpc-enter",entry);
    }
    const char *error=usb_original_dpc(entry,regs);
    if(!error && selected)gpu_av_interrupt_observe("dpc-return",entry);
    return error;
}
static uint32_t gpu_av_interrupt_call(unsigned ordinal,const uint32_t *args)
{
    uint32_t result=0;
    const char *error=pilot_io_device_lle(ordinal,memory,args,&result);
    if(error)fprintf(stderr,"[GPU AV INTERRUPT SERVICE] ordinal=%u error=%s\n",ordinal,error);
    CHECK(!error);return result;
}
static uint32_t gpu_av_interrupt_thread(const uint32_t args[6])
{
    /* Authored CALL/RET consumer on an actual original guest thread. Keep
     * its code/data in a new owned user allocation, outside loaded XBE pages. */
    uint32_t base=0,bytes=4096;
    CHECK(!vm_allocate(&base,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    CHECK(base>=0x10000 && base<0x80000000u && bytes==4096);
    const uint8_t caller[]={0x57,0x8b,0x7c,0x24,0x08,
        0xff,0x77,0x1c,0xff,0x77,0x18,0xff,0x77,0x14,
        0xff,0x77,0x10,0xff,0x77,0x0c,0xff,0x77,0x08,
        0xb8,0x0c,0,0,0xfe,0xff,0xd0,0x89,0x07,
        0xc7,0x47,0x04,1,0,0,0,0x5f,0xc2,0x04,0};
    memcpy(memory+base,caller,sizeof(caller));
    uint32_t context=base+0x100,out=base+0x140;
    MEM32(context)=0xcccccccc;MEM32(context+4)=0;
    memcpy(memory+context+8,args,24);
    const uint32_t create[]={out,0,base,context,0};
    CHECK(!gpu_av_interrupt_call(254,create));uint32_t handle=MEM32(out);
    const uint32_t reference[]={handle,0,out+4};
    CHECK(!gpu_av_interrupt_call(246,reference));uint32_t object=MEM32(out+4);
    const uint32_t join[]={object,0,0,0,0};
    CHECK(!gpu_av_interrupt_call(159,join));
    CHECK(MEM32(context+4)==1 && !memcmp(memory+context+8,args,24));
    uint32_t result=MEM32(context);
    CHECK(!gpu_av_interrupt_call(187,&handle));
    gpu_av_interrupt_call(250,&object);
    CHECK(gpu_av_interrupt_call(187,&handle)==0xc0000008u);
    bytes=0;CHECK(!vm_free(&base,&bytes,MEM_RELEASE));
    return result;
}
static void test_gpu_av_interrupt_lle(uint32_t phase,int threaded)
{
    install_fault_handler();
    CHECK(recomp_lookup(PILOT_DISPLAY_ISR) && recomp_lookup(PILOT_DISPLAY_DPC));
    /* Root's early fixture dispatch uses the real production load_image.
     * These exact XBE import slots must reach the ordinary kernel bridge;
     * no test-authored replacement or guessed global supplies a callback. */
    CHECK(MEM32(0x00189fc4u)==0xfe000000u+119u*4u); /* KeInsertQueueDpc */
    CHECK(MEM32(0x00189ee8u)==0xfe000000u+145u*4u); /* KeSetEvent */
    CHECK(!pilot_io_device_files(memory));
    g_fs_base=PILOT_IO_PCR_BASE;CHECK(pilot_io_device_irql()==0);
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));
    gpu_lle_open_profile(phase);
    CHECK(!pilot_io_device_clock_start());audio_reference_clock_start();
    CHECK(audio_reference_clock.max_step==32);
    CHECK(!gpu_lle_prepare_launch());gpu_test_completed_av_mode(phase);

    /* Actual pool ownership and original initialized dispatcher objects.
     * These are authored driver inputs, not seeding hardware or completion.
     * The empty ring makes13B330 return without any submitted flip. The
     * flags match the reached retained original profile (FIELD path enabled,
     * no interlace offset path), and NULL callback excludes unknown title
     * callback behavior from this small interrupt/control reproduction. */
    uint32_t context=pool_allocate(PILOT_DISPLAY_MINIPORT_BYTES,0x56415149u);
    CHECK(context);gpu_av_interrupt_context=context;
    memset(memory+context,0,PILOT_DISPLAY_MINIPORT_BYTES);
    MEM32(context)=0xfd000000u;
    MEM32(context+4)=2560;MEM32(context+8)=0x04010101;MEM32(context+12)=0x12;
    MEM32(context+PILOT_DISPLAY_MODE_OFFSET)=0x00600100;
    const uint32_t event[]={context+PILOT_DISPLAY_EVENT_OFFSET,0,0};
    gpu_av_interrupt_call(108,event);
    const uint32_t dpc[]={context+PILOT_DISPLAY_DPC_OBJECT_OFFSET,PILOT_DISPLAY_DPC,context};
    gpu_av_interrupt_call(107,dpc);
    const uint32_t irq[]={context+PILOT_DISPLAY_INTERRUPT_OFFSET,PILOT_DISPLAY_ISR,context,0x33,23,0,0};
    gpu_av_interrupt_call(109,irq);
    /* Original109 at8001C997 stores routine/context/IRQindex/IRQL at00/04/
     *08/0C, clears connected10, and copies22 thunk DWORDs at18..6F. Thus
     * miniport+14's70-byte interrupt ends exactly at the DPC at+84. */
    CHECK(MEM32(irq[0])==PILOT_DISPLAY_ISR && MEM32(irq[0]+4)==context);
    CHECK(MEM32(irq[0]+8)==3 && MEM32(irq[0]+12)==23 && !MEM8(irq[0]+16));
    CHECK(MEM32(dpc[0]+12)==PILOT_DISPLAY_DPC && MEM32(dpc[0]+16)==context);
    CHECK((gpu_av_interrupt_call(98,irq)&255)==1);
    MEM32(context+PILOT_DISPLAY_CONNECTED_OFFSET)=1;
    MEM32(context+PILOT_DISPLAY_PMC_ENABLE_OFFSET)=1;
    pilot_io_device_isr_bind(gpu_av_interrupt_isr);
    pilot_io_device_dpc_bind(gpu_av_interrupt_dpc);
    /* Only PCRTC vblank is enabled; no PFIFO/PGRAPH work or signal injection. */
    gpu_test_write(0x600140,1);gpu_test_write(0x140,1);

    uint32_t args[]={0xfd000000,0,0x04010101,0x12,2560,gpu_lle.launch_framebuffer&0x03ffffffu};
    uint32_t result=0xcccccccc;
    const char *error=NULL;
    if(threaded)result=gpu_av_interrupt_thread(args);
    else error=pilot_io_device_lle(3,memory,args,&result);
    fprintf(stderr,"[GPU AV INTERRUPT RESULT] threaded=%d error=%s result=%08x isrs=%u dpcs=%u frame=%u\n",
        threaded,error?error:"none",result,gpu_av_interrupt_isrs,gpu_av_interrupt_dpcs,
        MEM32(context+PILOT_DISPLAY_FRAME_OFFSET));
    CHECK(!error && result==0);
    gpu_test_completed_av_mode(phase^1u);
    CHECK(gpu_av_interrupt_isrs>=3 && gpu_av_interrupt_dpcs>=3);
    CHECK(MEM32(context+PILOT_DISPLAY_FRAME_OFFSET)>=3);
    CHECK(MEM32(context+PILOT_DISPLAY_EVENT_OFFSET+4)==1);
    CHECK(!MEM8(context+PILOT_DISPLAY_DPC_OBJECT_OFFSET+2));
    CHECK(!MEM32(context+0xa4)); /* Original DPC's active/queued driver flag. */
    CHECK(MEM32(context+PILOT_DISPLAY_CALLBACK_OFFSET)==0);
    /* Expected-only retirement: stop new hardware delivery, disconnect the
     * real KINTERRUPT, verify queue retirement, then release owned storage. */
    gpu_test_write(0x140,0);gpu_test_write(0x600140,0);
    CHECK((gpu_av_interrupt_call(100,irq)&255)==1);
    CHECK(!MEM8(context+PILOT_DISPLAY_INTERRUPT_OFFSET+16));
    CHECK(!MEM8(context+PILOT_DISPLAY_DPC_OBJECT_OFFSET+2));
    pilot_io_device_isr_bind(usb_original_isr);
    pilot_io_device_dpc_bind(usb_original_dpc);
    pool_free(context);gpu_av_interrupt_context=0;
    fprintf(stderr,"PASS original GPU ISR/DPC + original04010101 AV3 return under bounded shared time; no title/pixel qualification\n");
}
#endif
