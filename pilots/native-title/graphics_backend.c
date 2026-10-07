/* Guest-independent adapter over the prepared MIT sp00nznet D3D11 backend.
 * Native COM/window pointers stay here; the guest bridge sees only values. */
#include "d3d8_internal.h"
#include "d3d8_combiners.h"
#include "graphics_backend.h"
#include "pilot_d3d_checks.h"
#include <stdio.h>
static IDirect3D8 *pilot_factory;
static IDirect3DDevice8 *pilot_device;
static HWND pilot_window;
/* Slot 0 is the drawable back buffer; slot 1 retains the last presented image.
 * Xbox GetBackBuffer(-1) must not alias the image currently being drawn. */
static ID3D11Texture2D *pilot_surfaces[3];
static unsigned pilot_surface_refs[3];
static ID3D11DepthStencilView *pilot_default_depth;
static uint32_t pilot_color_target,pilot_depth_target;
static struct { IDirect3DTexture8 *object; ID3D11RenderTargetView *rtv; ID3D11DepthStencilView *dsv; uint32_t uploaded,gpu_written; } pilot_textures[256];
static struct { IDirect3DVertexBuffer8 *object; int uploaded; } pilot_vertices[256];
static uint32_t pilot_bound_textures[4],pilot_bound_vertices[16];
void pilot_d3d_raster_texture_mode(UINT native,const float scale[4][4]);

void pilot_gfx_pixel_fixed(void)
{
    if(!pilot_device)pilot_d3d_backend_stop("pixel binding before device creation");
    d3d8_combiners_set_pixel_shader(0);
    d3d8_shaders_prepare_pixel();
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
    if (message==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window,message,wparam,lparam);
}
void pilot_gfx_close(void)
{
    pilot_gfx_shaders_close();
    for (unsigned i=0;i<256;++i)
        if(pilot_vertices[i].object) pilot_d3d_backend_stop("device close with outstanding vertex ownership");
    for (unsigned i=0;i<256;++i)
        if(pilot_textures[i].object) pilot_d3d_backend_stop("device close with outstanding texture ownership");
    for (unsigned i=0;i<3;++i) {
        if (pilot_surface_refs[i]) pilot_d3d_backend_stop("device close with outstanding surface ownership");
        if (pilot_surfaces[i]) { ID3D11Texture2D_Release(pilot_surfaces[i]); pilot_surfaces[i]=NULL; }
    }
    if(pilot_default_depth){ID3D11DepthStencilView_Release(pilot_default_depth);pilot_default_depth=NULL;}
    pilot_color_target=pilot_depth_target=0;
    if (pilot_device) { pilot_device->lpVtbl->Release(pilot_device); pilot_device=NULL; }
    if (pilot_factory) { pilot_factory->lpVtbl->Release(pilot_factory); pilot_factory=NULL; }
    if (pilot_window) { DestroyWindow(pilot_window); pilot_window=NULL; }
    MSG message; while (PeekMessageW(&message,NULL,WM_QUIT,WM_QUIT,PM_REMOVE)) {}
}
int32_t pilot_gfx_create(const uint32_t p[17])
{
    if (pilot_device || !p || !p[0] || !p[1] || p[0]>4096 || p[1]>4096) return E_INVALIDARG;
    /* The native output is windowed, with an RGBA8 enhanced render target.
     * MSAA/preallocated surfaces need their actual resource contract first. */
    if ((p[2]!=0x06 && p[2]!=0x07 && p[2]!=0x12 && p[2]!=0x1e) ||
        p[3]!=1 || p[4]!=0x11 || p[5]!=1 || p[6] || p[7] || p[8]!=1 ||
        (p[9]!=0x2a && p[9]!=0x2e) || p[10] || (p[11] && p[11]!=60) || p[12]!=1 ||
        p[13] || p[14] || p[15] || p[16]) return E_NOTIMPL;
    WNDCLASSW window_class={0}; window_class.lpfnWndProc=pilot_window_proc;
    window_class.hInstance=GetModuleHandleW(NULL); window_class.lpszClassName=L"xlle-native-reference";
    if (!RegisterClassW(&window_class) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return HRESULT_FROM_WIN32(GetLastError());
    RECT client={0,0,(LONG)p[0],(LONG)p[1]}; AdjustWindowRect(&client,WS_OVERLAPPEDWINDOW,FALSE);
    /* Hidden until scene qualification; a successful hidden present is not
     * treated as title cadence evidence. Tests read actual GPU output. */
    pilot_window=CreateWindowW(window_class.lpszClassName,L"Xbox native reference",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,client.right-client.left,client.bottom-client.top,NULL,NULL,window_class.hInstance,NULL);
    if (!pilot_window) return HRESULT_FROM_WIN32(GetLastError());
    pilot_factory=xbox_Direct3DCreate8(0);
    if (!pilot_factory) { pilot_gfx_close(); return E_OUTOFMEMORY; }
    D3DPRESENT_PARAMETERS pp={0};
    pp.BackBufferWidth=p[0]; pp.BackBufferHeight=p[1]; pp.BackBufferFormat=(D3DFORMAT)p[2];
    pp.BackBufferCount=p[3]; pp.MultiSampleType=(D3DMULTISAMPLE_TYPE)p[4]; pp.SwapEffect=p[5];
    pp.hDeviceWindow=pilot_window; pp.Windowed=TRUE; pp.EnableAutoDepthStencil=p[8];
    pp.AutoDepthStencilFormat=(D3DFORMAT)p[9]; pp.Flags=p[10];
    pp.FullScreen_RefreshRateInHz=p[11]; pp.FullScreen_PresentationInterval=p[12];
    HRESULT result=pilot_factory->lpVtbl->CreateDevice(pilot_factory,0,1,pilot_window,0,&pp,&pilot_device);
    if (SUCCEEDED(result)) {
        ID3D11DeviceContext_OMGetRenderTargets(d3d8_GetD3D11Context(),0,NULL,&pilot_default_depth);
        if(!pilot_default_depth)pilot_d3d_backend_stop("native default depth surface is absent");
        ID3D11Resource *resource=NULL;ID3D11DepthStencilView_GetResource(pilot_default_depth,&resource);
        result=ID3D11Resource_QueryInterface(resource,&IID_ID3D11Texture2D,(void**)&pilot_surfaces[2]);ID3D11Resource_Release(resource);
        pilot_depth_target=UINT32_MAX;
    }
    if (SUCCEEDED(result)) {
        result=IDXGISwapChain_GetBuffer(d3d8_GetSwapChain(),0,&IID_ID3D11Texture2D,(void**)&pilot_surfaces[0]);
        if (SUCCEEDED(result)) {
            D3D11_TEXTURE2D_DESC desc; ID3D11Texture2D_GetDesc(pilot_surfaces[0],&desc);
            desc.BindFlags=0; desc.MiscFlags=0;
            result=ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(),&desc,NULL,&pilot_surfaces[1]);
        }
    }
    if (FAILED(result)) pilot_gfx_close();
    return result;
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
    if(rtv)ID3D11RenderTargetView_Release(rtv);if(dsv)ID3D11DepthStencilView_Release(dsv);return hr;
}
int32_t pilot_gfx_viewport(const void *viewport)
{
    if (!pilot_device) return E_FAIL;
    return pilot_device->lpVtbl->SetViewport(pilot_device,(const D3DVIEWPORT8*)viewport);
}
int32_t pilot_gfx_get_viewport(uint32_t viewport[6])
{
    if (!pilot_device || !viewport) return E_INVALIDARG;
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
    ID3D11DeviceContext_CopyResource(d3d8_GetD3D11Context(),(ID3D11Resource*)pilot_surfaces[1],(ID3D11Resource*)pilot_surfaces[0]);
    return pilot_device->lpVtbl->Swap(pilot_device,0);
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
    ID3D11Buffer *buffer=handle?pilot_vertex(handle)->d3d11_buffer:NULL;UINT offset=0;
    ID3D11DeviceContext_IASetVertexBuffers(d3d8_GetD3D11Context(),stream,1,&buffer,&stride,&offset);
    pilot_bound_vertices[stream]=handle;
}
void pilot_gfx_vertex_upload(uint32_t handle,const void *data,uint32_t bytes)
{
    D3D8VertexBuffer *vertex=pilot_vertex(handle);
    if(!data || bytes!=vertex->size) pilot_d3d_backend_stop("invalid vertex upload range");
    if(pilot_vertices[handle-1].uploaded && !memcmp(vertex->sys_mem,data,bytes)) return;
    BYTE *destination=NULL;
    pilot_d3d_require(vertex->iface.lpVtbl->Lock(&vertex->iface,0,bytes,&destination,0),__FILE__,__LINE__);
    memcpy(destination,data,bytes);
    pilot_d3d_require(vertex->iface.lpVtbl->Unlock(&vertex->iface),__FILE__,__LINE__);
    pilot_vertices[handle-1].uploaded=1;
}
int32_t pilot_gfx_vertex_readback(uint32_t handle,void *data,uint32_t capacity)
{
    D3D8VertexBuffer *vertex=pilot_vertex(handle);
    if(!data || capacity<vertex->size || !pilot_vertices[handle-1].uploaded) return E_INVALIDARG;
    D3D11_BUFFER_DESC desc;
    ID3D11Buffer_GetDesc(vertex->d3d11_buffer,&desc);
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.MiscFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ID3D11Buffer *staging=NULL;
    HRESULT hr=ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(),&desc,NULL,&staging);
    if(FAILED(hr)) return hr;
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11DeviceContext_CopyResource(context,(ID3D11Resource*)staging,(ID3D11Resource*)vertex->d3d11_buffer);
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
    if(!handle || handle>256 || !pilot_textures[handle-1].object)
        pilot_d3d_backend_stop("unowned native texture");
    return (D3D8Texture*)pilot_textures[handle-1].object;
}
void pilot_gfx_texture_bind(unsigned stage,uint32_t handle)
{
    if(!pilot_device || stage>=4)pilot_d3d_backend_stop("invalid native texture binding");
    D3D8Texture *texture=handle?pilot_texture(handle):NULL;
    if(handle && (handle==pilot_color_target || handle==pilot_depth_target))
        pilot_d3d_backend_stop("simultaneous texture sampling and render/depth output is unqualified");
    pilot_d3d_require(pilot_device->lpVtbl->SetTexture(pilot_device,stage,(IDirect3DBaseTexture8*)texture),__FILE__,__LINE__);
    pilot_bound_textures[stage]=handle;
}
void pilot_gfx_texture_prepare_sample(uint32_t handle,uint32_t level,const void *data,uint32_t bytes,int cpu_exposed)
{
    D3D8Texture *texture=pilot_texture(handle);
    if(level>=texture->levels)pilot_d3d_backend_stop("invalid sampled texture level");
    if(cpu_exposed || !(pilot_textures[handle-1].uploaded&(1u<<level)))pilot_gfx_texture_upload(handle,level,data,bytes);
}
int32_t pilot_gfx_targets(uint32_t color,uint32_t depth)
{
    for(unsigned i=0;i<4;++i)if(pilot_bound_textures[i] &&
        (pilot_bound_textures[i]==color || pilot_bound_textures[i]==depth))return E_INVALIDARG;
    ID3D11RenderTargetView *rtv=d3d8_GetDefaultRTV();
    ID3D11DepthStencilView *dsv=depth==UINT32_MAX?pilot_default_depth:NULL;
    unsigned width=pilot_gfx_width(),height=pilot_gfx_height();
    if(color){D3D8Texture *t=pilot_texture(color);rtv=pilot_textures[color-1].rtv;if(!rtv)return E_INVALIDARG;width=t->width;height=t->height;}
    if(depth && depth!=UINT32_MAX){D3D8Texture *t=pilot_texture(depth);dsv=pilot_textures[depth-1].dsv;if(!dsv || t->width<width || t->height<height)return E_INVALIDARG;}
    if(depth==UINT32_MAX && (width>pilot_gfx_width() || height>pilot_gfx_height()))return E_INVALIDARG;
    ID3D11DeviceContext_OMSetRenderTargets(d3d8_GetD3D11Context(),1,&rtv,dsv);
    pilot_color_target=color;pilot_depth_target=depth;return S_OK;
}
int32_t pilot_gfx_texture_create(uint32_t width,uint32_t height,uint32_t levels,uint32_t format,uint32_t *handle)
{return pilot_gfx_texture_create_usage(width,height,levels,format,0,handle);}
int32_t pilot_gfx_texture_create_usage(uint32_t width,uint32_t height,uint32_t levels,uint32_t format,uint32_t usage,uint32_t *handle)
{
    if(!pilot_device || !handle) return E_INVALIDARG;
    int linear=format==0x12 || format==0x1e || format==0x2e;
    if(!width || !height || width>4096 || height>4096 || (!linear && ((width&(width-1)) || (height&(height-1)))))return E_NOTIMPL;
    if(!levels || levels>13 || (linear && levels!=1) || (format==0x2e?usage!=2:(usage && (usage!=1 || !linear))) ||
       !(linear || (format>=4 && format<=7) || format==0xc || format==0xe || format==0xf))return E_NOTIMPL;
    unsigned slot;
    for(slot=0;slot<256 && pilot_textures[slot].object;++slot) {}
    if(slot==256) return E_OUTOFMEMORY;
    IDirect3DTexture8 *object=NULL;
    HRESULT hr=d3d8_CreateTextureImpl(width,height,levels,usage,(D3DFORMAT)format,&object);
    if(FAILED(hr)) return hr;
    ID3D11RenderTargetView *rtv=NULL;
    ID3D11DepthStencilView *dsv=NULL;
    if(usage==1) {
        hr=ID3D11Device_CreateRenderTargetView(d3d8_GetD3D11Device(),(ID3D11Resource*)((D3D8Texture*)object)->d3d11_texture,NULL,&rtv);
        if(FAILED(hr)){object->lpVtbl->Release(object);return hr;}
    }
    if(usage==2) {
        D3D11_DEPTH_STENCIL_VIEW_DESC desc={0};desc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;desc.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        hr=ID3D11Device_CreateDepthStencilView(d3d8_GetD3D11Device(),(ID3D11Resource*)((D3D8Texture*)object)->d3d11_texture,&desc,&dsv);
        if(FAILED(hr)){object->lpVtbl->Release(object);return hr;}
    }
    pilot_textures[slot].rtv=rtv;
    pilot_textures[slot].dsv=dsv;
    pilot_textures[slot].object=object; pilot_textures[slot].uploaded=0;
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
    if(texture->iface.lpVtbl->Release(&texture->iface)!=0)
        pilot_d3d_backend_stop("native texture still referenced at destruction");
    memset(&pilot_textures[handle-1],0,sizeof(pilot_textures[0]));
}
void pilot_gfx_texture_upload(uint32_t handle,uint32_t level,const void *data,uint32_t bytes)
{
    D3D8Texture *texture=pilot_texture(handle);
    if(level>=texture->levels || !data) pilot_d3d_backend_stop("invalid texture upload level/data");
    UINT width=texture->width>>level,height=texture->height>>level;
    if(!width)width=1; if(!height)height=1;
    UINT rows=d3d8_format_is_compressed(texture->d3d8_format)?(height+3)/4:height;
    int depth=texture->d3d8_format==0x2e;
    int linear=texture->d3d8_format==0x12 || texture->d3d8_format==0x1e || depth;
    UINT pitch=d3d8_row_pitch(texture->d3d8_format,width),guest_pitch=linear?(pitch+63)&~63u:pitch;
    if(bytes!=guest_pitch*rows)
        pilot_d3d_backend_stop("texture upload size does not match subresource");
    if(depth) {
        int changed=!pilot_textures[handle-1].uploaded || pilot_textures[handle-1].gpu_written;
        for(UINT row=0;row<rows && !changed;++row)changed=memcmp(texture->sys_mem+row*pitch,(const BYTE*)data+row*guest_pitch,pitch)!=0;
        if(!changed)return;
        /* Xbox Z24S8 stores stencil in the low byte; native D24S8 stores it
         * in the high byte. CPU writes use real staging copies, since D3D11
         * forbids UpdateSubresource on a depth-stencil destination. */
        D3D11_TEXTURE2D_DESC desc;ID3D11Texture2D_GetDesc(texture->d3d11_texture,&desc);
        desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        ID3D11Texture2D *staging=NULL;
        pilot_d3d_require(ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(),&desc,NULL,&staging),__FILE__,__LINE__);
        D3D11_MAPPED_SUBRESOURCE mapped;ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();
        pilot_d3d_require(ID3D11DeviceContext_Map(ctx,(ID3D11Resource*)staging,0,D3D11_MAP_WRITE,0,&mapped),__FILE__,__LINE__);
        for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x) {
            uint32_t value;memcpy(&value,(const BYTE*)data+y*guest_pitch+x*4,4);value=(value>>8)|(value<<24);
            memcpy((BYTE*)mapped.pData+y*mapped.RowPitch+x*4,&value,4);
        }
        ID3D11DeviceContext_Unmap(ctx,(ID3D11Resource*)staging,0);
        ID3D11DeviceContext_CopyResource(ctx,(ID3D11Resource*)texture->d3d11_texture,(ID3D11Resource*)staging);
        for(UINT row=0;row<rows;++row)memcpy(texture->sys_mem+row*pitch,(const BYTE*)data+row*guest_pitch,pitch);
        ID3D11Texture2D_Release(staging);pilot_textures[handle-1].uploaded=1;pilot_textures[handle-1].gpu_written=0;return;
    }
    D3DLOCKED_RECT lock;
    pilot_d3d_require(texture->iface.lpVtbl->LockRect(&texture->iface,level,&lock,NULL,0),__FILE__,__LINE__);
    int changed=!(pilot_textures[handle-1].uploaded&(1u<<level));
    for(UINT row=0;row<rows && !changed;++row)changed=memcmp((BYTE*)lock.pBits+row*pitch,(const BYTE*)data+row*guest_pitch,pitch)!=0;
    if(!changed) {
        texture->locked=FALSE; return; /* No changed CPU work to upload. */
    }
    for(UINT row=0;row<rows;++row)memcpy((BYTE*)lock.pBits+row*pitch,(const BYTE*)data+row*guest_pitch,pitch);
    pilot_d3d_require(texture->iface.lpVtbl->UnlockRect(&texture->iface,level),__FILE__,__LINE__);
    pilot_textures[handle-1].uploaded|=1u<<level;
    pilot_textures[handle-1].gpu_written=0;
    fprintf(stderr,"[TEXTURE UPLOAD] native=%u level=%u %ux%u bytes=%u\n",handle,level,width,height,bytes);
    /* Diagnostic readback is opt-in and absent from production upload work. */
    const char *capture=getenv("XLLE_PILOT_TEXTURE_CAPTURE");
    if(capture && *capture) {
        static unsigned sequence;
        UINT capture_bytes=pitch*rows;
        char path[1024]; unsigned char *pixels=malloc(capture_bytes);
        if(!pixels || snprintf(path,sizeof(path),"%s/texture-%u-level-%u-%u.bin",capture,handle,level,++sequence)>=(int)sizeof(path))
            pilot_d3d_backend_stop("texture capture allocation/path failed");
        pilot_d3d_require(pilot_gfx_texture_readback(handle,level,pixels,capture_bytes),__FILE__,__LINE__);
        FILE *output=fopen(path,"wb");
        uint32_t header[]={0x58544558,1,handle,level,width,height,(uint32_t)texture->d3d8_format,capture_bytes};
        if(!output || fwrite(header,sizeof(header),1,output)!=1 || fwrite(pixels,capture_bytes,1,output)!=1 || fclose(output))
            pilot_d3d_backend_stop("texture capture write failed");
        free(pixels);
    }
}
int32_t pilot_gfx_texture_clear(uint32_t handle,uint32_t color)
{
    pilot_texture(handle);ID3D11RenderTargetView *rtv=pilot_textures[handle-1].rtv;
    if(!rtv)return E_INVALIDARG;
    float rgba[]={((color>>16)&255)/255.0f,((color>>8)&255)/255.0f,(color&255)/255.0f,(color>>24)/255.0f};
    ID3D11DeviceContext_ClearRenderTargetView(d3d8_GetD3D11Context(),rtv,rgba);
    pilot_textures[handle-1].uploaded=1;
    pilot_textures[handle-1].gpu_written=1;
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
    pilot_textures[handle-1].uploaded=1;pilot_textures[handle-1].gpu_written=1;return S_OK;
}
int32_t pilot_gfx_texture_readback(uint32_t handle,uint32_t level,void *data,uint32_t capacity)
{
    D3D8Texture *texture=pilot_texture(handle);
    if(level>=texture->levels || !data) return E_INVALIDARG;
    UINT width=texture->width>>level,height=texture->height>>level;
    if(!width)width=1; if(!height)height=1;
    UINT pitch=d3d8_row_pitch(texture->d3d8_format,width);
    UINT rows=d3d8_format_is_compressed(texture->d3d8_format)?(height+3)/4:height;
    if(capacity<pitch*rows || !(pilot_textures[handle-1].uploaded&(1u<<level))) return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC desc;
    ID3D11Texture2D_GetDesc(texture->d3d11_texture,&desc);
    /* Retain the complete mip shape: a BC 1x1/2x2 tail cannot be the top
     * level of a standalone BC texture, but is valid inside this chain. */
    desc.BindFlags=0; desc.MiscFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *staging=NULL;
    HRESULT hr=ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(),&desc,NULL,&staging);
    if(FAILED(hr)) return hr;
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11DeviceContext_CopySubresourceRegion(context,(ID3D11Resource*)staging,level,0,0,0,
        (ID3D11Resource*)texture->d3d11_texture,level,NULL);
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr=ID3D11DeviceContext_Map(context,(ID3D11Resource*)staging,level,D3D11_MAP_READ,0,&mapped);
    if(SUCCEEDED(hr)) {
        for(UINT row=0;row<rows;++row) memcpy((char*)data+row*pitch,(char*)mapped.pData+row*mapped.RowPitch,pitch);
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
    pilot_d3d_require(pilot_device->lpVtbl->SetRenderState(pilot_device,(D3DRENDERSTATETYPE)state,value),__FILE__,__LINE__);
}
void pilot_gfx_set_texture_state(uint32_t stage,uint32_t state,uint32_t value)
{
    if (!pilot_device) pilot_d3d_backend_stop("texture state before device creation");
    pilot_d3d_require(pilot_device->lpVtbl->SetTextureStageState(pilot_device,stage,(D3DTEXTURESTAGESTATETYPE)state,value),__FILE__,__LINE__);
}
int32_t pilot_gfx_draw_up(uint32_t primitive,uint32_t count,const void *data,uint32_t stride,uint32_t fvf)
{
    if (!pilot_device) return E_FAIL;
    pilot_d3d_raster_texture_mode(0,NULL);
    HRESULT status=pilot_device->lpVtbl->SetVertexShader(pilot_device,fvf);
    if (SUCCEEDED(status)) status=pilot_device->lpVtbl->DrawPrimitiveUP(pilot_device,(D3DPRIMITIVETYPE)primitive,count,data,stride);
    if(SUCCEEDED(status)) {
        if(pilot_color_target){pilot_textures[pilot_color_target-1].uploaded=1;pilot_textures[pilot_color_target-1].gpu_written=1;}
        if(pilot_depth_target && pilot_depth_target!=UINT32_MAX){pilot_textures[pilot_depth_target-1].uploaded=1;pilot_textures[pilot_depth_target-1].gpu_written=1;}
    }
    return status;
}
int32_t pilot_gfx_draw_vertices(unsigned primitive,unsigned first,unsigned count)
{
    if(!pilot_device || !count || (uint64_t)first+count>UINT32_MAX)return E_INVALIDARG;
    D3D11_PRIMITIVE_TOPOLOGY topology;
    if(primitive==5 && count>=3 && count%3==0)topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    else if(primitive==6 && count>=3)topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    else return E_NOTIMPL;
    float scale[4][4];
    for(unsigned stage=0;stage<4;++stage) {
        scale[stage][0]=scale[stage][1]=scale[stage][2]=scale[stage][3]=1;
        if(pilot_bound_textures[stage]) {
            D3D8Texture *texture=pilot_texture(pilot_bound_textures[stage]);
            if(texture->d3d8_format==0x12 || texture->d3d8_format==0x1e || texture->d3d8_format==0x2e) {
                scale[stage][0]=1.0f/texture->width;scale[stage][1]=1.0f/texture->height;
            }
        }
    }
    pilot_d3d_raster_texture_mode(1,scale);d3d8_shaders_prepare_pixel();d3d8_states_apply();
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11DeviceContext_IASetPrimitiveTopology(context,topology);ID3D11DeviceContext_Draw(context,count,first);
    if(pilot_color_target){pilot_textures[pilot_color_target-1].uploaded=1;pilot_textures[pilot_color_target-1].gpu_written=1;}
    if(pilot_depth_target && pilot_depth_target!=UINT32_MAX){pilot_textures[pilot_depth_target-1].uploaded=1;pilot_textures[pilot_depth_target-1].gpu_written=1;}
    return S_OK;
}
uint32_t pilot_gfx_width(void) { return d3d8_GetBackbufferWidth(); }
uint32_t pilot_gfx_height(void) { return d3d8_GetBackbufferHeight(); }
int32_t pilot_gfx_readback(unsigned char *rgba,uint32_t capacity)
{
    return pilot_gfx_surface_readback(0,rgba,capacity);
}
int32_t pilot_gfx_surface_readback(unsigned surface,unsigned char *rgba,uint32_t capacity)
{
    if (!pilot_device || !rgba || surface>=2 || !pilot_surfaces[surface]) return E_INVALIDARG;
    ID3D11Texture2D *back=pilot_surfaces[surface],*staging=NULL;
    ID3D11Texture2D_AddRef(back);
    D3D11_TEXTURE2D_DESC desc; ID3D11Texture2D_GetDesc(back,&desc);
    if (desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM || capacity<(uint64_t)desc.Width*desc.Height*4) { ID3D11Texture2D_Release(back); return E_INVALIDARG; }
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
    HRESULT status=ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(),&desc,NULL,&staging);
    if (SUCCEEDED(status)) {
        ID3D11DeviceContext_CopyResource(d3d8_GetD3D11Context(),(ID3D11Resource*)staging,(ID3D11Resource*)back);
        D3D11_MAPPED_SUBRESOURCE mapped;
        status=ID3D11DeviceContext_Map(d3d8_GetD3D11Context(),(ID3D11Resource*)staging,0,D3D11_MAP_READ,0,&mapped);
        if (SUCCEEDED(status)) {
            for (UINT y=0;y<desc.Height;++y) memcpy(rgba+y*desc.Width*4,(unsigned char*)mapped.pData+y*mapped.RowPitch,desc.Width*4);
            ID3D11DeviceContext_Unmap(d3d8_GetD3D11Context(),(ID3D11Resource*)staging,0);
        }
        ID3D11Texture2D_Release(staging);
    }
    ID3D11Texture2D_Release(back); return status;
}
