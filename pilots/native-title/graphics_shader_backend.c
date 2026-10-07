#include "d3d8_internal.h"
#include "graphics_backend.h"
#include "pilot_d3d_checks.h"
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
#include "graphics_shader_source.inc"
struct NativeShader {
    ID3D11VertexShader *object;
    ID3D11InputLayout *layout;
    ID3DBlob *code;
    struct PilotVertexElement elements[16];
    unsigned used;
};
static struct NativeShader native_shaders[256];
static struct NativeShader *native_shader(uint32_t handle)
{
    if(!handle || handle>256 || !native_shaders[handle-1].object)pilot_d3d_backend_stop("unowned native vertex shader");
    return &native_shaders[handle-1];
}
int32_t pilot_gfx_shader_create(const uint32_t *code,unsigned count,const struct PilotVertexElement elements[16],uint32_t *handle)
{
    if(!code || !elements || !handle || !count || count>136 || !d3d8_GetD3D11Device())return E_INVALIDARG;
    unsigned slot;
    for(slot=0;slot<256 && native_shaders[slot].object;++slot){}
    if(slot==256)return E_OUTOFMEMORY;
    struct NativeShader shader={0};
    char *source=shader_source(code,count,elements,&shader.used);
    if(!source)return E_OUTOFMEMORY;
    ID3DBlob *errors=NULL;
    HRESULT result=D3DCompile(source,strlen(source),"xlle-nv2a",NULL,NULL,"main","vs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader.code,&errors);
    if(FAILED(result))fprintf(stderr,"[SHADER COMPILE] %s\n",errors?(const char*)ID3D10Blob_GetBufferPointer(errors):"no compiler diagnostic");
    const char *capture=getenv("XLLE_PILOT_SHADER_CAPTURE");
    if(capture && *capture) {
        static unsigned sequence;char path[1024];
        if(snprintf(path,sizeof(path),"%s/shader-%u.hlsl",capture,++sequence)>=(int)sizeof(path))pilot_d3d_backend_stop("shader capture path too long");
        FILE *file=fopen(path,"wb");
        if(!file || fwrite(source,strlen(source),1,file)!=1 || fclose(file))pilot_d3d_backend_stop("shader capture failed");
    }
    free(source);if(errors)ID3D10Blob_Release(errors);
    if(FAILED(result))return result;
    result=ID3D11Device_CreateVertexShader(d3d8_GetD3D11Device(),ID3D10Blob_GetBufferPointer(shader.code),ID3D10Blob_GetBufferSize(shader.code),NULL,&shader.object);
    D3D11_INPUT_ELEMENT_DESC layout[16]={0};unsigned n=0;
    for(unsigned i=0;i<16;++i)if(shader.used&(1u<<i)) {
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
        ID3D10Blob_Release(shader.code);return result;
    }
    memcpy(shader.elements,elements,sizeof(shader.elements));native_shaders[slot]=shader;*handle=slot+1;
    fprintf(stderr,"[SHADER CREATE] native=%u instructions=%u inputs=%04x bytecode=%zu\n",*handle,count,shader.used,ID3D10Blob_GetBufferSize(shader.code));
    return S_OK;
}
void pilot_gfx_shader_destroy(uint32_t handle)
{
    struct NativeShader *shader=native_shader(handle);
    if(shader->layout)ID3D11InputLayout_Release(shader->layout);
    ID3D11VertexShader_Release(shader->object);ID3D10Blob_Release(shader->code);memset(shader,0,sizeof(*shader));
}
void pilot_gfx_shaders_close(void)
{
    for(unsigned i=0;i<256;++i)if(native_shaders[i].object)pilot_gfx_shader_destroy(i+1);
}
/* Diagnostic only: execute the actual compiled vertex shader and read its
 * stream output, without rasterization or a duplicate production execution.
 * A deferred context preserves the caller's immediate graphics state. */
int32_t pilot_gfx_shader_probe(uint32_t handle,const struct PilotVertexProbeStream streams[16],unsigned count,
                              const float constants[192][4],float *outputs)
{
    struct NativeShader *shader=native_shader(handle);
    if(!streams || !outputs || !constants || !count || count>65536)return E_INVALIDARG;
    unsigned stream_mask=0;
    for(unsigned i=0;i<16;++i)if(shader->used&(1u<<i)) {
        unsigned stream=shader->elements[i].stream;stream_mask|=1u<<stream;
        if(!streams[stream].data || !streams[stream].stride || streams[stream].stride>2048 ||
            shader->elements[i].offset+shader_format(shader->elements[i].type)->bytes>streams[stream].stride)return E_INVALIDARG;
    }
    ID3D11Device *device=d3d8_GetD3D11Device();
    ID3D11DeviceContext *context=NULL;
    ID3D11GeometryShader *geometry=NULL;
    ID3D11Buffer *vb[16]={0},*cb=NULL,*output=NULL,*staging=NULL;
    ID3D11CommandList *commands=NULL;
    HRESULT result=ID3D11Device_CreateDeferredContext(device,0,&context);
    if(FAILED(result))goto cleanup;
    D3D11_SO_DECLARATION_ENTRY entries[17]={0};unsigned n=0;
    for(unsigned i=0;i<13;++i) {
        unsigned width=shader_output_widths[i];
        if(width) {
            entries[n].SemanticName=shader_semantics[i];entries[n].SemanticIndex=shader_semantic_indices[i];
            entries[n++].ComponentCount=(BYTE)width;
        }
        if(width<4)entries[n++].ComponentCount=(BYTE)(4-width);
    }
    UINT output_stride=13*16;
    result=ID3D11Device_CreateGeometryShaderWithStreamOutput(device,ID3D10Blob_GetBufferPointer(shader->code),
        ID3D10Blob_GetBufferSize(shader->code),entries,n,&output_stride,1,D3D11_SO_NO_RASTERIZED_STREAM,NULL,&geometry);
    if(FAILED(result))goto cleanup;
    D3D11_BUFFER_DESC desc={0};D3D11_SUBRESOURCE_DATA initial={0};
    UINT zero=0;
    desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    for(unsigned i=0;i<16;++i)if(stream_mask&(1u<<i)) {
        desc.ByteWidth=streams[i].stride*count;initial.pSysMem=streams[i].data;
        result=ID3D11Device_CreateBuffer(device,&desc,&initial,&vb[i]);if(FAILED(result))goto cleanup;
        ID3D11DeviceContext_IASetVertexBuffers(context,i,1,&vb[i],&streams[i].stride,&zero);
    }
    desc.ByteWidth=192*16;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;initial.pSysMem=constants;
    result=ID3D11Device_CreateBuffer(device,&desc,&initial,&cb);if(FAILED(result))goto cleanup;
    desc.ByteWidth=count*output_stride;desc.BindFlags=D3D11_BIND_STREAM_OUTPUT;desc.Usage=D3D11_USAGE_DEFAULT;
    result=ID3D11Device_CreateBuffer(device,&desc,NULL,&output);if(FAILED(result))goto cleanup;
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    result=ID3D11Device_CreateBuffer(device,&desc,NULL,&staging);if(FAILED(result))goto cleanup;
    ID3D11DeviceContext_IASetInputLayout(context,shader->layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(context,D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ID3D11DeviceContext_VSSetShader(context,shader->object,NULL,0);
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
    if(cb)ID3D11Buffer_Release(cb);for(unsigned i=0;i<16;++i)if(vb[i])ID3D11Buffer_Release(vb[i]);
    if(geometry)ID3D11GeometryShader_Release(geometry);if(context)ID3D11DeviceContext_Release(context);
    return result;
}
