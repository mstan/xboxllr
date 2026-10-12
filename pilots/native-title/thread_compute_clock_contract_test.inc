/* Authored integer loop boundary. The checkpoint is the actual JNE after
 * CMP EAX,80000000, not a synthetic service CALL or function return. Original
 * IRQ0 may wake a higher-priority peer while this C continuation is retained. */
#define THREAD_COMPUTE_HEAD 0x126e0u
#define THREAD_COMPUTE_BRANCH 0x126efu
#include "../../src/cpu/x86_integer_flags.h"
static uint32_t thread_compute_context,thread_compute_checks,thread_compute_peer_done;
static uint32_t thread_compute_cli,thread_compute_cli_checks;
static uint64_t thread_compute_elapsed;
static void test_thread_compute_body(void)
{
    uint32_t context=MEM32(g_esp+4);
    if(MEM32(context)) {
        MEM64(context+8)=(uint64_t)-(int64_t)MEM32(context+16);
        uint32_t wait[]={MEM32(context+4),0,context+8};
        uint64_t before=thread_interrupt_time();
        CHECK(thread_wait_call(233,wait,3)==0x102);
        thread_compute_elapsed=thread_interrupt_time()-before;
        CHECK(thread_compute_elapsed>=MEM32(context+16));
        MEM32(thread_compute_context)=1;thread_compute_peer_done=1;
    } else {
        uint32_t saved[]={g_ebx,g_ebp,g_esi,g_edi};
        uint32_t flags=0;CHECK(!pilot_io_device_thread_initial_flags(&flags));
        CHECK(!(flags&~0xed7u) && (flags&0x600)==0x200);
        g_eax=0x7ffffff0;g_ecx=0xdecafbad;g_edx=0x13579bdf;
        g_ebx=0xfedcba98;g_ebp=0x87654321;g_esi=0x12345678;g_edi=0x99aabbcc;
        volatile int32_t *request=pilot_io_device_thread_checkpoint_request();CHECK(request);
        uint64_t watchdog=dispatch_monotonic()+30000000;
        for(;;) {
            if(MEM32(thread_compute_context))break;
            ++g_eax;
            flags=xlle_x86_cmp32_flags(g_eax,0x80000000u,flags);
            if(thread_compute_cli && thread_compute_cli_checks<2)flags&=~0x200u;
            else flags|=0x200u;
            if(*request) {
                uint32_t regs[]={g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi};
                uint32_t expected[8];memcpy(expected,regs,sizeof(regs));uint32_t expected_flags=flags;
                if(!thread_compute_checks) {
                    CHECK(pilot_io_device_thread_checkpoint(0x9000u,regs,&flags));
                    uint32_t bad_flags=flags|0x100u;
                    CHECK(pilot_io_device_thread_checkpoint(THREAD_COMPUTE_BRANCH,regs,&bad_flags));
                    uint32_t bad_regs[8];memcpy(bad_regs,regs,sizeof(regs));bad_regs[4]=0x10000;
                    CHECK(pilot_io_device_thread_checkpoint(THREAD_COMPUTE_BRANCH,bad_regs,&flags));
                    CHECK(!memcmp(regs,expected,sizeof(regs)) && flags==expected_flags && *request);
                }
                CHECK(!pilot_io_device_thread_checkpoint(THREAD_COMPUTE_BRANCH,regs,&flags));
                CHECK(!memcmp(expected,regs,sizeof(regs)) && flags==expected_flags);
                CHECK(g_eax==regs[0] && g_ecx==regs[1] && g_edx==regs[2] && g_ebx==regs[3] &&
                    g_esp==regs[4] && g_ebp==regs[5] && g_esi==regs[6] && g_edi==regs[7]);
                CHECK(dispatch_monotonic()<watchdog);++thread_compute_checks;
                if(!(flags&0x200)) {
                    CHECK(!thread_compute_peer_done && !MEM32(thread_compute_context));
                    ++thread_compute_cli_checks;
                }
            }
            // Actual JNE backedge; equality clears EAX then takes JMP head.
            if(g_eax==0x80000000u)g_eax=0;
        }
        CHECK(thread_compute_peer_done && thread_compute_checks);
        g_ebx=saved[0];g_ebp=saved[1];g_esi=saved[2];g_edi=saved[3];
    }
    g_esp+=8;
}
static void test_thread_compute_clock_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));
    uint32_t command=0x34,low=0x65,high=4;
    CHECK(!pilot_usb_pic_port(0x43,1,1,&command));
    CHECK(!pilot_usb_pic_port(0x40,1,1,&low));CHECK(!pilot_usb_pic_port(0x40,1,1,&high));
    // cmp [19980],0; jne exit; inc eax; cmp eax,80000000; jne head;
    // xor eax,eax; jmp head; ret 4. Retained bytes certify the real boundary.
    const uint8_t loop[]={0x83,0x3d,0x80,0x99,0,0,0,0x75,0x0c,0x40,
        0x3d,0,0,0,0x80,0x75,0xef,0x31,0xc0,0xeb,0xeb,0xc2,4,0};
    memcpy(memory+THREAD_COMPUTE_HEAD,loop,sizeof(loop));
    for(unsigned lifetime=0;lifetime<2;++lifetime) {
        thread_compute_context=0x19980;MEM32(thread_compute_context)=0;
        thread_compute_checks=thread_compute_peer_done=thread_compute_cli_checks=0;
        thread_compute_cli=lifetime;thread_compute_elapsed=0;
        uint32_t out=0x19800,create_event[]={out,0,1,0};
        CHECK(!event_lle_call(189,create_event));uint32_t event=MEM32(out),handles[2],objects[2];
        for(unsigned id=0;id<2;++id) {
            uint32_t context=0x19900+id*32;MEM32(context)=id;MEM32(context+4)=event;
            MEM32(context+16)=lifetime?330000:100000;
            uint32_t create[]={out,0,THREAD_COMPUTE_HEAD,context,0};
            CHECK(!event_lle_call(254,create));handles[id]=MEM32(out);
            uint32_t ref[]={handles[id],0,out+4};CHECK(!event_lle_call(246,ref));objects[id]=MEM32(out+4);
            uint32_t priority[]={objects[id],id?30:29};CHECK(event_lle_call(148,priority)==8);
        }
        uint64_t next=0;CHECK(!pilot_usb_pit_clock(&thread_idle_clock.base,&next));
        thread_idle_clock.origin=dispatch_monotonic();thread_idle_clock.samples=0;
        if(thread_idle_runtime_clock>=2) {
            controlled_clock_ticks=thread_idle_clock.base;controlled_clock_blocks=0;
            controlled_clock_steps=controlled_clock_reads=controlled_clock_stalls=0;
            CHECK(!pilot_io_device_clock_bind_step(test_thread_controlled_clock_input,NULL,test_thread_controlled_clock_step));
        } else CHECK(!pilot_io_device_clock_bind(test_thread_idle_clock_input,&thread_idle_clock));
        for(unsigned id=0;id<2;++id) {
            uint32_t join[]={objects[id],0,0,0,0};CHECK(!event_lle_call(159,join));
            CHECK(!event_lle_call(187,handles+id));event_lle_call(250,objects+id);
        }
        CHECK(!pilot_io_device_clock_bind(NULL,NULL));CHECK(!event_lle_call(187,&event));
        CHECK(thread_compute_elapsed>=(lifetime?330000u:100000u));
        CHECK(thread_compute_checks && thread_compute_cli_checks==(lifetime?2u:0u));
        if(thread_idle_runtime_clock>=2) {
            CHECK(controlled_clock_steps && controlled_clock_steps<100);
            CHECK(controlled_clock_stalls==(thread_idle_runtime_clock==3?controlled_clock_steps:0));
            fprintf(stderr,"[CONTROLLED COMPUTE] lifetime=%u steps=%u blocks=%llu checks=%u CLI=%u elapsed=%llu; host-independent checkpoint opportunities\n",
                lifetime,controlled_clock_steps,(unsigned long long)controlled_clock_blocks,thread_compute_checks,
                thread_compute_cli_checks,(unsigned long long)thread_compute_elapsed);
        }
        fprintf(stderr,"PASS: original compute clock lifetime=%u checkpoints=%u CLI=%u elapsed=%llu; exact PC/eight GPRs/EFLAGS retained across IRQ0 and higher-priority peer\n",
            lifetime,thread_compute_checks,thread_compute_cli_checks,(unsigned long long)thread_compute_elapsed);
    }
}
