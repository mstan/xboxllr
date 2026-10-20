/* Optional private failure evidence. Capture readable low guest RAM and a
 * bounded caller-stack window; never commit or publish the captured payload.
 * The disabled path has no allocations or file writes. */
static void stop_capture(void)
{
    const char *path=getenv("XLLE_PILOT_STOP_CAPTURE");if(!path || !*path || !memory)return;
    static LONG entered;if(InterlockedCompareExchange(&entered,1,0))return;
    FILE *file=fopen(path,"wbx");if(!file){fprintf(stderr,"[STOP CAPTURE] exclusive create failed\n");return;}
    uint32_t header[]={0x50545358,1,g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_fs_base};
    int failed=fwrite(header,sizeof(header),1,file)!=1;
    uint64_t stack_low=g_esp>65536?(g_esp-65536u)&~4095u:0;
    uint64_t stack_high=((uint64_t)g_esp+65536+4095)&~4095ull;
    if(stack_high>0x100000000ull)stack_high=0x100000000ull;
    for(unsigned region=0;region<2 && !failed;++region) {
        uint64_t low=region?stack_low:0,high=region?stack_high:64ull*1024*1024;
        for(uint64_t at=low;at<high;at+=4096) {
            MEMORY_BASIC_INFORMATION info;
            if(!VirtualQuery(memory+at,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
               info.Protect&(PAGE_NOACCESS|PAGE_GUARD))continue;
            uint32_t record[]={(uint32_t)at,4096};
            if(fwrite(record,sizeof(record),1,file)!=1 || fwrite(memory+at,4096,1,file)!=1){failed=1;break;}
        }
    }
    if(fclose(file))failed=1;
    fprintf(stderr,"[STOP CAPTURE] private readable guest pages; status=%s path=%s\n",failed?"incomplete":"complete",path);
}
