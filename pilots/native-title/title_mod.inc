#include "title_mod.h"
#include <fenv.h>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
#ifdef PILOT_TITLE_MODS
static const struct PilotTitleMod *title_mod;
static int title_mod_access(uint32_t address,void *data,uint32_t bytes,int write)
{
    if(!data || !bytes || bytes>65536 || address>=0x04000000u || bytes>0x04000000u-address)return 0;
    uintptr_t current=(uintptr_t)(memory+address),end=current+bytes;
    while(current<end) {
        MEMORY_BASIC_INFORMATION info;
        if(!VirtualQuery((void*)current,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
           (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return 0;
        DWORD protection=info.Protect&255;
        if(protection==PAGE_EXECUTE)return 0;
        if(write && protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY)return 0;
        uintptr_t next=(uintptr_t)info.BaseAddress+info.RegionSize;
        if(next<=current)return 0;current=next;
    }
    if(write)memcpy(memory+address,data,bytes);else memcpy(data,memory+address,bytes);
    return 1;
}
static int title_mod_read(uint32_t address,void *data,uint32_t bytes)
{return title_mod_access(address,data,bytes,0);}
static int title_mod_write(uint32_t address,const void *data,uint32_t bytes)
{return title_mod_access(address,(void*)data,bytes,1);}
#include "title_functions.inc"
static void title_mod_initialize(void)
{
    static const struct PilotTitleModHost host={PILOT_TITLE_MOD_ABI,PILOT_XBE_SHA256,
        title_mod_read,title_mod_write,pilot_gfx_presentation,pilot_gfx_client_size,pilot_gfx_frame_shape,pilot_gfx_visual_mods,pilot_gfx_frame_scope,
        title_mod_invoke,pilot_gfx_radial_blur,media_resident_configure,media_resident_ready,dispatch_monotonic};
    title_mod=pilot_title_mod_open(&host);
    if(title_mod && title_mod->abi!=PILOT_TITLE_MOD_ABI)stop("title mod ABI mismatch",0);
}
#endif
