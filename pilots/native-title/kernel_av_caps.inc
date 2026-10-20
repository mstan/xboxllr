/* Software-only capability query. Mode changes/encoder writes stay guarded. */
static void kernel_av_query(void)
{
    if((uint64_t)g_esp+20>UINT64_C(0x100000000))stop("invalid AV query frame",g_esp);
    uint32_t base=MEM32(g_esp+4),option=MEM32(g_esp+8),parameter=MEM32(g_esp+12),output=MEM32(g_esp+16);
    if(base || option!=6 || parameter)stop("unqualified native AV option",option);
    if(!output || output>UINT32_MAX-3)stop("invalid AV capability output",output);
    if(!have_eeprom)stop("AV capabilities require EEPROM input",option);
    unsigned char snapshot[256];
    AcquireSRWLockExclusive(&eeprom_lock);
    if(!settings_cache_valid) {
        memcpy(settings_cache,eeprom,sizeof(settings_cache));
        for(unsigned i=0;i<sizeof(settings_cache);++i)
            if(settings_cache[i]!=0xff){settings_cache_valid=1;break;}
    }
    memcpy(snapshot,settings_cache,sizeof(snapshot));
    ReleaseSRWLockExclusive(&eeprom_lock);
    uint32_t capabilities;
    if(!pilot_av_capabilities(snapshot,&capabilities))stop("AV query requires valid factory/user settings",option);
    MEM32(output)=capabilities;
    if(pilot_verbose)fprintf(stderr,"[AV CAPS NATIVE] composite capabilities=%08x\n",capabilities);
    /* AvSendTVEncoderOption returns VOID. Do not invent an EAX status. */
    g_esp+=20;
}
