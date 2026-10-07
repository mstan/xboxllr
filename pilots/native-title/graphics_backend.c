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
