/* The owned original context creator independently establishes CW027f/SW0.
 * This fixture observes the actual fresh title-thread TLS through its normal
 * creation path, with a poisoned caller and reused slots. It does not qualify
 * empty x87 tags, extended arithmetic or lazy-NPX callback transport. */
static volatile LONG thread_fp_bodies;
static void test_thread_fp_body(void)
{
    CHECK(g_fp_control_word==0x027f && g_fp_cc==0);
    CHECK(g_fp_top==0 && g_fp_cmp==0);
    for(unsigned i=0;i<8;++i)CHECK(g_fp_stack[i]==0.0);
    RecompXmm xmm[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};
    unsigned char zero[sizeof(xmm)]={0};CHECK(!memcmp(xmm,zero,sizeof(xmm)));
    /* Leave modified child state behind: a later fresh thread must start from
     * the original defaults rather than the retired execution's state. */
    g_fp_control_word=0x0c7f;g_fp_cc=0x4500;g_fp_top=3;g_fp_stack[0]=42.0;
    InterlockedIncrement(&thread_fp_bodies);
}
static void test_thread_fp(void)
{
    CHECK(g_fp_control_word==0x027f && g_fp_cc==0);
    g_fp_control_word=0x087f;g_fp_cc=0x0100;g_fp_top=5;g_fp_stack[0]=17.0;
    for(unsigned lifetime=0;lifetime<4;++lifetime) {
        uint32_t args[]={0x19000,0,0x10000,0,0,0,0,0,0,0x12640};
        call_kernel(255,args,10);CHECK(!g_eax);
        uint32_t handle=MEM32(0x19000);HANDLE host=NULL;
        AcquireSRWLockShared(&thread_lock);
        struct PilotThread *thread=thread_from_handle(handle,&host);
        CHECK(thread && host);
        ReleaseSRWLockShared(&thread_lock);
        CHECK(WaitForSingleObject(host,2000)==WAIT_OBJECT_0);
        CHECK(thread_fp_bodies==(LONG)lifetime+1);
        CHECK(g_fp_control_word==0x087f && g_fp_cc==0x0100 &&
              g_fp_top==5 && g_fp_stack[0]==17.0);
        call_kernel(187,&handle,1);CHECK(!g_eax);
    }
    fprintf(stderr,"PASS: original initial x87 control/status, four fresh title thread contexts, child/caller isolation\n");
}
