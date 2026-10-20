#include "graphics_lighting_source.inc"
#ifdef PILOT_D3D_PHOBOS
struct NativeLightingBuffer {
    float ambient[4],diffuse[4],material_ambient[4],specular[4],emissive[4],parameters[4];
    uint32_t sources[4],count[4];
    struct {float position_type[4],direction_range[4],diffuse[4],ambient[4],specular[4],attenuation[4],spot[4];} lights[8];
};
static ID3D11Buffer *native_lighting_buffer;
static struct NativeLightingBuffer native_lighting_values;
static unsigned native_lighting_valid;
static void native_lighting_vector(float out[4],const float in[3],const float matrix[16],float w)
{
    for(unsigned i=0;i<3;++i)out[i]=in[0]*matrix[i]+in[1]*matrix[i+4]+in[2]*matrix[i+8]+w*matrix[i+12];
}
static int native_lighting_finite(const void *values,unsigned count)
{
    const unsigned char *bytes=values;
    for(unsigned i=0;i<count;++i){float value;memcpy(&value,bytes+i*4,4);if(!isfinite(value))return 0;}
    return 1;
}
#endif
int pilot_gfx_lighting_ready(void)
{
#ifdef PILOT_D3D_PHOBOS
    return native_lighting_valid!=0;
#else
    return 0;
#endif
}
void pilot_gfx_lighting_invalidate(void)
{
#ifdef PILOT_D3D_PHOBOS
    native_lighting_valid=0;
    if(d3d8_GetD3D11Context()) {
        ID3D11Buffer *empty=NULL;
        ID3D11DeviceContext_VSSetConstantBuffers(d3d8_GetD3D11Context(),4,1,&empty);
    }
#endif
}
int32_t pilot_gfx_lighting_publish(const struct PilotLightingSnapshot *snapshot)
{
#ifdef PILOT_D3D_PHOBOS
    if(!snapshot || !d3d8_GetD3D11Device() || snapshot->count>8 ||
       snapshot->color_vertex>1 || snapshot->local_viewer>1 || snapshot->specular_enable>1 || snapshot->normalize>1 ||
       !native_lighting_finite(snapshot->material.diffuse,4) ||
       !native_lighting_finite(snapshot->material.ambient,3) ||
       !native_lighting_finite(snapshot->material.emissive,3) ||
       (snapshot->specular_enable && (!native_lighting_finite(snapshot->material.specular,3) ||
        !isfinite(snapshot->material.power) || snapshot->material.power<0)) ||
       !native_lighting_finite(snapshot->view,16))return E_INVALIDARG;
    struct NativeLightingBuffer candidate={0};
    if(sizeof(candidate)!=1024)return E_UNEXPECTED;
    for(unsigned i=0;i<4;++i) {
        if(snapshot->sources[i]>2)return E_INVALIDARG;
        candidate.sources[i]=snapshot->color_vertex?snapshot->sources[i]:0;
    }
    candidate.ambient[0]=((snapshot->ambient>>16)&255)/255.0f;
    candidate.ambient[1]=((snapshot->ambient>>8)&255)/255.0f;
    candidate.ambient[2]=(snapshot->ambient&255)/255.0f;
    memcpy(candidate.diffuse,snapshot->material.diffuse,16);
    memcpy(candidate.material_ambient,snapshot->material.ambient,16);
    memcpy(candidate.specular,snapshot->material.specular,16);
    memcpy(candidate.emissive,snapshot->material.emissive,16);
    candidate.parameters[0]=snapshot->specular_enable?snapshot->material.power:0;
    candidate.parameters[1]=(float)snapshot->local_viewer;
    candidate.parameters[2]=(float)snapshot->normalize;candidate.parameters[3]=(float)snapshot->specular_enable;
    candidate.count[0]=snapshot->count;
    for(unsigned i=0;i<snapshot->count;++i) {
        const struct PilotLightingLight *light=&snapshot->lights[i];
        if(light->type<1 || light->type>3 || !native_lighting_finite(light->diffuse,3) ||
           !native_lighting_finite(light->ambient,3) ||
           (snapshot->specular_enable && !native_lighting_finite(light->specular,3)))return E_INVALIDARG;
        if(light->type!=3 && (!native_lighting_finite(light->position,3) || !isfinite(light->range) ||
           !native_lighting_finite(light->attenuation,3)))return E_INVALIDARG;
        if(light->type!=1 && !native_lighting_finite(light->direction,3))return E_INVALIDARG;
        if(light->type!=3 && (light->range<0 || light->attenuation[0]<0 || light->attenuation[1]<0 ||
           light->attenuation[2]<0 || light->attenuation[0]+light->attenuation[1]+light->attenuation[2]<=0))return E_NOTIMPL;
        if(light->type==2 && (!isfinite(light->falloff) || !isfinite(light->theta) || !isfinite(light->phi) ||
           light->falloff<0 || light->theta<0 || light->phi<light->theta || light->phi>3.141592654f))return E_NOTIMPL;
        if(light->type!=3)native_lighting_vector(candidate.lights[i].position_type,light->position,snapshot->view,1);
        if(light->type!=1)native_lighting_vector(candidate.lights[i].direction_range,light->direction,snapshot->view,0);
        if(light->type!=1) {
            const float *d=candidate.lights[i].direction_range;
            if(!(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>0))return E_NOTIMPL;
        }
        candidate.lights[i].position_type[3]=(float)light->type;
        candidate.lights[i].direction_range[3]=light->type!=3?light->range:0;
        memcpy(candidate.lights[i].diffuse,light->diffuse,16);
        memcpy(candidate.lights[i].ambient,light->ambient,16);
        memcpy(candidate.lights[i].specular,light->specular,16);
        if(light->type!=3)memcpy(candidate.lights[i].attenuation,light->attenuation,12);
        if(light->type==2) {
            candidate.lights[i].attenuation[3]=light->falloff;
            candidate.lights[i].spot[0]=cosf(light->theta*.5f);
            candidate.lights[i].spot[1]=cosf(light->phi*.5f);
        }
    }
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    if(!native_lighting_buffer) {
        D3D11_BUFFER_DESC desc={0};desc.ByteWidth=sizeof(candidate);
        desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial={&candidate,0,0};
        HRESULT result=ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(),&desc,&initial,&native_lighting_buffer);
        if(FAILED(result))return result;
    } else if(!native_lighting_valid || memcmp(&candidate,&native_lighting_values,sizeof(candidate)))
        ID3D11DeviceContext_UpdateSubresource(context,(ID3D11Resource*)native_lighting_buffer,0,NULL,&candidate,0,0);
    native_lighting_values=candidate;native_lighting_valid=1;
    ID3D11DeviceContext_VSSetConstantBuffers(context,4,1,&native_lighting_buffer);
    return S_OK;
#else
    (void)snapshot;return E_NOTIMPL;
#endif
}
static void native_lighting_close(void)
{
#ifdef PILOT_D3D_PHOBOS
    if(native_lighting_buffer) {
        ID3D11Buffer *empty=NULL;
        ID3D11DeviceContext_VSSetConstantBuffers(d3d8_GetD3D11Context(),4,1,&empty);
        ID3D11Buffer_Release(native_lighting_buffer);native_lighting_buffer=NULL;
    }
    memset(&native_lighting_values,0,sizeof(native_lighting_values));native_lighting_valid=0;
#endif
}
