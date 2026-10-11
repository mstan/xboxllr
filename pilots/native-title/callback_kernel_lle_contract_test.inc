#ifdef PILOT_SCHEDULING_LLE
/* Authored generated-body equivalent. Keep the actual original callback
 * stack; the runtime bridge must perform the real kernel operation and RET. */
static uint32_t callback_kernel_lle_call(unsigned ordinal,const uint32_t *args,unsigned count)
{
    uint32_t sp=g_esp,saved_ordinal=next_kernel_ordinal;
    uint32_t nonvol[]={g_ebx,g_ebp,g_esi,g_edi};
    CHECK(usb_device_callback_depth && count<=5);
    g_esp-=(count+1)*4;MEM32(g_esp)=0x12480;
    for(unsigned i=0;i<count;++i)MEM32(g_esp+4+i*4)=args[i];
    next_kernel_ordinal=ordinal;kernel_dispatch();
    CHECK(g_esp==sp && g_ebx==nonvol[0] && g_ebp==nonvol[1] &&
          g_esi==nonvol[2] && g_edi==nonvol[3]);
    next_kernel_ordinal=saved_ordinal;return g_eax;
}
static void test_callback_kernel_guard_lle(void)
{
    install_fault_handler();
    for(unsigned i=0;i<PILOT_IO_SERVICE_FRAMES;++i) {
        MEMORY_BASIC_INFORMATION guard,stack;
        uint32_t base=PILOT_IO_SERVICE_BASE+i*PILOT_IO_SERVICE_STRIDE;
        CHECK(VirtualQuery(memory+base,&guard,sizeof(guard)) && guard.State==MEM_RESERVE);
        CHECK(VirtualQuery(memory+base+4096,&stack,sizeof(stack)) &&
              stack.State==MEM_COMMIT && stack.Protect==PAGE_READWRITE);
    }
    g_esp=0x18000;g_eax=0x12345678;g_ecx=2;
    MEM32(g_esp)=0xabcdef01;MEM32(g_esp+4)=0x55667788;
    CHECK(!strcmp(original_callback_kernel_call(119),
                  "original kernel service requires an ISR/DPC callback"));
    usb_device_callback_depth=1;
    CHECK(!strcmp(original_callback_kernel_call(125),
                  "original ISR/DPC kernel service requires qualification"));
    CHECK(!strcmp(original_callback_kernel_call(49),
                  "original ISR/DPC kernel service requires qualification"));
    CHECK(g_esp==0x18000 && g_eax==0x12345678 && g_ecx==2 &&
          MEM32(0x18000)==0xabcdef01 && MEM32(0x18004)==0x55667788);
    MEM32(g_esp+4)=0x19b00;MEM32(g_esp+8)=0;MEM32(g_esp+12)=1;
    MEM32(0x19b00)=0xaabbccdd;
    CHECK(!strcmp(original_callback_kernel_call(145),
                  "original ISR/DPC Wait=TRUE requires scheduler handoff"));
    CHECK(g_esp==0x18000 && g_eax==0x12345678 && MEM32(0x19b00)==0xaabbccdd);
    g_esp=0xfffffffcu;
    CHECK(!strcmp(original_callback_kernel_call(119),
                  "original ISR/DPC kernel call frame wraps"));
    CHECK(g_esp==0xfffffffcu && g_eax==0x12345678);
    usb_device_callback_depth=0;
    fprintf(stderr,"PASS: unqualified callback services and wrapped frames stop before caller/device mutation\n");
}
#endif
