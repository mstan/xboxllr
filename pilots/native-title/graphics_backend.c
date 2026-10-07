/* Guest-independent adapter over the prepared MIT sp00nznet D3D11 backend.
 * Native COM/window pointers stay here; the guest bridge sees only values. */
#include "d3d8_internal.h"
#include "graphics_backend.h"
#include <stdio.h>
static IDirect3D8 *pilot_factory;
static IDirect3DDevice8 *pilot_device;
static HWND pilot_window;
/* Slot 0 is the drawable back buffer; slot 1 retains the last presented image.
 * Xbox GetBackBuffer(-1) must not alias the image currently being drawn. */
static ID3D11Texture2D *pilot_surfaces[2];
static unsigned pilot_surface_refs[2];
static struct { IDirect3DTexture8 *object; uint32_t uploaded; } pilot_textures[256];
static struct { IDirect3DVertexBuffer8 *object; int uploaded; } pilot_vertices[256];

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
    for (unsigned i=0;i<256;++i)
        if(pilot_vertices[i].object) pilot_d3d_backend_stop("device close with outstanding vertex ownership");
    for (unsigned i=0;i<256;++i)
        if(pilot_textures[i].object) pilot_d3d_backend_stop("device close with outstanding texture ownership");
    for (unsigned i=0;i<2;++i) {
        if (pilot_surface_refs[i]) pilot_d3d_backend_stop("device close with outstanding surface ownership");
        if (pilot_surfaces[i]) { ID3D11Texture2D_Release(pilot_surfaces[i]); pilot_surfaces[i]=NULL; }
    }
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
    return pilot_device->lpVtbl->Clear(pilot_device,0,NULL,flags,color,depth,stencil);
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
int32_t pilot_gfx_present(void)
{
    if (!pilot_device) return E_FAIL;
    ID3D11DeviceContext_CopyResource(d3d8_GetD3D11Context(),(ID3D11Resource*)pilot_surfaces[1],(ID3D11Resource*)pilot_surfaces[0]);
    return pilot_device->lpVtbl->Swap(pilot_device,0);
}
int32_t pilot_gfx_surface_ref(unsigned surface,int acquire)
{
    if (surface>=2 || !pilot_surfaces[surface]) return E_INVALIDARG;
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
    if(vertex->iface.lpVtbl->Release(&vertex->iface)!=0)
        pilot_d3d_backend_stop("native vertex buffer still referenced at destruction");
    memset(&pilot_vertices[handle-1],0,sizeof(pilot_vertices[0]));
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
int32_t pilot_gfx_texture_create(uint32_t width,uint32_t height,uint32_t levels,uint32_t format,uint32_t *handle)
{
    if(!pilot_device || !handle) return E_INVALIDARG;
    if(!width || !height || width>4096 || height>4096 || (width&(width-1)) || (height&(height-1)) ||
       !levels || levels>13 || format<4 || format>7) return E_NOTIMPL;
    unsigned slot;
    for(slot=0;slot<256 && pilot_textures[slot].object;++slot) {}
    if(slot==256) return E_OUTOFMEMORY;
    IDirect3DTexture8 *object=NULL;
    HRESULT hr=d3d8_CreateTextureImpl(width,height,levels,0,(D3DFORMAT)format,&object);
    if(FAILED(hr)) return hr;
    pilot_textures[slot].object=object; pilot_textures[slot].uploaded=0;
    *handle=slot+1;
    return S_OK;
}
void pilot_gfx_texture_destroy(uint32_t handle)
{
    D3D8Texture *texture=pilot_texture(handle);
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
    if(bytes!=width*height*(d3d8_format_bpp(texture->d3d8_format)/8))
        pilot_d3d_backend_stop("texture upload size does not match subresource");
    D3DLOCKED_RECT lock;
    pilot_d3d_require(texture->iface.lpVtbl->LockRect(&texture->iface,level,&lock,NULL,0),__FILE__,__LINE__);
    if((pilot_textures[handle-1].uploaded&(1u<<level)) && !memcmp(lock.pBits,data,bytes)) {
        texture->locked=FALSE; return; /* No changed CPU work to upload. */
    }
    memcpy(lock.pBits,data,bytes);
    pilot_d3d_require(texture->iface.lpVtbl->UnlockRect(&texture->iface,level),__FILE__,__LINE__);
    pilot_textures[handle-1].uploaded|=1u<<level;
    fprintf(stderr,"[TEXTURE UPLOAD] native=%u level=%u %ux%u bytes=%u\n",handle,level,width,height,bytes);
    /* Diagnostic readback is opt-in and absent from production upload work. */
    const char *capture=getenv("XLLE_PILOT_TEXTURE_CAPTURE");
    if(capture && *capture) {
        static unsigned sequence;
        char path[1024]; unsigned char *pixels=malloc(bytes);
        if(!pixels || snprintf(path,sizeof(path),"%s/texture-%u-level-%u-%u.bin",capture,handle,level,++sequence)>=(int)sizeof(path))
            pilot_d3d_backend_stop("texture capture allocation/path failed");
        pilot_d3d_require(pilot_gfx_texture_readback(handle,level,pixels,bytes),__FILE__,__LINE__);
        FILE *output=fopen(path,"wb");
        uint32_t header[]={0x58544558,1,handle,level,width,height,(uint32_t)texture->d3d8_format,bytes};
        if(!output || fwrite(header,sizeof(header),1,output)!=1 || fwrite(pixels,bytes,1,output)!=1 || fclose(output))
            pilot_d3d_backend_stop("texture capture write failed");
        free(pixels);
    }
}
int32_t pilot_gfx_texture_readback(uint32_t handle,uint32_t level,void *data,uint32_t capacity)
{
    D3D8Texture *texture=pilot_texture(handle);
    if(level>=texture->levels || !data) return E_INVALIDARG;
    UINT width=texture->width>>level,height=texture->height>>level;
    if(!width)width=1; if(!height)height=1;
    UINT pitch=width*(d3d8_format_bpp(texture->d3d8_format)/8);
    if(capacity<pitch*height || !(pilot_textures[handle-1].uploaded&(1u<<level))) return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC desc;
    ID3D11Texture2D_GetDesc(texture->d3d11_texture,&desc);
    desc.Width=width; desc.Height=height; desc.MipLevels=1;
    desc.BindFlags=0; desc.MiscFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *staging=NULL;
    HRESULT hr=ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(),&desc,NULL,&staging);
    if(FAILED(hr)) return hr;
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11DeviceContext_CopySubresourceRegion(context,(ID3D11Resource*)staging,0,0,0,0,
        (ID3D11Resource*)texture->d3d11_texture,level,NULL);
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr=ID3D11DeviceContext_Map(context,(ID3D11Resource*)staging,0,D3D11_MAP_READ,0,&mapped);
    if(SUCCEEDED(hr)) {
        for(UINT row=0;row<height;++row) memcpy((char*)data+row*pitch,(char*)mapped.pData+row*mapped.RowPitch,pitch);
        ID3D11DeviceContext_Unmap(context,(ID3D11Resource*)staging,0);
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
    HRESULT status=pilot_device->lpVtbl->SetVertexShader(pilot_device,fvf);
    if (SUCCEEDED(status)) status=pilot_device->lpVtbl->DrawPrimitiveUP(pilot_device,(D3DPRIMITIVETYPE)primitive,count,data,stride);
    return status;
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
