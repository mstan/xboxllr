/* Xbox guest ABI adapter. Native COM objects never enter guest memory.
 * Only qualified whole entry points return; other identified driver entries
 * stop before executing against a device owned by the native renderer. */
#include "graphics_backend.h"
#include "pilot_d3d_bindings.h"
static SRWLOCK graphics_lock=SRWLOCK_INIT;
static int graphics_active;
static uint32_t graphics_parameters[17];
static uint32_t graphics_headers,graphics_data[2];
static unsigned graphics_surface_refs[2];

static void graphics_surfaces_create(void)
{
    uint32_t bytes=4096;
    if (vm_allocate(&graphics_headers,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))
        stop("graphics surface header allocation failed",0);
    uint32_t width=graphics_parameters[0],height=graphics_parameters[1];
    uint32_t pitch=(width*4+63)&~63u;
    uint32_t format=(graphics_parameters[2]==6 || graphics_parameters[2]==0x12)?0x12:0x1e;
    for (unsigned i=0;i<2;++i) {
        bytes=pitch*height;
        /* GPU residence has an owned guest VA but no direct CPU mapping.
         * Lock/readback must explicitly synchronize actual GPU contents. */
        if (vm_allocate(&graphics_data[i],0,&bytes,MEM_RESERVE,PAGE_NOACCESS))
            stop("graphics surface address reservation failed",i);
        uint32_t *header=(uint32_t*)(memory+graphics_headers+i*32);
        header[0]=0x010d0000u; header[1]=graphics_data[i]; header[2]=0;
        header[3]=0x10021u|(format<<8);
        header[4]=(width-1)|((height-1)<<12)|((pitch/64-1)<<24); header[5]=0;
    }
}
static unsigned graphics_surface_index(uint32_t address)
{
    if (address!=graphics_headers && address!=graphics_headers+32)
        stop("unowned graphics surface",address);
    return (address-graphics_headers)/32;
}
static unsigned graphics_surface_ref(unsigned index,int acquire)
{
    if (acquire && graphics_surface_refs[index]==0xffff)
        stop("surface reference count overflow",index);
    int32_t result=pilot_gfx_surface_ref(index,acquire);
    if (result<0) stop("invalid native surface ownership",index);
    graphics_surface_refs[index]=(unsigned)result;
    MEM32(graphics_headers+32*index)=0x010d0000u|(uint32_t)result;
    return (unsigned)result;
}

static void *graphics_pointer(uint32_t address,uint32_t bytes)
{
    if (!address || (uint64_t)address+bytes>0x100000000ull)
        stop("invalid graphics guest range",address);
    /* Actual committed/protected memory is enforced by host page access. */
    return memory+address;
}

void pilot_d3d_dispatch(unsigned entry)
{
    if (entry>=PILOT_D3D_COUNT) stop("invalid graphics entry",entry);
    const uint32_t *arguments=graphics_pointer(g_esp+4,pilot_d3d_calls[entry].stack_bytes);
    fprintf(stderr,"[D3D] %s return=%08x",pilot_d3d_calls[entry].name,MEM32(g_esp));
    for (unsigned i=0;i<pilot_d3d_calls[entry].stack_bytes/4;i++) fprintf(stderr," %08x",arguments[i]);
    fprintf(stderr,"\n");
    AcquireSRWLockExclusive(&graphics_lock);
    if (entry!=PILOT_D3D_Direct3D_CreateDevice && !graphics_active)
        stop("graphics entry before native creation",pilot_d3d_calls[entry].address);
    switch(entry) {
    case PILOT_D3D_Direct3D_CreateDevice: {
        if (graphics_active) stop("second native device creation",entry);
        if (arguments[0] || arguments[1]!=1 || arguments[2] || (arguments[3]&~0x40u))
            stop("unqualified graphics creation options",entry);
        uint32_t staged[17]; memcpy(staged,graphics_pointer(arguments[4],sizeof(staged)),sizeof(staged));
        fprintf(stderr,"[D3D PARAMETERS]");
        for (unsigned i=0;i<17;i++) fprintf(stderr," %08x",staged[i]);
        fprintf(stderr,"\n");
        g_eax=(uint32_t)pilot_gfx_create(staged);
        if (!(g_eax&0x80000000u)) {
            graphics_active=1; memcpy(graphics_parameters,staged,sizeof(staged));
            graphics_surfaces_create();
            MEM32(PILOT_D3D_DEVICE_GLOBAL)=PILOT_D3D_DEVICE_OBJECT;
            if (arguments[5]) MEM32(arguments[5])=PILOT_D3D_DEVICE_OBJECT;
        } else {
            MEM32(PILOT_D3D_DEVICE_GLOBAL)=0;
            if (arguments[5]) MEM32(arguments[5])=0;
        }
        break;
    }
    case PILOT_D3D_D3DDevice_GetBackBuffer2: {
        if (arguments[0]!=0 && arguments[0]!=0xffffffffu)
            stop("back buffer index outside single-buffer presentation",arguments[0]);
        unsigned index=arguments[0]==0xffffffffu?1:0;
        graphics_surface_ref(index,1); g_eax=graphics_headers+32*index; break;
    }
    case PILOT_D3D_D3DResource_AddRef:
    case PILOT_D3D_D3DResource_Release:
        g_eax=graphics_surface_ref(graphics_surface_index(arguments[0]),entry==PILOT_D3D_D3DResource_AddRef); break;
    case PILOT_D3D_D3DDevice_Release: {
        if (graphics_surface_refs[0] || graphics_surface_refs[1])
            stop("device release with outstanding surface ownership",entry);
        pilot_gfx_close(); graphics_active=0;
        uint32_t bytes;
        for (unsigned i=0;i<2;++i) {
            bytes=0;
            if (vm_free(&graphics_data[i],&bytes,MEM_RELEASE)) stop("surface VA release failed",i);
            graphics_data[i]=0;
        }
        bytes=0;
        if (vm_free(&graphics_headers,&bytes,MEM_RELEASE)) stop("surface headers release failed",0);
        graphics_headers=0; MEM32(PILOT_D3D_DEVICE_GLOBAL)=0; g_eax=0; break;
    }
    case PILOT_D3D_D3DDevice_Clear: {
        uint32_t flags=arguments[2],channels=flags&0xf0u;
        if (arguments[0] || arguments[1] || (flags&~0xf3u) || (channels && channels!=0xf0u))
            stop("unqualified graphics clear rectangle/flags",entry);
        float depth; memcpy(&depth,&arguments[4],4);
        if ((flags&1u) && !(depth>=0 && depth<=1)) stop("unqualified clear depth",arguments[4]);
        /* Xbox uses bits0/1 for Z/stencil and four bits for target channels;
         * this donor's Clear uses desktop D3D bits0/1/2. */
        uint32_t native_flags=(channels?1u:0u)|((flags&1u)?2u:0u)|((flags&2u)?4u:0u);
        g_eax=(uint32_t)pilot_gfx_clear(native_flags,arguments[3],depth,arguments[5]); break;
    }
    case PILOT_D3D_D3DDevice_SetViewport: {
        uint32_t viewport[6]; memcpy(viewport,graphics_pointer(arguments[0],24),24);
        float low,high; memcpy(&low,viewport+4,4); memcpy(&high,viewport+5,4);
        if (!viewport[2] || !viewport[3] || (uint64_t)viewport[0]+viewport[2]>graphics_parameters[0] ||
            (uint64_t)viewport[1]+viewport[3]>graphics_parameters[1] || !(low>=0 && high<=1 && low<=high))
            stop("unqualified viewport bounds",arguments[0]);
        g_eax=(uint32_t)pilot_gfx_viewport(viewport); break;
    }
    case PILOT_D3D_D3DDevice_Swap:
        if (arguments[0]) stop("unqualified graphics swap flags",arguments[0]);
        g_eax=(uint32_t)pilot_gfx_present(); break;
    default:
        stop(pilot_d3d_calls[entry].name,pilot_d3d_calls[entry].address);
    }
    ReleaseSRWLockExclusive(&graphics_lock);
    g_esp+=4+pilot_d3d_calls[entry].stack_bytes;
}
