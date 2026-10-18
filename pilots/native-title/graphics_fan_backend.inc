/* Xbox FIRST provoking convention selects the leading rim vertex of each
 * fan triangle, not the shared anchor. Cyclic order (i+1,i+2,0) preserves
 * winding and lets the existing flat-color geometry shader broadcast it.
 * Keep topology in immutable GPU indices; live vertices are never expanded. */
static ID3D11Buffer *pilot_fan_indices;
static unsigned pilot_fan_capacity;
static void pilot_fan_close(void)
{
    if(pilot_fan_indices)ID3D11Buffer_Release(pilot_fan_indices);
    pilot_fan_indices=NULL;pilot_fan_capacity=0;
}
static void pilot_fan_draw(unsigned first,unsigned count)
{
    unsigned triangles=count-2;
    if(triangles>pilot_fan_capacity) {
        unsigned capacity=pilot_fan_capacity?pilot_fan_capacity:64;
        while(capacity<triangles) {
            if(capacity>UINT32_MAX/24){capacity=triangles;break;}
            capacity*=2;
        }
        uint32_t *indices=malloc((size_t)capacity*12);
        if(!indices)pilot_d3d_backend_stop("fan indices allocation failed");
        for(unsigned i=0;i<capacity;++i) {
            indices[i*3]=i+1;indices[i*3+1]=i+2;indices[i*3+2]=0;
        }
        D3D11_BUFFER_DESC desc={0};desc.ByteWidth=capacity*12;
        desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data={indices,0,0};ID3D11Buffer *buffer=NULL;
        HRESULT hr=ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(),&desc,&data,&buffer);
        free(indices);pilot_d3d_require(hr,__FILE__,__LINE__);
        pilot_fan_close();pilot_fan_indices=buffer;pilot_fan_capacity=capacity;
    }
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11Buffer *previous=NULL;DXGI_FORMAT format;UINT offset;
    ID3D11DeviceContext_IAGetIndexBuffer(context,&previous,&format,&offset);
    ID3D11DeviceContext_IASetIndexBuffer(context,pilot_fan_indices,DXGI_FORMAT_R32_UINT,0);
    ID3D11DeviceContext_DrawIndexed(context,triangles*3,0,(INT)first);
    ID3D11DeviceContext_IASetIndexBuffer(context,previous,format,offset);
    if(previous)ID3D11Buffer_Release(previous);
}
