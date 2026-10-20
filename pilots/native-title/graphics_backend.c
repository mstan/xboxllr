/* Guest-independent adapter over selectable sp00nznet/Phobos D3D11 backends.
 * Native COM/window pointers stay here; the guest bridge sees only values.
 * Sources, pinned revisions and retained terms: ../../THIRD_PARTY_ATTRIBUTION.md.
 * The Phobos variant is a private GPL-dependent reference build. */
#include "graphics_donor.h"
#include <d3d11_3.h>
#include "d3d8_combiners.h"
#include "graphics_backend.h"
#include "native_debug.h"
#include "pilot_d3d_checks.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "event_trace.h"
static IDirect3D8 *pilot_factory;
static IDirect3DDevice8 *pilot_device;
static HWND pilot_window;
static int pilot_visible;
static PilotPresentation pilot_presentation={0,0,0x11,1,0};
int pilot_d3d_has_auto_depth(void){return pilot_presentation.depth!=0;}
/* Optional raster policy is owned by the resolved static mod registry. */
__declspec(noreturn) void pilot_d3d_backend_stop(const char *reason);
#include "graphics_visual_backend.inc"
static uint32_t pilot_window_aspect[2],pilot_window_height,pilot_frame_aspect[2];
static int pilot_aspect_valid(uint32_t n,uint32_t d)
{return n && d && n<=65536 && d<=65536 && (uint64_t)n*3>=(uint64_t)d*4 && (uint64_t)n*9<=(uint64_t)d*32;}
int pilot_gfx_presentation(uint32_t numerator,uint32_t denominator,uint32_t height)
{
    if(pilot_device)return 0;
    if(!numerator && !denominator){memset(pilot_window_aspect,0,sizeof(pilot_window_aspect));pilot_window_height=0;return 1;}
#ifdef PILOT_D3D_PHOBOS
    if(!pilot_aspect_valid(numerator,denominator) || height<240 || height>2160)return 0;
    pilot_window_aspect[0]=numerator;pilot_window_aspect[1]=denominator;pilot_window_height=height;return 1;
#else
    return 0;
#endif
}
int pilot_gfx_frame_shape(uint32_t numerator,uint32_t denominator)
{
    if(!numerator && !denominator){memset(pilot_frame_aspect,0,sizeof(pilot_frame_aspect));return 1;}
#ifdef PILOT_D3D_PHOBOS
    if(!pilot_aspect_valid(numerator,denominator))return 0;
    pilot_frame_aspect[0]=numerator;pilot_frame_aspect[1]=denominator;return 1;
#else
    return 0;
#endif
}
int pilot_gfx_client_size(uint32_t size[2])
{
    RECT client;
    if(!size || !pilot_window || !GetClientRect(pilot_window,&client) || client.right<=0 || client.bottom<=0)return 0;
    size[0]=(uint32_t)client.right;size[1]=(uint32_t)client.bottom;return 1;
}
/* Called by the pinned donor's presentation resolve, after its default fit
 * policy. Rendering remains in the original guest-sized scene allocation. */
void pilot_d3d_present_shape(uint32_t *width,uint32_t *height)
{if(pilot_frame_aspect[0]){*width=pilot_frame_aspect[0];*height=pilot_frame_aspect[1];}}
static RECT pilot_client_rectangle(uint32_t width,uint32_t height)
{
    RECT client={0,0,(LONG)width,(LONG)height};
    if(pilot_window_aspect[0]) {
        client.right=(LONG)((uint64_t)pilot_window_height*pilot_window_aspect[0]/pilot_window_aspect[1]);
        client.bottom=(LONG)pilot_window_height;
        /* Keep the initial window inside the current desktop work area. */
        RECT work;if(SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0)) {
            LONG max_width=work.right-work.left-32,max_height=work.bottom-work.top-64;
            if(max_width>0 && client.right>max_width){client.bottom=(LONG)((int64_t)client.bottom*max_width/client.right);client.right=max_width;}
            if(max_height>0 && client.bottom>max_height){client.right=(LONG)((int64_t)client.right*max_height/client.bottom);client.bottom=max_height;}
        }
    }
    AdjustWindowRect(&client,WS_OVERLAPPEDWINDOW,FALSE);return client;
}
static uint32_t pilot_logic_copy,pilot_requested_blend;
static unsigned char *pilot_saved_display;
static uint32_t pilot_saved_width,pilot_saved_height;
/* Slot 0 is the drawable back buffer; slot 1 retains the last presented image.
 * Xbox GetBackBuffer(-1) must not alias the image currently being drawn. */
static ID3D11Texture2D *pilot_surfaces[3];
static unsigned pilot_surface_refs[3];
static ID3D11DepthStencilView *pilot_default_depth;
static uint32_t pilot_color_target,pilot_depth_target;
static struct PilotInitializerState pilot_initializer;
static uint32_t pilot_default_color_format;
static int pilot_initializer_target_format(void);
struct PilotImageUse;
struct PilotNativeTexture {
    IDirect3DTexture8 *object;
    ID3D11RenderTargetView *rtv;
    ID3D11DepthStencilView *dsv;
    ID3D11ShaderResourceView *base_srv;
    uint32_t uploaded,gpu_written,guest_format;
    struct PilotImageUse *use;
    uint64_t content_serial;
    ID3D11Texture2D *argb_image;
    ID3D11ShaderResourceView *argb_srv,*stencil_srv;
    ID3D11UnorderedAccessView *argb_uav;
    uint64_t argb_serial;
    unsigned argb_initialized;
    unsigned blend_depth_shape,blend_color_shape;
};
static struct PilotNativeTexture *pilot_textures;
static unsigned pilot_texture_capacity;
static struct { IDirect3DVertexBuffer8 *object; int uploaded,refetch; uint64_t last_use; } pilot_vertices[256];
static uint32_t pilot_bound_textures[4],pilot_bound_vertices[16];
static uint32_t pilot_bound_strides[16];
static unsigned pilot_blend_default_shape,pilot_blend_transient,pilot_blend_transient_proof,pilot_blend_draws;
static unsigned pilot_blend_noise;
static unsigned pilot_blend_default_depth_shape;
void pilot_gfx_noise_state(unsigned enabled) {pilot_blend_noise=enabled;}
static int pilot_blend_depth_shape(ID3D11Texture2D *image,unsigned sampled)
{
    D3D11_TEXTURE2D_DESC desc;ID3D11Texture2D_GetDesc(image,&desc);
    /* GetDesc copies immutable CPU-owned descriptor fields; no GPU work or
     * framebuffer readback. Retain this proof with the owning allocation.
     * Output-only D24S8 can be typed; sampling its depth/stencil channels
     * requires the typeless allocation used by the conversion cache. */
    return desc.MipLevels==1 && desc.ArraySize==1 && desc.SampleDesc.Count==1 &&
        !desc.SampleDesc.Quality && !(desc.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE) &&
        (desc.Format==DXGI_FORMAT_R24G8_TYPELESS ||
         (!sampled && desc.Format==DXGI_FORMAT_D24_UNORM_S8_UINT));
}
static int pilot_blend_color_shape(ID3D11Texture2D *image)
{
    D3D11_TEXTURE2D_DESC desc;ID3D11Texture2D_GetDesc(image,&desc);
    return desc.MipLevels==1 && desc.ArraySize==1 && desc.SampleDesc.Count==1 &&
        !desc.SampleDesc.Quality && !(desc.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE) &&
        (desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM);
}
static uint32_t pilot_sampling_formats[4];
static ID3D11ShaderResourceView *pilot_sampling_views[4];
static unsigned pilot_sampling_views_dirty=15;
void pilot_d3d_raster_texture_mode(UINT native,const float scale[4][4]);
void pilot_d3d_fvf_prepare(UINT fvf,const float scale[4],const float offset[4]);
#include "graphics_completion_backend.inc"
#include "graphics_visibility_backend.inc"
#include "graphics_quad_backend.inc"
#include "graphics_fan_backend.inc"
#include "graphics_index_backend.inc"
#include "graphics_flat_backend.inc"
#include "graphics_edge_backend.inc"
#include "graphics_depth_texture_backend.inc"

void pilot_gfx_pixel_fixed(void)
{
    if(!pilot_device)pilot_d3d_backend_stop("pixel binding before device creation");
    d3d8_combiners_set_pixel_shader(0);
    d3d8_shaders_prepare_pixel();
}

int32_t pilot_gfx_pixel_program(const uint32_t states[57],uint32_t texture_modes)
{
#ifdef PILOT_D3D_PHOBOS
    if(!pilot_device || !states || (states[53]&15)>8 || (texture_modes&~0xfffffu))return E_INVALIDARG;
    if(states[9]&0xe0u)return E_NOTIMPL; /* donor omits final sum complement/clamp */
    /* These are ABI register numbers, translated to the donor's host enum.
     * Unsupported donor texture programs must not produce a zero/fallback draw. */
    for(unsigned stage=0;stage<4;++stage) {
        unsigned mode=(texture_modes>>(stage*5))&31;
        if(mode>0x12 || mode==5 || mode==7 || mode==8 || mode==10 || mode==0x12)
            return E_NOTIMPL;
        if(mode==6 && (!stage || (stage>1 && ((states[56]>>(8+stage*4))&15)>=stage)))
            return E_INVALIDARG; /* A dependent lookup must read an earlier stage. */
    }
    const struct {unsigned xbox,host,count;} groups[]={
        {0,200,8},{8,208,2},{10,235,8},{18,243,8},{26,226,8},
        {34,210,8},{43,198,2},{45,218,8},{53,234,1},{55,252,2}};
    const DWORD *host=d3d8_GetRenderStates();
    for(unsigned g=0;g<sizeof(groups)/sizeof(groups[0]);++g)
        for(unsigned i=0;i<groups[g].count;++i) {
            unsigned slot=groups[g].host+i;uint32_t value=states[groups[g].xbox+i];
            if(host[slot]!=value)pilot_gfx_set_state(slot,value);
        }
    uint32_t modes=D3D8_PSTEXTUREMODES_XBOX|texture_modes;
    if(host[D3DRS_PSTEXTUREMODES]!=modes)pilot_gfx_set_state(D3DRS_PSTEXTUREMODES,modes);
    /* Nonzero selector; all structure and constants come from the state array. */
    d3d8_combiners_set_pixel_shader(0x80u|(states[53]&15));
    return S_OK;
#else
    (void)states;(void)texture_modes;return E_NOTIMPL;
#endif
}

__declspec(noreturn) void pilot_d3d_backend_stop(const char *reason)
{
    fprintf(stderr,"[D3D BACKEND STOP] %s\n",reason); fflush(stderr); ExitProcess(2);
}
HRESULT pilot_d3d_require(HRESULT result,const char *file,unsigned line)
{
    if (FAILED(result)) {
        fprintf(stderr,"[D3D FAILURE] %s:%u status=%08lx\n",file,line,(unsigned long)result);
        pilot_d3d_backend_stop("native graphics work failed");
    }
    return result;
}
static LRESULT CALLBACK pilot_window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
    if(message==WM_PAINT && !pilot_device && pilot_saved_display) {
        PAINTSTRUCT paint;HDC dc=BeginPaint(window,&paint);RECT client;GetClientRect(window,&client);
        BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=(LONG)pilot_saved_width;info.bmiHeader.biHeight=-(LONG)pilot_saved_height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        int result=StretchDIBits(dc,0,0,client.right,client.bottom,0,0,pilot_saved_width,pilot_saved_height,
            pilot_saved_display,&info,DIB_RGB_COLORS,SRCCOPY);
        EndPaint(window,&paint);
        if(result==GDI_ERROR)pilot_d3d_backend_stop("persisted display repaint failed");
        return 0;
    }
    if (message==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window,message,wparam,lparam);
}
void pilot_gfx_close(void)
{
    pilot_blend_default_shape=pilot_blend_transient=pilot_blend_transient_proof=pilot_blend_draws=0;
    pilot_blend_noise=0;
    pilot_blend_default_depth_shape=0;
    memset(pilot_bound_strides,0,sizeof(pilot_bound_strides));
    pilot_visibility_close();
    pilot_completion_close();
    pilot_quad_close();
    pilot_fan_close();
    pilot_index_close();
    pilot_flat_close();
    pilot_edge_close();
    pilot_depth_texture_close();
    pilot_visual_close();
    pilot_logic_copy=pilot_requested_blend=0;
    pilot_gfx_shaders_close();
    for (unsigned i=0;i<256;++i)
        if(pilot_vertices[i].object) pilot_d3d_backend_stop("device close with outstanding vertex ownership");
    for (unsigned i=0;i<pilot_texture_capacity;++i)
        if(pilot_textures[i].object) pilot_d3d_backend_stop("device close with outstanding texture ownership");
    free(pilot_textures);pilot_textures=NULL;pilot_texture_capacity=0;
    memset(pilot_sampling_views,0,sizeof(pilot_sampling_views));pilot_sampling_views_dirty=15;
    memset(pilot_sampling_formats,0,sizeof(pilot_sampling_formats));
    for (unsigned i=0;i<3;++i) {
        if (pilot_surface_refs[i]) pilot_d3d_backend_stop("device close with outstanding surface ownership");
        if (pilot_surfaces[i]) { ID3D11Texture2D_Release(pilot_surfaces[i]); pilot_surfaces[i]=NULL; }
        pilot_image_use_release(pilot_surface_use[i]);pilot_surface_use[i]=NULL;
    }
    if(pilot_default_depth){ID3D11DepthStencilView_Release(pilot_default_depth);pilot_default_depth=NULL;}
    pilot_color_target=pilot_depth_target=0;
    memset(&pilot_initializer,0,sizeof(pilot_initializer));pilot_default_color_format=0;
#ifdef PILOT_D3D_PHOBOS
    pilot_guest_resolve_close();
#endif
    if (pilot_device) { pilot_device->lpVtbl->Release(pilot_device); pilot_device=NULL; }
    if (pilot_factory) { pilot_factory->lpVtbl->Release(pilot_factory); pilot_factory=NULL; }
    if(pilot_window && pilot_saved_display) {InvalidateRect(pilot_window,NULL,FALSE);UpdateWindow(pilot_window);}
    else {
        if (pilot_window) { DestroyWindow(pilot_window); pilot_window=NULL; }
        pilot_visible=0;
    }
    MSG message; while (PeekMessageW(&message,NULL,WM_QUIT,WM_QUIT,PM_REMOVE)) {}
}
int32_t pilot_gfx_parameters(const uint32_t p[17])
{
    if (!p || !p[0] || !p[1] || p[0]>4096 || p[1]>4096) return E_INVALIDARG;
    PilotPresentation parsed;
    if(!pilot_presentation_parse(p,&parsed))return E_NOTIMPL;
    /* A logical-size Gaussian front image must not silently discard an
     * elective higher-resolution scene. A separate enhanced resolve owner is
     * needed before that combination can be enabled. */
    if(p[4]==0x2222 && (pilot_visual.internal_height || pilot_visual.supersample!=1))return E_NOTIMPL;
#ifndef PILOT_D3D_PHOBOS
    if(p[4]!=0x11 || !p[8] || p[10])return E_NOTIMPL;
#endif
    return S_OK;
}
int32_t pilot_gfx_create(const uint32_t p[17])
{
    if(pilot_device)return E_INVALIDARG;
    int32_t validation=pilot_gfx_parameters(p);if(validation<0)return validation;
    if(!pilot_presentation_parse(p,&pilot_presentation))return E_INVALIDARG;
    pilot_initializer.precision_policy=PILOT_COLOR_NATIVE_RGBA8;pilot_default_color_format=p[2];
    WNDCLASSW window_class={0}; window_class.lpfnWndProc=pilot_window_proc;
    window_class.hInstance=GetModuleHandleW(NULL); window_class.lpszClassName=L"xlle-native-reference";
    if (!RegisterClassW(&window_class) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return HRESULT_FROM_WIN32(GetLastError());
    RECT client=pilot_client_rectangle(p[0],p[1]);
    /* Hidden until scene qualification; a successful hidden present is not
     * treated as title cadence evidence. Tests read actual GPU output. */
    if(!pilot_window)pilot_window=CreateWindowW(window_class.lpszClassName,L"Xbox native reference",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,client.right-client.left,client.bottom-client.top,NULL,NULL,window_class.hInstance,NULL);
    if (!pilot_window) return HRESULT_FROM_WIN32(GetLastError());
    if(!SetWindowPos(pilot_window,NULL,0,0,client.right-client.left,client.bottom-client.top,
                    SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE))return HRESULT_FROM_WIN32(GetLastError());
    pilot_factory=xbox_Direct3DCreate8(0);
    if (!pilot_factory) { pilot_gfx_close(); return E_OUTOFMEMORY; }
    D3DPRESENT_PARAMETERS pp={0};
    pp.BackBufferWidth=p[0]; pp.BackBufferHeight=p[1]; pp.BackBufferFormat=(D3DFORMAT)p[2];
    pp.BackBufferCount=p[3]; pp.MultiSampleType=(D3DMULTISAMPLE_TYPE)p[4]; pp.SwapEffect=p[5];
    pp.hDeviceWindow=pilot_window; pp.Windowed=TRUE; pp.EnableAutoDepthStencil=p[8];
    pp.AutoDepthStencilFormat=(D3DFORMAT)p[9]; pp.Flags=p[10];
    pp.FullScreen_RefreshRateInHz=p[11]; pp.FullScreen_PresentationInterval=p[12];
    HRESULT result=pilot_factory->lpVtbl->CreateDevice(pilot_factory,0,1,pilot_window,0,&pp,&pilot_device);
    if (SUCCEEDED(result) && p[8]) {
        ID3D11DeviceContext_OMGetRenderTargets(d3d8_GetD3D11Context(),0,NULL,&pilot_default_depth);
        if(!pilot_default_depth)pilot_d3d_backend_stop("native default depth surface is absent");
        ID3D11Resource *resource=NULL;ID3D11DepthStencilView_GetResource(pilot_default_depth,&resource);
        result=ID3D11Resource_QueryInterface(resource,&IID_ID3D11Texture2D,(void**)&pilot_surfaces[2]);ID3D11Resource_Release(resource);
        pilot_depth_target=UINT32_MAX;
    }
    if (SUCCEEDED(result)) {
#ifdef PILOT_D3D_PHOBOS
        /* Phobos renders into a scene image and resolves it at Swap. The
         * swapchain itself contains the previous resolved presentation. */
        ID3D11Resource *scene=(ID3D11Resource*)rhi_d3d11_native_image(d3d8_GetSceneImage());
        result=scene?ID3D11Resource_QueryInterface(scene,&IID_ID3D11Texture2D,(void**)&pilot_surfaces[0]):E_FAIL;
#else
        result=IDXGISwapChain_GetBuffer(d3d8_GetSwapChain(),0,&IID_ID3D11Texture2D,(void**)&pilot_surfaces[0]);
#endif
        if (SUCCEEDED(result)) {
            D3D11_TEXTURE2D_DESC desc; ID3D11Texture2D_GetDesc(pilot_surfaces[0],&desc);
            pilot_blend_default_shape=pilot_gfx_width()==640 && pilot_gfx_height()==480 &&
                desc.Width==d3d8_GetBackbufferWidth() && desc.Height==d3d8_GetBackbufferHeight() && desc.MipLevels==1 &&
                desc.ArraySize==1 && desc.SampleDesc.Count==1 && !desc.SampleDesc.Quality &&
                !(desc.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE) && desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM;
            pilot_blend_default_depth_shape=pilot_surfaces[2] && pilot_blend_depth_shape(pilot_surfaces[2],0);
#ifdef PILOT_D3D_PHOBOS
            if(p[4]==0x2222) {
                result=pilot_guest_resolve_prepare(p[0],p[1]);
                if(SUCCEEDED(result)) {
                    pilot_surfaces[1]=(ID3D11Texture2D*)rhi_d3d11_native_image(pilot_guest_resolved.image);
                    ID3D11Texture2D_AddRef(pilot_surfaces[1]);
                }
            } else
#endif
            {
                desc.BindFlags=D3D11_BIND_SHADER_RESOURCE; desc.MiscFlags=0;
                result=ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(),&desc,NULL,&pilot_surfaces[1]);
            }
        }
    }
    if(SUCCEEDED(result))for(unsigned i=0;i<3;++i)if(pilot_surfaces[i]) {
        pilot_surface_use[i]=pilot_image_use_create();
        if(!pilot_surface_use[i]){result=E_OUTOFMEMORY;break;}
    }
    if (FAILED(result)) pilot_gfx_close();
    else {free(pilot_saved_display);pilot_saved_display=NULL;
#ifdef PILOT_D3D_PHOBOS
        pilot_donor_output_images(d3d8_GetSceneImage(),pilot_donor_default_depth(),1);
#endif
    }
    return result;
}
int32_t pilot_gfx_reset(const uint32_t p[17])
{
#ifdef PILOT_D3D_PHOBOS
    if(!pilot_device)return E_FAIL;
    HRESULT result=pilot_gfx_parameters(p);if(FAILED(result))return result;
    PilotPresentation previous=pilot_presentation,next;
    if(!pilot_presentation_parse(p,&next))return E_INVALIDARG;
    struct PilotImageUse *uses[3]={0};ID3D11Texture2D *front=NULL;
    PilotGuestResolved retained=pilot_guest_resolved,staged={0};
    for(unsigned i=0;i<3;++i)if(pilot_presentation_mask(&next)&(1u<<i)) {
        uses[i]=pilot_image_use_create();if(!uses[i]){result=E_OUTOFMEMORY;goto stage_failed;}
    }
    if(next.sample_type==0x2222) {
        memset(&pilot_guest_resolved,0,sizeof(pilot_guest_resolved));
        result=pilot_guest_resolve_prepare(p[0],p[1]);staged=pilot_guest_resolved;
        pilot_guest_resolved=retained;
        if(FAILED(result))goto stage_failed;
        front=(ID3D11Texture2D*)rhi_d3d11_native_image(staged.image);ID3D11Texture2D_AddRef(front);
    } else {
        uint32_t size[2];
        if(!pilot_visual_mods_size(&pilot_visual,p[0],p[1],p[0],p[1],16384,size)){result=E_INVALIDARG;goto stage_failed;}
        D3D11_TEXTURE2D_DESC desc={0};desc.Width=size[0];desc.Height=size[1];desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        result=ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(),&desc,NULL,&front);if(FAILED(result))goto stage_failed;
    }
    /* All fallible adapter allocations precede the donor's transactional reset.
     * Its scene-size callback also updates logical metadata: restore on failure. */
    ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();
    ID3D11RenderTargetView *old_color=NULL;ID3D11DepthStencilView *old_depth=NULL;
    ID3D11DeviceContext_OMGetRenderTargets(ctx,1,&old_color,&old_depth);
    uint32_t old_width=pilot_guest_width,old_height=pilot_guest_height,old_viewport[6];
    memcpy(old_viewport,pilot_guest_viewport,sizeof(old_viewport));pilot_presentation=next;
    extern HRESULT pilot_donor_reset_display(UINT width,UINT height,DWORD format);
    result=pilot_donor_reset_display(p[0],p[1],p[2]);
    if(FAILED(result)) {
        ID3D11DeviceContext_OMSetRenderTargets(ctx,1,&old_color,old_depth);
        pilot_presentation=previous;pilot_guest_width=old_width;pilot_guest_height=old_height;
        memcpy(pilot_guest_viewport,old_viewport,sizeof(old_viewport));
    }
    if(old_color)ID3D11RenderTargetView_Release(old_color);
    if(old_depth)ID3D11DepthStencilView_Release(old_depth);
    if(FAILED(result))goto stage_failed;
    pilot_visual_close();
    for(unsigned i=0;i<3;++i) {
        while(pilot_surface_refs[i]){ID3D11Texture2D_Release(pilot_surfaces[i]);--pilot_surface_refs[i];}
        if(pilot_surfaces[i])ID3D11Texture2D_Release(pilot_surfaces[i]);pilot_surfaces[i]=NULL;
        pilot_image_use_release(pilot_surface_use[i]);pilot_surface_use[i]=uses[i];uses[i]=NULL;
    }
    if(pilot_default_depth)ID3D11DepthStencilView_Release(pilot_default_depth);pilot_default_depth=NULL;
    pilot_guest_resolve_close();pilot_guest_resolved=staged;memset(&staged,0,sizeof(staged));
    pilot_surfaces[1]=front;front=NULL;
    if(next.depth) {
        ID3D11DeviceContext_OMGetRenderTargets(ctx,0,NULL,&pilot_default_depth);
        if(!pilot_default_depth)pilot_d3d_backend_stop("reset did not allocate requested depth storage");
        ID3D11Resource *depth=NULL;ID3D11DepthStencilView_GetResource(pilot_default_depth,&depth);
        pilot_d3d_require(ID3D11Resource_QueryInterface(depth,&IID_ID3D11Texture2D,(void**)&pilot_surfaces[2]),__FILE__,__LINE__);
        ID3D11Resource_Release(depth);
    }
    ID3D11Resource *scene=(ID3D11Resource*)rhi_d3d11_native_image(d3d8_GetSceneImage());
    pilot_d3d_require(ID3D11Resource_QueryInterface(scene,&IID_ID3D11Texture2D,(void**)&pilot_surfaces[0]),__FILE__,__LINE__);
    D3D11_TEXTURE2D_DESC desc;ID3D11Texture2D_GetDesc(pilot_surfaces[0],&desc);
    pilot_blend_default_shape=pilot_gfx_width()==640 && pilot_gfx_height()==480 &&
        desc.Width==d3d8_GetBackbufferWidth() && desc.Height==d3d8_GetBackbufferHeight() && desc.MipLevels==1 &&
        desc.ArraySize==1 && desc.SampleDesc.Count==1 && !desc.SampleDesc.Quality &&
        !(desc.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE) && desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM;
    pilot_blend_default_depth_shape=pilot_surfaces[2] && pilot_blend_depth_shape(pilot_surfaces[2],0);
    pilot_blend_transient=pilot_blend_transient_proof=0;
    pilot_color_target=0;pilot_depth_target=next.depth?UINT32_MAX:0;pilot_default_color_format=p[2];
    pilot_edge_close();
    memset(&pilot_initializer,0,sizeof(pilot_initializer));pilot_initializer.precision_policy=PILOT_COLOR_NATIVE_RGBA8;
    pilot_donor_output_images(d3d8_GetSceneImage(),pilot_donor_default_depth(),1);
    /* Guest Reset reallocates console presentation storage. The host window
     * belongs to the player: preserve its size, position and maximized state.
     * The donor's next present fits/resizes its swapchain to that client area. */
    free(pilot_saved_display);pilot_saved_display=NULL;
    return S_OK;
stage_failed:
    if(front)ID3D11Texture2D_Release(front);
    for(unsigned i=0;i<3;++i)pilot_image_use_release(uses[i]);
    pilot_guest_resolved=staged;pilot_guest_resolve_close();pilot_guest_resolved=retained;
    return result;
#else
    (void)p;return E_NOTIMPL;
#endif
}
int32_t pilot_gfx_clear(uint32_t flags,uint32_t color,float depth,uint32_t stencil)
{
    if (!pilot_device) return E_FAIL;
    ID3D11RenderTargetView *rtv=NULL;ID3D11DepthStencilView *dsv=NULL;
    ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();ID3D11DeviceContext_OMGetRenderTargets(ctx,1,&rtv,&dsv);
    HRESULT hr=S_OK;
    if((flags&1) && !rtv || (flags&6) && !dsv)hr=E_INVALIDARG;
    if(SUCCEEDED(hr)) {
        if(flags&1){float rgba[]={((color>>16)&255)/255.0f,((color>>8)&255)/255.0f,(color&255)/255.0f,(color>>24)/255.0f};ID3D11DeviceContext_ClearRenderTargetView(ctx,rtv,rgba);}
        if(flags&6)ID3D11DeviceContext_ClearDepthStencilView(ctx,dsv,((flags&2)?1:0)|((flags&4)?2:0),depth,(UINT8)stencil);
        if((flags&1) && pilot_color_target){pilot_textures[pilot_color_target-1].uploaded=1;pilot_textures[pilot_color_target-1].gpu_written=1;}
        if((flags&6) && pilot_depth_target && pilot_depth_target!=UINT32_MAX){pilot_textures[pilot_depth_target-1].uploaded=1;pilot_textures[pilot_depth_target-1].gpu_written=1;}
    }
    if(SUCCEEDED(hr) && flags)pilot_mark_targets(flags,pilot_mark_work());
    if(rtv)ID3D11RenderTargetView_Release(rtv);if(dsv)ID3D11DepthStencilView_Release(dsv);return hr;
}
int32_t pilot_gfx_viewport(const void *viewport)
{
    if (!pilot_device) return E_FAIL;
    HRESULT result=pilot_device->lpVtbl->SetViewport(pilot_device,(const D3DVIEWPORT8*)viewport);
    if(SUCCEEDED(result)) {
        memcpy(pilot_guest_viewport,viewport,sizeof(pilot_guest_viewport));
        if(pilot_visual_scaled() || pilot_presentation.sample_type==0x2222) {
            /* Resources retained over Reset keep their own physical scale.
             * Derive it from the actual current target, not today's backbuffer. */
            ID3D11RenderTargetView *rtv=NULL;ID3D11Resource *resource=NULL;
            ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();
            ID3D11DeviceContext_OMGetRenderTargets(ctx,1,&rtv,NULL);
            if(!rtv)return E_FAIL;ID3D11RenderTargetView_GetResource(rtv,&resource);ID3D11RenderTargetView_Release(rtv);
            D3D11_TEXTURE2D_DESC desc;ID3D11Texture2D_GetDesc((ID3D11Texture2D*)resource,&desc);ID3D11Resource_Release(resource);
            D3D8Texture *target=pilot_color_target?(D3D8Texture*)pilot_textures[pilot_color_target-1].object:NULL;
            float sx=(float)desc.Width/(target?target->width:pilot_gfx_width());
            float sy=(float)desc.Height/(target?target->height:pilot_gfx_height());
            const D3DVIEWPORT8 *v=viewport;
            D3D11_VIEWPORT scaled={v->X*sx,v->Y*sy,v->Width*sx,v->Height*sy,v->MinZ,v->MaxZ};
            ID3D11DeviceContext_RSSetViewports(ctx,1,&scaled);
        }
    }
    return result;
}
int32_t pilot_gfx_get_viewport(uint32_t viewport[6])
{
    if (!pilot_device || !viewport) return E_INVALIDARG;
    if(pilot_visual_scaled() || pilot_presentation.sample_type==0x2222){memcpy(viewport,pilot_guest_viewport,sizeof(pilot_guest_viewport));return S_OK;}
    D3D11_VIEWPORT current; UINT count=1;
    ID3D11DeviceContext_RSGetViewports(d3d8_GetD3D11Context(),&count,&current);
    if (count!=1) return E_FAIL;
    viewport[0]=(uint32_t)current.TopLeftX; viewport[1]=(uint32_t)current.TopLeftY;
    viewport[2]=(uint32_t)current.Width; viewport[3]=(uint32_t)current.Height;
    memcpy(viewport+4,&current.MinDepth,4); memcpy(viewport+5,&current.MaxDepth,4);
    return S_OK;
}
int32_t pilot_gfx_transform(unsigned state,const float matrix[16])
{
    static const D3DTRANSFORMSTATETYPE states[]={D3DTS_VIEW,D3DTS_PROJECTION,
        D3DTS_TEXTURE0,D3DTS_TEXTURE1,D3DTS_TEXTURE2,D3DTS_TEXTURE3,
        D3DTS_WORLD,D3DTS_WORLD1,D3DTS_WORLD2,D3DTS_WORLD3};
    if(!pilot_device || state>=10 || !matrix)return E_INVALIDARG;
    return pilot_device->lpVtbl->SetTransform(pilot_device,states[state],(const D3DMATRIX*)matrix);
}
int32_t pilot_gfx_present(void)
{
    if (!pilot_device) return E_FAIL;
    const char *visible=getenv("XLLE_PILOT_VISIBLE");
    if(!pilot_visible && visible && !strcmp(visible,"1")) {
        ShowWindow(pilot_window,SW_SHOWNOACTIVATE);UpdateWindow(pilot_window);pilot_visible=1;
    }
    uint64_t started=pilot_gpu_call_begin();
#ifdef PILOT_D3D_PHOBOS
    if(pilot_presentation.sample_type==0x2222) {
        int32_t resolved=pilot_guest_resolve();if(resolved<0)return resolved;
    } else
#endif
    ID3D11DeviceContext_CopyResource(d3d8_GetD3D11Context(),(ID3D11Resource*)pilot_surfaces[1],(ID3D11Resource*)pilot_surfaces[0]);
    pilot_gpu_call_end("present CopyResource",started);
    pilot_surface_use[0]->serial=pilot_surface_use[1]->serial=pilot_mark_work();
    started=pilot_gpu_call_begin();
#ifdef PILOT_D3D_PHOBOS
    pilot_visual_real_present=1;
#endif
    int32_t result=pilot_device->lpVtbl->Swap(pilot_device,0);
#ifdef PILOT_D3D_PHOBOS
    pilot_visual_real_present=0;
#endif
    pilot_gpu_call_end("donor Swap",started);
    if(SUCCEEDED(result))pilot_debug_frame();
#ifndef PILOT_D3D_PHOBOS
    uint64_t request=pilot_debug_capture_requested();
    if(request)pilot_debug_capture_complete(request,NULL,0,0,E_NOTIMPL);
#endif
    return result;
}
int32_t pilot_gfx_persist_display(const unsigned char *bgra,uint32_t width,uint32_t height,uint32_t pitch)
{
    if(!pilot_device || !bgra || width!=pilot_gfx_width() || height!=pilot_gfx_height() || pitch<width*4)return E_INVALIDARG;
    unsigned char *copy=malloc((size_t)width*height*4);if(!copy)return E_OUTOFMEMORY;
    for(uint32_t y=0;y<height;++y)memcpy(copy+(size_t)y*width*4,bgra+(size_t)y*pitch,width*4);
    free(pilot_saved_display);pilot_saved_display=copy;pilot_saved_width=width;pilot_saved_height=height;
    return S_OK;
}
int32_t pilot_gfx_surface_ref(unsigned surface,int acquire)
{
    if (surface>=3 || !pilot_surfaces[surface]) return E_INVALIDARG;
    if (acquire) {
        ID3D11Texture2D_AddRef(pilot_surfaces[surface]); ++pilot_surface_refs[surface];
    } else {
        if (!pilot_surface_refs[surface]) return E_INVALIDARG;
        ID3D11Texture2D_Release(pilot_surfaces[surface]); --pilot_surface_refs[surface];
    }
    return (int32_t)pilot_surface_refs[surface];
}
static D3D8VertexBuffer *pilot_vertex(uint32_t handle)
{
    if(!handle || handle>256 || !pilot_vertices[handle-1].object)
        pilot_d3d_backend_stop("unowned native vertex buffer");
    return (D3D8VertexBuffer*)pilot_vertices[handle-1].object;
}
int32_t pilot_gfx_vertex_create(uint32_t bytes,uint32_t *handle)
{
    if(!pilot_device || !handle || !bytes || bytes>64u*1024*1024) return E_INVALIDARG;
    unsigned slot;
    for(slot=0;slot<256 && pilot_vertices[slot].object;++slot) {}
    if(slot==256) return E_OUTOFMEMORY;
    IDirect3DVertexBuffer8 *object=NULL;
    HRESULT hr=d3d8_CreateVertexBufferImpl(bytes,0,0,&object);
    if(FAILED(hr)) return hr;
    pilot_vertices[slot].object=object; pilot_vertices[slot].uploaded=0;
    *handle=slot+1; return S_OK;
}
void pilot_gfx_vertex_destroy(uint32_t handle)
{
    D3D8VertexBuffer *vertex=pilot_vertex(handle);
    for(unsigned i=0;i<16;++i)if(pilot_bound_vertices[i]==handle)pilot_d3d_backend_stop("destroying a bound vertex buffer");
    if(vertex->iface.lpVtbl->Release(&vertex->iface)!=0)
        pilot_d3d_backend_stop("native vertex buffer still referenced at destruction");
    memset(&pilot_vertices[handle-1],0,sizeof(pilot_vertices[0]));
}
void pilot_gfx_vertex_bind(unsigned stream,uint32_t handle,uint32_t stride)
{
    if(!pilot_device || stream>=16 || stride>2048)pilot_d3d_backend_stop("invalid native vertex binding");
    ID3D11Buffer *buffer=handle?pilot_donor_vertex(pilot_vertex(handle)):NULL;UINT offset=0;
    ID3D11DeviceContext_IASetVertexBuffers(d3d8_GetD3D11Context(),stream,1,&buffer,&stride,&offset);
    pilot_bound_vertices[stream]=handle;
    pilot_bound_strides[stream]=handle?stride:0;
}
void pilot_gfx_vertex_upload(uint32_t handle,const void *data,uint32_t bytes)
{
    D3D8VertexBuffer *vertex=pilot_vertex(handle);
    if(!data || bytes!=vertex->size) pilot_d3d_backend_stop("invalid vertex upload range");
    if(pilot_vertices[handle-1].uploaded && !pilot_vertices[handle-1].refetch && !memcmp(vertex->sys_mem,data,bytes)) return;
    BYTE *destination=NULL;
    pilot_d3d_require(vertex->iface.lpVtbl->Lock(&vertex->iface,0,bytes,&destination,0),__FILE__,__LINE__);
    memcpy(destination,data,bytes);
    pilot_d3d_require(vertex->iface.lpVtbl->Unlock(&vertex->iface),__FILE__,__LINE__);
    pilot_vertices[handle-1].uploaded=1;pilot_vertices[handle-1].refetch=0;
    pilot_vertices[handle-1].last_use=pilot_mark_work();
}
void pilot_gfx_vertex_upload_range(uint32_t handle,const void *data,uint32_t offset,uint32_t bytes)
{
    D3D8VertexBuffer *vertex=pilot_vertex(handle);
    if(!data || !bytes || offset>vertex->size || bytes>vertex->size-offset)
        pilot_d3d_backend_stop("invalid vertex synchronization span");
    /* Startup-only A/B selection. Phobos's HLE vertex bridge submits only
     * draw-consumed vertices; retain our multistream bindings and ownership. */
    static int ranges=-1;
    if(ranges<0) {const char *setting=getenv("XLLE_PILOT_VERTEX_RANGES");ranges=!(setting && !strcmp(setting,"0"));}
    if(!ranges || !pilot_vertices[handle-1].uploaded || pilot_vertices[handle-1].refetch) {
        pilot_gfx_vertex_upload(handle,data,vertex->size);return;
    }
    const BYTE *source=(const BYTE*)data+offset;
    if(!memcmp(vertex->sys_mem+offset,source,bytes))return;
    /* DEFAULT storage and the immediate context: UpdateSubresource snapshots
     * source bytes and preserves earlier draws before this ordered update. */
    D3D11_BOX box={offset,0,0,offset+bytes,1,1};
    ID3D11DeviceContext_UpdateSubresource(d3d8_GetD3D11Context(),
        (ID3D11Resource*)pilot_donor_vertex(vertex),0,&box,source,0,0);
    memcpy(vertex->sys_mem+offset,source,bytes);
    pilot_vertices[handle-1].last_use=pilot_mark_work();
}
int32_t pilot_gfx_vertex_readback(uint32_t handle,void *data,uint32_t capacity)
{
    D3D8VertexBuffer *vertex=pilot_vertex(handle);
    if(!data || capacity<vertex->size || !pilot_vertices[handle-1].uploaded) return E_INVALIDARG;
    D3D11_BUFFER_DESC desc;
    ID3D11Buffer_GetDesc(pilot_donor_vertex(vertex),&desc);
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.MiscFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ID3D11Buffer *staging=NULL;
    HRESULT hr=ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(),&desc,NULL,&staging);
    if(FAILED(hr)) return hr;
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11DeviceContext_CopyResource(context,(ID3D11Resource*)staging,(ID3D11Resource*)pilot_donor_vertex(vertex));
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr=ID3D11DeviceContext_Map(context,(ID3D11Resource*)staging,0,D3D11_MAP_READ,0,&mapped);
    if(SUCCEEDED(hr)) {
        memcpy(data,mapped.pData,vertex->size);
        ID3D11DeviceContext_Unmap(context,(ID3D11Resource*)staging,0);
    }
    ID3D11Buffer_Release(staging); return hr;
}
static D3D8Texture *pilot_texture(uint32_t handle)
{
    if(!handle || handle>pilot_texture_capacity || !pilot_textures[handle-1].object)
        pilot_d3d_backend_stop("unowned native texture");
    return (D3D8Texture*)pilot_textures[handle-1].object;
}
uint64_t pilot_gfx_resource_serial(unsigned kind,uint32_t handle)
{
    if(!pilot_device)pilot_d3d_backend_stop("completion query before device creation");
    uint64_t serial;
    switch(kind) {
    case PILOT_GFX_DEVICE:serial=pilot_submitted;break;
    case PILOT_GFX_SURFACE:
        if(handle>=3 || !pilot_surfaces[handle])pilot_d3d_backend_stop("unowned surface completion query");
        serial=pilot_surface_use[handle]->serial;break;
    case PILOT_GFX_TEXTURE: {
        pilot_texture(handle);serial=pilot_textures[handle-1].use->serial;
        break;
    }
    case PILOT_GFX_VERTEX:pilot_vertex(handle);serial=pilot_vertices[handle-1].last_use;break;
    default:pilot_d3d_backend_stop("invalid completion resource kind");
    }
    return serial;
}
int pilot_gfx_resource_busy(unsigned kind,uint32_t handle)
{return pilot_serial_busy(pilot_gfx_resource_serial(kind,handle));}
uint32_t pilot_gfx_feedback_mask(void)
{
    uint32_t mask=0;
    for(unsigned stage=0;stage<4;++stage)if(pilot_bound_textures[stage]) {
        ID3D11Texture2D *sample=pilot_donor_texture(pilot_texture(pilot_bound_textures[stage]));
        if(sample==(pilot_color_target?pilot_donor_texture(pilot_texture(pilot_color_target)):pilot_surfaces[0]))mask|=1u<<stage;
        if(pilot_depth_target && sample==(pilot_depth_target==UINT32_MAX?pilot_surfaces[2]:pilot_donor_texture(pilot_texture(pilot_depth_target))))mask|=1u<<(stage+8);
    }
    return mask;
}
static uint32_t pilot_feedback_validate(void)
{
    /* A native-size lookup target may borrow the large default depth surface
     * for a depthless pass. It must not test depth using a different pixel
     * grid. Retained resources after Reset have their own real scale. */
    if(pilot_visual_scaled() && pilot_depth_target &&
       (d3d8_GetRenderStates()[D3DRS_ZENABLE] || d3d8_GetRenderStates()[D3DRS_STENCILENABLE])) {
        ID3D11Texture2D *color=pilot_color_target?pilot_donor_texture(pilot_texture(pilot_color_target)):pilot_surfaces[0];
        ID3D11Texture2D *depth=pilot_depth_target==UINT32_MAX?pilot_surfaces[2]:pilot_donor_texture(pilot_texture(pilot_depth_target));
        D3D11_TEXTURE2D_DESC c,d;ID3D11Texture2D_GetDesc(color,&c);ID3D11Texture2D_GetDesc(depth,&d);
        uint64_t cw=pilot_color_target?pilot_texture(pilot_color_target)->width:pilot_gfx_width();
        uint64_t ch=pilot_color_target?pilot_texture(pilot_color_target)->height:pilot_gfx_height();
        uint64_t dw=pilot_depth_target==UINT32_MAX?pilot_gfx_width():pilot_texture(pilot_depth_target)->width;
        uint64_t dh=pilot_depth_target==UINT32_MAX?pilot_gfx_height():pilot_texture(pilot_depth_target)->height;
        int64_t dx=(int64_t)(c.Width*dw)-(int64_t)(d.Width*cw);
        int64_t dy=(int64_t)(c.Height*dh)-(int64_t)(d.Height*ch);
        if(llabs(dx)>(int64_t)(cw>dw?cw:dw) || llabs(dy)>(int64_t)(ch>dh?ch:dh))
            pilot_d3d_backend_stop("enhanced target and depth use incompatible raster grids");
    }
    uint32_t mask=pilot_gfx_feedback_mask();
    if(mask&0xf00u)pilot_d3d_backend_stop("depth-output feedback is unqualified");
    for(unsigned stage=0;stage<4;++stage)if(mask&(1u<<stage)) {
        D3D8Texture *texture=pilot_texture(pilot_bound_textures[stage]);
        if(texture->levels!=1 || texture->d3d8_format!=0x12)
            pilot_d3d_backend_stop("feedback requires a single-level linear ARGB8 texture");
    }
#ifndef PILOT_D3D_PHOBOS
    if(mask)pilot_d3d_backend_stop("selected donor lacks qualified feedback");
#endif
    return mask;
}
void pilot_gfx_texture_bind(unsigned stage,uint32_t handle)
{
    if(!pilot_device || stage>=4)pilot_d3d_backend_stop("invalid native texture binding");
    D3D8Texture *texture=handle?pilot_texture(handle):NULL;
    if(handle && (pilot_donor_texture(texture)==(pilot_color_target?pilot_donor_texture(pilot_texture(pilot_color_target)):pilot_surfaces[0]) ||
        (pilot_depth_target && pilot_donor_texture(texture)==(pilot_depth_target==UINT32_MAX?pilot_surfaces[2]:pilot_donor_texture(pilot_texture(pilot_depth_target)))))) {
        if(getenv("XLLE_PILOT_FEEDBACK_TRACE"))fprintf(stderr,"[GPU OVERLAP] stage=%u texture=%u color=%u depth=%u format=%x levels=%u size=%ux%u\n",
            stage,handle,pilot_color_target,pilot_depth_target,texture->d3d8_format,texture->levels,texture->width,texture->height);
        /* Binding alone performs no sampling; draw submission guards overlap. */
    }
    uint64_t started=pilot_gpu_call_begin();
    pilot_d3d_require(pilot_device->lpVtbl->SetTexture(pilot_device,stage,(IDirect3DBaseTexture8*)texture),__FILE__,__LINE__);
    pilot_gpu_call_end("donor SetTexture",started);
    pilot_bound_textures[stage]=handle;
    pilot_sampling_formats[stage]=texture?pilot_textures[handle-1].guest_format:0;
    pilot_sampling_views_dirty|=1u<<stage;
}
void pilot_gfx_texture_sampling_format(unsigned stage,uint32_t format)
{
    if(!pilot_device || stage>=4 || !pilot_bound_textures[stage])
        pilot_d3d_backend_stop("sampling format requires a bound texture");
    D3D8Texture *texture=pilot_texture(pilot_bound_textures[stage]);
    if(format!=pilot_textures[pilot_bound_textures[stage]-1].guest_format && !(format==0x12 && texture->d3d8_format==0x2e))
        pilot_d3d_backend_stop("unqualified texture sampling reinterpretation");
    if(pilot_sampling_formats[stage]!=format) {
        pilot_sampling_formats[stage]=format;pilot_sampling_views_dirty|=1u<<stage;
    }
}
DWORD pilot_d3d_texture_sampling_format(UINT stage,DWORD allocation)
{
    return stage<4 && pilot_bound_textures[stage]?pilot_sampling_formats[stage]:allocation;
}
/* Clamping sampler MaxLOD to zero also selects the magnification filter on
 * D3D11. A cached one-level resource view keeps the original LOD0 minification
 * filter independent, using the native GPU sampler and unchanged texture bytes. */
void pilot_d3d_texture_sampling_views(void)
{
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    for(unsigned stage=0;stage<4;++stage) {
        uint32_t handle=pilot_bound_textures[stage];
        if(!handle) {pilot_sampling_views[stage]=NULL;continue;}
        D3D8Texture *texture=pilot_texture(handle);
#ifdef PILOT_D3D_PHOBOS
        ID3D11ShaderResourceView *view=(ID3D11ShaderResourceView*)rhi_d3d11_native_view(texture->srv);
#else
        ID3D11ShaderResourceView *view=texture->srv;
#endif
        if(texture->d3d8_format==0x2e && pilot_sampling_formats[stage]==0x12)
            view=pilot_depth_texture_argb(handle,view);
        if(texture->levels>1 && !d3d8_GetTSS(stage)[D3DTSS_MIPFILTER]) {
            ID3D11ShaderResourceView **base=&pilot_textures[handle-1].base_srv;
            if(!*base) {
                D3D11_SHADER_RESOURCE_VIEW_DESC desc;ID3D11ShaderResourceView_GetDesc(view,&desc);
                if(desc.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D)pilot_d3d_backend_stop("LOD0 view requires 2D texture");
                desc.Texture2D.MipLevels=1;
                pilot_d3d_require(ID3D11Device_CreateShaderResourceView(d3d8_GetD3D11Device(),
                    (ID3D11Resource*)pilot_donor_texture(texture),&desc,base),__FILE__,__LINE__);
            }
            view=*base;
        }
        if((pilot_sampling_views_dirty&(1u<<stage)) || pilot_sampling_views[stage]!=view)
            ID3D11DeviceContext_PSSetShaderResources(context,stage,1,&view);
        pilot_sampling_views[stage]=view;
    }
    pilot_sampling_views_dirty=0;
}
void pilot_gfx_texture_prepare_sample(uint32_t handle,uint32_t level,const void *data,uint32_t bytes,int cpu_exposed)
{
    D3D8Texture *texture=pilot_texture(handle);
    if(level>=texture->levels)pilot_d3d_backend_stop("invalid sampled texture level");
    uint64_t started=pilot_gpu_call_begin();
    if(cpu_exposed || !(pilot_textures[handle-1].uploaded&(1u<<level)))pilot_gfx_texture_upload(handle,level,data,bytes);
    pilot_gpu_call_end("texture upload",started);
}
int32_t pilot_gfx_targets(uint32_t color,uint32_t depth)
{
    if(pilot_initializer.dither && color && !pilot_initializer_format(pilot_texture(color)->d3d8_format))return E_NOTIMPL;
    pilot_sampling_views_dirty=15;
#ifndef PILOT_D3D_PHOBOS
    for(unsigned i=0;i<4;++i)if(pilot_bound_textures[i]) {
        ID3D11Texture2D *sample=pilot_donor_texture(pilot_texture(pilot_bound_textures[i]));
        if(sample==(color?pilot_donor_texture(pilot_texture(color)):pilot_surfaces[0]) ||
           (depth && sample==(depth==UINT32_MAX?pilot_surfaces[2]:pilot_donor_texture(pilot_texture(depth)))))return E_INVALIDARG;
    }
#endif
    uint64_t started=pilot_gpu_call_begin();
    ID3D11RenderTargetView *rtv=d3d8_GetDefaultRTV();
    if(depth==UINT32_MAX && !pilot_default_depth)return E_INVALIDARG;
    ID3D11DepthStencilView *dsv=depth==UINT32_MAX?pilot_default_depth:NULL;
    unsigned width=pilot_gfx_width(),height=pilot_gfx_height();
    if(color){D3D8Texture *t=pilot_texture(color);rtv=pilot_textures[color-1].rtv;if(!rtv)return E_INVALIDARG;width=t->width;height=t->height;}
    if(depth && depth!=UINT32_MAX){D3D8Texture *t=pilot_texture(depth);dsv=pilot_textures[depth-1].dsv;if(!dsv || t->width<width || t->height<height)return E_INVALIDARG;}
    if(depth==UINT32_MAX && (width>pilot_gfx_width() || height>pilot_gfx_height()))return E_INVALIDARG;
#ifdef PILOT_D3D_PHOBOS
    /* Remove old outputs before restoring their sampled views. Overlapping
     * stages keep their real guest/donor bindings; a draw uses the snapshot. */
    ID3D11DeviceContext_OMSetRenderTargets(d3d8_GetD3D11Context(),0,NULL,NULL);
    pilot_donor_output_images(color?pilot_texture(color)->image:d3d8_GetSceneImage(),
        depth==UINT32_MAX?pilot_donor_default_depth():(depth?pilot_texture(depth)->image:NULL),1);
#endif
    ID3D11DeviceContext_OMSetRenderTargets(d3d8_GetD3D11Context(),1,&rtv,dsv);
    pilot_gpu_call_end("targets",started);
    pilot_color_target=color;pilot_depth_target=depth;return S_OK;
}
int32_t pilot_gfx_texture_create(uint32_t width,uint32_t height,uint32_t levels,uint32_t format,uint32_t *handle)
{return pilot_gfx_texture_create_usage(width,height,levels,format,0,handle);}
int32_t pilot_gfx_texture_create_usage(uint32_t width,uint32_t height,uint32_t levels,uint32_t format,uint32_t usage,uint32_t *handle)
{
    if(!pilot_device || !handle) return E_INVALIDARG;
    int yuv=format==0x24 || format==0x25;
#ifndef PILOT_D3D_PHOBOS
    if(yuv)return E_NOTIMPL;
#endif
    if(yuv && (usage || (width&1)))return E_INVALIDARG;
    int linear=format==0x12 || format==0x1e || format==0x2e || yuv;
    if(!width || !height || width>4096 || height>4096 || (!linear && ((width&(width-1)) || (height&(height-1)))))return E_NOTIMPL;
    if(!levels || levels>13 || (linear && levels!=1) || (format==0x2e?usage!=2:(usage && (usage!=1 || !linear))) ||
       !(linear || (format>=4 && format<=7) || format==0xc || format==0xe || format==0xf || format==0x28))return E_NOTIMPL;
    unsigned slot;
    for(slot=0;slot<pilot_texture_capacity && pilot_textures[slot].object;++slot) {}
    if(slot==pilot_texture_capacity) {
        if(pilot_texture_capacity>UINT_MAX/2/sizeof(*pilot_textures))return E_OUTOFMEMORY;
        unsigned capacity=pilot_texture_capacity?pilot_texture_capacity*2:256;
        struct PilotNativeTexture *next=realloc(pilot_textures,capacity*sizeof(*next));
        if(!next)return E_OUTOFMEMORY;
        memset(next+pilot_texture_capacity,0,(capacity-pilot_texture_capacity)*sizeof(*next));
        pilot_textures=next;pilot_texture_capacity=capacity;
    }
    IDirect3DTexture8 *object=NULL;
    /* NV2A G8B8 (0x28) samples unsigned (B,G,B,G), including alpha=G.
     * Keep guest layout/format in the bridge; use ordinary BGRA storage on
     * either donor and expand only changed CPU uploads. This is not V8U8.
     * Reference: xemu GL/Vulkan SZ_G8B8 swizzles, Cxbx G8B8 conversion. */
    HRESULT hr=d3d8_CreateTextureImpl(width,height,levels,usage,(D3DFORMAT)(format==0x28?6:format),&object);
    if(FAILED(hr)) return hr;
    ID3D11RenderTargetView *rtv=NULL;
    ID3D11DepthStencilView *dsv=NULL;
    if(usage==1) {
        hr=ID3D11Device_CreateRenderTargetView(d3d8_GetD3D11Device(),(ID3D11Resource*)pilot_donor_texture((D3D8Texture*)object),NULL,&rtv);
        if(FAILED(hr)){object->lpVtbl->Release(object);return hr;}
    }
    if(usage==2) {
        D3D11_DEPTH_STENCIL_VIEW_DESC desc={0};desc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;desc.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        hr=ID3D11Device_CreateDepthStencilView(d3d8_GetD3D11Device(),(ID3D11Resource*)pilot_donor_texture((D3D8Texture*)object),&desc,&dsv);
        if(FAILED(hr)){object->lpVtbl->Release(object);return hr;}
    }
    struct PilotImageUse *use=pilot_image_use_create();
    if(!use) {
        if(rtv)ID3D11RenderTargetView_Release(rtv);
        if(dsv)ID3D11DepthStencilView_Release(dsv);
        object->lpVtbl->Release(object);return E_OUTOFMEMORY;
    }
    pilot_textures[slot].use=use;
    pilot_textures[slot].rtv=rtv;
    pilot_textures[slot].dsv=dsv;
    pilot_textures[slot].object=object; pilot_textures[slot].uploaded=0;
    pilot_textures[slot].guest_format=format;
    pilot_textures[slot].blend_depth_shape=pilot_blend_depth_shape(pilot_donor_texture((D3D8Texture*)object),1);
    pilot_textures[slot].blend_color_shape=pilot_blend_color_shape(pilot_donor_texture((D3D8Texture*)object));
    *handle=slot+1;
    return S_OK;
}
void pilot_gfx_texture_destroy(uint32_t handle)
{
    D3D8Texture *texture=pilot_texture(handle);
    if(handle==pilot_color_target || handle==pilot_depth_target)pilot_d3d_backend_stop("destroying a bound target");
    for(unsigned i=0;i<4;++i)if(pilot_bound_textures[i]==handle)pilot_d3d_backend_stop("destroying a sampled texture");
    if(pilot_textures[handle-1].rtv)ID3D11RenderTargetView_Release(pilot_textures[handle-1].rtv);
    if(pilot_textures[handle-1].dsv)ID3D11DepthStencilView_Release(pilot_textures[handle-1].dsv);
    if(pilot_textures[handle-1].base_srv)ID3D11ShaderResourceView_Release(pilot_textures[handle-1].base_srv);
    pilot_depth_texture_destroy(&pilot_textures[handle-1]);
    if(texture->iface.lpVtbl->Release(&texture->iface)!=0)
        pilot_d3d_backend_stop("native texture still referenced at destruction");
    pilot_image_use_release(pilot_textures[handle-1].use);
    memset(&pilot_textures[handle-1],0,sizeof(pilot_textures[0]));
}
int32_t pilot_gfx_texture_alias(unsigned kind,uint32_t source,uint32_t format,uint32_t *handle)
{
#ifdef PILOT_D3D_PHOBOS
    if(!pilot_device || !handle || format!=0x12)return E_NOTIMPL;
    ID3D11Texture2D *native=NULL;RhiImage *image=NULL;struct PilotImageUse *use=NULL;
    if(kind==PILOT_GFX_SURFACE && source<2){native=pilot_surfaces[source];use=pilot_surface_use[source];}
    else if(kind==PILOT_GFX_TEXTURE){D3D8Texture *t=pilot_texture(source);native=pilot_donor_texture(t);image=t->image;use=pilot_textures[source-1].use;}
    if(!native)return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC desc;ID3D11Texture2D_GetDesc(native,&desc);
    if(desc.MipLevels!=1 || desc.ArraySize!=1 || desc.SampleDesc.Count!=1 || !(desc.BindFlags&D3D11_BIND_SHADER_RESOURCE))return E_NOTIMPL;
    /* Borrow the donor's actual reference-counted RHI storage and sampled view.
     * No GPU/CPU copy and no independent pixel cache: later writes stay visible. */
    image=image?rhi_image_retain(image):rhi_d3d11_wrap_image(native);
    if(!image)return E_OUTOFMEMORY;
    RhiView *view=rhi_view_create(image,RHI_VIEW_SAMPLED,NULL);
    if(!view){rhi_image_destroy(image);return E_FAIL;}
    unsigned logical_width=kind==PILOT_GFX_SURFACE?pilot_presentation.width*(source==0?pilot_presentation_axis(&pilot_presentation):1):pilot_texture(source)->width;
    unsigned logical_height=kind==PILOT_GFX_SURFACE?pilot_presentation.height*(source==0?pilot_presentation_axis(&pilot_presentation):1):pilot_texture(source)->height;
    HRESULT hr=pilot_gfx_texture_create(logical_width,logical_height,1,format,handle);
    if(FAILED(hr)){rhi_view_destroy(view);rhi_image_destroy(image);return hr;}
    D3D8Texture *texture=pilot_texture(*handle);
    rhi_view_destroy(texture->srv);rhi_image_destroy(texture->image);
    texture->image=image;texture->srv=view;
    pilot_image_use_retain(use);pilot_image_use_release(pilot_textures[*handle-1].use);
    pilot_textures[*handle-1].use=use;
    RhiImageDesc info;rhi_image_get_desc(image,&info);texture->host_format=info.format;
    pilot_textures[*handle-1].uploaded=1;pilot_textures[*handle-1].gpu_written=1;
    return S_OK;
#else
    (void)kind;(void)source;(void)format;(void)handle;return E_NOTIMPL;
#endif
}
void pilot_gfx_texture_upload(uint32_t handle,uint32_t level,const void *data,uint32_t bytes)
{
    D3D8Texture *texture=pilot_texture(handle);
    if(level>=texture->levels || !data) pilot_d3d_backend_stop("invalid texture upload level/data");
    UINT width=texture->width>>level,height=texture->height>>level;
    if(!width)width=1; if(!height)height=1;
    UINT rows=d3d8_format_is_compressed(texture->d3d8_format)?(height+3)/4:height;
    int depth=texture->d3d8_format==0x2e;
    int yuv=texture->d3d8_format==0x24 || texture->d3d8_format==0x25;
    int pair=pilot_textures[handle-1].guest_format==0x28;
    int linear=texture->d3d8_format==0x12 || texture->d3d8_format==0x1e || depth || yuv;
    UINT pitch=d3d8_row_pitch(texture->d3d8_format,width),guest_pitch=pair?width*2:(linear?(pitch+63)&~63u:pitch);
    if(bytes!=guest_pitch*rows)
        pilot_d3d_backend_stop("texture upload size does not match subresource");
    if(depth) {
        int changed=!pilot_textures[handle-1].uploaded || pilot_textures[handle-1].gpu_written;
        for(UINT row=0;row<rows && !changed;++row)changed=memcmp(texture->sys_mem+row*pitch,(const BYTE*)data+row*guest_pitch,pitch)!=0;
        if(!changed)return;
        /* Xbox Z24S8 stores stencil in the low byte; native D24S8 stores it
         * in the high byte. CPU writes use real staging copies, since D3D11
         * forbids UpdateSubresource on a depth-stencil destination. */
        D3D11_TEXTURE2D_DESC desc;ID3D11Texture2D_GetDesc(pilot_donor_texture(texture),&desc);
        desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        ID3D11Texture2D *staging=NULL;
        pilot_d3d_require(ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(),&desc,NULL,&staging),__FILE__,__LINE__);
        D3D11_MAPPED_SUBRESOURCE mapped;ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();
        pilot_d3d_require(ID3D11DeviceContext_Map(ctx,(ID3D11Resource*)staging,0,D3D11_MAP_WRITE,0,&mapped),__FILE__,__LINE__);
        for(UINT y=0;y<desc.Height;++y)for(UINT x=0;x<desc.Width;++x) {
            unsigned sx=pilot_visual_sample(x,desc.Width,width),sy=pilot_visual_sample(y,desc.Height,height);
            uint32_t value;memcpy(&value,(const BYTE*)data+(size_t)sy*guest_pitch+sx*4,4);value=(value>>8)|(value<<24);
            memcpy((BYTE*)mapped.pData+y*mapped.RowPitch+x*4,&value,4);
        }
        ID3D11DeviceContext_Unmap(ctx,(ID3D11Resource*)staging,0);
        ID3D11DeviceContext_CopyResource(ctx,(ID3D11Resource*)pilot_donor_texture(texture),(ID3D11Resource*)staging);
        for(UINT row=0;row<rows;++row)memcpy(texture->sys_mem+row*pitch,(const BYTE*)data+row*guest_pitch,pitch);
        ID3D11Texture2D_Release(staging);pilot_textures[handle-1].uploaded=1;pilot_textures[handle-1].gpu_written=0;
        pilot_textures[handle-1].content_serial=pilot_textures[handle-1].use->serial=pilot_mark_work();return;
    }
    D3DLOCKED_RECT lock;
    pilot_d3d_require(texture->iface.lpVtbl->LockRect(&texture->iface,level,&lock,NULL,0),__FILE__,__LINE__);
    int changed=!(pilot_textures[handle-1].uploaded&(1u<<level));
    if(pair) {
        const BYTE *src=data;const BYTE *dst=lock.pBits;
        for(UINT i=0;i<width*height && !changed;++i)
            changed=dst[i*4]!=src[i*2] || dst[i*4+1]!=src[i*2+1];
    } else for(UINT row=0;row<rows && !changed;++row)changed=memcmp((BYTE*)lock.pBits+row*pitch,(const BYTE*)data+row*guest_pitch,pitch)!=0;
    if(!changed) {
        texture->locked=FALSE; return; /* No changed CPU work to upload. */
    }
    if(pair) {
        /* Both streams retain the same Morton texel order. The existing
         * donor upload unswizzles the expanded four-byte texels once. */
        const BYTE *src=data;BYTE *dst=lock.pBits;
        for(UINT i=0;i<width*height;++i) {
            dst[i*4]=dst[i*4+2]=src[i*2];
            dst[i*4+1]=dst[i*4+3]=src[i*2+1];
        }
    } else for(UINT row=0;row<rows;++row)memcpy((BYTE*)lock.pBits+row*pitch,(const BYTE*)data+row*guest_pitch,pitch);
    pilot_d3d_require(texture->iface.lpVtbl->UnlockRect(&texture->iface,level),__FILE__,__LINE__);
    pilot_textures[handle-1].uploaded|=1u<<level;
    pilot_textures[handle-1].gpu_written=0;
    pilot_textures[handle-1].use->serial=pilot_mark_work();
    if(pilot_event_trace_enabled())fprintf(stderr,"[TEXTURE UPLOAD] native=%u level=%u %ux%u bytes=%u\n",handle,level,width,height,bytes);
    /* Diagnostic readback is opt-in and absent from production upload work. */
    const char *capture=getenv("XLLE_PILOT_TEXTURE_CAPTURE");
    if(capture && *capture) {
        static unsigned sequence;
        UINT capture_bytes=yuv?width*height*4:pitch*rows;
        char path[1024]; unsigned char *pixels=malloc(capture_bytes);
        if(!pixels || snprintf(path,sizeof(path),"%s/texture-%u-level-%u-%u.bin",capture,handle,level,++sequence)>=(int)sizeof(path))
            pilot_d3d_backend_stop("texture capture allocation/path failed");
        pilot_d3d_require(pilot_gfx_texture_readback(handle,level,pixels,capture_bytes),__FILE__,__LINE__);
        FILE *output=fopen(path,"wb");
        uint32_t header[]={0x58544558,1,handle,level,width,height,yuv?0x12u:(uint32_t)texture->d3d8_format,capture_bytes};
        if(!output || fwrite(header,sizeof(header),1,output)!=1 || fwrite(pixels,capture_bytes,1,output)!=1 || fclose(output))
            pilot_d3d_backend_stop("texture capture write failed");
        free(pixels);
    }
}
int32_t pilot_gfx_movie_luma(uint32_t handle,unsigned step,unsigned char *samples,unsigned bytes)
{
    D3D8Texture *t=pilot_texture(handle);
    if(!step || !samples || (t->d3d8_format!=0x24 && t->d3d8_format!=0x25) ||
       !(pilot_textures[handle-1].uploaded&1) || pilot_textures[handle-1].gpu_written)return E_INVALIDARG;
    unsigned columns=(t->width+step-1)/step,rows=(t->height+step-1)/step;
    if(bytes!=(uint64_t)columns*rows)return E_INVALIDARG;
    unsigned pitch=d3d8_row_pitch(t->d3d8_format,t->width),offset=t->d3d8_format==0x25;
    for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<columns;++x) {
        unsigned sx=x*step+step/2,sy=y*step+step/2;
        if(sx>=t->width)sx=t->width-1;if(sy>=t->height)sy=t->height-1;
        samples[y*columns+x]=t->sys_mem[sy*pitch+sx*2+offset];
    }
    return S_OK;
}
int32_t pilot_gfx_texture_clear(uint32_t handle,uint32_t color)
{
    pilot_texture(handle);ID3D11RenderTargetView *rtv=pilot_textures[handle-1].rtv;
    if(!rtv)return E_INVALIDARG;
    float rgba[]={((color>>16)&255)/255.0f,((color>>8)&255)/255.0f,(color&255)/255.0f,(color>>24)/255.0f};
    ID3D11DeviceContext_ClearRenderTargetView(d3d8_GetD3D11Context(),rtv,rgba);
    pilot_textures[handle-1].uploaded=1;
    pilot_textures[handle-1].gpu_written=1;
    pilot_textures[handle-1].use->serial=pilot_mark_work();
    return S_OK;
}
void pilot_gfx_texture_download(uint32_t handle,void *data,uint32_t bytes)
{
    D3D8Texture *texture=pilot_texture(handle);
    if(texture->levels!=1 || !(texture->d3d8_format==0x12 || texture->d3d8_format==0x1e || texture->d3d8_format==0x2e))
        pilot_d3d_backend_stop("unqualified GPU texture download format");
    UINT pitch=texture->width*4,guest_pitch=(pitch+63)&~63u;
    if(bytes!=guest_pitch*texture->height || !data)pilot_d3d_backend_stop("invalid GPU texture download range");
    if(!pilot_textures[handle-1].uploaded) {
        /* A newly created texture has no GPU-produced contents. Initialize it
         * from its actual guest storage before making it available to readers. */
        pilot_gfx_texture_upload(handle,0,data,bytes);return;
    }
    if(!pilot_textures[handle-1].gpu_written)return;
    pilot_d3d_require(pilot_gfx_texture_readback(handle,0,texture->sys_mem,pitch*texture->height),__FILE__,__LINE__);
    for(UINT row=0;row<texture->height;++row)memcpy((BYTE*)data+row*guest_pitch,texture->sys_mem+row*pitch,pitch);
    pilot_textures[handle-1].gpu_written=0;
}
int32_t pilot_gfx_texture_clear_depth(uint32_t handle,uint32_t flags,float depth,uint32_t stencil)
{
    pilot_texture(handle);ID3D11DepthStencilView *dsv=pilot_textures[handle-1].dsv;
    if(!dsv || !(flags&3) || (flags&~3u) || !(depth>=0 && depth<=1) || stencil>255)return E_INVALIDARG;
    ID3D11DeviceContext_ClearDepthStencilView(d3d8_GetD3D11Context(),dsv,flags,depth,(UINT8)stencil);
    pilot_textures[handle-1].uploaded=1;pilot_textures[handle-1].gpu_written=1;
    pilot_textures[handle-1].content_serial=pilot_textures[handle-1].use->serial=pilot_mark_work();return S_OK;
}
int32_t pilot_gfx_texture_readback(uint32_t handle,uint32_t level,void *data,uint32_t capacity)
{
    D3D8Texture *texture=pilot_texture(handle);
    if(level>=texture->levels || !data) return E_INVALIDARG;
    UINT width=texture->width>>level,height=texture->height>>level;
    if(!width)width=1; if(!height)height=1;
    UINT pitch=d3d8_row_pitch(texture->d3d8_format,width);
    /* Diagnostic readback exposes the donor's decoded BGRA movie image. Guest
     * YUV bytes remain CPU-owned and unchanged; YUV render targets are excluded. */
    if(texture->d3d8_format==0x24 || texture->d3d8_format==0x25)pitch=width*4;
    UINT rows=d3d8_format_is_compressed(texture->d3d8_format)?(height+3)/4:height;
    if(capacity<pitch*rows || !(pilot_textures[handle-1].uploaded&(1u<<level))) return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC desc;
    ID3D11Texture2D_GetDesc(pilot_donor_texture(texture),&desc);
    /* Retain the complete mip shape: a BC 1x1/2x2 tail cannot be the top
     * level of a standalone BC texture, but is valid inside this chain. */
    desc.BindFlags=0; desc.MiscFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *staging=NULL;
    HRESULT hr=ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(),&desc,NULL,&staging);
    if(FAILED(hr)) return hr;
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11DeviceContext_CopySubresourceRegion(context,(ID3D11Resource*)staging,level,0,0,0,
        (ID3D11Resource*)pilot_donor_texture(texture),level,NULL);
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr=ID3D11DeviceContext_Map(context,(ID3D11Resource*)staging,level,D3D11_MAP_READ,0,&mapped);
    if(SUCCEEDED(hr)) {
        if(level==0 && (desc.Width!=width || desc.Height!=height)) {
            if(pitch!=width*4 || rows!=height)pilot_d3d_backend_stop("scaled readback requires a single-level 32-bit target");
            for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x) {
                unsigned sx=pilot_visual_sample(x,width,desc.Width),sy=pilot_visual_sample(y,height,desc.Height);
                memcpy((BYTE*)data+(size_t)y*pitch+x*4,(BYTE*)mapped.pData+(size_t)sy*mapped.RowPitch+sx*4,4);
            }
        } else for(UINT row=0;row<rows;++row)memcpy((char*)data+row*pitch,(char*)mapped.pData+row*mapped.RowPitch,pitch);
        if(texture->d3d8_format==0x12 && desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM)
            for(UINT i=0;i<width*height;++i){BYTE *p=(BYTE*)data+i*4,t=p[0];p[0]=p[2];p[2]=t;}
        if(texture->d3d8_format==0x2e)for(UINT i=0;i<width*height;++i) {
            uint32_t value;memcpy(&value,(BYTE*)data+i*4,4);value=(value<<8)|(value>>24);memcpy((BYTE*)data+i*4,&value,4);
        }
        ID3D11DeviceContext_Unmap(context,(ID3D11Resource*)staging,level);
    }
    ID3D11Texture2D_Release(staging);
    return hr;
}
void pilot_gfx_set_state(uint32_t state,uint32_t value)
{
    if (!pilot_device) pilot_d3d_backend_stop("state change before device creation");
    if(state==D3DRS_ALPHABLENDENABLE) {
        pilot_requested_blend=value;
        if(pilot_logic_copy)value=0;
    }
    pilot_d3d_require(pilot_device->lpVtbl->SetRenderState(pilot_device,(D3DRENDERSTATETYPE)state,value),__FILE__,__LINE__);
}
int pilot_gfx_logic_op(uint32_t value)
{
    if(value && value!=0x1503)return 0;
    /* Logic COPY writes source color, overriding blending but retaining write
     * masks. Reuse the donor's actual blend-state cache/creation. Keep the
     * requested blend state so disabling logic restores it, including writes
     * made while COPY was enabled. Other bitwise operations are not ignored. */
    pilot_logic_copy=value!=0;
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,pilot_requested_blend);
    return 1;
}
void pilot_gfx_set_texture_state(uint32_t stage,uint32_t state,uint32_t value)
{
    if (!pilot_device) pilot_d3d_backend_stop("texture state before device creation");
    pilot_d3d_require(pilot_device->lpVtbl->SetTextureStageState(pilot_device,stage,(D3DTEXTURESTAGESTATETYPE)state,value),__FILE__,__LINE__);
}
int32_t pilot_gfx_draw_up(uint32_t primitive,uint32_t count,const void *data,uint32_t stride,uint32_t fvf)
{
    if (!pilot_device) return E_FAIL;
    if(pilot_edge_requested)return E_NOTIMPL; /* The internal donor UP path owns its raster setup. */
    if((pilot_initializer.controls || pilot_initializer.dither) &&
       !pilot_gfx_initializer_consumer(0,primitive==4?5:primitive==5?6:0,fvf))return E_NOTIMPL;
    pilot_gfx_shader_fixed_binding();
#ifdef PILOT_D3D_PHOBOS
    pilot_donor_blend_candidate(0);
#endif
    pilot_feedback_validate();
    pilot_d3d_raster_texture_mode(0,NULL);
    pilot_d3d_program_fog_mode(0);
    HRESULT status=pilot_device->lpVtbl->SetVertexShader(pilot_device,fvf);
    if (SUCCEEDED(status)) status=pilot_flat_prepare(primitive==4?5:primitive==5?6:0);
    if (SUCCEEDED(status)) status=pilot_device->lpVtbl->DrawPrimitiveUP(pilot_device,(D3DPRIMITIVETYPE)primitive,count,data,stride);
    ID3D11DeviceContext_GSSetShader(d3d8_GetD3D11Context(),NULL,NULL,0);
    if(SUCCEEDED(status)) {
        pilot_mark_draw();
        if(pilot_color_target){pilot_textures[pilot_color_target-1].uploaded=1;pilot_textures[pilot_color_target-1].gpu_written=1;}
        if(pilot_depth_target && pilot_depth_target!=UINT32_MAX){pilot_textures[pilot_depth_target-1].uploaded=1;pilot_textures[pilot_depth_target-1].gpu_written=1;}
    }
    return status;
}
uint32_t pilot_gfx_fvf_layout(uint32_t fvf,struct PilotVertexElement elements[16])
{
    unsigned textures=(fvf>>8)&15,position=fvf&15;
    if((position!=2 && position!=4) || (fvf&(position==4?0xf030u:0xf020u)) || textures>4) {
        fprintf(stderr,"[D3D FVF] rejected format=%08x\n",fvf);
        pilot_d3d_backend_stop("unqualified FVF: expected XYZ or XYZRHW with admitted normal/colors/coordinates");
    }
    if((fvf>>16)>>(textures*2))pilot_d3d_backend_stop("FVF coordinate size without coordinate");
    struct PilotVertexElement layout[16]={0};
    layout[0]=(struct PilotVertexElement){position==2?0x32:0x42,0,0};
    unsigned extent=position==2?12:16;
    /* The original FVF decoder uses FLOAT3 normals. Packed CMP normals are
     * a declaration format; the existing typed core supports both. */
    if(fvf&0x10){layout[2]=(struct PilotVertexElement){0x32,0,extent};extent+=12;}
    for(unsigned i=0;i<2;++i)if(fvf&(0x40u<<i)) {
        layout[3+i]=(struct PilotVertexElement){0x40,0,extent};extent+=4;
    }
    for(unsigned i=0;i<textures;++i) {
        unsigned lanes=((((fvf>>(16+i*2))+1)&3)+1);
        layout[9+i]=(struct PilotVertexElement){(lanes<<4)|2,0,extent};extent+=lanes*4;
    }
    if(elements)memcpy(elements,layout,sizeof(layout));
    return extent;
}
uint32_t pilot_gfx_fvf_extent(uint32_t fvf){return pilot_gfx_fvf_layout(fvf,NULL);}
int32_t pilot_gfx_fvf_prepare(uint32_t fvf,float screen_x,float screen_y)
{return pilot_gfx_fvf_placement(fvf,screen_x,screen_y,1,0);}
int32_t pilot_gfx_fvf_placement(uint32_t fvf,float screen_x,float screen_y,float x_scale,float x_anchor)
{
    if(!pilot_device)return E_FAIL;
    pilot_gfx_shader_fixed_binding();
    pilot_gfx_fvf_extent(fvf);
    if((fvf&15)!=4)return E_NOTIMPL; /* XYZ uses the transform owner, not screen placement. */
    uint32_t viewport[6];HRESULT hr=pilot_gfx_get_viewport(viewport);if(FAILED(hr))return hr;
    float low,high;memcpy(&low,viewport+4,4);memcpy(&high,viewport+5,4);
    if(!viewport[2] || !viewport[3] || !(high>low))return E_INVALIDARG;
    /* Original pretransformed screen+bias is rounded before four-bit
     * truncation. The VS owns that operation; spare w lanes carry the bias. */
    float scale[]={2.0f/viewport[2],-2.0f/viewport[3],1/(high-low),screen_x};
    float offset[]={-1-viewport[0]*scale[0],1-viewport[1]*scale[1],-low/(high-low),screen_y};
    offset[0]+=(1-x_scale)*x_anchor*scale[0];scale[0]*=x_scale;
    pilot_d3d_program_fog_mode(0);
    pilot_d3d_fvf_prepare(fvf,scale,offset);return S_OK;
}
#include "graphics_blend_backend.inc"
static int32_t pilot_gfx_draw_submit(unsigned primitive,unsigned first,unsigned count,const void *indices,int32_t base)
{
#ifdef PILOT_D3D_PHOBOS
    pilot_donor_blend_candidate(0);
#endif
    if(!pilot_device || !count || (uint64_t)first+count>UINT32_MAX)return E_INVALIDARG;
    if(pilot_initializer.dither && !pilot_initializer_target_format())return E_NOTIMPL;
    uint32_t feedback=pilot_feedback_validate();
    if(pilot_edge_requested && feedback)return E_NOTIMPL;
    D3D11_PRIMITIVE_TOPOLOGY topology;
    if(primitive==5 && count>=3 && count%3==0)topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    else if(primitive==6 && count>=3)topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    else if(primitive==7 && count>=3 && count-2<=UINT32_MAX/12 && first<=INT32_MAX)
        topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    else if(primitive==8 && count>=4 && !(count%4) && first<=INT32_MAX && count<=UINT32_MAX/6)
        topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    else return E_NOTIMPL;
    float scale[4][4];
    for(unsigned stage=0;stage<4;++stage) {
        scale[stage][0]=scale[stage][1]=scale[stage][2]=scale[stage][3]=1;
        if(pilot_bound_textures[stage]) {
            D3D8Texture *texture=pilot_texture(pilot_bound_textures[stage]);
            if(texture->d3d8_format==0x12 || texture->d3d8_format==0x1e || texture->d3d8_format==0x2e ||
               texture->d3d8_format==0x24 || texture->d3d8_format==0x25) {
                scale[stage][0]=1.0f/texture->width;scale[stage][1]=1.0f/texture->height;
            }
        }
    }
#ifdef PILOT_D3D_PHOBOS
    pilot_donor_blend_candidate(!indices && pilot_gfx_blend_candidate_geometry(primitive,first,count));
#endif
    pilot_d3d_raster_texture_mode(1,scale);d3d8_shaders_prepare_pixel();d3d8_states_apply();
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    HRESULT flat_result=pilot_flat_prepare(primitive);if(FAILED(flat_result)) {
#ifdef PILOT_D3D_PHOBOS
        pilot_donor_blend_candidate(0);
#endif
        return flat_result;
    }
    int flat_quad=primitive==8 && d3d8_GetRenderStates()[D3DRS_SHADEMODE]==1;
    unsigned index_words=0;
    if(indices) {
        HRESULT hr=pilot_index_prepare(primitive,indices,count,flat_quad,&index_words);
        if(FAILED(hr))return hr;
    }
    if(flat_quad)topology=D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
    ID3D11DeviceContext_IASetPrimitiveTopology(context,topology);
#ifdef PILOT_D3D_PHOBOS
    uint32_t copied=pilot_donor_feedback_begin();
    if(copied!=feedback)pilot_d3d_backend_stop("feedback copy did not cover bound aliases");
    ID3D11BlendState *saved_blend=NULL;FLOAT saved_factors[4]={0};UINT saved_mask=0;
    int explicit_blend=pilot_donor_blend_active();
    if(pilot_edge_requested && explicit_blend) {
        pilot_donor_blend_candidate(0);return E_NOTIMPL;
    }
    if(explicit_blend) {
        if(copied)pilot_d3d_backend_stop("explicit blend cannot share an aliased sampling draw");
        RhiView *view=pilot_donor_blend_destination();
        ID3D11ShaderResourceView *native=(ID3D11ShaderResourceView*)rhi_d3d11_native_view(view);
        ID3D11DeviceContext_PSSetShaderResources(context,4,1,&native);
        ID3D11DeviceContext_OMGetBlendState(context,&saved_blend,saved_factors,&saved_mask);
        ID3D11DeviceContext_OMSetBlendState(context,NULL,NULL,saved_mask);
    }
#endif
    struct PilotEdgeDraw edge_draw;
    HRESULT edge_result=pilot_edge_begin(&edge_draw);if(FAILED(edge_result)) {
#ifdef PILOT_D3D_PHOBOS
        pilot_donor_blend_candidate(0);
#endif
        return edge_result;
    }
    if(indices) {
        ID3D11Buffer *previous=NULL;DXGI_FORMAT format;UINT offset;
        ID3D11DeviceContext_IAGetIndexBuffer(context,&previous,&format,&offset);
        ID3D11DeviceContext_IASetIndexBuffer(context,pilot_index_buffer,DXGI_FORMAT_R32_UINT,0);
        ID3D11DeviceContext_DrawIndexed(context,index_words,0,base);
        ID3D11DeviceContext_IASetIndexBuffer(context,previous,format,offset);
        if(previous)ID3D11Buffer_Release(previous);
    } else if(primitive==7)pilot_fan_draw(first,count);
    else if(primitive==8 && !flat_quad)pilot_quad_draw(first,count);
    else ID3D11DeviceContext_Draw(context,count,first);
    pilot_edge_end(&edge_draw);
#ifdef PILOT_D3D_PHOBOS
    if(explicit_blend) {
        ID3D11ShaderResourceView *none=NULL;
        ID3D11DeviceContext_PSSetShaderResources(context,4,1,&none);
        ID3D11DeviceContext_OMSetBlendState(context,saved_blend,saved_factors,saved_mask);
        if(saved_blend)ID3D11BlendState_Release(saved_blend);
        ++pilot_blend_draws;
    }
    pilot_donor_blend_candidate(0);
    pilot_donor_feedback_end(copied);
#endif
    ID3D11DeviceContext_GSSetShader(context,NULL,NULL,0);
    pilot_mark_draw();
    if(pilot_color_target){pilot_textures[pilot_color_target-1].uploaded=1;pilot_textures[pilot_color_target-1].gpu_written=1;}
    if(pilot_depth_target && pilot_depth_target!=UINT32_MAX){pilot_textures[pilot_depth_target-1].uploaded=1;pilot_textures[pilot_depth_target-1].gpu_written=1;}
    return S_OK;
}
int32_t pilot_gfx_draw_vertices(unsigned primitive,unsigned first,unsigned count)
{return pilot_gfx_draw_submit(primitive,first,count,NULL,0);}
int32_t pilot_gfx_draw_indexed(unsigned primitive,unsigned count,const void *indices,int32_t base)
{return indices?pilot_gfx_draw_submit(primitive,0,count,indices,base):E_INVALIDARG;}
uint32_t pilot_gfx_width(void) { return pilot_guest_width?pilot_guest_width:d3d8_GetBackbufferWidth(); }
int32_t pilot_gfx_draw_memory(unsigned primitive,unsigned count,const void *vertices,unsigned stride)
{
#ifdef PILOT_D3D_PHOBOS
    HRESULT pilot_donor_upload_vertices(const void*,UINT,UINT);
    if(!pilot_device)return E_FAIL;
    if(!vertices || !stride || !count || (uint64_t)count*stride>UINT32_MAX)return E_INVALIDARG;
    if(pilot_blend_transient)return E_INVALIDARG;
    pilot_blend_transient=1;
    pilot_blend_transient_proof=pilot_gfx_shader_blend_rectangle(vertices,count*stride,stride,primitive,count);
    ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();ID3D11Buffer *saved=NULL;UINT old_stride=0,offset=0;
    ID3D11DeviceContext_IAGetVertexBuffers(ctx,0,1,&saved,&old_stride,&offset);
    HRESULT hr=pilot_donor_upload_vertices(vertices,count*stride,stride);
    if(SUCCEEDED(hr))hr=pilot_gfx_draw_vertices(primitive,0,count);
    ID3D11DeviceContext_IASetVertexBuffers(ctx,0,1,&saved,&old_stride,&offset);
    if(saved)ID3D11Buffer_Release(saved);
    pilot_blend_transient=pilot_blend_transient_proof=0;
    pilot_donor_blend_candidate(0);
    return hr;
#else
    (void)primitive;(void)count;(void)vertices;(void)stride;return E_NOTIMPL;
#endif
}
uint32_t pilot_gfx_height(void) { return pilot_guest_height?pilot_guest_height:d3d8_GetBackbufferHeight(); }
int32_t pilot_gfx_readback(unsigned char *rgba,uint32_t capacity)
{
    return pilot_gfx_surface_readback(pilot_presentation.sample_type==0x2222?1:0,rgba,capacity);
}
int32_t pilot_gfx_surface_readback(unsigned surface,unsigned char *rgba,uint32_t capacity)
{
    if (!pilot_device || !rgba || surface>=2 || !pilot_surfaces[surface]) return E_INVALIDARG;
    ID3D11Texture2D *back=pilot_surfaces[surface],*staging=NULL;
    ID3D11Texture2D_AddRef(back);
    D3D11_TEXTURE2D_DESC desc; ID3D11Texture2D_GetDesc(back,&desc);
    uint32_t extent[3];if(!pilot_presentation_surface(&pilot_presentation,surface,extent)){ID3D11Texture2D_Release(back);return E_INVALIDARG;}
    unsigned width=extent[0],height=extent[1];
    if (desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM || capacity<(uint64_t)width*height*4) { ID3D11Texture2D_Release(back); return E_INVALIDARG; }
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
    HRESULT status=ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(),&desc,NULL,&staging);
    if (SUCCEEDED(status)) {
        ID3D11DeviceContext_CopyResource(d3d8_GetD3D11Context(),(ID3D11Resource*)staging,(ID3D11Resource*)back);
        D3D11_MAPPED_SUBRESOURCE mapped;
        status=ID3D11DeviceContext_Map(d3d8_GetD3D11Context(),(ID3D11Resource*)staging,0,D3D11_MAP_READ,0,&mapped);
        if (SUCCEEDED(status)) {
            if(width==desc.Width && height==desc.Height) {
                for(UINT y=0;y<height;++y)memcpy(rgba+(size_t)y*width*4,(BYTE*)mapped.pData+(size_t)y*mapped.RowPitch,width*4);
            } else for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x) {
                unsigned sx=pilot_visual_sample(x,width,desc.Width),sy=pilot_visual_sample(y,height,desc.Height);
                memcpy(rgba+((size_t)y*width+x)*4,(BYTE*)mapped.pData+(size_t)sy*mapped.RowPitch+sx*4,4);
            }
            ID3D11DeviceContext_Unmap(d3d8_GetD3D11Context(),(ID3D11Resource*)staging,0);
        }
        ID3D11Texture2D_Release(staging);
    }
    ID3D11Texture2D_Release(back); return status;
}
int32_t pilot_gfx_reference_raster(struct PilotRasterObservation *out)
{
    if(!out)return E_INVALIDARG;
    memset(out,0,sizeof(*out));if(!pilot_device)return E_FAIL;
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    D3D11_VIEWPORT viewport={0};UINT count=1;
    ID3D11DeviceContext_RSGetViewports(context,&count,&viewport);
    if(count==1) {
        out->available|=1;out->viewport[0]=viewport.TopLeftX;out->viewport[1]=viewport.TopLeftY;
        out->viewport[2]=viewport.Width;out->viewport[3]=viewport.Height;
        out->viewport[4]=viewport.MinDepth;out->viewport[5]=viewport.MaxDepth;
    }
    D3D11_RECT rect={0};count=1;ID3D11DeviceContext_RSGetScissorRects(context,&count,&rect);
    if(count==1) {
        out->available|=2;out->scissor[0]=rect.left;out->scissor[1]=rect.top;
        out->scissor[2]=rect.right;out->scissor[3]=rect.bottom;
    }
    ID3D11RasterizerState *raster=NULL;ID3D11DeviceContext_RSGetState(context,&raster);
    if(raster) {
        D3D11_RASTERIZER_DESC desc;ID3D11RasterizerState_GetDesc(raster,&desc);
        out->available|=4;out->scissor_enabled=desc.ScissorEnable!=0;ID3D11RasterizerState_Release(raster);
    }
    ID3D11ShaderResourceView *view=NULL;ID3D11DeviceContext_PSGetShaderResources(context,0,1,&view);
    if(view) {
        ID3D11Resource *resource=NULL;ID3D11ShaderResourceView_GetResource(view,&resource);
        ID3D11ShaderResourceView_Release(view);
        if(resource) {
            ID3D11Texture2D *image=NULL;
            HRESULT result=ID3D11Resource_QueryInterface(resource,&IID_ID3D11Texture2D,(void**)&image);
            ID3D11Resource_Release(resource);
            if(FAILED(result))return result;
            D3D11_TEXTURE2D_DESC desc;ID3D11Texture2D_GetDesc(image,&desc);ID3D11Texture2D_Release(image);
            out->available|=8;out->source_width=desc.Width;out->source_height=desc.Height;
        }
    }
    return S_OK;
}
int32_t pilot_gfx_reference_target_readback(unsigned depth,void *data,uint32_t capacity,uint32_t metadata[3])
{
    if(!pilot_device || depth>1 || !metadata)return E_INVALIDARG;
    memset(metadata,0,3*sizeof(*metadata));
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11RenderTargetView *color=NULL;ID3D11DepthStencilView *zeta=NULL;
    ID3D11DeviceContext_OMGetRenderTargets(context,1,&color,&zeta);
    ID3D11Resource *resource=NULL;
    if(depth && zeta)ID3D11DepthStencilView_GetResource(zeta,&resource);
    if(!depth && color)ID3D11RenderTargetView_GetResource(color,&resource);
    if(color)ID3D11RenderTargetView_Release(color);if(zeta)ID3D11DepthStencilView_Release(zeta);
    if(!resource)return S_FALSE;
    ID3D11Texture2D *source=NULL,*staging=NULL;
    HRESULT status=ID3D11Resource_QueryInterface(resource,&IID_ID3D11Texture2D,(void**)&source);
    ID3D11Resource_Release(resource);if(FAILED(status))return status;
    D3D11_TEXTURE2D_DESC desc;ID3D11Texture2D_GetDesc(source,&desc);
    /* The captured Blinx targets are one 2D image. Reject other views before
     * returning a base-level shape that might describe the wrong subresource. */
    if(desc.MipLevels!=1 || desc.ArraySize!=1 || desc.SampleDesc.Count!=1 ||
        (desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM &&
         desc.Format!=DXGI_FORMAT_R24G8_TYPELESS && desc.Format!=DXGI_FORMAT_D24_UNORM_S8_UINT)) {
        ID3D11Texture2D_Release(source);return E_INVALIDARG;
    }
    metadata[0]=desc.Width;metadata[1]=desc.Height;metadata[2]=desc.Format;
    if(!data && !capacity){ID3D11Texture2D_Release(source);return S_OK;}
    if(!data || capacity<(uint64_t)desc.Width*desc.Height*4) {
        ID3D11Texture2D_Release(source);return E_INVALIDARG;
    }
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    status=ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(),&desc,NULL,&staging);
    if(SUCCEEDED(status)) {
        ID3D11DeviceContext_CopySubresourceRegion(context,(ID3D11Resource*)staging,0,0,0,0,(ID3D11Resource*)source,0,NULL);
        D3D11_MAPPED_SUBRESOURCE mapped;
        status=ID3D11DeviceContext_Map(context,(ID3D11Resource*)staging,0,D3D11_MAP_READ,0,&mapped);
        if(SUCCEEDED(status)) {
            for(UINT y=0;y<desc.Height;++y)memcpy((BYTE*)data+y*desc.Width*4,(BYTE*)mapped.pData+y*mapped.RowPitch,desc.Width*4);
            ID3D11DeviceContext_Unmap(context,(ID3D11Resource*)staging,0);
        }
        ID3D11Texture2D_Release(staging);
    }
    ID3D11Texture2D_Release(source);return status;
}

/* Both admitted guest modes are non-multisampled rasters (ordinary or 2x2
 * supersampled). Original SetRenderTarget leaves device bit 0x8000 clear
 * for SSAA, so either requested enable value leaves that raster unchanged.
 * Partial masks and actual guest MSAA formats remain unqualified. */
int pilot_gfx_multisample(unsigned enable,uint32_t mask)
{
    if(!pilot_device || enable>1 || mask!=UINT32_MAX)return 0;
    return pilot_presentation.sample_type==0x11 || pilot_presentation.sample_type==0x2222;
}

/* Only native RGBA8 quantization is admitted. Dither requests remain visible
 * under that explicit approximation policy; no ignored donor enum is used. */
static int pilot_initializer_target_format(void)
{
    return pilot_initializer_format(pilot_color_target?
        pilot_texture(pilot_color_target)->d3d8_format:pilot_default_color_format);
}
static int pilot_initializer_zero_bias(void)
{
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();ID3D11RasterizerState *old=NULL;
    ID3D11DeviceContext_RSGetState(context,&old);
    if(!old)return 1; /* D3D11's actual default has all three coefficients zero. */
    D3D11_RASTERIZER_DESC desc;ID3D11RasterizerState_GetDesc(old,&desc);ID3D11RasterizerState_Release(old);
    if(!desc.DepthBias && desc.DepthBiasClamp==0 && desc.SlopeScaledDepthBias==0)return 1;
    desc.DepthBias=0;desc.DepthBiasClamp=desc.SlopeScaledDepthBias=0;
    ID3D11RasterizerState *zero=NULL;
    HRESULT result=ID3D11Device_CreateRasterizerState(d3d8_GetD3D11Device(),&desc,&zero);
    if(FAILED(result))return 0;
    ID3D11DeviceContext_RSSetState(context,zero);ID3D11RasterizerState_Release(zero);return 1;
}
int pilot_gfx_initializer_admits(uint32_t state,uint32_t value)
{
    if(!pilot_device || !pilot_initializer_value(state,value))return 0;
    if(state==65 && value && !pilot_initializer_target_format())return 0;
    if(state==149 && !pilot_initializer_zero_bias())return 0;
    return 1;
}
int pilot_gfx_initializer_set(uint32_t state,uint32_t value)
{return pilot_gfx_initializer_admits(state,value) && pilot_initializer_observe(&pilot_initializer,state,value);}
int pilot_gfx_vertex_cache_break(int apply)
{
    if(!pilot_device)return 0;
    // Whole native draws have no partially assembled primitive. Discard only
    // source-upload reuse: the next draw snapshots/refetches its vertex bytes.
    // Existing GPU draws retain their ordered D3D11 resources and work.
    if(apply)for(unsigned i=0;i<sizeof(pilot_vertices)/sizeof(*pilot_vertices);++i)
        if(pilot_vertices[i].object)pilot_vertices[i].refetch=1;
    return 1;
}
int pilot_gfx_command_submit(void)
{
    if(!pilot_device)return 0;
    ID3D11DeviceContext_Flush(d3d8_GetD3D11Context());
    return SUCCEEDED(ID3D11Device_GetDeviceRemovedReason(d3d8_GetD3D11Device()));
}
int pilot_gfx_initializer_snapshot(struct PilotInitializerState *out)
{
    if(!pilot_device || !out)return 0;*out=pilot_initializer;return 1;
}
int pilot_gfx_initializer_consumer(unsigned program,uint32_t primitive,uint32_t fvf)
{
    if(!pilot_device || !pilot_initializer_consumer(program,primitive,fvf))return 0;
    if(program==2 && d3d8_GetRenderStates()[D3DRS_LIGHTING])return 0;
    return !pilot_initializer.dither || pilot_initializer_target_format();
}
