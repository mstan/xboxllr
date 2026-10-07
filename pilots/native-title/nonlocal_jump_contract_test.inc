/* Native transfer/lifetime tests. The actual title separately executes its
 * original CRT; this fixture supplies the CRT's postcondition to the bridge. */
static void jump_test_save(uint32_t buffer)
{
    MEM32(buffer)=g_seh_ebp;
    MEM32(buffer+4)=g_ebx; MEM32(buffer+8)=g_edi; MEM32(buffer+12)=g_esi;
    MEM32(buffer+16)=g_esp-4; MEM32(buffer+20)=0x12340;
    MEM32(buffer+24)=MEM32(g_fs_base);
}

static void jump_test_finish(uint32_t buffer,uint32_t value)
{
    pilot_longjmp_begin(buffer);
    g_ebx=MEM32(buffer+4); g_edi=MEM32(buffer+8); g_esi=MEM32(buffer+12);
    g_esp=MEM32(buffer+16)+4; g_seh_ebp=MEM32(buffer);
    recomp_guest_longjmp(buffer,value);
    CHECK(0);
}

__declspec(noinline) static void jump_test_inner(uint32_t outer)
{
    g_esp=0x17000; g_seh_ebp=0x17100;
    if (setjmp(*pilot_setjmp_slot(0x19100,0x12340,0x17200))) CHECK(0);
    jump_test_save(0x19100);
    g_ebx=77; g_esi=88; g_edi=99;
    jump_test_finish(outer,0);
}

static DWORD WINAPI jump_test_thread(void *argument)
{
    (void)argument;
    for (unsigned i=0;i<PILOT_JUMP_SLOTS;++i) CHECK(!jump_slots[i].order);
    CHECK(jump_pending==NULL);
    if (setjmp(*pilot_setjmp_slot(0x19000,0x12340,0x18100))) CHECK(0);
    pilot_jump_frame_leave(0x18100);
    return 0;
}

static void test_jumps(void)
{
    g_fs_base=0x1a000; MEM32(g_fs_base)=0xffffffffu;
    g_esp=0x18000; g_seh_ebp=g_ebp=0x18080;
    g_ebx=0x11111111; g_esi=0x22222222; g_edi=0x33333333;
    int value=setjmp(*pilot_setjmp_slot(0x19000,0x12340,0x18100));
    if (!value) {
        jump_test_save(0x19000);
        HANDLE thread=CreateThread(NULL,0,jump_test_thread,NULL,0,NULL);
        CHECK(thread && WaitForSingleObject(thread,5000)==WAIT_OBJECT_0); CloseHandle(thread);
        jump_test_inner(0x19000);
    }
    CHECK(value==1 && g_esp==0x18000 && g_ebp==0x18080 && g_seh_ebp==0x18080);
    CHECK(g_ebx==0x11111111 && g_esi==0x22222222 && g_edi==0x33333333);
    unsigned active=0;
    for (unsigned i=0;i<PILOT_JUMP_SLOTS;++i) active+=!!jump_slots[i].order;
    CHECK(active==1);
    /* The same buffer may be rearmed, and all 32 return-value bits survive. */
    value=setjmp(*pilot_setjmp_slot(0x19000,0x12340,0x18100));
    if (!value) { jump_test_save(0x19000); jump_test_finish(0x19000,0x80000005u); }
    CHECK((uint32_t)value==0x80000005u);
    pilot_jump_frame_leave(0x18100);
    /* Returned decoder frames release slots even without a longjmp. */
    for (unsigned i=0;i<128;++i) {
        if (setjmp(*pilot_setjmp_slot(0x19000+i*64,0x12340,0x18100))) CHECK(0);
        pilot_jump_frame_leave(0x18100);
    }
    for (unsigned i=0;i<PILOT_JUMP_SLOTS;++i) CHECK(!jump_slots[i].order);
    uint32_t before=g_esp;
    pilot_cpu_mmx_available();
    CHECK(g_eax==1 && g_edx==1 && g_ecx==0 && g_esp==before+4);
    CHECK(g_ebx==0x11111111 && g_esi==0x22222222 && g_edi==0x33333333 && g_ebp==0x18080);
    fprintf(stderr,"PASS: real nested native jumps, zero normalization, full return value, callee saves, per-thread slots, rearm and returned-frame cleanup; configured CPU MMX query ABI\n");
}

static void test_jump_failure(const char *kind)
{
    g_fs_base=0x1a000; MEM32(g_fs_base)=0xffffffffu;
    g_esp=0x18000; g_seh_ebp=0x18080;
    if (!strcmp(kind,"missing")) pilot_longjmp_begin(0x19000);
    if (setjmp(*pilot_setjmp_slot(0x19000,0x12340,0x18100))) CHECK(0);
    jump_test_save(0x19000);
    if (!strcmp(kind,"returned")) { pilot_jump_frame_leave(0x18100); pilot_longjmp_begin(0x19000); }
    if (!strcmp(kind,"corrupt")) { MEM32(0x19014)^=4; pilot_longjmp_begin(0x19000); }
    if (!strcmp(kind,"bypass")) recomp_guest_longjmp(0x19000,1);
    if (!strcmp(kind,"unwind")) {
        pilot_longjmp_begin(0x19000); MEM32(g_fs_base)=0x1a100;
        recomp_guest_longjmp(0x19000,1);
    }
    if (!strcmp(kind,"exhausted"))
        for (unsigned i=1;i<=PILOT_JUMP_SLOTS;++i) pilot_setjmp_slot(0x19000+i*64,0x12340,0x18100);
    CHECK(0);
}
