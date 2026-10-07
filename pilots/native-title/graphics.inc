/* Xbox guest ABI adapter. Native COM objects never enter guest memory.
 * Only qualified whole entry points return; other identified driver entries
 * stop before executing against a device owned by the native renderer. */
#include "graphics_backend.h"
#include "pilot_d3d_bindings.h"
static SRWLOCK graphics_lock=SRWLOCK_INIT;
static int graphics_active;
static uint32_t graphics_parameters[17];
static uint32_t graphics_headers,graphics_data[3],graphics_target,graphics_depth;
static unsigned graphics_surface_refs[3];
#include "graphics_clock.inc"
#include "graphics_state.inc"

static void graphics_surfaces_create(void)
{
    uint32_t bytes=4096;
    if (vm_allocate(&graphics_headers,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))
        stop("graphics surface header allocation failed",0);
    uint32_t width=graphics_parameters[0],height=graphics_parameters[1];
    uint32_t pitch=(width*4+63)&~63u;
    uint32_t format=(graphics_parameters[2]==6 || graphics_parameters[2]==0x12)?0x12:0x1e;
    for (unsigned i=0;i<3;++i) {
        bytes=pitch*height;
        /* GPU residence has an owned guest VA but no direct CPU mapping.
         * Lock/readback must explicitly synchronize actual GPU contents. */
        if (vm_allocate(&graphics_data[i],0,&bytes,MEM_RESERVE,PAGE_NOACCESS))
            stop("graphics surface address reservation failed",i);
        uint32_t *header=(uint32_t*)(memory+graphics_headers+i*32);
        header[0]=0x010d0000u; header[1]=graphics_data[i]; header[2]=0;
        header[3]=0x10021u|((i==2?0x2e:format)<<8);
        header[4]=(width-1)|((height-1)<<12)|((pitch/64-1)<<24); header[5]=0;
    }
    graphics_target=graphics_headers;graphics_depth=graphics_headers+64;
}
static unsigned graphics_surface_index(uint32_t address)
{
    if (address!=graphics_headers && address!=graphics_headers+32 && address!=graphics_headers+64)
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
#include "graphics_texture.inc"
#include "graphics_vertex.inc"
#include "graphics_shader.inc"
#include "graphics_pixel.inc"
#include "graphics_stateblock.inc"
#include "graphics_transform.inc"
static void graphics_target_prepare(uint32_t header);
#include "graphics_draw.inc"
#include "graphics_completion.inc"
static void graphics_resource_bindings_close(void)
{
    for(unsigned i=0;i<4;++i)graphics_texture_bind(i,0);
    for(unsigned i=0;i<16;++i)graphics_vertex_bind(i,0,0);
}
static void graphics_target_ref(uint32_t header,int acquire)
{
    if(!header)return;
    if(header==graphics_headers || header==graphics_headers+64)graphics_surface_ref(graphics_surface_index(header),acquire);
    else graphics_texture_ref(graphics_texture_slot(header),acquire);
}
static void graphics_target_prepare(uint32_t header)
{
    if(!header || header==graphics_headers || header==graphics_headers+64)return;
    struct GraphicsTextureObject *s=&graphics_textures[graphics_texture_slot(header)];
    if(!s->parent)stop("target preparation requires a surface",header);
    struct GraphicsTextureObject *t=&graphics_textures[s->parent-1];unsigned exposed=t->cpu_exposed;
    for(unsigned i=0;i<GRAPHICS_TEXTURE_OBJECTS;++i)if(graphics_textures[i].parent==s->parent)exposed|=graphics_textures[i].cpu_exposed;
    if(exposed)graphics_texture_upload(t,0);
    for(unsigned i=0;i<GRAPHICS_TEXTURE_OBJECTS;++i)if(&graphics_textures[i]==t || graphics_textures[i].parent==s->parent)graphics_textures[i].cpu_exposed=0;
}
static void graphics_set_targets(uint32_t color,uint32_t depth)
{
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
        if(!s->parent || s->usage!=2 || s->level)stop("unqualified depth target",depth);
        struct GraphicsTextureObject *t=&graphics_textures[s->parent-1];depth_native=t->native;
        graphics_target_prepare(depth);
    }
    if(pilot_gfx_targets(color_native,depth_native)<0)stop("native render/depth target binding failed",color);
    // Acquire before releasing the previous binding, including self-rebinds.
    if(color!=graphics_headers)graphics_target_ref(color,1);
    if(depth && depth!=graphics_headers+64)graphics_target_ref(depth,1);
    if(graphics_target!=graphics_headers)graphics_target_ref(graphics_target,0);
    if(graphics_depth && graphics_depth!=graphics_headers+64)graphics_target_ref(graphics_depth,0);
    graphics_target=color;graphics_depth=depth;
    uint32_t viewport[]={0,0,width,height,0,0x3f800000};
    if(pilot_gfx_viewport(viewport)<0)stop("target viewport reset failed",color);
    graphics_transform_viewport(viewport);
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
    case PILOT_D3D_D3DDevice_SetTransform:graphics_transform_set(arguments[0],arguments[1]);break;
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
    case PILOT_D3D_D3DDevice_BlockUntilVerticalBlank:graphics_wait_vblank();break;
    case PILOT_D3D_D3DDevice_CreatePixelShader:g_eax=graphics_pixel_create(arguments);break;
    case PILOT_D3D_D3DDevice_SetPixelShader:graphics_pixel_bind(arguments[0]);break;
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
    case PILOT_D3D_D3DDevice_GetVertexShaderConstant:graphics_shader_get_constants(arguments);break;
    case PILOT_D3D_D3DDevice_CreateVertexBuffer2:g_eax=graphics_vertex_allocate(arguments[0]);break;
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
        memcpy(graphics_pointer(arguments[0],sizeof(mode)),mode,sizeof(mode)); break;
    }
    case PILOT_D3D_D3DDevice_GetDeviceCaps:
        /* Xbox's original void query copies its complete immutable caps table.
         * This reports the emulated device, never the host adapter's caps. */
        memcpy(graphics_pointer(arguments[0],sizeof(pilot_d3d_device_caps)),
               pilot_d3d_device_caps,sizeof(pilot_d3d_device_caps)); break;
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
            graphics_clock_origin=dispatch_monotonic();
            graphics_clock_hz=staged[11]?staged[11]:60;
            graphics_clock_closing=graphics_clock_presented=0;
            graphics_state_init();
            graphics_surfaces_create();
            graphics_snapshot_init();
            graphics_transform_init();
            MEM32(PILOT_D3D_DEVICE_GLOBAL)=PILOT_D3D_DEVICE_OBJECT;
            if (arguments[5]) MEM32(arguments[5])=PILOT_D3D_DEVICE_OBJECT;
        } else {
            MEM32(PILOT_D3D_DEVICE_GLOBAL)=0;
            if (arguments[5]) MEM32(arguments[5])=0;
        }
        break;
    }
    case PILOT_D3D_D3DDevice_SetRenderState_Simple:
        fprintf(stderr,"[D3D SIMPLE] method=%08x value=%08x\n",g_ecx,g_edx);
        graphics_simple_state(g_ecx,g_edx); break;
    case PILOT_D3D_D3DDevice_SetRenderState_CullMode:graphics_render_state(147,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_ZEnable:graphics_render_state(143,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_StencilEnable:graphics_render_state(144,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_StencilFail:graphics_render_state(145,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_FogColor:graphics_render_state(138,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_TextureFactor:graphics_render_state(148,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderState_FillMode:graphics_render_state(139,arguments[0],1);break;
    case PILOT_D3D_D3DDevice_SetRenderStateNotInline:
        graphics_render_state(arguments[0],arguments[1],1);break;
    case PILOT_D3D_D3DDevice_SetTextureState_TexCoordIndex:
        graphics_texture_state(arguments[0],28,arguments[1],1);break;
    case PILOT_D3D_D3DDevice_SetTextureState_BumpEnv:
        if(arguments[1]<22 || arguments[1]>27)stop("invalid bump state index",arguments[1]);
        graphics_texture_state(arguments[0],arguments[1],arguments[2],1);break;
    case PILOT_D3D_D3DDevice_DrawVertices:
        graphics_draw_vertices(arguments);break;
    case PILOT_D3D_D3DResource_IsBusy:g_eax=graphics_resource_busy(arguments[0]);break;
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
        if(arguments[0]==graphics_headers || arguments[0]==graphics_headers+32 || arguments[0]==graphics_headers+64)
            g_eax=graphics_surface_ref(graphics_surface_index(arguments[0]),entry==PILOT_D3D_D3DResource_AddRef);
        else if(graphics_vertex_find(arguments[0])>=0)
            g_eax=graphics_vertex_ref(arguments[0],entry==PILOT_D3D_D3DResource_AddRef);
        else g_eax=graphics_texture_ref(graphics_texture_slot(arguments[0]),entry==PILOT_D3D_D3DResource_AddRef);
        break;
    case PILOT_D3D_D3DDevice_Release: {
        if(graphics_clock_waiters)stop("device release with active vertical-blank waits",entry);
        graphics_clock_close();
        if(graphics_target!=graphics_headers || (graphics_depth && graphics_depth!=graphics_headers+64))
            graphics_set_targets(graphics_headers,graphics_headers+64);
        if (graphics_surface_refs[0] || graphics_surface_refs[1] || graphics_surface_refs[2])
            stop("device release with outstanding surface ownership",entry);
        graphics_resource_bindings_close();graphics_blocks_release();graphics_shaders_release();graphics_pixels_release();pilot_gfx_close(); graphics_active=0;
        uint32_t bytes;
        for (unsigned i=0;i<3;++i) {
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
    case PILOT_D3D_D3DDevice_Swap:
        if (arguments[0]) stop("unqualified graphics swap flags",arguments[0]);
        if(graphics_callback)stop("swap/vblank callback completion contract unqualified",entry);
        g_eax=(uint32_t)pilot_gfx_present();
        if(!(g_eax&0x80000000u))graphics_clock_presented=1;
        break;
    default:
        stop(pilot_d3d_calls[entry].name,pilot_d3d_calls[entry].address);
    }
    ReleaseSRWLockExclusive(&graphics_lock);
    g_esp+=4+pilot_d3d_calls[entry].stack_bytes;
}
