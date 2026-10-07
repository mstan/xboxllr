/* The MIT donor's quad conversion uses (0,1,2),(0,2,3). Keep that topology
 * in a reusable index buffer instead of copying every live vertex per draw. */
static ID3D11Buffer *pilot_quad_indices;
static unsigned pilot_quad_capacity;
static void pilot_quad_close(void)
{
    if(pilot_quad_indices)ID3D11Buffer_Release(pilot_quad_indices);
    pilot_quad_indices=NULL;pilot_quad_capacity=0;
}
static void pilot_quad_draw(unsigned first,unsigned count)
{
    unsigned quads=count/4;
    if(quads>pilot_quad_capacity) {
        unsigned capacity=pilot_quad_capacity?pilot_quad_capacity:64;
        while(capacity<quads) {
            if(capacity>UINT32_MAX/48)pilot_d3d_backend_stop("quad index capacity exceeded");
            capacity*=2;
        }
        uint32_t *indices=malloc((size_t)capacity*24);if(!indices)pilot_d3d_backend_stop("quad indices allocation failed");
        for(unsigned i=0;i<capacity;++i) {
            uint32_t *p=indices+i*6,v=i*4;p[0]=v;p[1]=v+1;p[2]=v+2;p[3]=v;p[4]=v+2;p[5]=v+3;
        }
        D3D11_BUFFER_DESC desc={0};desc.ByteWidth=capacity*24;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data={indices,0,0};ID3D11Buffer *buffer=NULL;
        HRESULT hr=ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(),&desc,&data,&buffer);free(indices);
        pilot_d3d_require(hr,__FILE__,__LINE__);pilot_quad_close();pilot_quad_indices=buffer;pilot_quad_capacity=capacity;
    }
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();ID3D11Buffer *previous=NULL;DXGI_FORMAT format;UINT offset;
    ID3D11DeviceContext_IAGetIndexBuffer(context,&previous,&format,&offset);
    ID3D11DeviceContext_IASetIndexBuffer(context,pilot_quad_indices,DXGI_FORMAT_R32_UINT,0);
    ID3D11DeviceContext_DrawIndexed(context,quads*6,0,(INT)first);
    ID3D11DeviceContext_IASetIndexBuffer(context,previous,format,offset);if(previous)ID3D11Buffer_Release(previous);
}
