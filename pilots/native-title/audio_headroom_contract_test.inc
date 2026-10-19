#if defined(PILOT_AUDIO_DSP) && PILOT_AUDIO_HAS_MIXBIN_HEADROOM
static void test_audio_mixbin_headroom(void)
{
    vm_ensure();uint64_t before=vm_committed(0,0x100000000ull);
    for(unsigned lifetime=0;lifetime<2;++lifetime) {
        uint32_t device=audio_test_device(),a[8]={device,0,0x102};
        /* Device state applies even to voices created after the setter. */
        CHECK(!audio_test_call(PILOT_AUDIO_IDirectSound_SetMixBinHeadroom,a));
        a[1]=31;a[2]=0xff;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSound_SetMixBinHeadroom,a));
        uint32_t iface=audio_test_buffer_format(device,19200,2,16,48000,0);
        a[0]=iface;a[1]=0;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetHeadroom,a));
        audio_test_rewrite(iface,8192,-4096);a[2]=0;a[3]=3;
        CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Play,a));
        float average[32];audio_test_observe(average,0);
        CHECK(fabs(average[0]-0.0625)<0.00002 && fabs(average[1]+0.125)<0.00002);
        for(unsigned c=2;c<32;++c)CHECK(fabs(average[c])<0.000001);
        a[0]=device;a[1]=0;a[2]=0xff;
        CHECK(!audio_test_call(PILOT_AUDIO_IDirectSound_SetMixBinHeadroom,a));
        audio_test_observe(average,1);
        CHECK(fabs(average[0]-0.25/128)<0.00002 && fabs(average[1]+0.125)<0.00002);
        a[2]=0;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSound_SetMixBinHeadroom,a));
        audio_test_observe(average,1);
        CHECK(fabs(average[0]-0.25)<0.00002 && fabs(average[1]+0.125)<0.00002);
        CHECK(pilot_audio_mixbin_headroom(32,1)<0);
        a[0]=iface;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Release,a));
        a[0]=device;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSound_Release,a));
        CHECK(vm_committed(0,0x100000000ull)==before);
    }
    puts("PASS mix-bin headroom: Xbox ABI, low three bits, future voices, live gain, independent bins, real mixer/GP output and complete lifetimes");
}
#endif
