#ifdef PILOT_D3D_PHOBOS
#include <d3dcompiler.h>
/* Submit through the donor's real prepared-draw hook with the state classes
 * declined by dual-render capture. The primary path must keep all of them. */
static int test_hd_primary_state(void)
{
    struct PilotVisualMods options;pilot_visual_mods_default(&options);options.supersample=3;
    CHECK(pilot_gfx_visual_mods(&options));
    uint32_t pp[17]={64,64,D3DFMT_X8R8G8B8,1,0x11,1,0,0,1,D3DFMT_D24S8,0,60,1};
    CHECK(!pilot_gfx_create(pp));
    uint32_t vp[]={0,0,64,64,0,0x3f800000};CHECK(!pilot_gfx_viewport(vp));
    pilot_gfx_pixel_fixed();pilot_gfx_set_state(D3DRS_ZENABLE,0);
    pilot_gfx_set_state(D3DRS_STENCILENABLE,0);pilot_gfx_set_state(D3DRS_CULLMODE,1);
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    CHECK(!pilot_gfx_clear(3,0xff000000,1,0));CHECK(!visibility_quad(0,0,20,20,.2f));
    ID3D11Device *device=d3d8_GetD3D11Device();ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();
    ID3D11RenderTargetView *views[2]={0};ID3D11DepthStencilView *depth=NULL;
    ID3D11DeviceContext_OMGetRenderTargets(ctx,1,views,&depth);
    ID3D11Resource *resource=NULL;ID3D11RenderTargetView_GetResource(views[0],&resource);
    D3D11_TEXTURE2D_DESC desc;ID3D11Texture2D_GetDesc((ID3D11Texture2D*)resource,&desc);
    ID3D11Resource_Release(resource);ID3D11Texture2D *second=NULL,*staging=NULL;
    CHECK(!ID3D11Device_CreateTexture2D(device,&desc,NULL,&second));
    CHECK(!ID3D11Device_CreateRenderTargetView(device,(ID3D11Resource*)second,NULL,&views[1]));
    float black[4]={0};ID3D11DeviceContext_ClearRenderTargetView(ctx,views[1],black);
    const char code[]="Buffer<float4> source:register(t0);struct Out{float4 a:SV_Target0;float4 b:SV_Target1;};Out main(){Out o;o.a=source[0];o.b=float4(0,1,0,1);return o;}";
    ID3DBlob *blob=NULL,*errors=NULL;ID3D11PixelShader *shader=NULL,*saved_shader=NULL;
    HRESULT hr=D3DCompile(code,sizeof(code)-1,NULL,NULL,NULL,"main","ps_4_0",0,0,&blob,&errors);
    if(errors)ID3D10Blob_Release(errors);CHECK(!hr);
    CHECK(!ID3D11Device_CreatePixelShader(device,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),NULL,&shader));
    ID3D10Blob_Release(blob);
    float magenta[]={1,0,1,1};D3D11_BUFFER_DESC bd={0};bd.ByteWidth=sizeof(magenta);bd.Usage=D3D11_USAGE_IMMUTABLE;bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial={magenta,0,0};ID3D11Buffer *buffer=NULL;
    CHECK(!ID3D11Device_CreateBuffer(device,&bd,&initial,&buffer));
    D3D11_SHADER_RESOURCE_VIEW_DESC sd={0};sd.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;sd.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;sd.Buffer.NumElements=1;
    ID3D11ShaderResourceView *srv=NULL,*saved_srv=NULL;
    CHECK(!ID3D11Device_CreateShaderResourceView(device,(ID3D11Resource*)buffer,&sd,&srv));
    ID3D11DeviceContext_PSGetShader(ctx,&saved_shader,NULL,NULL);ID3D11DeviceContext_PSGetShaderResources(ctx,0,1,&saved_srv);
    ID3D11DeviceContext_OMSetRenderTargets(ctx,2,views,NULL);ID3D11DeviceContext_PSSetShader(ctx,shader,NULL,0);ID3D11DeviceContext_PSSetShaderResources(ctx,0,1,&srv);
    D3D11_QUERY_DESC qd={D3D11_QUERY_OCCLUSION_PREDICATE,0};ID3D11Predicate *predicate=NULL;
    CHECK(!ID3D11Device_CreatePredicate(device,&qd,&predicate));
    void pilot_native_draw_prepared(unsigned,unsigned);
    ID3D11DeviceContext_Begin(ctx,(ID3D11Asynchronous*)predicate);pilot_native_draw_prepared(6,0);ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous*)predicate);
    ID3D11DeviceContext_Flush(ctx);BOOL visible=FALSE;DWORD start=GetTickCount();
    do{hr=ID3D11DeviceContext_GetData(ctx,(ID3D11Asynchronous*)predicate,&visible,sizeof(visible),D3D11_ASYNC_GETDATA_DONOTFLUSH);if(hr==S_FALSE)Sleep(1);}while(hr==S_FALSE && GetTickCount()-start<2000);
    CHECK(hr==S_OK && visible);
    for(unsigned value=0;value<2;++value) {
        CHECK(!pilot_gfx_visibility_begin());ID3D11DeviceContext_SetPredication(ctx,predicate,value);
        pilot_native_draw_prepared(6,0);ID3D11DeviceContext_SetPredication(ctx,NULL,FALSE);
        CHECK(!pilot_gfx_visibility_end(90+value));CHECK(!visibility_expect(90+value,value?0:400,NULL));
    }
    ID3D11DeviceContext_OMSetRenderTargets(ctx,1,views,depth);ID3D11DeviceContext_PSSetShader(ctx,saved_shader,NULL,0);ID3D11DeviceContext_PSSetShaderResources(ctx,0,1,&saved_srv);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    CHECK(!ID3D11Device_CreateTexture2D(device,&desc,NULL,&staging));ID3D11DeviceContext_CopyResource(ctx,(ID3D11Resource*)staging,(ID3D11Resource*)second);
    D3D11_MAPPED_SUBRESOURCE mapped;CHECK(!ID3D11DeviceContext_Map(ctx,(ID3D11Resource*)staging,0,D3D11_MAP_READ,0,&mapped));
    unsigned char *pixel=(unsigned char*)mapped.pData+30*mapped.RowPitch+30*4;CHECK(!pixel[0] && pixel[1]==255 && !pixel[2]);ID3D11DeviceContext_Unmap(ctx,(ID3D11Resource*)staging,0);
    unsigned char pixels[64*64*4];CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));pixel=pixels+(10*64+10)*4;CHECK(pixel[0]==255 && !pixel[1] && pixel[2]==255);
    ID3D11Texture2D_Release(staging);ID3D11Predicate_Release(predicate);ID3D11ShaderResourceView_Release(srv);if(saved_srv)ID3D11ShaderResourceView_Release(saved_srv);
    ID3D11Buffer_Release(buffer);ID3D11PixelShader_Release(shader);if(saved_shader)ID3D11PixelShader_Release(saved_shader);
    ID3D11RenderTargetView_Release(views[0]);ID3D11RenderTargetView_Release(views[1]);ID3D11Texture2D_Release(second);if(depth)ID3D11DepthStencilView_Release(depth);
    CHECK(!test_feedback());
    pilot_gfx_close();fprintf(stderr,"PASS: HD-primary prepared draws preserve MRT, buffer SRV, predication and feedback at3x\n");return 0;
}
#endif
