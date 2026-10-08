/* A real OHCI root-port change invokes the original kernel's connected gate,
 * native ISR ABI, nested original service and deferred work. No test IRQ flag
 * bypasses the device or PIC. The Python owned-code oracle covers the wrapper
 * independently of this native callback adapter. */
static unsigned usb_isr_calls;
static volatile LONG usb_dpc_calls;
static void test_usb_dpc(void)
{
    CHECK(pilot_io_device_irql()==2);
    CHECK(MEM32(g_esp+4)==0x19a00 && MEM32(g_esp+8)==0x12345678);
    InterlockedIncrement(&usb_dpc_calls);g_esp+=20;
}
static void test_usb_isr(void)
{
    CHECK(MEM32(g_esp+4)==0x19800 && MEM32(g_esp+8)==0x98765432);
    CHECK(pilot_io_device_irql()==25);++usb_isr_calls;
    /* A nested original service cannot overwrite the suspended IRQ frame. */
    g_esp-=4;MEM32(g_esp)=0x12420;next_kernel_ordinal=103;kernel_dispatch();
    CHECK((g_eax&255)==25);
    CHECK(usb_test_read(0x0c)&64);
    usb_test_write(0x54,0x1f0000);usb_test_write(0x0c,64);
    g_esp-=16;MEM32(g_esp)=0x12430;MEM32(g_esp+4)=0x19a00;
    MEM32(g_esp+8)=0;MEM32(g_esp+12)=0;
    next_kernel_ordinal=119;kernel_dispatch();CHECK((g_eax&255)==1);
    CHECK((unsigned)usb_dpc_calls+1==usb_isr_calls);g_eax=1;g_esp+=12;
}
static void test_usb_irq(void)
{
    install_fault_handler();scheduler_enabled=1;scheduler_publish(0,0,0);scheduler_enter(0);
    CHECK(usb_test_read(0)==0x10);
    uint32_t init[]={0x19800,0x12400,0x98765432,0x31,25,0,0};
    call_kernel(109,init,7);
    uint32_t object=init[0];call_kernel(98,&object,1);CHECK((g_eax&255)==1 && MEM8(object+16)==1);
    call_kernel(98,&object,1);CHECK((g_eax&255)==0); // actual duplicate rejection
    uint32_t dpc[]={0x19a00,0x12440,0x12345678};call_kernel(107,dpc,3);
    call_kernel(129,NULL,0);CHECK((g_eax&255)==0 && pilot_io_device_irql()==2);
    g_ecx=31;call_kernel(160,NULL,0);CHECK((g_eax&255)==2 && pilot_io_device_irql()==31);
    usb_test_write(0x10,0x80000040u);usb_test_write(0x54,1u<<4);
    CHECK(usb_test_read(0x0c)&64);CHECK(pilot_usb_pic_vector(0)==0x31);
    usb_poll();CHECK(!usb_isr_calls && !usb_dpc_calls && pilot_io_device_irql()==31);
    CHECK(pilot_usb_pic_vector(0)<0); // actual kernel deferred/masked the IRQ
    g_ecx=0;call_kernel(161,NULL,0);
    CHECK(usb_isr_calls==1 && pilot_io_device_irql()==0);
    uint64_t deadline=GetTickCount64()+2000;
    while(!usb_dpc_calls && GetTickCount64()<deadline){scheduler_checkpoint();Sleep(1);}
    CHECK(usb_dpc_calls==1 && pilot_usb_pic_vector(0)<0);
    uint32_t select=0x0b,isr=0;
    CHECK(!pilot_usb_pic_port(0x20,1,1,&select));
    CHECK(!pilot_usb_pic_port(0x20,1,0,&isr));CHECK(!(isr&2)); // original specific EOI
    /* Also deliver directly at DISPATCH_LEVEL: hardware IRQ25 may preempt,
     * but its queued DPC must remain inserted until IRQL becomes passive. */
    call_kernel(129,NULL,0);CHECK(pilot_io_device_irql()==2);
    usb_test_write(0x54,1u<<4);usb_poll();
    CHECK(usb_isr_calls==2 && usb_dpc_calls==1 && pilot_io_device_irql()==2);
    Sleep(20);CHECK(MEM8(0x19a02)==1);
    uint32_t duplicate[]={0x19a00,0,0};call_kernel(119,duplicate,3);CHECK((g_eax&255)==0);
    g_ecx=0;call_kernel(161,NULL,0);
    deadline=GetTickCount64()+2000;
    while(usb_dpc_calls<2 && GetTickCount64()<deadline){scheduler_checkpoint();Sleep(1);}
    CHECK(usb_dpc_calls==2);
    pilot_cpu_cli();
    call_kernel(103,NULL,0);CHECK(!cpu_interrupt_flag && (g_eax&255)==0);
    usb_test_write(0x54,1u<<4);usb_poll();
    CHECK(usb_isr_calls==2 && usb_dpc_calls==2 && pilot_usb_pic_vector(0)==0x31);
    pilot_cpu_sti();usb_poll();
    CHECK(cpu_interrupt_flag && cpu_interrupt_shadow && usb_isr_calls==2);
    /* This store represents the actual following instruction; delivery must
     * not occur until the recompiler emits its shadow-complete boundary. */
    MEM32(0x19b00)=0x12345678;
    pilot_cpu_sti_complete();CHECK(usb_isr_calls==3 && !cpu_interrupt_shadow);
    deadline=GetTickCount64()+2000;
    while(usb_dpc_calls<3 && GetTickCount64()<deadline){scheduler_checkpoint();Sleep(1);}
    CHECK(usb_dpc_calls==3 && MEM32(0x19b00)==0x12345678);
    call_kernel(100,&object,1);CHECK((g_eax&255)==1 && !MEM8(object+16));
    call_kernel(100,&object,1);CHECK((g_eax&255)==0);
    usb_test_write(0x54,1u<<4);CHECK(pilot_usb_pic_vector(0)<0); // disconnected/masked
    usb_test_write(8,1);scheduler_retire();scheduler_enabled=0;
    fprintf(stderr,"PASS: original connect/duplicate/disconnect, OHCI/PIC IRQ, high-IRQL deferral/lowering, ISR ABI/nested service, real EOI and DPC exclusion\n");
}
static void test_usb_irq_clock(void)
{
    install_fault_handler();scheduler_enabled=1;scheduler_publish(0,0,0);scheduler_enter(0);
    CHECK(usb_test_read(0)==0x10);
    uint32_t init[]={0x19800,0x12400,0x98765432,0x31,25,0,0};
    call_kernel(109,init,7);call_kernel(98,init,1);CHECK((g_eax&255)==1);
    uint32_t dpc[]={0x19a00,0x12440,0x12345678};call_kernel(107,dpc,3);
    usb_clock_start();
    usb_test_write(0x10,0x80000040u);usb_test_write(0x54,1u<<4);
    /* No guest MMIO, service or checkpoint after assertion: all guest work
     * blocks and the hardware worker must obtain the idle CPU itself. */
    int blocked=scheduler_block_begin();CHECK(blocked);
    uint64_t deadline=GetTickCount64()+2000;
    while(!usb_dpc_calls && GetTickCount64()<deadline)Sleep(1);
    CHECK(usb_dpc_calls==1);scheduler_block_end(blocked);
    CHECK(usb_isr_calls==1 && pilot_usb_pic_vector(0)<0);
    CHECK(vm_committed(PILOT_IO_BRIDGE_BASE,PILOT_IO_BRIDGE_BYTES)==0);
    CHECK(vm_committed(PILOT_IO_CLOCK_BASE,PILOT_IO_CLOCK_BYTES)==0);
    call_kernel(100,init,1);CHECK((g_eax&255)==1);
    usb_test_write(8,1);scheduler_retire();
    fprintf(stderr,"PASS: USB IRQ and original ISR/DPC while all guest threads block; no polling caller or concurrent guest execution\n");
}
