#ifdef PILOT_SCHEDULING_LLE
/* Authored generated-body equivalents exercise the exact original153 body,
 * original connected KINTERRUPT and nested one-argument generated ABI. These
 * do not stand in for the later title's actual177ECC waveform/lifecycle proof. */
static unsigned synchronize_calls,synchronize_dpcs;
static int synchronize_expected_if;
static int synchronize_disconnected;
static DWORD WINAPI synchronize_wrong_owner(void *unused)
{
    (void)unused;uint32_t result=0x13579bdf,args[]={0x17de78,0x177ecc,0x19a00};
    CHECK(pilot_io_device_synchronize_call(memory,0x178326,args,NULL,&result));
    CHECK(result==0x13579bdf);
    CHECK(pilot_io_device_lle(103,memory,NULL,&result));
    CHECK(result==0x13579bdf);return 0;
}
static void test_synchronize_callback(void)
{
    CHECK(pilot_io_device_irql()==25 && MEM8(g_fs_base+0x24)==25);
    CHECK(cpu_interrupt_flag==synchronize_expected_if && !cpu_interrupt_shadow);
    CHECK(dispatch_in_dpc && g_fs_base==PILOT_IO_PCR_BASE);
    CHECK(MEM32(g_esp)==0x8001e1ec && MEM32(g_esp+4)==0x19a00);
    CHECK(MEM32(0x19a00)==synchronize_calls);
    ++synchronize_calls;MEM32(0x19a00)=synchronize_calls;
    const uint32_t old_sp=g_esp;
    CHECK((callback_kernel_lle_call(103,NULL,0)&255)==25);
    CHECK(g_esp==old_sp && pilot_io_device_irql()==25);
    uint32_t args[]={0x17de78,0x177ecc,0x19a00},result=0xcccccccc;
    CHECK(pilot_io_device_synchronize_call(memory,0x178326,args,NULL,&result));
    CHECK(result==0xcccccccc);
    HANDLE other=CreateThread(NULL,0,synchronize_wrong_owner,NULL,0,NULL);CHECK(other);
    CHECK(WaitForSingleObject(other,5000)==WAIT_OBJECT_0 && CloseHandle(other));
    g_eax=0x12340000+synchronize_calls;g_ecx=0xeeeeeeee;g_edx=0xdddddddd;
    g_ebp=0xbebebebe; // generated frame publication; local EBP restores on LEAVE
    g_fp_stack[0]=-12.5;g_fp_top=7;g_fp_control_word=0x27f;
    memset(&g_xmm0,0x54,sizeof(g_xmm0));memset(&g_mm0,0x76,sizeof(g_mm0));
    g_esp+=8;
}
static void test_synchronize_dpc(void)
{
    CHECK(pilot_io_device_irql()==2 && dispatch_in_dpc);++synchronize_dpcs;
    uint32_t original_sp=g_esp,original_esi=g_esi,ordinal=next_kernel_ordinal;
    g_esi=0x19a00;
    const uint32_t nonvol[]={g_ebx,g_ebp,g_esi,g_edi};
    for(int enabled=1;enabled>=0;--enabled) {
        synchronize_expected_if=enabled;cpu_interrupt_flag=enabled;pilot_io_device_set_if(enabled);
        g_esp-=16;MEM32(g_esp)=0x178326;MEM32(g_esp+4)=0x17de78;
        MEM32(g_esp+8)=0x177ecc;MEM32(g_esp+12)=g_esi;
        g_eax=0xcccccccc;
        MEM32(g_esp+8)=0x12480;
        CHECK(original_callback_kernel_call(153));
        CHECK(g_eax==0xcccccccc && g_esp==original_sp-16 && synchronize_calls==(unsigned)(1-enabled));
        MEM32(g_esp+8)=0x177ecc;
        if(synchronize_disconnected) {
            const char *error=original_callback_kernel_call(153);
            CHECK(error && strstr(error,"connected interrupt/IRQL owner"));
            CHECK(g_eax==0xcccccccc && g_esp==original_sp-16 && !synchronize_calls);
            CHECK(pilot_io_device_irql()==2 && cpu_interrupt_flag==enabled);
            g_esp=original_sp;break;
        }
        const double fp=g_fp_stack[0];const int top=g_fp_top;
        const uint16_t control=g_fp_control_word;const RecompXmm xmm=g_xmm0;const RecompMmx mm=g_mm0;
        const char *error=original_callback_kernel_call(153);
        if(error)fprintf(stderr,"synchronize original153: %s\n",error);
        CHECK(!error && g_eax==0x12340000+synchronize_calls && g_esp==original_sp);
        CHECK(g_ebx==nonvol[0] && g_ebp==nonvol[1] && g_esi==nonvol[2] && g_edi==nonvol[3]);
        CHECK(pilot_io_device_irql()==2 && MEM8(g_fs_base+0x24)==2 && cpu_interrupt_flag==enabled);
        CHECK(g_fp_stack[0]==fp && g_fp_top==top && g_fp_control_word==control);
        CHECK(!memcmp(&xmm,&g_xmm0,sizeof(xmm)) && !memcmp(&mm,&g_mm0,sizeof(mm)));
    }
    cpu_interrupt_flag=1;pilot_io_device_set_if(1);
    g_esi=original_esi;next_kernel_ordinal=ordinal;g_esp=original_sp+20;
}
static void test_synchronize_lle(int disconnected)
{
    synchronize_disconnected=disconnected;
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    commit(0x17d000,0x2000);
    commit(0x177000,0x1000);MEM8(0x177ecc)=0xc3; // never executed: generated handoff at entry
    uint32_t result=0xcccccccc,args[]={0x17de78,0x177ecc,0x19a00};
    CHECK(pilot_io_device_synchronize_call(memory,0x178326,args,NULL,&result));
    CHECK(result==0xcccccccc);
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));
    uint32_t intr[]={0x17de78,0x12400,0x98765432,0x31,25,0,0};
    event_lle_call(109,intr);
    if(!disconnected)CHECK((event_lle_call(98,intr)&255)==1);
    uint32_t dpc[]={0x19540,0x124c0,0x19a00};event_lle_call(107,dpc);
    uint32_t init[]={0x19500,1},set[]={0x19500,0,0,0x19540};
    event_lle_call(113,init);
    const uint32_t original_sp=g_esp;const double fp=3.25;
    g_fp_stack[0]=fp;g_fp_top=2;MEM32(0x19a00)=0;
    event_lle_call(149,set);
    CHECK(synchronize_dpcs==1 && synchronize_calls==(disconnected?0u:2u) && MEM32(0x19a00)==synchronize_calls);
    CHECK(!pilot_io_device_irql() && g_esp==original_sp && g_fp_stack[0]==fp && g_fp_top==2);
    fprintf(stderr,"PASS: %s\n",disconnected?"unconnected interrupt rejected before original service or callback mutation":"original153 raises/restoresIRQL, actual connected owner, generated one-argument callback, nested service, IF0/1, GPR/FP/SIMD restoration and rejected ownership/callback");
}
#endif
