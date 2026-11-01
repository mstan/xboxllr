#include "gpu_profile.h"
#include <stddef.h>
#if defined(_WIN32) && !defined(PILOT_GPU_PROFILE_PORTABLE)
#define COBJMACROS
#include <d3d11.h>
#include <stdlib.h>
#include <string.h>
struct GpuProfileFrame {
    ID3D11Query *disjoint,*timestamps[PILOT_GPU_PROFILE_PASSES*2];
    PilotGpuProfileSample sample;
    unsigned sealed;
};
struct PilotGpuProfile {
    ID3D11DeviceContext *context;
    struct GpuProfileFrame frames[PILOT_GPU_PROFILE_FRAMES];
    unsigned head,pending,open,active;
    uint64_t dropped;
};
static struct GpuProfileFrame *current(PilotGpuProfile *p)
{return p && p->open?&p->frames[(p->head+p->pending)%PILOT_GPU_PROFILE_FRAMES]:NULL;}
void pilot_gpu_profile_destroy(PilotGpuProfile *p)
{
    if(!p)return;
    if(p->open)pilot_gpu_profile_end_frame(p,0);
    for(unsigned i=0;i<PILOT_GPU_PROFILE_FRAMES;++i){
        struct GpuProfileFrame *f=p->frames+i;
        if(f->disjoint)ID3D11Query_Release(f->disjoint);
        for(unsigned j=0;j<PILOT_GPU_PROFILE_PASSES*2;++j)
            if(f->timestamps[j])ID3D11Query_Release(f->timestamps[j]);
    }
    if(p->context)ID3D11DeviceContext_Release(p->context);
    free(p);
}
PilotGpuProfile *pilot_gpu_profile_create(void *device,void *context)
{
    if(!device || !context || ID3D11DeviceContext_GetType((ID3D11DeviceContext*)context)!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return NULL;
    PilotGpuProfile *p=calloc(1,sizeof(*p));if(!p)return NULL;
    p->context=context;ID3D11DeviceContext_AddRef(p->context);
    for(unsigned i=0;i<PILOT_GPU_PROFILE_FRAMES;++i){
        D3D11_QUERY_DESC desc={D3D11_QUERY_TIMESTAMP_DISJOINT,0};
        if(FAILED(ID3D11Device_CreateQuery((ID3D11Device*)device,&desc,&p->frames[i].disjoint)))goto fail;
        desc.Query=D3D11_QUERY_TIMESTAMP;
        for(unsigned j=0;j<PILOT_GPU_PROFILE_PASSES*2;++j)
            if(FAILED(ID3D11Device_CreateQuery((ID3D11Device*)device,&desc,&p->frames[i].timestamps[j])))goto fail;
    }
    return p;
fail:pilot_gpu_profile_destroy(p);return NULL;
}
int pilot_gpu_profile_begin_frame(PilotGpuProfile *p,uint64_t frame,uint32_t nw,uint32_t nh,uint32_t rw,uint32_t rh)
{
    if(!p || p->open || !nw || !nh || !rw || !rh)return 0;
    if(p->pending==PILOT_GPU_PROFILE_FRAMES){++p->dropped;return 0;}
    struct GpuProfileFrame *f=&p->frames[(p->head+p->pending)%PILOT_GPU_PROFILE_FRAMES];
    memset(&f->sample,0,sizeof(f->sample));f->sealed=0;
    f->sample.source_frame=frame;f->sample.native_width=nw;f->sample.native_height=nh;
    f->sample.render_width=rw;f->sample.render_height=rh;
    p->open=1;p->active=0;
    ID3D11DeviceContext_Begin(p->context,(ID3D11Asynchronous*)f->disjoint);return 1;
}
int pilot_gpu_profile_begin_pass(PilotGpuProfile *p,uint32_t pass)
{
    struct GpuProfileFrame *f=current(p);if(!f)return 0;
    if(p->active){f->sample.flags|=PILOT_GPU_PROFILE_ORDER_ERROR;return 0;}
    if(f->sample.passes==PILOT_GPU_PROFILE_PASSES){f->sample.flags|=PILOT_GPU_PROFILE_OVERFLOW;return 0;}
    unsigned i=f->sample.passes++;f->sample.pass_id[i]=pass;p->active=1;
    ID3D11DeviceContext_End(p->context,(ID3D11Asynchronous*)f->timestamps[i*2]);return 1;
}
int pilot_gpu_profile_end_pass(PilotGpuProfile *p)
{
    struct GpuProfileFrame *f=current(p);if(!f)return 0;
    if(!p->active){f->sample.flags|=PILOT_GPU_PROFILE_ORDER_ERROR;return 0;}
    ID3D11DeviceContext_End(p->context,(ID3D11Asynchronous*)f->timestamps[f->sample.passes*2-1]);
    p->active=0;return 1;
}
int pilot_gpu_profile_upload(PilotGpuProfile *p,uint32_t kind,uint64_t bytes)
{
    struct GpuProfileFrame *f=current(p);if(!f || kind>2)return 0;
    if(UINT64_MAX-f->sample.upload_bytes<bytes){f->sample.flags|=PILOT_GPU_PROFILE_OVERFLOW;return 0;}
    f->sample.upload_bytes+=bytes;
    if(kind==1)f->sample.vertex_bytes+=bytes;
    if(kind==2)f->sample.texture_bytes+=bytes;
    return 1;
}
int pilot_gpu_profile_end_frame(PilotGpuProfile *p,int uploads_complete)
{
    struct GpuProfileFrame *f=current(p);if(!f)return 0;
    if(p->active){f->sample.flags|=PILOT_GPU_PROFILE_ORDER_ERROR;pilot_gpu_profile_end_pass(p);}
    if(!uploads_complete)f->sample.flags|=PILOT_GPU_PROFILE_UPLOADS_UNKNOWN;
    if(!f->sample.passes)f->sample.flags|=PILOT_GPU_PROFILE_ORDER_ERROR;
    ID3D11DeviceContext_End(p->context,(ID3D11Asynchronous*)f->disjoint);
    f->sealed=1;p->open=0;++p->pending;return 1;
}
int pilot_gpu_profile_poll(PilotGpuProfile *p,PilotGpuProfileSample *sample)
{
    if(!p || !sample)return -1;if(!p->pending)return 0;
    struct GpuProfileFrame *f=p->frames+p->head;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data={0};
    HRESULT hr=ID3D11DeviceContext_GetData(p->context,(ID3D11Asynchronous*)f->disjoint,&data,sizeof(data),D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if(hr==S_FALSE)return 0;
    if(FAILED(hr)){f->sample.flags|=PILOT_GPU_PROFILE_DEVICE_ERROR;*sample=f->sample;return -1;}
    if(data.Disjoint || !data.Frequency)f->sample.flags|=PILOT_GPU_PROFILE_DISJOINT;
    f->sample.frequency=data.Frequency;
    for(unsigned i=0;i<f->sample.passes;++i){
        for(unsigned j=0;j<2;++j){
            uint64_t *value=j?&f->sample.end[i]:&f->sample.begin[i];
            hr=ID3D11DeviceContext_GetData(p->context,(ID3D11Asynchronous*)f->timestamps[i*2+j],value,sizeof(*value),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(hr==S_FALSE)return 0;
            if(FAILED(hr)){f->sample.flags|=PILOT_GPU_PROFILE_DEVICE_ERROR;*sample=f->sample;return -1;}
        }
        if(f->sample.end[i]<f->sample.begin[i])f->sample.flags|=PILOT_GPU_PROFILE_DISJOINT;
    }
    *sample=f->sample;f->sealed=0;p->head=(p->head+1)%PILOT_GPU_PROFILE_FRAMES;--p->pending;return 1;
}
unsigned pilot_gpu_profile_pending(const PilotGpuProfile *p){return p?p->pending:0;}
uint64_t pilot_gpu_profile_dropped(const PilotGpuProfile *p){return p?p->dropped:0;}
#else
struct PilotGpuProfile {int unavailable;};
PilotGpuProfile *pilot_gpu_profile_create(void *d,void *c){(void)d;(void)c;return NULL;}
void pilot_gpu_profile_destroy(PilotGpuProfile *p){(void)p;}
int pilot_gpu_profile_begin_frame(PilotGpuProfile *p,uint64_t f,uint32_t nw,uint32_t nh,uint32_t rw,uint32_t rh)
{(void)p;(void)f;(void)nw;(void)nh;(void)rw;(void)rh;return 0;}
int pilot_gpu_profile_begin_pass(PilotGpuProfile *p,uint32_t pass){(void)p;(void)pass;return 0;}
int pilot_gpu_profile_end_pass(PilotGpuProfile *p){(void)p;return 0;}
int pilot_gpu_profile_upload(PilotGpuProfile *p,uint32_t k,uint64_t b){(void)p;(void)k;(void)b;return 0;}
int pilot_gpu_profile_end_frame(PilotGpuProfile *p,int u){(void)p;(void)u;return 0;}
int pilot_gpu_profile_poll(PilotGpuProfile *p,PilotGpuProfileSample *s){(void)p;(void)s;return -1;}
unsigned pilot_gpu_profile_pending(const PilotGpuProfile *p){(void)p;return 0;}
uint64_t pilot_gpu_profile_dropped(const PilotGpuProfile *p){(void)p;return 0;}
#endif
