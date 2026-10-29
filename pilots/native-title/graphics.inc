/* Xbox guest ABI adapter. Native COM objects never enter guest memory.
 * Only qualified whole entry points return; other identified driver entries
 * stop before executing against a device owned by the native renderer. */
#include "graphics_backend.h"
#include "pilot_d3d_bindings.h"
#ifndef PILOT_D3D_CREATE_BEHAVIOR_MASK
/* Compatibility with already generated headers for the original adapter. */
#define PILOT_D3D_CREATE_BEHAVIOR_MASK 0x40u
#endif
#include "graphics_profile.inc"
static SRWLOCK graphics_lock=SRWLOCK_INIT;
struct GraphicsResourceRetirement {uint32_t header,data,owner,active;uint64_t serial;struct GraphicsResourceRetirement *next;};
static struct GraphicsResourceRetirement *graphics_retirements;
static unsigned graphics_resource_retiring;
static void graphics_original_retire_check(uint32_t header,uint32_t data,unsigned borrowers);
static void graphics_original_retire_queue(uint32_t header,uint32_t data,uint64_t serial,unsigned borrowers);
static int graphics_resource_is_retiring(uint32_t header)
{
    for(struct GraphicsResourceRetirement *r=graphics_retirements;r;r=r->next)if(r->header==header)return 1;
    return 0;
}
#include "graphics_interpolation.inc"
#ifdef PILOT_SCHEDULING_LLE
/* Participating whole components acquire the real original recursive RTL
 * object before any renderer/VM host lock. A preempting graphics thread can
 * then block in the original kernel, leaving its native worker lock-free. */
static uint32_t graphics_original_gate;
static RECOMP_TLS unsigned graphics_original_gate_depth,graphics_original_dispatch_depth;
static const char *graphics_original_enter(void)
{
    if(!graphics_original_gate || !pilot_io_device_thread_component_active() || graphics_original_gate_depth)
        return "original graphics gate outside a nonrecursive component";
    const char *error=native_component_original_enter();
    if(error)return error;
    if(MEM32(graphics_original_gate+20)!=1 ||
       MEM32(graphics_original_gate+24)!=MEM32(0x8003da64u))
        return "original graphics gate lost its actual thread owner";
    graphics_original_gate_depth=1;return NULL;
}
static const char *graphics_original_leave(void)
{
    if(graphics_original_gate_depth!=1 || graphics_original_dispatch_depth)
        return "original graphics gate release before native dispatch completion";
    const char *error=native_component_original_leave();
    if(!error)graphics_original_gate_depth=0;
    return error;
}
#endif

#include "graphics_abi.inc"
#include "graphics_state_cache.inc"
static int graphics_active;
#include "graphics_diagnostics.inc"
static uint32_t graphics_parameters[17];
static uint32_t graphics_headers,graphics_data[4],graphics_target,graphics_depth;
static unsigned graphics_surface_refs[4];
static int32_t graphics_parameters_check(const uint32_t parameters[17])
{
    int32_t status=pilot_gfx_parameters(parameters);if(status<0)return status;
#ifdef PILOT_D3D_PROFILE_CLASSIC
    const unsigned version=2;
#elif defined(PILOT_D3D_GUEST_PRESENTATION_VERSION)
    const unsigned version=PILOT_D3D_GUEST_PRESENTATION_VERSION;
#else
    const unsigned version=0;
#endif
    if(!pilot_presentation_profile_admits(parameters,version))return (int32_t)0x80004001u;
    return status;
}
static PilotPresentation graphics_presentation(void)
{
    PilotPresentation result;if(!pilot_presentation_parse(graphics_parameters,&result))stop("invalid guest presentation descriptor",0);
    return result;
}
static unsigned graphics_surface_mask(void)
{PilotPresentation p=graphics_presentation();return pilot_presentation_mask(&p);}
static int graphics_surface_owned(unsigned index)
{return index<4 && (graphics_surface_mask()&(1u<<index))!=0;}

#include "graphics_surfaces.h"
static void graphics_surfaces_project(unsigned mode)
{
#ifdef PILOT_D3D_SURFACE_PROJECTION_VERSION
#if PILOT_D3D_SURFACE_PROJECTION_VERSION != 1
#error Unsupported presentation surface projection ABI
#endif
    const PilotSurfaceLayout layout={PILOT_D3D_PRESENTATION_COUNT,PILOT_D3D_PRESENTATION_COLORS,
        PILOT_D3D_PRESENTATION_DEPTH,PILOT_D3D_CURRENT_COLOR,PILOT_D3D_CURRENT_DEPTH};
    if(!pilot_surface_project_mask((uint32_t*)(memory+PILOT_D3D_DEVICE_OBJECT),
            PILOT_D3D_DEVICE_BYTES,&layout,graphics_headers,graphics_target,graphics_depth,mode,graphics_surface_mask()))
        stop("invalid presentation surface projection",mode);
#else
    (void)mode;
#endif
}
#ifdef PILOT_D3D_SURFACE_PROJECTION_VERSION
static int32_t graphics_surface_backend_ref(void *context,unsigned index,int acquire)
{(void)context;return pilot_gfx_surface_ref(index,acquire);}
#endif
static void graphics_surfaces_reconcile(void)
{
#ifdef PILOT_D3D_SURFACE_PROJECTION_VERSION
    uint32_t common[4];for(unsigned i=0;i<4;++i)common[i]=MEM32(graphics_headers+i*32);
    int error=pilot_surface_sync_mask(common,graphics_surface_refs,graphics_surface_mask(),graphics_surface_backend_ref,NULL);
    if(error)stop("invalid inline surface ownership",(uint32_t)error);
#endif
}
static void graphics_movie_capture_present(uint64_t time,uint32_t swap,uint64_t serial);
#include "graphics_clock.inc"
#ifdef PILOT_DISPLAY_LLE
#include "display_lle.inc"
#endif
#include "graphics_state.inc"

static void graphics_surfaces_create(void)
{
    uint32_t bytes=4096;
    if (vm_allocate(&graphics_headers,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))
        stop("graphics surface header allocation failed",0);
    PilotPresentation presentation=graphics_presentation();
    uint32_t format=graphics_parameters[2]==0x11?0x11:(graphics_parameters[2]==6 || graphics_parameters[2]==0x12)?0x12:0x1e;
    for (unsigned i=0;i<4;++i) {
        uint32_t *header=(uint32_t*)(memory+graphics_headers+i*32),extent[3];
        if(!pilot_presentation_surface(&presentation,i,extent)){memset(header,0,24);continue;}
        uint32_t width=extent[0],height=extent[1],pitch=extent[2];bytes=pitch*height;
        /* GPU residence has an owned guest VA but no direct CPU mapping. */
        if (vm_allocate(&graphics_data[i],0,&bytes,MEM_RESERVE,PAGE_NOACCESS))
            stop("graphics surface address reservation failed",i);
        header[0]=0x010d0000u;header[1]=graphics_data[i];header[2]=0;
        header[3]=0x10021u|((i==2?0x2e:format)<<8);
        header[4]=(width-1)|((height-1)<<12)|((pitch/64-1)<<24);header[5]=0;
    }
    graphics_target=graphics_headers;graphics_depth=presentation.depth?graphics_headers+64:0;
    graphics_surfaces_project(PILOT_SURFACE_PUBLISH);
}
static unsigned graphics_surface_index(uint32_t address)
{
    if (address<graphics_headers || address-graphics_headers>=128 || ((address-graphics_headers)&31))
        stop("unowned graphics surface",address);
    unsigned index=(address-graphics_headers)/32;
    if(!graphics_surface_owned(index))stop("absent presentation surface",address);return index;
}
static unsigned graphics_surface_ref(unsigned index,int acquire)
{
#ifdef PILOT_D3D_SURFACE_PROJECTION_VERSION
    if(!graphics_surface_owned(index))stop("invalid owned surface index",index);
    uint32_t common=MEM32(graphics_headers+32*index);
    int error=pilot_surface_adjust(index,&common,&graphics_surface_refs[index],acquire?1:-1,
                                  graphics_surface_backend_ref,NULL);
    if(error)stop("invalid inline surface reference",(uint32_t)error);
    MEM32(graphics_headers+32*index)=common;return graphics_surface_refs[index];
#else
    if (acquire && graphics_surface_refs[index]==0xffff)
        stop("surface reference count overflow",index);
    int32_t result=pilot_gfx_surface_ref(index,acquire);
    if (result<0) stop("invalid native surface ownership",index);
    graphics_surface_refs[index]=(unsigned)result;
    MEM32(graphics_headers+32*index)=0x010d0000u|(uint32_t)result;
    return (unsigned)result;
#endif
}

static void *graphics_pointer(uint32_t address,uint32_t bytes)
{
    if (!address || (uint64_t)address+bytes>0x100000000ull)
        stop("invalid graphics guest range",address);
    /* Actual committed/protected memory is enforced by host page access. */
    return memory+address;
}
#include "graphics_texture.inc"
#include "graphics_movie_capture.inc"
#include "graphics_vertex.inc"
#include "graphics_index.inc"
#include "graphics_shader.inc"
#include "graphics_pixel.inc"
#include "graphics_stateblock.inc"
#include "graphics_transform.inc"
static void graphics_target_prepare(uint32_t header);
#include "graphics_frame_audit.inc"
#include "graphics_draw.inc"
#include "graphics_completion.inc"
#include "graphics_immediate.inc"
#include "graphics_gpu_callback.inc"
#include "graphics_callback_effects.inc"
#if defined(PILOT_SCHEDULING_LLE) && !defined(PILOT_DISPLAY_LLE)
#include "graphics_original_callbacks.inc"
#endif
#include "graphics_persistence.inc"
#include "graphics_resource_destroy.inc"
static void graphics_resource_bindings_close(void)
{
    for(unsigned i=0;i<4;++i)graphics_texture_bind(i,0);
    for(unsigned i=0;i<16;++i)graphics_vertex_bind(i,0,0);
    graphics_index_bind(0,0);
}
static void graphics_target_ref(uint32_t header,int acquire)
{
    if(!header)return;
    if(header==graphics_headers || header==graphics_headers+32 || header==graphics_headers+64 || (graphics_surface_owned(3) && header==graphics_headers+96))graphics_surface_ref(graphics_surface_index(header),acquire);
    else graphics_texture_ref(graphics_texture_slot(header),acquire);
}
static void graphics_target_prepare(uint32_t header)
{
    if(!header || header==graphics_headers || header==graphics_headers+64)return;
    unsigned slot=graphics_texture_slot(header);
    struct GraphicsTextureObject *s=&graphics_textures[slot];
    if(!s->parent && !s->standalone_surface)stop("target preparation requires a surface",header);
    unsigned owner=s->parent?s->parent:slot+1;
    struct GraphicsTextureObject *t=&graphics_textures[owner-1];
    if(t->cpu_borrowed)graphics_texture_refresh_alias(owner-1);
    if(graphics_texture_exposure_cache() && !t->family_exposed)return;
    unsigned exposed=t->cpu_exposed;
    for(unsigned i=0;i<graphics_texture_capacity;++i)if(graphics_textures[i].parent==owner)exposed|=graphics_textures[i].cpu_exposed;
    if(exposed)graphics_texture_upload(t,0);
    for(unsigned i=0;i<graphics_texture_capacity;++i)if(&graphics_textures[i]==t || graphics_textures[i].parent==owner)graphics_textures[i].cpu_exposed=0;
    t->family_exposed=0;
}
static void graphics_set_targets(uint32_t color,uint32_t depth)
{
    graphics_surfaces_reconcile();
    if(!color)color=graphics_target; /* Original NULL color preserves current target. */
    uint32_t color_native=0,depth_native=depth==graphics_headers+64?UINT32_MAX:0;
    uint32_t width=graphics_parameters[0],height=graphics_parameters[1];
    if(color!=graphics_headers) {
        struct GraphicsTextureObject *s=&graphics_textures[graphics_texture_slot(color)];
        if(!s->parent || s->usage!=1 || s->level)stop("unqualified color target",color);
        struct GraphicsTextureObject *t=&graphics_textures[s->parent-1];color_native=t->native;width=s->width;height=s->height;
        graphics_target_prepare(color);
    }
    if(depth && depth!=graphics_headers+64) {
        struct GraphicsTextureObject *s=&graphics_textures[graphics_texture_slot(depth)];
        if((!s->parent && !s->standalone_surface) || s->usage!=2 || s->level)stop("unqualified depth target",depth);
        struct GraphicsTextureObject *t=s->parent?&graphics_textures[s->parent-1]:s;depth_native=t->native;
        graphics_target_prepare(depth);
    }
    if(pilot_gfx_targets(color_native,depth_native)<0)stop("native render/depth target binding failed",color);
    // Acquire before releasing the previous binding, including self-rebinds.
    if(color!=graphics_headers)graphics_target_ref(color,1);
    if(depth && depth!=graphics_headers+64)graphics_target_ref(depth,1);
    if(graphics_target!=graphics_headers)graphics_target_ref(graphics_target,0);
    if(graphics_depth && graphics_depth!=graphics_headers+64)graphics_target_ref(graphics_depth,0);
    graphics_target=color;graphics_depth=depth;
    PilotPresentation presentation=graphics_presentation();
    float axis=color==graphics_headers?(float)pilot_presentation_axis(&presentation):1.0f;
    graphics_store_float(TRANSFORM_FIELD(PIXEL_SCALE),axis);graphics_store_float(TRANSFORM_FIELD(PIXEL_SCALE)+4,axis);
    graphics_surfaces_project(PILOT_SURFACE_CURRENT);
    uint32_t viewport[]={0,0,width,height,0,0x3f800000};
    if(pilot_gfx_viewport(viewport)<0)stop("target viewport reset failed",color);
    graphics_transform_viewport(viewport);
}

#include "graphics_reset.inc"
#include "graphics_commands.inc"
#include "graphics_scalar_preparation.inc"
#ifndef PILOT_SCHEDULING_LLE
/* Standalone surfaces have caller-owned headers, not CreateTexture parents.
 * A caller can remove its tile mapping and free the physical image directly,
 * retaining the header for reuse. Drop only our unbound cached GPU image at
 * that explicit allocation-lifetime boundary. Ordinary registered resources
 * still require their normal release/unregister path. */
static void graphics_contiguous_release(uint32_t base)
{
    AcquireSRWLockExclusive(&graphics_lock);
    if(graphics_active) {
        pilot_gfx_batch_flush();
#ifdef PILOT_D3D_COMMANDS_VERSION
        graphics_commands_flush();
#endif
        AcquireSRWLockShared(&vm_lock);
        struct PhysicalAllocation *allocation=physical_find(base,1);
        uint32_t bytes=allocation && allocation->base==base?allocation->bytes:0;
        ReleaseSRWLockShared(&vm_lock);
        for(unsigned i=0;bytes && i<graphics_texture_capacity;++i) {
            struct GraphicsTextureObject *s=&graphics_textures[i];
            if(!s->header || !s->standalone_surface || !s->cpu_borrowed ||
               s->data<base || (uint64_t)s->data+s->bytes>(uint64_t)base+bytes)continue;
            graphics_texture_refresh_alias(i);
            if(s->header==graphics_depth || s->header==graphics_target || s->bindings || s->waiters ||
               s->retire_original || s->refs!=1 || (MEM32(s->header)&0x78ffffu)!=1)
                stop("physical release of referenced standalone surface",s->header);
#ifdef PILOT_D3D_TILE_CACHE_VERSION
            for(unsigned tile=0;tile<PILOT_D3D_TILE_COUNT;++tile) {
                uint32_t at=PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_TILE_OFFSET+tile*PILOT_D3D_TILE_STRIDE;
                uint32_t data=MEM32(at+4),extent=MEM32(at+8);
                if(data && ((data&0x03ffffffu)|0x80000000u)<(uint64_t)base+bytes &&
                   (uint64_t)((data&0x03ffffffu)|0x80000000u)+extent>base)
                    stop("physical release of tiled standalone surface",s->header);
            }
#endif
            ++s->waiters;
            graphics_completion_wait(pilot_gfx_resource_serial(PILOT_GFX_TEXTURE,s->native));
            /* Completion can yield; the ownership table may have grown. */
            s=&graphics_textures[i];--s->waiters;
            pilot_gfx_texture_destroy(s->native);graphics_texture_borrow_release(s);
            fprintf(stderr,"[STANDALONE SURFACE RETIRED] header=%08x data=%08x\n",s->header,s->data);
            memset(s,0,sizeof(*s));
        }
    }
    ReleaseSRWLockExclusive(&graphics_lock);
}
#endif
static void graphics_dispatch_impl(unsigned entry)
{
    if(g_pilot_session_enabled)session_watched_graphics_lock=&graphics_lock;
    if (entry>=PILOT_D3D_COUNT) stop("invalid graphics entry",entry);
    const uint32_t *arguments=graphics_pointer(g_esp+4,pilot_d3d_calls[entry].stack_bytes);
    if(pilot_verbose){fprintf(stderr,"[D3D] %s return=%08x",pilot_d3d_calls[entry].name,MEM32(g_esp));
    for (unsigned i=0;i<pilot_d3d_calls[entry].stack_bytes/4;i++) fprintf(stderr," %08x",arguments[i]);
    fprintf(stderr,"\n");}
#ifdef PILOT_D3D_NATIVE_CPU_SERVICE_VERSION
    int cpu_service=pilot_d3d_native_cpu_service_index[entry];
    if(cpu_service>=0) {
#ifdef PILOT_D3D_IMMEDIATE_VERSION
        if(graphics_immediate.primitive)stop("unqualified original CPU graphics call inside immediate batch",entry);
#endif
        if((unsigned)cpu_service>=PILOT_D3D_NATIVE_CPU_SERVICE_COUNT)stop("invalid native CPU service index",entry);
        graphics_original_cpu_service(pilot_d3d_native_cpu_services[cpu_service].function,
                                      pilot_d3d_native_cpu_services[cpu_service].stack_bytes);return;
    }
#endif
    if(entry==PILOT_D3D_D3DDevice_BlockUntilVerticalBlank) {
        /* This original API waits for a display event, independent of FIFO
         * execution or renderer ownership. GPU observations and native-input
         * mutations retain their own ordered batch flushes. */
        graphics_wait_vblank();
        g_esp+=4+pilot_d3d_calls[entry].stack_bytes;return;
    }
    uint64_t entered=graphics_service_profile_enabled()?dispatch_monotonic():0;
    AcquireSRWLockExclusive(&graphics_lock);
    uint64_t acquired=entered?dispatch_monotonic():0;
    /* The native operation owner flushes at actual input/resource mutations
     * and GPU observations, including inline packets. An ABI transition is
     * not itself a GPU dependency. Keep the old policy for other backends and
     * matched startup-selected controls. Guest cache/ABI effects still occur
     * synchronously below, and the real callback barrier remains intact. */
    if(entry!=PILOT_D3D_D3DDevice_DrawVertices && !pilot_gfx_native_operation_owner())
        pilot_gfx_batch_flush();
    if(entry==graphics_resource_wait_entry() && !graphics_active) {
        ReleaseSRWLockExclusive(&graphics_lock);g_esp+=4+pilot_d3d_calls[entry].stack_bytes;return;
    }
    if(entry==PILOT_D3D_D3DDevice_GetDeviceCaps && !graphics_active) {
        memcpy(graphics_pointer(arguments[0],sizeof(pilot_d3d_device_caps)),pilot_d3d_device_caps,sizeof(pilot_d3d_device_caps));
        ReleaseSRWLockExclusive(&graphics_lock);g_esp+=4+pilot_d3d_calls[entry].stack_bytes;return;
    }
    if (entry!=PILOT_D3D_Direct3D_CreateDevice && !graphics_active)
        stop("graphics entry before native creation",pilot_d3d_calls[entry].address);
#ifdef PILOT_D3D_COMMANDS_VERSION
    if(entry==PILOT_D3D_COMMANDS_KICKOFF_ENTRY && g_ecx!=PILOT_D3D_DEVICE_OBJECT)
        stop("inline kickoff has unowned CDevice",g_ecx);
    if(graphics_active && entry!=PILOT_D3D_COMMANDS_RESERVE_ENTRY && entry!=PILOT_D3D_COMMANDS_SPACE_ENTRY)
        graphics_commands_flush();
#endif
    struct GraphicsScalarPreparation scalar_preparation;
    graphics_scalar_prepare_frontier(entry,&scalar_preparation);
    if(entry!=PILOT_D3D_D3DDevice_InsertCallback)graphics_gpu_barrier();
    uint64_t barrier=entered?dispatch_monotonic():0;
#ifdef PILOT_GRAPHICS_FRAME_AUDIT
    graphics_frame_inputs_before(entry,arguments);
#endif
#ifdef PILOT_D3D_IMMEDIATE_VERSION
    if(graphics_immediate_dispatch(entry,arguments))goto graphics_dispatch_done;
#endif
    if(graphics_scalar_consume(&scalar_preparation))goto graphics_dispatch_done;
    switch(entry) {
#ifdef PILOT_D3D_COMMANDS_VERSION
    case PILOT_D3D_COMMANDS_RESERVE_ENTRY:
        g_eax=graphics_commands_reserve(arguments[0],arguments[1]);break;
    case PILOT_D3D_COMMANDS_SPACE_ENTRY: {
        uint32_t window=MEM32(PILOT_D3D_COMMANDS_REQUEST_GLOBAL);
        g_eax=graphics_commands_reserve(window/2,window);break;
    }
    case PILOT_D3D_COMMANDS_KICKOFF_ENTRY:
        if(!pilot_gfx_command_submit())stop("inline native kickoff failed",0);break;
#endif
    case PILOT_D3D_D3DDevice_SetTransform:graphics_transform_set(arguments[0],arguments[1]);break;
    case PILOT_D3D_D3DDevice_InsertCallback:
        graphics_gpu_insert(arguments[0],arguments[1],arguments[2]);break;
    case PILOT_D3D_D3D_UpdateProjectionViewportTransform:graphics_projection_viewport();break;
    case PILOT_D3D_D3DDevice_GetViewportOffsetAndScale: {
        float offset[4],scale[4];graphics_viewport_offset_scale(offset,scale);
        memcpy(graphics_pointer(arguments[0],16),offset,16);memcpy(graphics_pointer(arguments[1],16),scale,16);break;
    }
    case PILOT_D3D_D3DDevice_CreateStateBlock:g_eax=graphics_block_create(arguments);break;
    case PILOT_D3D_D3DDevice_SetVerticalBlankCallback:graphics_set_vblank_callback(arguments[0]);break;
    case PILOT_D3D_D3DDevice_GetRenderTarget2:
        graphics_target_ref(graphics_target,1);g_eax=graphics_target;break;
    case PILOT_D3D_D3DDevice_GetDepthStencilSurface2:
        graphics_target_ref(graphics_depth,1);g_eax=graphics_depth;break;
    case PILOT_D3D_D3DDevice_SetRenderTarget:graphics_set_targets(arguments[0],arguments[1]);break;
    case PILOT_D3D_D3DDevice_CreatePixelShader:g_eax=graphics_pixel_create(arguments);break;
    case PILOT_D3D_D3DDevice_SetPixelShader:graphics_pixel_bind(arguments[0]);break;
    case PILOT_D3D_D3DDevice_SetPixelShaderConstant:graphics_pixel_constants(arguments);break;
    case PILOT_D3D_D3DDevice_GetPixelShader: {
        /* Query the guest handle, not the donor's private combiner selector.
         * Original XDK returns a borrowed DWORD without a shader AddRef. */
        uint32_t handle=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_PIXEL_SHADER);
        memcpy(graphics_pointer(arguments[0],4),&handle,4);break;
    }
    case PILOT_D3D_D3DDevice_SetTexture:graphics_texture_bind(arguments[0],arguments[1]);break;
    case PILOT_D3D_D3DDevice_SetStreamSource:graphics_vertex_bind(arguments[0],arguments[1],arguments[2]);break;
    case PILOT_D3D_D3DDevice_CreateVertexShader:g_eax=graphics_shader_create(arguments);break;
    case PILOT_D3D_D3DDevice_SetVertexShader:graphics_shader_bind(arguments[0]);break;
    case PILOT_D3D_D3DDevice_GetVertexShader:
        MEM32(arguments[0])=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER);break;
    case PILOT_D3D_D3DDevice_SetVertexShaderConstant1:graphics_shader_constants(g_ecx,g_edx,4,1);break;
    case PILOT_D3D_D3DDevice_SetVertexShaderConstant4:graphics_shader_constants(g_ecx,g_edx,16,1);break;
    case PILOT_D3D_D3DDevice_SetVertexShaderConstantNotInlineFast:
        graphics_shader_constants(g_ecx,g_edx,arguments[0],0);break;
    case PILOT_D3D_D3DDevice_SetVertexShaderConstantNotInline:
        graphics_shader_constants(g_ecx,g_edx,arguments[0],graphics_shader_records());break;
#ifdef PILOT_D3D_PROFILE_CLASSIC
#ifdef PILOT_D3D_GAMMA_SETTER_VERSION
    case PILOT_D3D_D3DDevice_SetGammaRamp: {
        if(arguments[0]&~1u)stop("unqualified immediate gamma timing",arguments[0]);
        const uint8_t *ramp=graphics_pointer(arguments[1],768);
        if(pilot_gfx_gamma(ramp)<0)stop("native gamma presentation mapping failed",arguments[0]);
        uint32_t bank=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SWAP_COUNTER)&1;
        memmove(graphics_pointer(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_GAMMA_RAMPS+bank*768,768),ramp,768);
        MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_GAMMA_CURRENT)=bank;
        MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_GAMMA_PENDING+bank*4)=1;
        g_eax=bank;break;
    }
#endif
#ifdef PILOT_D3D_TILE_CACHE_VERSION
    case PILOT_D3D_D3DDevice_SetTile: {
        if(arguments[0]>=PILOT_D3D_TILE_COUNT)stop("tile index outside native cache",arguments[0]);
        uint32_t tile[6]={0};
        if(arguments[1])memcpy(tile,graphics_pointer(arguments[1],sizeof(tile)),sizeof(tile));
        if(tile[1]) {
            if((tile[0]&~0x84000001u) || !(tile[0]&1) || !tile[2] || !tile[3] ||
               (tile[1]&0x3fff) || (tile[2]&0x3fff) || (tile[3]&63))
                stop("unqualified native tile layout",arguments[0]);
            uint32_t data=(tile[1]&0x03ffffffu)|0x80000000u;
            AcquireSRWLockExclusive(&vm_lock);
            int owned=physical_find(data,tile[2])!=NULL;
            ReleaseSRWLockExclusive(&vm_lock);
            if(!owned)stop("native tile requires owned contiguous backing",tile[1]);
            /* RHI images own logical color/depth; hardware tile and Z-tag
             * compression are storage encodings under that same interface.
             * Preserve the original queryable descriptor, not NV2A MMIO or
             * fictitious register acknowledgements. Its CPU setter clears
             * the two Z fields when compression is disabled. */
            if(!(tile[0]&0x80000000u))tile[4]=tile[5]=0;
        } else memset(tile,0,sizeof(tile));
        memcpy(graphics_pointer(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_TILE_OFFSET+arguments[0]*PILOT_D3D_TILE_STRIDE,sizeof(tile)),tile,sizeof(tile));
        break;
    }
#endif
    case PILOT_D3D_D3DVertexBuffer_Lock: {
        struct GraphicsVertexObject *vertex=graphics_vertex(arguments[0]);
        if(arguments[1]>vertex->bytes || arguments[2]>vertex->bytes-arguments[1])
            stop("vertex lock range outside owned storage",arguments[1]);
        graphics_pointer(arguments[3],4);
        g_eax=graphics_vertex_lock(arguments[0],arguments[4])+arguments[1];
        MEM32(arguments[3])=g_eax; /* Original leaves the returned data in EAX. */
        break;
    }
    case PILOT_D3D_D3DDevice_CreateTexture: {
        graphics_pointer(arguments[6],4);
        uint32_t allocation[]={arguments[0],arguments[1],1,arguments[2],arguments[3],arguments[4],3};
        uint32_t header=graphics_texture_allocate(allocation);
        if(header)MEM32(arguments[6])=header;
        g_eax=header?0:0x8007000eu;break;
    }
    case PILOT_D3D_D3DTexture_GetSurfaceLevel: {
        graphics_pointer(arguments[2],4);
        uint32_t header=graphics_texture_surface(arguments[0],arguments[1]);
        if(header)MEM32(arguments[2])=header;
        g_eax=header?0:0x8007000eu;break;
    }
    case PILOT_D3D_D3DDevice_CreateVertexBuffer:
    case PILOT_D3D_D3DDevice_CreateIndexBuffer: {
        graphics_pointer(arguments[4],4);
        uint32_t header=entry==PILOT_D3D_D3DDevice_CreateVertexBuffer?
            graphics_vertex_allocate(arguments[0]):graphics_index_allocate(arguments[0]);
        /* The original routines ignore Usage/FVF-or-Format/Pool, and publish
         * the output only after allocation succeeds. Keep that caller ABI. */
        if(header)MEM32(arguments[4])=header;
        g_eax=header?0:0x8007000eu;break;
    }
    case PILOT_D3D_D3DDevice_GetTexture2: {
        if(arguments[0]>=4)stop("unqualified texture query stage",arguments[0]);
        graphics_pointer(arguments[1],4);
        uint32_t header=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES+4*arguments[0]);
        if(header)graphics_target_ref(header,1);
        MEM32(arguments[1])=header;break;
    }
    case PILOT_D3D_D3DDevice_GetDepthStencilSurface:
        graphics_pointer(arguments[0],4);MEM32(arguments[0])=graphics_depth;
        if(graphics_depth)graphics_target_ref(graphics_depth,1);
        g_eax=graphics_depth?0:0x88760866u;break;
    case PILOT_D3D_D3DDevice_GetBackBuffer: {
        if(arguments[0]!=0 && arguments[0]!=UINT32_MAX && !(arguments[0]==1 && graphics_presentation().copy_buffers==2))
            stop("back buffer index outside single-buffer presentation",arguments[0]);
        graphics_pointer(arguments[2],4);
        unsigned index=arguments[0]==UINT32_MAX?1:arguments[0]==1?3:0;
        uint32_t header=graphics_headers+32*index;
        graphics_surface_ref(index,1);MEM32(arguments[2])=header;break;
    }
    case PILOT_D3D_D3DDevice_SetVertexShaderConstant: {
        int64_t first=(int32_t)arguments[0];first+=96;
        if(first<0 || first>=192 || !arguments[2] || arguments[2]>192-(uint32_t)first)
            stop("unqualified stdcall vertex constant range",arguments[0]);
        graphics_shader_constants((uint32_t)first,arguments[1],arguments[2]*4,graphics_shader_records());break;
    }
#endif
    case PILOT_D3D_D3DDevice_GetVertexShaderConstant:graphics_shader_get_constants(arguments);break;
    case PILOT_D3D_D3DDevice_CreateVertexBuffer2:g_eax=graphics_vertex_allocate(arguments[0]);break;
#ifdef PILOT_D3D_INDEX_DATA_GLOBAL
#ifndef PILOT_D3D_PROFILE_CLASSIC
    case PILOT_D3D_D3DDevice_CreateIndexBuffer2:g_eax=graphics_index_allocate(arguments[0]);break;
#endif
    case PILOT_D3D_D3DDevice_SetIndices:graphics_index_bind(arguments[0],arguments[1]);break;
    case PILOT_D3D_D3D_DestroyResource:graphics_resource_destroy(arguments[0]);break;
#endif
    case PILOT_D3D_D3DVertexBuffer_Lock2:g_eax=graphics_vertex_lock(arguments[0],arguments[1]);break;
    case PILOT_D3D_D3DResource_Register:graphics_vertex_register(arguments[0],arguments[1]);break;
    case PILOT_D3D_D3DDevice_CreateTexture2:g_eax=graphics_texture_allocate(arguments);break;
    case PILOT_D3D_D3DBaseTexture_GetLevelCount:
        g_eax=graphics_textures[graphics_texture_slot(arguments[0])].levels;break;
    case PILOT_D3D_D3DTexture_GetSurfaceLevel2:
        g_eax=graphics_texture_surface(arguments[0],arguments[1]);break;
    case PILOT_D3D_D3DSurface_GetDesc:graphics_texture_desc(arguments[0],0,arguments[1]);break;
    case PILOT_D3D_Get2DSurfaceDesc:graphics_texture_desc(arguments[0],arguments[1],arguments[2]);break;
    case PILOT_D3D_D3DSurface_LockRect:
        graphics_texture_lock(arguments[0],0,arguments[1],arguments[2],arguments[3]);break;
    case PILOT_D3D_D3DTexture_LockRect:
        graphics_texture_lock(arguments[0],arguments[1],arguments[2],arguments[3],arguments[4]);break;
    case PILOT_D3D_D3DDevice_GetDisplayMode: {
        /* Xbox layout is Width,Height,RefreshRate,Flags,Format (20 bytes).
         * The enhanced output is progressive; its configured logical cadence
         * and linear guest color format do not depend on the host desktop. */
        uint32_t mode[5]={graphics_parameters[0],graphics_parameters[1],
            graphics_parameters[11]?graphics_parameters[11]:60,0x40,
            (MEM32(graphics_headers+12)>>8)&255};
#if defined(PILOT_D3D_GUEST_PRESENTATION_VERSION) || defined(PILOT_D3D_PROFILE_CLASSIC)
        mode[3]=graphics_parameters[10];
#endif
        memcpy(graphics_pointer(arguments[0],sizeof(mode)),mode,sizeof(mode)); break;
    }
    case PILOT_D3D_D3DDevice_GetDeviceCaps:
        /* Xbox's original void query copies its complete immutable caps table.
         * This reports the emulated device, never the host adapter's caps. */
        memcpy(graphics_pointer(arguments[0],sizeof(pilot_d3d_device_caps)),
               pilot_d3d_device_caps,sizeof(pilot_d3d_device_caps)); break;
    case PILOT_D3D_Direct3D_CreateDevice: {
        if (graphics_active) stop("second native device creation",entry);
        if (arguments[0] || arguments[1]!=1 || arguments[2] ||
            (arguments[3]&~PILOT_D3D_CREATE_BEHAVIOR_MASK))
            stop("unqualified graphics creation options",entry);
        uint32_t staged[17]; memcpy(staged,graphics_pointer(arguments[4],sizeof(staged)),sizeof(staged));
        fprintf(stderr,"[D3D PARAMETERS]");
        for (unsigned i=0;i<17;i++) fprintf(stderr," %08x",staged[i]);
        fprintf(stderr,"\n");
        g_eax=(uint32_t)graphics_parameters_check(staged);
        if(!(g_eax&0x80000000u))g_eax=(uint32_t)pilot_gfx_create(staged);
        if (!(g_eax&0x80000000u)) {
            graphics_persistence_clear();
            graphics_active=1; memcpy(graphics_parameters,staged,sizeof(staged));
#ifdef PILOT_D3D_DEVICE_REFERENCES
            MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_DEVICE_REFERENCES)=1;
#endif
            MEM32(PILOT_D3D_PRESENT_INTERVAL)=staged[12];
            graphics_clock_origin=graphics_clock_now();
#if defined(PILOT_SCHEDULING_LLE) && !defined(PILOT_DISPLAY_LLE)
            graphics_original_pulse_frame=0;
#endif
            graphics_clock_hz=staged[11]?staged[11]:60;
            graphics_clock_closing=graphics_clock_presented=0;
            graphics_present_count=0;graphics_swap_expected=0;graphics_swap_submitted=graphics_swap_completed=0;graphics_swap_deadline=0;
            MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SWAP_COUNTER)=0;
            graphics_state_init();
            graphics_surfaces_create();
#ifdef PILOT_D3D_SAMPLE_COUNT_OFFSET
            MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SAMPLE_COUNT_OFFSET)=staged[4];
#endif
            graphics_snapshot_init();
            graphics_transform_init();
#ifdef PILOT_D3D_GAMMA_CACHE_VERSION
            /* Original miniport initialization creates two identity ramps,
             * each containing 256 bytes per channel. The original CPU getter
             * reads this same cache; nonidentity setting remains guarded. */
            for(unsigned bank=0;bank<2;++bank)for(unsigned channel=0;channel<3;++channel)
                for(unsigned i=0;i<256;++i)MEM8(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_GAMMA_RAMPS+bank*768+channel*256+i)=(uint8_t)i;
            MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_GAMMA_CURRENT)=0;
            MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_GAMMA_PENDING)=0;
            MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_GAMMA_PENDING+4)=0;
#endif
            AcquireSRWLockExclusive(&graphics_display_lock);
            graphics_display_profile=graphics_diagnostic(GRAPHICS_DIAGNOSTIC_WAIT)!=NULL;
            graphics_display_open=1;
            ReleaseSRWLockExclusive(&graphics_display_lock);
            MEM32(PILOT_D3D_DEVICE_GLOBAL)=PILOT_D3D_DEVICE_OBJECT;
#ifdef PILOT_D3D_COMMANDS_VERSION
            graphics_commands_open();
#endif
            if (arguments[5]) MEM32(arguments[5])=PILOT_D3D_DEVICE_OBJECT;
        } else {
            MEM32(PILOT_D3D_DEVICE_GLOBAL)=0;
            if (arguments[5]) MEM32(arguments[5])=0;
        }
        break;
    }
#ifdef PILOT_D3D_GUEST_PRESENTATION_VERSION
    case PILOT_D3D_D3DDevice_SetRenderState_MultiSampleAntiAlias:
        if(!pilot_gfx_multisample(arguments[0],UINT32_MAX))stop("unqualified multisample enable",arguments[0]);
        graphics_render_write(PILOT_D3D_SAMPLE_AA_STATE,arguments[0]);break;
    case PILOT_D3D_D3DDevice_SetRenderState_MultiSampleMask:
        if(!pilot_gfx_multisample(1,arguments[0]))stop("unqualified multisample mask",arguments[0]);
        graphics_render_write(PILOT_D3D_SAMPLE_MASK_STATE,arguments[0]);break;
#endif
    case PILOT_D3D_D3DDevice_SetRenderState_Simple:
        if(pilot_verbose)fprintf(stderr,"[D3D SIMPLE] method=%08x value=%08x\n",g_ecx,g_edx);
        graphics_simple_state(g_ecx,g_edx); break;
    case PILOT_D3D_D3DDevice_SetRenderState_CullMode:graphics_render_state(147,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_ZBias:graphics_render_state(149,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_NormalizeNormals:graphics_render_state(142,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_VertexBlend:graphics_render_state(137,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_ZEnable:graphics_render_state(143,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_StencilEnable:graphics_render_state(144,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_StencilFail:graphics_render_state(145,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_FogColor:graphics_render_state(138,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_TextureFactor:graphics_render_state(148,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_FillMode:graphics_render_state(139,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_EdgeAntiAlias:graphics_render_state(151,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_LogicOp:graphics_render_state(150,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_YuvEnable:graphics_render_state(PILOT_D3D_YUV_STATE,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderStateNotInline:
        graphics_render_state(graphics_render_from_guest(arguments[0]),arguments[1],1);break;
    case PILOT_D3D_D3DDevice_SetTextureState_TexCoordIndex:
        graphics_texture_state(arguments[0],28,arguments[1],1);break;
    case PILOT_D3D_D3DDevice_SetTextureState_BumpEnv:
        if(arguments[1]<22 || arguments[1]>27)stop("invalid bump state index",arguments[1]);
        graphics_texture_state(arguments[0],arguments[1],arguments[2],1);break;
    case PILOT_D3D_D3DDevice_DrawVertices:
        graphics_draw_common(arguments,0,0);break;
#ifdef PILOT_D3D_INDEX_DATA_GLOBAL
    case PILOT_D3D_D3DDevice_DrawIndexedVertices:graphics_draw_indexed(arguments);break;
#endif
    case PILOT_D3D_D3DDevice_DrawVerticesUP: {
        uint32_t draw[]={arguments[0],0,arguments[1]};
        graphics_draw_common(draw,arguments[2],arguments[3]);break;
    }
    case PILOT_D3D_D3DResource_IsBusy:g_eax=graphics_resource_busy(arguments[0]);break;
    case PILOT_D3D_D3DDevice_BeginVisibilityTest:
        g_eax=pilot_gfx_visibility_begin();
        // Original Begin has no HRESULT contract: failure cannot be returned
        // in an ignored register and later mistaken for a valid measurement.
        if(g_eax&0x80000000u)stop("native visibility begin failed",g_eax);
        break;
    case PILOT_D3D_D3DDevice_EndVisibilityTest:g_eax=pilot_gfx_visibility_end(arguments[0]);break;
    case PILOT_D3D_D3DDevice_GetVisibilityTestResult: {
        uint32_t count;uint64_t stamp;
        g_eax=pilot_gfx_visibility_result(arguments[0],&count,&stamp);
        if(!g_eax) {
            memcpy(graphics_pointer(arguments[1],4),&count,4);
            if(arguments[2])memcpy(graphics_pointer(arguments[2],8),&stamp,8);
        }
        break;
    }
    case PILOT_D3D_D3DDevice_InsertFence:g_eax=graphics_fence_insert();break;
    case PILOT_D3D_D3DDevice_BlockOnFence:graphics_fence_wait(arguments[0]);break;
    case PILOT_D3D_D3DDevice_SetTextureStageStateNotInline:
        graphics_texture_state(arguments[0],arguments[1],arguments[2],1);break;
    case PILOT_D3D_D3DDevice_SetShaderConstantMode:
        /* The reviewed mode-1 branch only records mode/reservation flags; it
         * issues no GPU commands. Creation already allocated the donor's real
         * 192-float4 buffer. Mode zero has fixed-pipeline reset work and remains
         * unsupported until that whole transition is qualified. */
        if(arguments[0]!=1)stop("unqualified shader constant mode transition",arguments[0]);
        MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET)=1;
        MEM32(PILOT_D3D_DEVICE_OBJECT+8)&=~0x200u;
        g_eax=1;break;
    case PILOT_D3D_D3DDevice_GetBackBuffer2: {
        if (arguments[0]!=0 && arguments[0]!=0xffffffffu)
            stop("back buffer index outside single-buffer presentation",arguments[0]);
        unsigned index=arguments[0]==0xffffffffu?1:0;
        graphics_surface_ref(index,1); g_eax=graphics_headers+32*index; break;
    }
    case PILOT_D3D_D3DResource_AddRef:
    case PILOT_D3D_D3DResource_Release:
        if(arguments[0]==graphics_headers || arguments[0]==graphics_headers+32 || arguments[0]==graphics_headers+64 || (graphics_surface_owned(3) && arguments[0]==graphics_headers+96))
            g_eax=graphics_surface_ref(graphics_surface_index(arguments[0]),entry==PILOT_D3D_D3DResource_AddRef);
        else if(graphics_index_find(arguments[0])>=0 ||
                (MEM32(arguments[0])&0x01070000u)==0x01010000u)
            g_eax=graphics_index_ref(arguments[0],entry==PILOT_D3D_D3DResource_AddRef);
        else if(graphics_vertex_find(arguments[0])>=0 ||
                (MEM32(arguments[0])&0x01070000u)==0x01000000u)
            g_eax=graphics_vertex_ref(arguments[0],entry==PILOT_D3D_D3DResource_AddRef);
        else g_eax=graphics_texture_ref(graphics_texture_slot(arguments[0]),entry==PILOT_D3D_D3DResource_AddRef);
        break;
#ifdef PILOT_D3D_DEVICE_REFERENCES
    case PILOT_D3D_D3DDevice_AddRef: {
        uint32_t field=PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_DEVICE_REFERENCES,refs=MEM32(field);
        if(!refs || refs==UINT32_MAX)stop("invalid device reference acquisition",refs);
        MEM32(field)=g_eax=refs+1;break;
    }
#endif
    case PILOT_D3D_D3DDevice_Release: {
#ifdef PILOT_D3D_DEVICE_REFERENCES
        uint32_t field=PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_DEVICE_REFERENCES,refs=MEM32(field);
        if(!refs)stop("invalid device reference release",refs);
        if(refs>1){MEM32(field)=g_eax=refs-1;break;}
#endif
        graphics_surfaces_reconcile();
#ifdef PILOT_D3D_SURFACE_PROJECTION_VERSION
        if(graphics_surface_refs[0] || graphics_surface_refs[1] || graphics_surface_refs[2] || graphics_surface_refs[3])
            stop("device release with outstanding inline surface ownership",entry);
#endif
        if(graphics_clock_waiters || graphics_resource_waiters || graphics_resource_retiring)stop("device release with active graphics waits/retirement",entry);
#ifdef PILOT_SCHEDULING_LLE
        if(pilot_io_device_thread_component_active()) {
            graphics_present_drain();
            graphics_fence_wait(graphics_fence_insert());
        }
#endif
        graphics_clock_close();
        graphics_gpu_close();
        if(graphics_target!=graphics_headers || (graphics_depth && graphics_depth!=graphics_headers+64))
            graphics_set_targets(graphics_headers,graphics_parameters[8]?graphics_headers+64:0);
        if (graphics_surface_refs[0] || graphics_surface_refs[1] || graphics_surface_refs[2] || graphics_surface_refs[3])
            stop("device release with outstanding surface ownership",entry);
        graphics_surfaces_project(PILOT_SURFACE_CLEAR);
        graphics_resource_bindings_close();graphics_texture_aliases_close();graphics_blocks_release();graphics_indices_close();graphics_vertices_close();graphics_shaders_release();graphics_pixels_release();
#ifdef PILOT_D3D_COMMANDS_VERSION
        graphics_commands_close();
#endif
        pilot_immediate_close(&graphics_immediate);
        pilot_gfx_close(); graphics_active=0;
#ifdef PILOT_D3D_DEVICE_REFERENCES
        memset(memory+PILOT_D3D_DEVICE_OBJECT,0,PILOT_D3D_DEVICE_BYTES);
#endif
        graphics_fence_next=1;graphics_fence_done=0;memset(graphics_fences,0,sizeof(graphics_fences));
        uint32_t bytes;
        for (unsigned i=0;i<4;++i) {
            bytes=0;
            if (graphics_data[i] && vm_free(&graphics_data[i],&bytes,MEM_RELEASE)) stop("surface VA release failed",i);
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
        if(channels)graphics_target_prepare(graphics_target);
        if(flags&3)graphics_target_prepare(graphics_depth);
        g_eax=(uint32_t)pilot_gfx_clear(native_flags,arguments[3],depth,arguments[5]); break;
    }
    case PILOT_D3D_D3DDevice_SetViewport: {
        uint32_t viewport[6]; memcpy(viewport,graphics_pointer(arguments[0],24),24);
        uint32_t width=graphics_parameters[0],height=graphics_parameters[1];
        if(graphics_target!=graphics_headers){struct GraphicsTextureObject *t=&graphics_textures[graphics_texture_slot(graphics_target)];width=t->width;height=t->height;}
        float low,high; memcpy(&low,viewport+4,4); memcpy(&high,viewport+5,4);
        if (!viewport[2] || !viewport[3] || (uint64_t)viewport[0]+viewport[2]>width ||
            (uint64_t)viewport[1]+viewport[3]>height || !(low>=0 && high<=1 && low<=high))
            stop("unqualified viewport bounds",arguments[0]);
        g_eax=(uint32_t)pilot_gfx_viewport(viewport);
        if(!(g_eax&0x80000000u))graphics_transform_viewport(viewport);break;
    }
    case PILOT_D3D_D3DDevice_Reset:
        graphics_surfaces_reconcile();
#ifdef PILOT_D3D_SURFACE_PROJECTION_VERSION
        if(graphics_surface_refs[0] || graphics_surface_refs[1] || graphics_surface_refs[2] || graphics_surface_refs[3])
            stop("reset with outstanding inline surface ownership",entry);
#endif
        g_eax=graphics_reset(arguments[0]);
        if(!(g_eax&0x80000000u))graphics_surfaces_project(PILOT_SURFACE_PUBLISH);
        break;
    case PILOT_D3D_D3DDevice_PersistDisplay:g_eax=graphics_persist_display();break;
    case PILOT_D3D_D3DDevice_Swap:
        if (arguments[0]) stop("unqualified graphics swap flags",arguments[0]);
        g_eax=graphics_present();
        break;
    default:
        if(entry==graphics_resource_wait_entry()){graphics_resource_wait(arguments[0]);break;}
        if(!strcmp(pilot_d3d_calls[entry].name,"D3DDevice_SetTextureState_ColorKeyColor")) {
            graphics_texture_state(arguments[0],30,arguments[1],1);break;
        }
        if(!strcmp(pilot_d3d_calls[entry].name,"D3DDevice_SetTextureState_BorderColor")) {
            graphics_texture_state(arguments[0],29,arguments[1],1);break;
        }
        {
            static const struct {const char *name;unsigned state;} named[]={
                {"D3DDevice_SetRenderState_LineWidth",157},
                {"D3DDevice_SetRenderState_ShadowFunc",156},
                {"D3DDevice_SetRenderState_BackFillMode",140},
                {"D3DDevice_SetRenderState_TwoSidedLighting",141},
                {"D3DDevice_SetRenderState_Dxt1NoiseEnable",159},
                {"D3DDevice_SetRenderState_OcclusionCullEnable",161},
                {"D3DDevice_SetRenderState_StencilCullEnable",162},
                {"D3DDevice_SetRenderState_RopZCmpAlwaysRead",163},
                {"D3DDevice_SetRenderState_RopZRead",164},
                {"D3DDevice_SetRenderState_DoNotCullUncompressed",165}};
            unsigned i;for(i=0;i<sizeof(named)/sizeof(*named);++i)
                if(!strcmp(pilot_d3d_calls[entry].name,named[i].name))break;
            if(i<sizeof(named)/sizeof(*named)){graphics_render_state(named[i].state,arguments[0],1);break;}
        }
        fprintf(stderr,"[D3D UNQUALIFIED] %s",pilot_d3d_calls[entry].name);
        for(unsigned i=0;i<pilot_d3d_calls[entry].stack_bytes/4;++i)fprintf(stderr," %08x",arguments[i]);
        fputc('\n',stderr);
        stop(pilot_d3d_calls[entry].name,pilot_d3d_calls[entry].address);
    }
graphics_dispatch_done:
    graphics_original_retire_drain();
    uint64_t unlocking=entered?dispatch_monotonic():0;
#ifdef PILOT_GRAPHICS_FRAME_AUDIT
    graphics_frame_audit_call(entry,arguments);
#endif
#if defined(PILOT_SCHEDULING_LLE) && !defined(PILOT_DISPLAY_LLE)
    graphics_original_callback_sync();
#endif
    graphics_interpolation_service();
    ReleaseSRWLockExclusive(&graphics_lock);
    if(entered) {
        uint64_t ended=dispatch_monotonic();
        if(ended-entered>500000)fprintf(stderr,"[GRAPHICS PHASES] name=%s lock_ms=%.3f barrier_ms=%.3f implementation_ms=%.3f unlock_ms=%.3f\n",
            pilot_d3d_calls[entry].name,(acquired-entered)/10000.0,(barrier-acquired)/10000.0,(unlocking-barrier)/10000.0,(ended-unlocking)/10000.0);
    }
    g_esp+=4+pilot_d3d_calls[entry].stack_bytes;
}

void pilot_d3d_dispatch(unsigned entry);
#ifdef PILOT_DISPLAY_QUALIFICATION
#include "display_qualification.inc"
#endif
#ifdef PILOT_SCHEDULING_LLE
static void graphics_original_dispatch(unsigned entry)
#else
void pilot_d3d_dispatch(unsigned entry)
#endif
{
#ifdef PILOT_SCHEDULING_LLE
    /* Generated ISR/DPC bodies bypass the raw CPU's component admission.
     * They cannot take renderer/VM host locks held by a suspended component
     * or enter a blocking thread gate at interrupt IRQL. */
#ifdef PILOT_USB_LLE
    if(usb_device_callback_depth)
        stop("original scheduling compiled callback graphics ownership requires integration",entry);
#endif
    if(dispatch_in_dpc)
        stop("original scheduling native DPC graphics ownership requires integration",entry);
#endif
#ifdef PILOT_DISPLAY_LLE
    if(entry==PILOT_D3D_D3DDevice_BlockUntilVerticalBlank || entry==PILOT_D3D_D3DDevice_SetVerticalBlankCallback) {
        if(!display_lle.active)stop("original display API without active display",entry);
        if(entry==PILOT_D3D_D3DDevice_BlockUntilVerticalBlank) {
            static unsigned waits;
            ++display_lle.waiters;
            PILOT_DISPLAY_WAIT();
            --display_lle.waiters;
            if(++waits<=3)fprintf(stderr,"[DISPLAY LLE WAIT] completed=%u status=%08x\n",waits,g_eax);
        } else {
            uint32_t callback=MEM32(g_esp+4);PILOT_DISPLAY_REGISTER();
            fprintf(stderr,"[DISPLAY LLE CALLBACK] registered=%08x\n",callback);
        }
        PILOT_THREAD_CHECKPOINT();return;
    }
    if(entry==PILOT_D3D_D3DDevice_Reset) {
        const uint32_t *p=graphics_pointer(MEM32(g_esp+4),17*4);
        if(p[0]!=display_lle.width || p[1]!=display_lle.height || p[2]!=graphics_parameters[2] ||
           (p[11]?p[11]:60)!=display_lle.hz)
            stop("display LLE scanout mode change not yet qualified",entry);
    }
    if(entry==PILOT_D3D_D3DDevice_Release) {
        int draining=scheduler_block_begin();
        AcquireSRWLockExclusive(&graphics_lock);graphics_present_drain();ReleaseSRWLockExclusive(&graphics_lock);
        scheduler_block_end(draining);display_lle_close();
    }
#endif
    /* Phobos 05040ec hle_d3d8.c installs RHI wait hooks instead of handing
     * off at every state setter/draw. Here the real callback barrier/fence
     * wait has its own hook; whole blocking lifecycle/present services keep
     * their outer release. Native work and actual GPU completion are intact. */
    int waits=entry==graphics_resource_wait_entry() || entry==PILOT_D3D_D3DDevice_BlockUntilVerticalBlank ||
        entry==PILOT_D3D_D3DDevice_BlockOnFence || entry==PILOT_D3D_D3DDevice_Swap ||
        entry==PILOT_D3D_Direct3D_CreateDevice || entry==PILOT_D3D_D3DDevice_Reset ||
        entry==PILOT_D3D_D3DDevice_Release;
    if(g_pilot_session_enabled && entry<PILOT_D3D_COUNT) {
        const void *arguments=graphics_pointer(g_esp+4,pilot_d3d_calls[entry].stack_bytes);
        session_service_evidence(UINT64_C(0x100000000)|entry,arguments,pilot_d3d_calls[entry].stack_bytes);
    }
    int blocked=waits?scheduler_block_begin():0;
    if(!graphics_service_profile_enabled() || dispatch_in_dpc || entry>=PILOT_D3D_COUNT) {
        graphics_dispatch_impl(entry);scheduler_block_end(blocked);
        if(entry==PILOT_D3D_D3DDevice_Swap)session_frame_boundary(graphics_swap_submitted);
#ifdef PILOT_DISPLAY_LLE
        if(entry==PILOT_D3D_Direct3D_CreateDevice && !(g_eax&0x80000000u))
            display_lle_open(graphics_parameters[0],graphics_parameters[1],graphics_clock_hz,graphics_clock_origin);
        if(entry==PILOT_D3D_D3DDevice_Reset && !(g_eax&0x80000000u))display_lle_reset();
#ifdef PILOT_DISPLAY_QUALIFICATION
        if(entry==PILOT_D3D_Direct3D_CreateDevice && !(g_eax&0x80000000u))display_qualification();
#endif
#endif
        PILOT_THREAD_CHECKPOINT();return;
    }
    uint64_t begin=dispatch_monotonic();
    graphics_dispatch_impl(entry);
    graphics_service_profile_record(entry,begin,dispatch_monotonic());
    scheduler_block_end(blocked);
    if(entry==PILOT_D3D_D3DDevice_Swap)session_frame_boundary(graphics_swap_submitted);
#ifdef PILOT_DISPLAY_LLE
    if(entry==PILOT_D3D_Direct3D_CreateDevice && !(g_eax&0x80000000u))
        display_lle_open(graphics_parameters[0],graphics_parameters[1],graphics_clock_hz,graphics_clock_origin);
    if(entry==PILOT_D3D_D3DDevice_Reset && !(g_eax&0x80000000u))display_lle_reset();
#ifdef PILOT_DISPLAY_QUALIFICATION
    if(entry==PILOT_D3D_Direct3D_CreateDevice && !(g_eax&0x80000000u))display_qualification();
#endif
#endif
    PILOT_THREAD_CHECKPOINT();
}
#ifdef PILOT_SCHEDULING_LLE
void pilot_d3d_dispatch(unsigned entry)
{
    int display_wait=entry==PILOT_D3D_D3DDevice_BlockUntilVerticalBlank;
    if(graphics_original_dispatch_depth ||
       (pilot_io_device_thread_component_active() &&
        (display_wait?(graphics_original_gate_depth || native_component_original_depth):graphics_original_gate_depth!=1)))
        stop("original graphics dispatch lacks nonrecursive gate ownership",entry);
    graphics_original_dispatch_depth=1;
    graphics_original_dispatch(entry);
    graphics_original_dispatch_depth=0;
}
#endif
