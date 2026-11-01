#include "gpu_profile.h"
#include <assert.h>
#include <stdio.h>
#if defined(_WIN32) && !defined(PILOT_GPU_PROFILE_PORTABLE)
#define COBJMACROS
#include <d3d11.h>
static void consume(PilotGpuProfile *p,unsigned expected)
{
    DWORD start=GetTickCount();unsigned count=0,valid=0;PilotGpuProfileSample sample;
    while(pilot_gpu_profile_pending(p)){
        int ready=pilot_gpu_profile_poll(p,&sample);assert(ready>=0);
        if(!ready){assert(GetTickCount()-start<10000);Sleep(1);continue;}
        assert(sample.source_frame<8 && sample.passes==2 && sample.frequency);
        assert(sample.upload_bytes==3072 && sample.vertex_bytes==1024 && sample.texture_bytes==2048);
        assert(!(sample.flags&~PILOT_GPU_PROFILE_DISJOINT));
        if(!(sample.flags&PILOT_GPU_PROFILE_DISJOINT)){
            ++valid;
            assert(sample.end[0]>=sample.begin[0] && sample.end[1]>=sample.begin[1]);
            printf("GPU frame=%llu frequency=%llu pass0=%llu pass1=%llu flags=%u\n",
                (unsigned long long)sample.source_frame,(unsigned long long)sample.frequency,
                (unsigned long long)(sample.end[0]-sample.begin[0]),(unsigned long long)(sample.end[1]-sample.begin[1]),sample.flags);
        }
        ++count;
    }
    assert(count==expected && valid>0);
}
int main(void)
{
    ID3D11Device *device=NULL;ID3D11DeviceContext *context=NULL;D3D_FEATURE_LEVEL level;
    HRESULT hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,NULL,0,D3D11_SDK_VERSION,&device,&level,&context);
    assert(SUCCEEDED(hr) && device && context);
    PilotGpuProfile *p=pilot_gpu_profile_create(device,context);assert(p);
    D3D11_TEXTURE2D_DESC td={0};td.Width=64;td.Height=64;td.MipLevels=td.ArraySize=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *texture=NULL;ID3D11RenderTargetView *target=NULL;
    assert(SUCCEEDED(ID3D11Device_CreateTexture2D(device,&td,NULL,&texture)));
    assert(SUCCEEDED(ID3D11Device_CreateRenderTargetView(device,(ID3D11Resource*)texture,NULL,&target)));
    for(unsigned frame=0;frame<8;++frame){
        assert(pilot_gpu_profile_begin_frame(p,frame,32,32,64,64));
        for(unsigned pass=0;pass<2;++pass){
            float color[4]={pass?0.25f:0.75f,0,0,1};
            assert(pilot_gpu_profile_begin_pass(p,pass));
            ID3D11DeviceContext_ClearRenderTargetView(context,target,color);
            assert(pilot_gpu_profile_end_pass(p));
        }
        assert(pilot_gpu_profile_upload(p,1,1024));assert(pilot_gpu_profile_upload(p,2,2048));
        assert(pilot_gpu_profile_end_frame(p,1));
    }
    assert(!pilot_gpu_profile_begin_frame(p,8,32,32,64,64));assert(pilot_gpu_profile_dropped(p)==1);
    /* Headless fixture submission only. The profiler itself never flushes. */
    ID3D11DeviceContext_Flush(context);consume(p,8);
    pilot_gpu_profile_destroy(p);ID3D11RenderTargetView_Release(target);ID3D11Texture2D_Release(texture);
    ID3D11DeviceContext_Release(context);ID3D11Device_Release(device);
    puts("PASS bounded hardware timestamp ring; component fixture only, not title scaling/performance qualification");return 0;
}
#else
int main(void)
{
    PilotGpuProfileSample sample;
    assert(!pilot_gpu_profile_create(NULL,NULL));assert(!pilot_gpu_profile_begin_frame(NULL,1,1,1,1,1));
    assert(!pilot_gpu_profile_begin_pass(NULL,1));assert(!pilot_gpu_profile_end_pass(NULL));
    assert(!pilot_gpu_profile_upload(NULL,0,1));assert(!pilot_gpu_profile_end_frame(NULL,0));
    assert(pilot_gpu_profile_poll(NULL,&sample)==-1);pilot_gpu_profile_destroy(NULL);
    assert(!pilot_gpu_profile_pending(NULL) && !pilot_gpu_profile_dropped(NULL));
    puts("PASS instrumentation-unavailable fallback; no renderer/output qualification implied");return 0;
}
#endif
