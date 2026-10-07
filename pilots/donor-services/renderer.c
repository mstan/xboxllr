/* Exercise the external donor's real GPU backend, without its guest/kernel
 * runtime. This proves backend extraction, not Xbox ABI or game compatibility. */
#include "d3d8_xbox.h"
#include <stdio.h>
#include <stdint.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define HR(x) CHECK((x)==S_OK)

int main(void)
{
    WNDCLASSW wc={0};
    wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(NULL);
    wc.lpszClassName=L"XlleDonorReferenceProbe";
    CHECK(RegisterClassW(&wc));
    HWND window=CreateWindowW(wc.lpszClassName,L"Donor reference probe",WS_OVERLAPPEDWINDOW,
        0,0,128,128,NULL,NULL,wc.hInstance,NULL);
    CHECK(window);
    IDirect3D8 *api=xbox_Direct3DCreate8(0);
    CHECK(api);
    D3DPRESENT_PARAMETERS pp={0};
    pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.Windowed=TRUE;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;
    IDirect3DDevice8 *device=NULL;
    HR(api->lpVtbl->CreateDevice(api,0,0,window,0,&pp,&device));
    CHECK(device);
    HR(device->lpVtbl->Clear(device,0,NULL,D3DCLEAR_TARGET,0xff204060u,1,0));
    IDirect3DSurface8 *surface=NULL;
    HR(device->lpVtbl->GetBackBuffer(device,0,0,&surface));
    CHECK(surface);
    D3DLOCKED_RECT locked={0};
    HR(surface->lpVtbl->LockRect(surface,&locked,NULL,D3DLOCK_READONLY));
    CHECK(locked.pBits && locked.Pitch>=64*4);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        const uint32_t *row=(const uint32_t*)((const uint8_t*)locked.pBits+y*locked.Pitch);
        if((row[x]&0xffffffu)!=0x204060u) {
            fprintf(stderr,"FAIL: surface pixel (%u,%u)=%08x, expected X8R8G8B8 xx204060\n",x,y,row[x]);
            return 1;
        }
    }
    HR(surface->lpVtbl->UnlockRect(surface));
    surface->lpVtbl->Release(surface);
    device->lpVtbl->Release(device);api->lpVtbl->Release(api);
    CHECK(DestroyWindow(window));
    CHECK(UnregisterClassW(wc.lpszClassName,wc.hInstance));
    puts("PASS: external renderer created D3D11 device and read back 4096 actual cleared pixels; no guest ABI/presentation claim");
    return 0;
}
