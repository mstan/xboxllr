#include "graphics_donor.h"
#include "graphics_backend.h"
#include "pilot_d3d_checks.h"
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifdef PILOT_D3D_PHOBOS
#include "rhi.h"
#include "rhi_shader_cache.h"
#endif
#include "graphics_shader_source.inc"
#include "graphics_current_backend.inc"
struct NativeShader {
    ID3D11VertexShader *object;
    ID3D11InputLayout *layout;
    ID3DBlob *code;
    ID3D11VertexShader *raw_object;
    ID3D11InputLayout *raw_layout;
    ID3DBlob *raw_code;
    char *source;
    struct PilotVertexElement elements[16];
    unsigned used,current;
    unsigned stream_mask;
    uint32_t stream_extents[16];
    unsigned blend_recipe;
    uint32_t fixed_fvf,source_fvf,immediate_fvf,fog_fvf,fog_immediate;
};
static struct NativeShader native_shaders[256];
static ID3D11Buffer *native_constants_buffer;
static uint32_t native_constants[192][4];
static unsigned native_constants_dirty,native_constants_uploads,native_shader_bound;
static ID3D11Buffer *native_raster_buffer;
static float native_raster_values[12];
static unsigned native_shader_active,native_constants_bound,native_raster_bound;
static struct NativeShader *native_shader(uint32_t handle)
{
    if(!handle || handle>256 || !native_shaders[handle-1].object)pilot_d3d_backend_stop("unowned native vertex shader");
    return &native_shaders[handle-1];
}
#include "graphics_blend_admission_backend.inc"
int32_t pilot_gfx_shader_bind(uint32_t handle)
{
    pilot_gfx_batch_flush();
    native_shader_active=0;
    struct NativeShader *shader=native_shader(handle);
    if(shader->fixed_fvf) {
        pilot_gfx_shader_fixed_binding();native_shader_bound=handle;return S_OK;
    }
    if(!native_constants_buffer) {
        D3D11_BUFFER_DESC desc={0};
        desc.ByteWidth=sizeof(native_constants);desc.Usage=D3D11_USAGE_DYNAMIC;
        desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        HRESULT result=ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(),&desc,NULL,&native_constants_buffer);
        if(FAILED(result))return result;
        native_constants_dirty=1;
    }
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11DeviceContext_VSSetShader(context,shader->object,NULL,0);
    ID3D11DeviceContext_IASetInputLayout(context,shader->layout);
    ID3D11DeviceContext_VSSetConstantBuffers(context,1,1,&native_constants_buffer);
    native_shader_bound=handle;native_shader_active=native_constants_bound=1;
    pilot_d3d_program_fog_mode(1);return shader->current?native_current_flush():S_OK;
}
void pilot_gfx_shader_constants(unsigned first,unsigned count,const void *data)
{
    if(first>=192 || !count || count>192-first || !data)pilot_d3d_backend_stop("vertex constant range");
    if(memcmp(native_constants[first],data,count*16)) {
        memcpy(native_constants[first],data,count*16);native_constants_dirty=1;
    }
}
int32_t pilot_gfx_shader_flush(void)
{
    if(!native_constants_buffer || !native_shader_bound)return E_INVALIDARG;
    if(native_constants_dirty) {
        pilot_gfx_batch_flush();
        ID3D11DeviceContext *context=d3d8_GetD3D11Context();D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT result=ID3D11DeviceContext_Map(context,(ID3D11Resource*)native_constants_buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
        if(FAILED(result))return result;
        memcpy(mapped.pData,native_constants,sizeof(native_constants));
        ID3D11DeviceContext_Unmap(context,(ID3D11Resource*)native_constants_buffer,0);
        native_constants_dirty=0;++native_constants_uploads;
    }
    return native_shader(native_shader_bound)->current?native_current_flush():S_OK;
}
unsigned pilot_gfx_shader_uploads(void) {return native_constants_uploads;}
unsigned pilot_gfx_shader_current_inputs(uint32_t handle){return native_shader(handle)->current;}
unsigned pilot_gfx_shader_streams(uint32_t extents[16])
{
    struct NativeShader *shader=native_shader(native_shader_bound);
    static int plans=-1;
    if(plans<0) {const char *value=getenv("XLLE_PILOT_SHADER_STREAM_PLAN");plans=!(value && !strcmp(value,"0"));}
    if(plans) {memcpy(extents,shader->stream_extents,sizeof(shader->stream_extents));return shader->stream_mask;}
    unsigned mask=0;memset(extents,0,16*4);
    for(unsigned i=0;i<16;++i)if((shader->used&(1u<<i)) && shader->elements[i].type) {
        struct PilotVertexElement *element=&shader->elements[i];unsigned stream=element->stream;
        uint32_t end=element->offset+shader_format(element->type)->bytes;
        if(end>extents[stream])extents[stream]=end;mask|=1u<<stream;
    }
    return mask;
}
void pilot_gfx_shader_capture_constants(void *data) {memcpy(data,native_constants,sizeof(native_constants));}
static int32_t native_shader_compile(struct NativeShader *candidate,char *source,
                                    const struct PilotVertexElement elements[16])
{
    struct NativeShader shader=*candidate;
    ID3DBlob *errors=NULL;
    HRESULT result=E_FAIL;
#ifdef PILOT_D3D_PHOBOS
    /* Reuse the renderer's exact-source/entry/profile/settings cache. Input
     * layouts and device-owned shader objects still belong to each caller.
     * A rejected blob is a miss; compilation remains the portable fallback. */
    const char *cache_tag="xboxlle-native-d3dcompiler47-vs5-O3-v1";
    RhiShaderSource cache_source={0};
    cache_source.hlsl=source;cache_source.len=strlen(source);cache_source.name="xlle-nv2a";
    cache_source.entry="raster_main";cache_source.target="vs_5_0";cache_source.optimize=1;
    void *cached=NULL;size_t cached_bytes=0;
    if(rhi_shader_cache_get(&cache_source,cache_tag,&cached,&cached_bytes)) {
        result=D3DCreateBlob(cached_bytes,&shader.code);
        if(SUCCEEDED(result)) {
            memcpy(ID3D10Blob_GetBufferPointer(shader.code),cached,cached_bytes);
            result=ID3D11Device_CreateVertexShader(d3d8_GetD3D11Device(),cached,cached_bytes,NULL,&shader.object);
            if(FAILED(result)) {
                fprintf(stderr,"[NATIVE SHADER CACHE] rejected %08x; recompiling\n",(unsigned)result);
                if(shader.object)ID3D11VertexShader_Release(shader.object);
                shader.object=NULL;ID3D10Blob_Release(shader.code);shader.code=NULL;
            } else fprintf(stderr,"[NATIVE SHADER CACHE] hit bytes=%zu\n",cached_bytes);
        }
        free(cached);
    }
#endif
    if(!shader.code) {
        result=D3DCompile(source,strlen(source),"xlle-nv2a",NULL,NULL,"raster_main","vs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader.code,&errors);
#ifdef PILOT_D3D_PHOBOS
        if(SUCCEEDED(result))rhi_shader_cache_put(&cache_source,cache_tag,
            ID3D10Blob_GetBufferPointer(shader.code),ID3D10Blob_GetBufferSize(shader.code));
#endif
    }
    if(FAILED(result))fprintf(stderr,"[SHADER COMPILE] %s\n",errors?(const char*)ID3D10Blob_GetBufferPointer(errors):"no compiler diagnostic");
    const char *capture=getenv("XLLE_PILOT_SHADER_CAPTURE");
    if(capture && *capture) {
        static unsigned sequence;char path[1024];
        if(snprintf(path,sizeof(path),"%s/shader-%u.hlsl",capture,++sequence)>=(int)sizeof(path))pilot_d3d_backend_stop("shader capture path too long");
        FILE *file=fopen(path,"wb");
        if(!file || fwrite(source,strlen(source),1,file)!=1 || fclose(file))pilot_d3d_backend_stop("shader capture failed");
    }
    if(errors)ID3D10Blob_Release(errors);
    if(FAILED(result)){free(source);return result;}
    shader.source=source;
    if(!shader.object)result=ID3D11Device_CreateVertexShader(d3d8_GetD3D11Device(),ID3D10Blob_GetBufferPointer(shader.code),ID3D10Blob_GetBufferSize(shader.code),NULL,&shader.object);
    D3D11_INPUT_ELEMENT_DESC layout[16]={0};unsigned n=0;
    for(unsigned i=0;i<16;++i)if((shader.used&(1u<<i)) && elements[i].type) {
        if(elements[i].stream>=16)pilot_d3d_backend_stop("invalid declaration stream");
        layout[n].SemanticName="ATTR";layout[n].SemanticIndex=i;
        layout[n].Format=shader_format(elements[i].type)->native;
        layout[n].InputSlot=elements[i].stream;layout[n].AlignedByteOffset=elements[i].offset;
        layout[n++].InputSlotClass=D3D11_INPUT_PER_VERTEX_DATA;
    }
    if(SUCCEEDED(result) && n)result=ID3D11Device_CreateInputLayout(d3d8_GetD3D11Device(),layout,n,
        ID3D10Blob_GetBufferPointer(shader.code),ID3D10Blob_GetBufferSize(shader.code),&shader.layout);
    if(FAILED(result)) {
        if(shader.object)ID3D11VertexShader_Release(shader.object);
        ID3D10Blob_Release(shader.code);free(source);return result;
    }
    /* Declaration and used-input mask are immutable for this shader's entire
     * lifetime. Build the native stream job once for both programmed and fixed
     * declarations, retaining the original per-draw calculation for diagnostics. */
    memset(shader.stream_extents,0,sizeof(shader.stream_extents));shader.stream_mask=0;
    for(unsigned i=0;i<16;++i)if((shader.used&(1u<<i)) && elements[i].type) {
        unsigned stream=elements[i].stream;
        uint32_t end=elements[i].offset+shader_format(elements[i].type)->bytes;
        if(end>shader.stream_extents[stream])shader.stream_extents[stream]=end;
        shader.stream_mask|=1u<<stream;
    }
    memcpy(shader.elements,elements,sizeof(shader.elements));*candidate=shader;
    return S_OK;
}
int32_t pilot_gfx_shader_create(const uint32_t *code,unsigned count,const struct PilotVertexElement elements[16],uint32_t *handle)
{
    if(!code || !elements || !handle || !count || count>136 || !d3d8_GetD3D11Device())return E_INVALIDARG;
    unsigned slot;
    for(slot=0;slot<256 && native_shaders[slot].object;++slot){}
    if(slot==256)return E_OUTOFMEMORY;
    struct NativeShader shader={0};shader.blend_recipe=shader_blend_recipe(code,count,elements);
    char *source=shader_source(code,count,elements,&shader.used);if(!source)return E_OUTOFMEMORY;
    for(unsigned i=0;i<16;++i)if((shader.used&(1u<<i)) && !elements[i].type)shader.current|=1u<<i;
    HRESULT result=native_shader_compile(&shader,source,elements);if(FAILED(result))return result;
    native_shaders[slot]=shader;*handle=slot+1;
    fprintf(stderr,"[SHADER CREATE] native=%u instructions=%u inputs=%04x current=%04x bytecode=%zu\n",*handle,count,shader.used,shader.current,ID3D10Blob_GetBufferSize(shader.code));
    return S_OK;
}
#include "graphics_lighting_backend.inc"
#include "graphics_fixed_backend.inc"
#include "graphics_fvf_unblended_backend.inc"
#include "graphics_immediate_backend.inc"
#include "graphics_pretransformed_fog_backend.inc"
void pilot_gfx_shader_destroy(uint32_t handle)
{
    pilot_gfx_batch_flush();
    struct NativeShader *shader=native_shader(handle);
    if(native_shader_bound==handle) {
        ID3D11DeviceContext_VSSetShader(d3d8_GetD3D11Context(),NULL,NULL,0);
        ID3D11DeviceContext_IASetInputLayout(d3d8_GetD3D11Context(),NULL);
        native_shader_bound=0;
        pilot_gfx_shader_fixed_binding();
    }
    if(shader->layout)ID3D11InputLayout_Release(shader->layout);
    if(shader->raw_layout)ID3D11InputLayout_Release(shader->raw_layout);
    if(shader->raw_object)ID3D11VertexShader_Release(shader->raw_object);
    if(shader->raw_code)ID3D10Blob_Release(shader->raw_code);
    free(shader->source);
    ID3D11VertexShader_Release(shader->object);ID3D10Blob_Release(shader->code);memset(shader,0,sizeof(*shader));
}
void pilot_gfx_shaders_close(void)
{
    pilot_gfx_batch_flush();
    native_fixed_window_pending=0;
    native_pretransformed_fog_close();native_lighting_close();
    native_current_close();
    for(unsigned i=0;i<256;++i)if(native_shaders[i].object)pilot_gfx_shader_destroy(i+1);
    if(native_constants_buffer) {
        ID3D11Buffer *empty=NULL;
        ID3D11DeviceContext_VSSetConstantBuffers(d3d8_GetD3D11Context(),1,1,&empty);
        ID3D11Buffer_Release(native_constants_buffer);native_constants_buffer=NULL;
    }
    memset(native_constants,0,sizeof(native_constants));native_constants_dirty=native_constants_uploads=0;
    if(native_raster_buffer){ID3D11Buffer *empty=NULL;ID3D11DeviceContext_VSSetConstantBuffers(d3d8_GetD3D11Context(),0,1,&empty);ID3D11Buffer_Release(native_raster_buffer);native_raster_buffer=NULL;}
    memset(native_raster_values,0,sizeof(native_raster_values));
    pilot_gfx_shader_fixed_binding();
}
int32_t pilot_gfx_shader_raster(const float scale[4],const float offset[4])
{
    float values[12]={0};memcpy(values,scale,16);memcpy(values+4,offset,16);
    const DWORD *states=d3d8_GetRenderStates();
    if(states[D3DRS_FOGENABLE]) {
        values[8]=(float)states[D3DRS_FOGTABLEMODE];
        memcpy(values+9,states+D3DRS_FOGSTART,4);
        memcpy(values+10,states+D3DRS_FOGEND,4);
        memcpy(values+11,states+D3DRS_FOGDENSITY,4);
        if(values[8]<1 || values[8]>3 || !isfinite(values[9]) || !isfinite(values[10]) ||
           !isfinite(values[11]) || (values[8]==3 && values[9]==values[10]))return E_INVALIDARG;
    }
    ID3D11Device *device=d3d8_GetD3D11Device();ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    if(!native_raster_buffer) {
        D3D11_BUFFER_DESC desc={0};desc.ByteWidth=sizeof(values);desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial={values,0,0};HRESULT hr=ID3D11Device_CreateBuffer(device,&desc,&initial,&native_raster_buffer);
        if(FAILED(hr))return hr;memcpy(native_raster_values,values,sizeof(values));
    } else if(memcmp(native_raster_values,values,sizeof(values))) {
        pilot_gfx_batch_flush();
        ID3D11DeviceContext_UpdateSubresource(context,(ID3D11Resource*)native_raster_buffer,0,NULL,values,0,0);
        memcpy(native_raster_values,values,sizeof(values));
    }
    /* A prepared strip group already owns this exact VS0 binding. Keep all
     * byte/fog validation and constant/current-input flushes above/below; only
     * the redundant COM rebind is reused. Fixed/foreign shader binding clears
     * native_raster_bound, and all native input mutations retire the group.
     * Presentation touches PS7 and restores shaders; it does not replace VS0. */
    static int reuse=-1;
    if(reuse<0) {const char *value=getenv("XLLE_PILOT_RASTER_BINDING_REUSE");reuse=!(value && !strcmp(value,"0"));}
    if(!reuse || !native_raster_bound || !pilot_gfx_batch_prepared())
        ID3D11DeviceContext_VSSetConstantBuffers(context,0,1,&native_raster_buffer);
    native_raster_bound=1;
    return pilot_gfx_shader_flush();
}
static int32_t shader_raw_prepare(struct NativeShader *shader)
{
    if(shader->raw_object)return S_OK;
    ID3DBlob *errors=NULL;
    HRESULT hr=D3DCompile(shader->source,strlen(shader->source),"xlle-nv2a-reference",NULL,NULL,"main","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader->raw_code,&errors);
    if(errors)ID3D10Blob_Release(errors);if(FAILED(hr))goto failure;
    hr=ID3D11Device_CreateVertexShader(d3d8_GetD3D11Device(),ID3D10Blob_GetBufferPointer(shader->raw_code),ID3D10Blob_GetBufferSize(shader->raw_code),NULL,&shader->raw_object);
    if(FAILED(hr))goto failure;
    D3D11_INPUT_ELEMENT_DESC layout[16]={0};unsigned n=0;
    for(unsigned i=0;i<16;++i)if((shader->used&(1u<<i)) && shader->elements[i].type) {
        layout[n].SemanticName="ATTR";layout[n].SemanticIndex=i;
        layout[n].Format=shader_format(shader->elements[i].type)->native;
        layout[n].InputSlot=shader->elements[i].stream;layout[n].AlignedByteOffset=shader->elements[i].offset;
        layout[n++].InputSlotClass=D3D11_INPUT_PER_VERTEX_DATA;
    }
    if(n)hr=ID3D11Device_CreateInputLayout(d3d8_GetD3D11Device(),layout,n,
        ID3D10Blob_GetBufferPointer(shader->raw_code),ID3D10Blob_GetBufferSize(shader->raw_code),&shader->raw_layout);
    if(FAILED(hr))goto failure;
    return hr;
failure:
    if(shader->raw_layout){ID3D11InputLayout_Release(shader->raw_layout);shader->raw_layout=NULL;}
    if(shader->raw_object){ID3D11VertexShader_Release(shader->raw_object);shader->raw_object=NULL;}
    if(shader->raw_code){ID3D10Blob_Release(shader->raw_code);shader->raw_code=NULL;}
    return hr;
}
/* Diagnostic only: execute the actual compiled vertex shader and read its
 * stream output, without rasterization or a duplicate production execution.
 * A deferred context preserves the caller's immediate graphics state. */
static int32_t shader_probe(uint32_t handle,const struct PilotVertexProbeStream streams[16],unsigned count,
                           const float constants[192][4],float *outputs,int raster)
{
    struct NativeShader *shader=native_shader(handle);
    if(!streams || !outputs || !count || count>65536)return E_INVALIDARG;
    if(shader->fixed_fvf)return E_NOTIMPL; /* This probe owns NV2A program outputs. */
    HRESULT raw_result=raster?S_OK:shader_raw_prepare(shader);if(FAILED(raw_result))return raw_result;
    unsigned stream_mask=0;
    for(unsigned i=0;i<16;++i)if((shader->used&(1u<<i)) && shader->elements[i].type) {
        unsigned stream=shader->elements[i].stream;stream_mask|=1u<<stream;
        if(!streams[stream].data || !streams[stream].stride || streams[stream].stride>2048 ||
            shader->elements[i].offset+shader_format(shader->elements[i].type)->bytes>streams[stream].stride)return E_INVALIDARG;
    }
    ID3D11Device *device=d3d8_GetD3D11Device();
    ID3D11DeviceContext *context=NULL;
    ID3D11GeometryShader *geometry=NULL;
    ID3D11Buffer *vb[16]={0},*cb=NULL,*raster_cb=NULL,*current_cb=NULL,*output=NULL,*staging=NULL;
    ID3D11CommandList *commands=NULL;
    HRESULT result=ID3D11Device_CreateDeferredContext(device,0,&context);
    if(FAILED(result))goto cleanup;
    D3D11_SO_DECLARATION_ENTRY entries[17]={0};unsigned n=0;
    static const char *raster_semantics[13]={"SV_POSITION",NULL,NULL,"COLOR","COLOR","TEXCOORD","PSIZE","TEXCOORD","TEXCOORD","TEXCOORD","TEXCOORD","TEXCOORD","TEXCOORD"};
    static const unsigned raster_indices[13]={0,0,0,0,1,4,0,6,7,0,1,2,3};
    for(unsigned i=0;i<13;++i) {
        unsigned width=raster && (i==7 || i==8)?4:shader_output_widths[i];
        if(width) {
            entries[n].SemanticName=raster?raster_semantics[i]:shader_semantics[i];
            entries[n].SemanticIndex=raster?raster_indices[i]:shader_semantic_indices[i];
            entries[n++].ComponentCount=(BYTE)width;
        }
        if(width<4)entries[n++].ComponentCount=(BYTE)(4-width);
    }
    UINT output_stride=13*16;
    ID3DBlob *probe_code=raster?shader->code:shader->raw_code;
    result=ID3D11Device_CreateGeometryShaderWithStreamOutput(device,ID3D10Blob_GetBufferPointer(probe_code),
        ID3D10Blob_GetBufferSize(probe_code),entries,n,&output_stride,1,D3D11_SO_NO_RASTERIZED_STREAM,NULL,&geometry);
    if(FAILED(result))goto cleanup;
    D3D11_BUFFER_DESC desc={0};D3D11_SUBRESOURCE_DATA initial={0};
    UINT zero=0;
    desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    for(unsigned i=0;i<16;++i)if(stream_mask&(1u<<i)) {
        desc.ByteWidth=streams[i].stride*count;initial.pSysMem=streams[i].data;
        result=ID3D11Device_CreateBuffer(device,&desc,&initial,&vb[i]);if(FAILED(result))goto cleanup;
        ID3D11DeviceContext_IASetVertexBuffers(context,i,1,&vb[i],&streams[i].stride,&zero);
    }
    if(constants) {
        desc.ByteWidth=192*16;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;initial.pSysMem=constants;
        result=ID3D11Device_CreateBuffer(device,&desc,&initial,&cb);if(FAILED(result))goto cleanup;
    } else {
        /* Assert the real binding, then execute with its actual GPU buffer.
         * This diagnostic never substitutes a copy of the expected values. */
        if(native_shader_bound!=handle){result=E_INVALIDARG;goto cleanup;}
        result=pilot_gfx_shader_flush();if(FAILED(result))goto cleanup;
        ID3D11DeviceContext *immediate=d3d8_GetD3D11Context();
        ID3D11VertexShader *bound=NULL;ID3D11InputLayout *layout=NULL;
        ID3D11DeviceContext_VSGetShader(immediate,&bound,NULL,NULL);
        ID3D11DeviceContext_IAGetInputLayout(immediate,&layout);
        int matches=bound==shader->object && layout==shader->layout;
        if(bound)ID3D11VertexShader_Release(bound);if(layout)ID3D11InputLayout_Release(layout);
        if(!matches){result=E_INVALIDARG;goto cleanup;}
        ID3D11DeviceContext_VSGetConstantBuffers(immediate,1,1,&cb);
        if(cb!=native_constants_buffer){result=E_INVALIDARG;goto cleanup;}
    }
    if(shader->current) {
        if(constants) {
            float values[16][4];result=native_current_snapshot(values);if(FAILED(result))goto cleanup;
            desc.ByteWidth=sizeof(values);desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;initial.pSysMem=values;
            result=ID3D11Device_CreateBuffer(device,&desc,&initial,&current_cb);if(FAILED(result))goto cleanup;
        } else {
            ID3D11DeviceContext_VSGetConstantBuffers(d3d8_GetD3D11Context(),3,1,&current_cb);
            if(current_cb!=native_current_buffer){result=E_INVALIDARG;goto cleanup;}
        }
        ID3D11DeviceContext_VSSetConstantBuffers(context,3,1,&current_cb);
    }
    desc.ByteWidth=count*output_stride;desc.BindFlags=D3D11_BIND_STREAM_OUTPUT;desc.Usage=D3D11_USAGE_DEFAULT;
    result=ID3D11Device_CreateBuffer(device,&desc,NULL,&output);if(FAILED(result))goto cleanup;
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    result=ID3D11Device_CreateBuffer(device,&desc,NULL,&staging);if(FAILED(result))goto cleanup;
    if(raster) {
        if(native_shader_bound!=handle || !native_raster_buffer){result=E_INVALIDARG;goto cleanup;}
        ID3D11DeviceContext_VSGetConstantBuffers(d3d8_GetD3D11Context(),0,1,&raster_cb);
        if(raster_cb!=native_raster_buffer){result=E_INVALIDARG;goto cleanup;}
        ID3D11DeviceContext_VSSetConstantBuffers(context,0,1,&raster_cb);
    }
    ID3D11DeviceContext_IASetInputLayout(context,raster?shader->layout:shader->raw_layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(context,D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ID3D11DeviceContext_VSSetShader(context,raster?shader->object:shader->raw_object,NULL,0);
    ID3D11DeviceContext_VSSetConstantBuffers(context,1,1,&cb);
    ID3D11DeviceContext_GSSetShader(context,geometry,NULL,0);
    ID3D11DeviceContext_SOSetTargets(context,1,&output,&zero);
    ID3D11DeviceContext_Draw(context,count,0);
    ID3D11DeviceContext_SOSetTargets(context,0,NULL,NULL);
    ID3D11DeviceContext_CopyResource(context,(ID3D11Resource*)staging,(ID3D11Resource*)output);
    result=ID3D11DeviceContext_FinishCommandList(context,FALSE,&commands);if(FAILED(result))goto cleanup;
    ID3D11DeviceContext *immediate=d3d8_GetD3D11Context();
    ID3D11DeviceContext_ExecuteCommandList(immediate,commands,TRUE);
    D3D11_MAPPED_SUBRESOURCE mapped;
    result=ID3D11DeviceContext_Map(immediate,(ID3D11Resource*)staging,0,D3D11_MAP_READ,0,&mapped);
    if(SUCCEEDED(result)) {memcpy(outputs,mapped.pData,desc.ByteWidth);ID3D11DeviceContext_Unmap(immediate,(ID3D11Resource*)staging,0);}
cleanup:
    if(commands)ID3D11CommandList_Release(commands);
    if(staging)ID3D11Buffer_Release(staging);if(output)ID3D11Buffer_Release(output);
    if(raster_cb)ID3D11Buffer_Release(raster_cb);
    if(cb)ID3D11Buffer_Release(cb);if(current_cb)ID3D11Buffer_Release(current_cb);
    for(unsigned i=0;i<16;++i)if(vb[i])ID3D11Buffer_Release(vb[i]);
    if(geometry)ID3D11GeometryShader_Release(geometry);if(context)ID3D11DeviceContext_Release(context);
    return result;
}
int32_t pilot_gfx_shader_probe(uint32_t handle,const struct PilotVertexProbeStream streams[16],unsigned count,
                              const float constants[192][4],float *outputs)
{return shader_probe(handle,streams,count,constants,outputs,0);}
/* Diagnostic stream output of the exact production raster_main bytecode.
 * Its observations become raster inputs only after independent shader bounds
 * pass. The shipping draw path never calls this function. */
int32_t pilot_gfx_shader_raster_probe(uint32_t handle,const struct PilotVertexProbeStream streams[16],unsigned count,
                                     const float constants[192][4],float *outputs)
{return shader_probe(handle,streams,count,constants,outputs,1);}
