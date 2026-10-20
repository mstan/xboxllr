/* Reusable GPU index stream. Widening words avoids D3D11's implicit R16 strip
 * restart at 0xffff, which is a valid Xbox vertex index. Fan/quad conversion
 * changes index topology only; live vertices remain in their native buffers. */
static ID3D11Buffer *pilot_index_buffer;
static uint32_t *pilot_index_shadow,*pilot_index_work;
static unsigned pilot_index_capacity,pilot_index_words;
static void pilot_index_close(void)
{
    if(pilot_index_buffer)ID3D11Buffer_Release(pilot_index_buffer);
    free(pilot_index_shadow);free(pilot_index_work);
    pilot_index_buffer=NULL;pilot_index_shadow=pilot_index_work=NULL;
    pilot_index_capacity=pilot_index_words=0;
}
static uint32_t pilot_index_word(const unsigned char *data,unsigned index)
{return (uint32_t)data[index*2]|((uint32_t)data[index*2+1]<<8);}
static int32_t pilot_index_prepare(unsigned primitive,const void *data,unsigned count,int flat_quad,unsigned *words)
{
    if(!data || !count)return E_INVALIDARG;
    uint64_t needed=primitive==7?(uint64_t)(count-2)*3:
        primitive==8 && !flat_quad?(uint64_t)count/4*6:count;
    if(needed>64u*1024*1024/4)return E_NOTIMPL;
    if(needed>pilot_index_capacity) {
        unsigned capacity=pilot_index_capacity?pilot_index_capacity:256;
        while(capacity<needed)capacity*=2;
        D3D11_BUFFER_DESC desc={0};desc.ByteWidth=capacity*4;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_INDEX_BUFFER;
        ID3D11Buffer *buffer=NULL;HRESULT result=ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(),&desc,NULL,&buffer);
        if(FAILED(result))return result;
        uint32_t *shadow=malloc((size_t)capacity*4),*work=malloc((size_t)capacity*4);
        if(!shadow || !work){free(shadow);free(work);ID3D11Buffer_Release(buffer);return E_OUTOFMEMORY;}
        pilot_index_close();pilot_index_buffer=buffer;pilot_index_shadow=shadow;pilot_index_work=work;pilot_index_capacity=capacity;
    }
    const unsigned char *source=data;unsigned at=0;
    if(primitive==7)for(unsigned i=0;i<count-2;++i) {
        pilot_index_work[at++]=pilot_index_word(source,i+1);pilot_index_work[at++]=pilot_index_word(source,i+2);
        pilot_index_work[at++]=pilot_index_word(source,0);
    } else if(primitive==8 && !flat_quad)for(unsigned i=0;i<count;i+=4) {
        static const unsigned offsets[]={0,1,2,0,2,3};
        for(unsigned j=0;j<6;++j)pilot_index_work[at++]=pilot_index_word(source,i+offsets[j]);
    } else for(unsigned i=0;i<count;++i)pilot_index_work[at++]=pilot_index_word(source,i);
    if(at!=needed)pilot_d3d_backend_stop("index topology extent disagreement");
    if(at!=pilot_index_words || memcmp(pilot_index_shadow,pilot_index_work,(size_t)at*4)) {
        D3D11_BOX box={0,0,0,at*4,1,1};
        ID3D11DeviceContext_UpdateSubresource(d3d8_GetD3D11Context(),(ID3D11Resource*)pilot_index_buffer,0,&box,pilot_index_work,0,0);
        memcpy(pilot_index_shadow,pilot_index_work,(size_t)at*4);pilot_index_words=at;
    }
    *words=at;return S_OK;
}
