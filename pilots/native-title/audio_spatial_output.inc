/* Original SDK calculation views live in private guest-addressable scratch.
 * Only the immutable host coefficient/send snapshot enters the audio callback.
 * Source PCM, cursor, looping and notification ownership remain unchanged. */
#ifdef PILOT_AUDIO_SPATIAL_OUTPUT
static void audio_spatial_default_mix(struct PilotAudioBuffer *b)
{
    b->mix_count=pilot_audio_spatial_default_mix_descriptor[0];
    if(b->mix_count>8)stop("invalid original spatial default routing",b->mix_count);
    for(unsigned i=0;i<b->mix_count;++i) {
        uint32_t bin=pilot_audio_spatial_default_mix_pairs[i*2];
        if(bin>=32)stop("invalid original spatial default bin",bin);
        b->mix_bins[i]=bin;b->mix_volumes[bin]=(int32_t)pilot_audio_spatial_default_mix_pairs[i*2+1];
    }
}
static void audio_spatial_initialize(struct PilotAudioBuffer *b)
{
    memcpy(b->spatial_cache.pending,pilot_audio_spatial_default_source,76);
    memcpy(b->spatial_cache.pending+19,pilot_audio_spatial_default_source_i3dl2,36);
    memcpy(b->spatial_cache.applied,b->spatial_cache.pending,112);
    b->spatial_cache.known=b->spatial_cache.applied_known=0x0fffffffu;
    /* Preserve setters issued before the first 3D buffer, including a genuinely
     * deferred listener update. Supply only previously unknown SDK defaults. */
    for(unsigned i=0;i<16;++i) {
        if(!(audio_listener.known&(1u<<i)))audio_listener.pending[i]=pilot_audio_spatial_default_listener[i];
        if(!(audio_listener.applied_known&(1u<<i)))audio_listener.applied[i]=pilot_audio_spatial_default_listener[i];
    }
    audio_listener.known=audio_listener.applied_known=0xffff;
    audio_spatial_default_mix(b);
}
static void audio_spatial_retire(struct PilotAudioBuffer *b)
{
    if(b->spatial_workspace) {
        uint32_t zero=0;
        if(vm_free(&b->spatial_workspace,&zero,MEM_RELEASE))stop("spatial workspace release failed",b->base);
        b->spatial_workspace=0;
    }
}
static uint32_t audio_spatial_word(float value)
{uint32_t word;memcpy(&word,&value,4);return word;}
static float audio_spatial_pole(int32_t volume,float reference)
{
    if(volume>=0)return 0;
    double g=pow(10.0,volume/2000.0),square=g*g;
    double t=(1-square*cos(6.283185307179586*reference/48000))/(1-square);
    double pole=1/(t+sqrt(fmax(0,t*t-1)));
    return (float)fmin(.999999,fmax(0,pole));
}
static int32_t audio_spatial_publish(struct PilotAudioBuffer *b,void *host)
{
    if(!host)return 0;
    if(!b->spatial_workspace) {
        uint32_t bytes=4096;
        if(vm_allocate(&b->spatial_workspace,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))return (int32_t)0x8007000e;
    }
    uint32_t w=b->spatial_workspace,l=w,lc=w+0x40,s=w+0x80,c=w+0x100;
    uint32_t src=w+0x180,driver=w+0x200,voice=w+0x280,gains=w+0x380,pitch=w+0x3c0;
    uint32_t reverb=w+0x400,rp=w+0x420;
    memset(memory+w,0,4096);
    memcpy(memory+l,audio_listener.applied,64);memcpy(memory+s,b->spatial_cache.applied,112);
    memcpy(memory+rp,audio_i3dl2.known?audio_i3dl2.applied:pilot_audio_spatial_default_listener_i3dl2,48);
    MEM32(reverb)=rp;
    static int initialized;
    if(!initialized){native_cpu_pure_call(PILOT_AUDIO_SPATIAL_TABLE_INITIALIZE,0,0,NULL);initialized=1;}
    if(MEM32(PILOT_AUDIO_SPATIAL_INDIRECT_0_SLOT)!=PILOT_AUDIO_SPATIAL_INDIRECT_0_TARGET)
        stop("original spatial lookup initializer changed",PILOT_AUDIO_SPATIAL_INDIRECT_0_SLOT);
    uint32_t arg[2]={l};native_cpu_pure_call(PILOT_AUDIO_SPATIAL_LISTENER_INITIALIZE,lc,1,arg);
    unsigned offset,size;uint32_t speakers=0;
    if(!settings_user_field(9,&offset,&size) || size!=4)stop("spatial speaker setting layout",9);
    AcquireSRWLockShared(&eeprom_lock);
    memcpy(&speakers,(settings_cache_valid?settings_cache:eeprom)+offset,4);
    ReleaseSRWLockShared(&eeprom_lock);
    MEM8(lc+0x10)=((speakers&0xffff)==2 || (speakers&0x80010000u)==0x10000u);
    arg[0]=0xff;native_cpu_pure_call(PILOT_AUDIO_SPATIAL_LISTENER_UPDATE,lc,1,arg);
    arg[0]=lc;arg[1]=s;native_cpu_pure_call(PILOT_AUDIO_SPATIAL_CALCULATOR_INITIALIZE,c,2,arg);
    struct PilotAudioSpatialState output={0};output.taps[0][0]=output.taps[1][0]=1;
    unsigned mode=MEM32(s+0x3c);
    if(mode>2)stop("unqualified spatial source mode",mode);
    if(mode!=2) {
        const uint32_t roots[]={PILOT_AUDIO_SPATIAL_RELATIVE,PILOT_AUDIO_SPATIAL_HRTF,PILOT_AUDIO_SPATIAL_DISTANCE,
            PILOT_AUDIO_SPATIAL_CONE_ANGLE,PILOT_AUDIO_SPATIAL_CONE_GAIN,PILOT_AUDIO_SPATIAL_WEIGHTS,PILOT_AUDIO_SPATIAL_DOPPLER};
        arg[0]=c;for(unsigned i=0;i<sizeof(roots)/sizeof(roots[0]);++i)native_cpu_pure_call(roots[i],0,1,arg);
        arg[0]=reverb;arg[1]=s+0x4c;native_cpu_pure_call(PILOT_AUDIO_SPATIAL_SOURCE_I3DL2_INITIALIZE,src,2,arg);
        double distance;memcpy(&distance,memory+c+0x18,8);
        arg[0]=audio_spatial_word((float)distance);native_cpu_pure_call(PILOT_AUDIO_SPATIAL_SOURCE_I3DL2_CALCULATE,src,1,arg);
        float azimuth;memcpy(&azimuth,memory+c+0x20,4);
        uint32_t records[2]={MEM32(c+0x44),MEM32(c+0x48)};
        for(unsigned ear=0;ear<2;++ear) {
            const int8_t *record=audio_pointer(records[ear],32);float sum=0;
            for(unsigned k=0;k<31;++k){output.taps[ear][k]=record[k]/128.0f;sum+=fabsf(output.taps[ear][k]);}
            if(sum>0)for(unsigned k=0;k<31;++k)output.taps[ear][k]/=sum;
        }
        output.delay=azimuth>=0?(float)MEM8(records[0]+31):-(float)MEM8(records[1]+31);
        output.delay=fminf(42,fmaxf(-42,output.delay));
        float reference;memcpy(&reference,memory+rp+0x2c,4);
        if(!isfinite(reference) || reference<=0 || reference>=24000)stop("spatial HF reference outside consumer range",rp);
        output.direct_pole=audio_spatial_pole((int32_t)MEM32(src+4),reference);
        output.room_pole=audio_spatial_pole((int32_t)MEM32(src+12),reference);
    }
    MEM8(driver+0x64)=1;MEM32(driver+0x70)=c;MEM32(driver+0x74)=src;MEM32(driver+0x78)=voice;
    MEM32(voice+8)=0x10;MEM32(voice+0x1c)=(uint32_t)((int32_t)b->volume-(int32_t)b->headroom);
    MEM32(voice+0x24)=b->mix_count;MEM32(voice+0xb4)=s;
    for(unsigned i=0;i<b->mix_count;++i)MEM8(voice+0x28+i)=(uint8_t)b->mix_bins[i];
    memcpy(memory+voice+0x30,b->mix_volumes,128);
    arg[0]=gains;native_cpu_pure_call(PILOT_AUDIO_SPATIAL_GAIN,driver,1,arg);
    uint32_t a=MEM32(gains),d=MEM32(gains+12),e=MEM32(gains+24);
    unsigned attenuation[]={a>>4&4095,a>>20,d>>4&4095,d>>20,e>>4&4095,e>>20,
        (a&15)|((d&15)<<4)|((e&15)<<8),((a>>16)&15)|(((d>>16)&15)<<4)|(((e>>16)&15)<<8)};
    for(unsigned i=0;i<b->mix_count;++i) {
        unsigned bin=b->mix_bins[i],channel=bin==6?0:bin==7?1:bin==8?2:bin==9?3:bin==10?4:5;
        output.matrix[bin*PILOT_AUDIO_SPATIAL_CHANNELS+channel]+=attenuation[i]==4095?0:(float)pow(10.0,-(double)attenuation[i]/1280.0);
    }
    if(mode!=2 && (b->spatial&0x20000)) {
        double distance;float maximum;memcpy(&distance,memory+c+0x18,8);memcpy(&maximum,memory+s+0x38,4);
        if(distance>=maximum)memset(output.matrix,0,sizeof(output.matrix));
    }
    arg[0]=audio_spatial_word(b->frequency/48000.0f);
    MEM32(voice+0x18)=native_cpu_pure_call(PILOT_AUDIO_SPATIAL_FREQUENCY_PITCH,0,1,arg);
    arg[0]=pitch;native_cpu_pure_call(PILOT_AUDIO_SPATIAL_PITCH,driver,1,arg);
    output.frequency_ratio=(float)exp2((int16_t)(MEM32(pitch)>>16)/4096.0);
    return pilot_audio_buffer_spatial(host,&output);
}
#endif
