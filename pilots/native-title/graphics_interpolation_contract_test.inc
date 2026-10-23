static void test_graphics_interpolation_service(void)
{
#ifdef PILOT_D3D_PHOBOS
    GRAPHICS_TEST_REQUIRE("cooperative presentation",PILOT_D3D_D3DDevice_Swap);
    CHECK(pilot_gfx_interpolation_rate()==60);
    CHECK(!pilot_gfx_clear(3,0xffc02040,1,0));
    uint32_t flags=0;call_graphics(PILOT_D3D_D3DDevice_Swap,&flags);
    CHECK(graphics_interpolation_thread);
    unsigned char before[64*64*4],after[64*64*4];
    CHECK(!pilot_gfx_readback(before,sizeof(before)));
    CHECK(before[0]==0xc0 && before[1]==0x20 && before[2]==0x40);
    AcquireSRWLockExclusive(&graphics_lock);
    /* Quiesce the real worker under renderer ownership so a new legitimate
     * deadline cannot race the one-shot assertions below. This also proves
     * that stopping/joining never needs this owner to release its lock. */
    CHECK(SetEvent(graphics_interpolation_stop));
    CHECK(WaitForSingleObject(graphics_interpolation_thread,2000)==WAIT_OBJECT_0);
    struct PilotInterpolationStats first,last;pilot_gfx_interpolation_stats(&first);
    CHECK(first.sources && first.history);
    int saved=graphics_interpolation_cooperative_enabled;
    uint64_t delivered=graphics_interpolation_observation.cooperative_presents;
    /* Model an actual missed worker tick while the same renderer lock is
     * occupied. The owner consumes it without waiting for that worker. */
    graphics_interpolation_cooperative_enabled=1;
    fenv_t original_fp;CHECK(!fegetenv(&original_fp) && !fesetround(FE_DOWNWARD));
    CHECK(!feraiseexcept(FE_INEXACT));int original_flags=fetestexcept(FE_ALL_EXCEPT);
    InterlockedExchange(&graphics_interpolation_due,1);
    graphics_interpolation_service();
    CHECK(fegetround()==FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT)==original_flags);
    CHECK(!fesetenv(&original_fp));
    CHECK(!graphics_interpolation_due && graphics_interpolation_observation.cooperative_presents==delivered+1);
    pilot_gfx_interpolation_stats(&last);
    CHECK(last.sources==first.sources && last.presents==first.presents+1);
    graphics_interpolation_service();
    CHECK(graphics_interpolation_observation.cooperative_presents==delivered+1);
    /* Neither a baseline diagnostic nor a native DPC consumes the request. */
    graphics_interpolation_cooperative_enabled=0;InterlockedExchange(&graphics_interpolation_due,1);
    graphics_interpolation_service();CHECK(graphics_interpolation_due);
    graphics_interpolation_cooperative_enabled=1;
    unsigned dpc=dispatch_in_dpc;dispatch_in_dpc=1;
    graphics_interpolation_service();CHECK(graphics_interpolation_due);
    dispatch_in_dpc=dpc;graphics_interpolation_service();
    CHECK(!graphics_interpolation_due && graphics_interpolation_observation.cooperative_presents==delivered+2);
    /* A host presentation cannot add fullscreen samples to a guest query.
     * Retain its pending tick until End instead of dropping that request. */
    CHECK(!pilot_gfx_visibility_begin());InterlockedExchange(&graphics_interpolation_due,1);
    graphics_interpolation_service();
    CHECK(graphics_interpolation_due && graphics_interpolation_observation.cooperative_presents==delivered+2);
    CHECK(!pilot_gfx_visibility_end(77));graphics_interpolation_service();
    CHECK(!graphics_interpolation_due && graphics_interpolation_observation.cooperative_presents==delivered+3);
    CHECK(!pilot_gfx_readback(after,sizeof(after)) && !memcmp(before,after,sizeof(before)));
    uint32_t samples=0xdeadbeef;uint64_t stamp=0;
    CHECK(!pilot_gfx_visibility_result(77,&samples,&stamp) && samples==0);
    /* Closing while the owner holds the lock must retain the worker's
     * nonblocking/join-safe contract and clear any outstanding tick. */
    InterlockedExchange(&graphics_interpolation_due,1);graphics_interpolation_close();
    CHECK(!graphics_interpolation_thread && !graphics_interpolation_due);
    graphics_interpolation_cooperative_enabled=saved;
    ReleaseSRWLockExclusive(&graphics_lock);
    fprintf(stderr,"PASS cooperative interpolation: actual GPU present, unchanged source pixels/count, one-shot request, zero guest query samples, baseline/DPC guards and close under renderer ownership\n");
#else
    fprintf(stderr,"SKIP: interpolation requires the qualified PHOBOS backend\n");exit(77);
#endif
}
