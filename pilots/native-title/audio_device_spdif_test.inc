/* Native shared-engine digital ownership and separate capture. Included by
 * the existing offline fixture; no title, host playback or Xbox connection. */
static int spdif_contract(const char *mode,const char *directory) {
    CHECK(directory);
    PilotPcmCapture *digital=nullptr,*analog=nullptr;
    const std::string path=std::string(directory)+"/"+mode+"-"+std::to_string(GetCurrentProcessId())+".bin";
    CHECK(!pilot_spdif_capture_create(&digital,path.c_str(),!strcmp(mode,"spdif-capacity")?1024:16384,32));
    if(!strcmp(mode,"spdif-misbound")) {
        CHECK(!pilot_audio_device_lle_open(pilot_pcm_capture_consume,digital));
        CHECK(pilot_pcm_capture_bind(digital));
        CHECK(pilot_audio_device_lle_consumer_attached(digital) && !pilot_audio_device_lle_spdif_consumer_attached(digital));
        CHECK(pilot_pcm_capture_destroy(digital)); // wrong kind must not free a live callback context
        CHECK(!pilot_audio_device_lle_close());CHECK(pilot_pcm_capture_finish(digital,1));
        CHECK(GetFileAttributesA(path.c_str())==INVALID_FILE_ATTRIBUTES);CHECK(!pilot_pcm_capture_destroy(digital));
        puts("PASS rejected cross-kind binding retains capture until every registered route is retired");return 0;
    }
    if(!strcmp(mode,"spdif-rollback")) {
        CHECK(!pilot_io_device_mmio_bind(0xfec00000,0x1000,dummy));
        CHECK(pilot_audio_device_lle_open_outputs(consume,nullptr,pilot_spdif_capture_consume,digital));
        PilotAudioDeviceState state;pilot_audio_device_lle_state(&state);
        CHECK(!state.owned && !state.work_binding && !pilot_audio_device_lle_spdif_consumer_attached(digital));
        CHECK(!pilot_pcm_capture_destroy(digital));
        CHECK(!pilot_io_device_mmio_bind(0xfec00000,0x1000,nullptr));
        puts("PASS digital open rollback leaves no callback into destroyed capture");return 0;
    }
    const bool observe=!strcmp(mode,"spdif-observation") || !strcmp(mode,"spdif-observation-failure");
    const bool positive=!strcmp(mode,"spdif-positive") || observe,absent=!strcmp(mode,"spdif-absent");
    if(positive)CHECK(!pilot_pcm_capture_create(&analog,(path+".pcm").c_str(),4096,8));
    CHECK(!pilot_audio_device_lle_open_outputs(analog?pilot_pcm_capture_consume:consume,analog,
        absent?nullptr:pilot_spdif_capture_consume,digital));
    if(!absent)CHECK(!pilot_pcm_capture_bind(digital));
    if(analog)CHECK(!pilot_pcm_capture_bind(analog));
    if(observe) {
        CHECK(!pilot_audio_device_lle_trace_start(nullptr));
        CHECK(!pilot_audio_device_lle_trace_start((path+".stages").c_str()));
        CHECK(pilot_audio_device_lle_trace_start((path+".duplicate").c_str()));
    }
    CHECK(pilot_audio_device_lle_spdif_consumer_attached(digital)==!absent);
    CHECK(!pilot_audio_device_lle_consumer_attached(digital));
    if(!strcmp(mode,"spdif-kind") || !strcmp(mode,"spdif-format")) {
        const uint8_t bytes[4]={1,2,3,4};
        CHECK(!(!strcmp(mode,"spdif-kind")?pilot_pcm_capture_consume(digital,bytes,4):
            pilot_spdif_capture_consume(digital,bytes,4,0)));
        uint64_t count=9,stamps=9;pilot_pcm_capture_view(digital,nullptr,&count,nullptr,&stamps);
        CHECK(!count && !stamps && pilot_pcm_capture_error(digital));
        CHECK(!pilot_audio_device_lle_close());CHECK(pilot_pcm_capture_finish(digital,1));
        CHECK(GetFileAttributesA(path.c_str())==INVALID_FILE_ATTRIBUTES);
        CHECK(!pilot_pcm_capture_destroy(digital));
        puts("PASS digital capture rejects mixed stream kind or unknown format before acceptance");return 0;
    }
    uint32_t allocation=16384,va=call(165,&allocation),pa=call(173,&va);CHECK(va && pa);
    memset(guest+va,0,allocation);
    word(va)=pa+4096;word(va+4)=0xc0000200;
    word(va+256)=pa+8192;word(va+260)=0xc0000400;
    word(va+264)=pa+12288;word(va+268)=0xc0000800;
    for(unsigned i=0;i<1024;++i)guest[va+4096+i]=uint8_t(i*17+1);
    for(unsigned i=0;i<2048;++i)guest[va+8192+i]=uint8_t(i*31+7);
    for(unsigned i=0;i<4096;++i)guest[va+12288+i]=uint8_t(i*43+29);
    auto read=[](uint32_t address,unsigned size=1){uint32_t value=0;CHECK(!pilot_audio_device_lle_access(address,size,0,&value));return value;};
    access(0xfec0017c,4,0x02000000);access(0xfec00170,4,pa+256);access(0xfec00175,1,1);
    if(absent) {
        uint32_t run=0x1d;const char *error=pilot_audio_device_lle_access(0xfec0017b,1,1,&run);
        CHECK(error && strstr(error,"real SPDIF consumer"));
        PilotAudioDeviceState state;pilot_audio_device_lle_state(&state);
        CHECK(!state.dma_reads && !state.spdif_words && !state.spdif_descriptors);
        CHECK(!pilot_audio_device_lle_close());CHECK(!pilot_pcm_capture_destroy(digital));call(171,&va);
        puts("PASS absent native digital consumer rejects RUN before descriptor DMA");return 0;
    }
    access(0xfec0017b,1,0x1d);
    if(positive){access(0xfec00110,4,pa);access(0xfec0011b,1,0x1d);}
    CHECK(!pilot_usb_pit_time(66000));CHECK(!pilot_audio_device_lle_sample());
    PilotAudioDeviceState state;pilot_audio_device_lle_state(&state);
    CHECK(state.spdif_words==512 && !state.spdif_descriptors && state.gp_held && state.ep_held);
    const uint8_t *data=nullptr;const PilotPcmCaptureStamp *stamps=nullptr;uint64_t bytes=0,count=0;
    pilot_pcm_capture_view(digital,&data,&bytes,&stamps,&count);
    CHECK(bytes==1024 && count==1 && !memcmp(data,guest+va+8192,1024));
    CHECK(stamps[0].reserved==0x02000000 && stamps[0].codec_frame==11 && stamps[0].observed_pit_ticks==66000);
    if(observe) {
        CHECK(!pilot_audio_device_observation_start(nullptr,nullptr,nullptr));
        CHECK(!pilot_audio_device_observation_start("",nullptr,nullptr));
        for(const char *bad:{"0","-1","+1"," 1","1x","3600001","18446744073709551616"})
            CHECK(pilot_audio_device_observation_start(bad,analog,digital));
        CHECK(pilot_audio_device_observation_start("1",nullptr,digital));
        CHECK(pilot_audio_device_observation_start("1",digital,analog));
        CHECK(!pilot_audio_device_observation_start("1",analog,digital));
        CHECK(pilot_audio_device_observation_start("1",analog,digital));
        if(!strcmp(mode,"spdif-observation-failure"))pilot_pcm_capture_test_fail(analog,2);
        ticks=66000;CHECK(!pilot_io_device_clock_bind(clock_input,nullptr));
        Sleep(20); // Exceed GetTickCount64 granularity; guest clock stays fixed.
        uint32_t result=0;const char *error=pilot_io_device_lle(103,guest,nullptr,&result);
        if(error)fprintf(stderr,"observation return: %s\n",error);
        CHECK(!strcmp(mode,"spdif-observation-failure") && error && strstr(error,"partial capture write failed"));
        PilotAudioDeviceState after;pilot_audio_device_lle_state(&after);
        CHECK(after.owned && after.clock_ticks==66000 && after.pcm_words==512 && after.spdif_words==512);
        CHECK(pilot_audio_device_lle_consumer_attached(analog) && pilot_audio_device_lle_spdif_consumer_attached(digital));
        CHECK(!pilot_pcm_capture_error(digital));
        puts("PASS failed PCM persistence still finalizes digital prefix, retains original owners and supplies no work or time");return 0;
    }
    if(!strcmp(mode,"spdif-paused") || !strcmp(mode,"spdif-capacity")) {
        if(!strcmp(mode,"spdif-paused"))access(0xfec0017b,1,0);
        CHECK(!pilot_usb_pit_time(72000));
        if(!strcmp(mode,"spdif-paused"))CHECK(!pilot_audio_device_lle_sample());
        else CHECK(pilot_audio_device_lle_sample());
        PilotAudioDeviceState after;pilot_audio_device_lle_state(&after);
        CHECK(after.spdif_words==512 && !after.spdif_descriptors);
        pilot_pcm_capture_view(digital,&data,&bytes,&stamps,&count);
        CHECK(bytes==1024 && count==1 && !memcmp(data,guest+va+8192,1024));
        CHECK(pilot_audio_device_lle_close());CHECK(pilot_pcm_capture_destroy(digital));
        CHECK(pilot_audio_device_lle_spdif_consumer_attached(digital));
        pilot_pcm_capture_finish(digital,0);CHECK(GetFileAttributesA(path.c_str())==INVALID_FILE_ATTRIBUTES);
        puts("PASS paused/rejected digital output retains original DMA owner and unchanged accepted prefix");return 0;
    }
    CHECK(positive && state.pcm_words==512 && state.descriptors==1);
    // This mixed-stream fixture captures exactly one analog frame. The
    // hardware ring repeats, so pause it explicitly while retaining its IRQ.
    access(0xfec0011b,1,0x1c);
    CHECK(!pilot_usb_pit_time(96000));CHECK(!pilot_audio_device_lle_sample());
    CHECK(read(0xfec00176)==12 && read(0xfec00178,2)==1024 && !read(0xfec00174) && !read(0xfec0017a));
    CHECK((read(0xfec00130,4)&0x50)==0x50);
    access(0xfec00176,1,0xc);CHECK((read(0xfec00130,4)&0x50)==0x40);
    access(0xfec00116,1,0xc);CHECK(!(read(0xfec00130,4)&0x50));
    CHECK(!pilot_usb_pit_time(102000));CHECK(!pilot_audio_device_lle_sample());
    access(0xfec0017b,1,0);CHECK(read(0xfec00178,2)==512 && read(0xfec00176)==1);
    CHECK(!pilot_usb_pit_time(114000));CHECK(!pilot_audio_device_lle_sample());
    CHECK(read(0xfec00178,2)==512);access(0xfec0017b,1,0x1d);
    CHECK(!pilot_usb_pit_time(120000));CHECK(!pilot_audio_device_lle_sample());
    CHECK(read(0xfec00174)==1 && read(0xfec0017a)==1 && read(0xfec00178,2)==2048);
    access(0xfec0017b,1,0);access(0xfec0017b,1,2);CHECK(!read(0xfec00170,4) && read(0xfec00176)==1);
    access(0xfec0017c,4,0x02000002);access(0xfec00170,4,pa+256);access(0xfec00175,1,1);access(0xfec0017b,1,0x1d);
    CHECK(!pilot_usb_pit_time(156000));CHECK(!pilot_audio_device_lle_sample());
    std::vector<uint8_t> ring(guest+va+8192,guest+va+8192+2048);
    ring.insert(ring.end(),guest+va+12288,guest+va+16384);
    std::vector<uint8_t> expected=ring;expected.insert(expected.end(),guest+va+8192,guest+va+8192+2048);
    expected.insert(expected.end(),ring.begin(),ring.end());
    pilot_pcm_capture_view(digital,&data,&bytes,&stamps,&count);
    CHECK(bytes==14336 && count==14 && !memcmp(data,expected.data(),expected.size()));
    pilot_audio_device_lle_state(&state);CHECK(state.spdif_words==7168 && state.spdif_descriptors==5 && state.pcm_words==512);
    for(unsigned i=0;i<14;++i) {
        const uint64_t frame=10+(i<7?i+1:i+3);
        CHECK(stamps[i].codec_frame==frame && stamps[i].observed_pit_ticks==frame*6000 &&
            stamps[i].byte_offset==i*1024 && stamps[i].bytes==1024 &&
            stamps[i].reserved==(i<8?0x02000000u:0x02000002u));
    }
    access(0xfec0011b,1,0);access(0xfec0011b,1,2);access(0xfec0017b,1,0);access(0xfec0017b,1,2);
    CHECK(!pilot_audio_device_lle_close());CHECK(!pilot_audio_device_lle_spdif_consumer_attached(digital));
    CHECK(!pilot_pcm_capture_finish(digital,1));CHECK(!pilot_pcm_capture_finish(analog,1));
    FILE *file=fopen(path.c_str(),"rb");CHECK(file);PilotPcmCaptureHeader header{};
    CHECK(fread(&header,sizeof(header),1,file)==1 && !memcmp(header.magic,"XLLESPD1",8) &&
        header.version==1 && header.header_bytes==sizeof(header) && !header.status && header.pcm_bytes==bytes && header.stamp_count==count);
    std::vector<PilotPcmCaptureStamp> saved(size_t(count),PilotPcmCaptureStamp{});
    CHECK(fread(saved.data(),sizeof(saved[0]),saved.size(),file)==saved.size() && !memcmp(saved.data(),stamps,saved.size()*sizeof(saved[0])));
    std::vector<uint8_t> captured(size_t(bytes),0);CHECK(fread(captured.data(),1,captured.size(),file)==captured.size() && captured==expected);
    PilotPcmCaptureFooter footer{};CHECK(fread(&footer,sizeof(footer),1,file)==1 && !memcmp(footer.magic,"XLLEEND1",8) &&
        footer.pcm_bytes==bytes && footer.stamp_count==count && !footer.status && fgetc(file)==EOF);CHECK(!fclose(file));
    capture_readback(path+".pcm",1024,1,0,guest+va+4096);
    CHECK(!pilot_pcm_capture_destroy(digital));CHECK(!pilot_pcm_capture_destroy(analog));call(171,&va);
    puts("PASS native cyclic SPDIF: separate exact bytes/format captures, shared IRQ, catch-up timestamps, pause/reset/reuse and retired ownership");return 0;
}
