/* Some games pack editable images into a shared BGRA sprite atlas. Match the
 * complete native rectangle, then upscale the atlas on the GPU and overlay
 * only that rectangle. Other sprites retain their native colors/UVs. This
 * avoids a 64 MiB CPU upscale on each changed 1024x1024 atlas upload. */
static void pilot_replacement_atlas_release(struct PilotNativeTexture *n)
{
    if(n->replacement_atlas) {
        D3D11_TEXTURE2D_DESC d;ID3D11Texture2D_GetDesc(n->replacement_atlas,&d);
        pilot_replacement_atlas_bytes-=(uint64_t)d.Width*d.Height*8;
        ID3D11Texture2D_Release(n->replacement_atlas);
    }
    if(n->replacement_atlas_view)ID3D11ShaderResourceView_Release(n->replacement_atlas_view);
    if(n->replacement_atlas_base)ID3D11ShaderResourceView_Release(n->replacement_atlas_base);
    if(n->replacement_atlas_uav)ID3D11UnorderedAccessView_Release(n->replacement_atlas_uav);
    n->replacement_atlas=NULL;n->replacement_atlas_view=n->replacement_atlas_base=NULL;n->replacement_atlas_uav=NULL;
    n->replacement_atlas_scale=0;pilot_sampling_views_dirty=15;
}
static const unsigned char *pilot_replacement_source(struct PilotReplacement *p)
{
    if(p->source_checked)return p->source;
    p->source_checked=1;struct PilotReplacementRecord *r=&p->record;
    WCHAR path[32768];size_t length=wcslen(pilot_replacement_folder);
    if(length+72>=32768)return NULL;
    wcscpy(path,pilot_replacement_folder);const WCHAR *hex=L"0123456789abcdef";
    for(unsigned i=0;i<32;++i){path[length+i*2]=hex[r->source_sha[i]>>4];path[length+i*2+1]=hex[r->source_sha[i]&15];}
    wcscpy(path+length+64,L".native");FILE *f=_wfopen(path,L"rb");
    unsigned char *data=malloc(r->bytes),digest[32];
    int valid=f && data && fread(data,1,r->bytes,f)==r->bytes && fgetc(f)==EOF && !ferror(f) &&
        pilot_replacement_hash(data,r->bytes,digest) && !memcmp(digest,r->source_sha,32);
    if(f)fclose(f);if(!valid){free(data);return NULL;}
    /* Pick an informative, pixel-aligned 16-byte probe, avoiding a transparent
     * border as the search key. Full row comparisons still prove every byte. */
    unsigned best=0;
    for(unsigned at=0;at+16<=r->bytes;at+=16) {
        unsigned score=0;
        for(unsigned i=0;i<16;++i){unsigned duplicate=0;for(unsigned j=0;j<i;++j)duplicate|=data[at+i]==data[at+j];score+=!duplicate;}
        if(score>best){best=score;p->probe=at;if(best==16)break;}
    }
    p->source=data;return data;
}
static int pilot_replacement_rect(struct PilotReplacement *p,const unsigned char *data,unsigned width,unsigned height,unsigned *x,unsigned *y)
{
    struct PilotReplacementRecord *r=&p->record;
    if(r->width>width || r->height>height)return 0;
    const unsigned char *source=pilot_replacement_source(p);if(!source)return 0;
    unsigned pitch=width*4,rpitch=r->width*4,sy=p->probe/rpitch,sx=(p->probe%rpitch)/4;
    const unsigned char *probe=source+p->probe;unsigned found=0,checks=0;
    for(unsigned row=0;row<=height-r->height;++row) {
        const unsigned char *first=data+(row+sy)*pitch+sx*4,*end=first+(width-r->width)*4+1;
        while(first<end) {
            const unsigned char *hit=memchr(first,probe[0],(size_t)(end-first));if(!hit)break;
            first=hit+1;size_t offset=(size_t)(hit-data);
            if(offset%4 || memcmp(hit,probe,16))continue;
            if(++checks>4096)return 0; /* Ambiguous uniform regions use native. */
            unsigned col=(unsigned)(offset%pitch)/4-sx,match=1;
            for(unsigned j=0;j<r->height && match;++j)
                match=!memcmp(data+(row+j)*pitch+col*4,source+j*rpitch,rpitch);
            if(!match)continue;
            if(found++)return 0; /* Cannot distinguish two identical rectangles. */
            *x=col;*y=row;
        }
    }
    return found==1;
}
static int pilot_replacement_upscale_open(void)
{
    if(pilot_replacement_upscale)return 1;
    static const char code[]=
        "Texture2D<float4> image:register(t0);RWTexture2D<float4> target:register(u0);"
        "cbuffer Params:register(b0){uint scale,width,height,dx;uint dy,pad0,pad1,pad2;}"
        "[numthreads(8,8,1)]void main(uint3 p:SV_DispatchThreadID){"
        "if(p.x<width&&p.y<height)target[p.xy+uint2(dx,dy)]=image.Load(int3(p.xy/scale,0));}";
    ID3DBlob *blob=NULL,*errors=NULL;
    HRESULT hr=D3DCompile(code,sizeof(code)-1,NULL,NULL,NULL,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors);
    if(errors)ID3D10Blob_Release(errors);
    if(FAILED(hr))return 0;
    hr=ID3D11Device_CreateComputeShader(d3d8_GetD3D11Device(),ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),NULL,&pilot_replacement_upscale);
    ID3D10Blob_Release(blob);if(FAILED(hr))return 0;
    D3D11_BUFFER_DESC desc={0};desc.ByteWidth=32;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    hr=ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(),&desc,NULL,&pilot_replacement_constants);
    if(FAILED(hr)){ID3D11ComputeShader_Release(pilot_replacement_upscale);pilot_replacement_upscale=NULL;return 0;}
    return 1;
}
static void pilot_replacement_dispatch(ID3D11DeviceContext *ctx,ID3D11ShaderResourceView *source,unsigned scale,unsigned w,unsigned h,unsigned x,unsigned y)
{
    unsigned params[8]={scale,w,h,x,y,0,0,0};
    ID3D11DeviceContext_UpdateSubresource(ctx,(ID3D11Resource*)pilot_replacement_constants,0,NULL,params,0,0);
    ID3D11DeviceContext_CSSetShaderResources(ctx,0,1,&source);
    ID3D11DeviceContext_Dispatch(ctx,(w+7)/8,(h+7)/8,1);
}
static void pilot_replacement_atlas_update(uint32_t handle,const void *data,unsigned bytes)
{
    struct PilotNativeTexture *n=&pilot_textures[handle-1];D3D8Texture *t=(D3D8Texture*)n->object;
    if(t->width>1024 || t->height>1024 || t->width%16 || bytes!=t->width*t->height*4){pilot_replacement_atlas_release(n);return;}
    struct {unsigned index,x,y;} patches[64];unsigned count=0,scale=0;
    for(unsigned i=0;i<pilot_replacement_count && count<64;++i) {
        struct PilotReplacement *p=&pilot_replacements[i];unsigned x,y;
        if(p->record.format!=0x12 || !pilot_replacement_rect(p,data,t->width,t->height,&x,&y))continue;
        patches[count].index=i;patches[count].x=x;patches[count++].y=y;
        unsigned s=p->record.hd_width/p->record.width;if(s>scale)scale=s;
    }
    if(!count || !pilot_replacement_upscale_open()){pilot_replacement_atlas_release(n);return;}
    if(n->replacement_atlas_scale!=scale)pilot_replacement_atlas_release(n);
    if(!n->replacement_atlas) {
        uint64_t charge=(uint64_t)t->width*t->height*scale*scale*8;
        if(pilot_replacement_atlas_bytes+charge>256ull*1024*1024)return;
        D3D11_TEXTURE2D_DESC desc={0};desc.Width=t->width*scale;desc.Height=t->height*scale;
        desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ID3D11Device *device=d3d8_GetD3D11Device();
        HRESULT hr=ID3D11Device_CreateTexture2D(device,&desc,NULL,&n->replacement_atlas);
        if(SUCCEEDED(hr)){pilot_replacement_atlas_bytes+=charge;n->replacement_atlas_scale=scale;}
        if(SUCCEEDED(hr))hr=ID3D11Device_CreateShaderResourceView(device,(ID3D11Resource*)n->replacement_atlas,NULL,&n->replacement_atlas_view);
        if(SUCCEEDED(hr))hr=ID3D11Device_CreateUnorderedAccessView(device,(ID3D11Resource*)n->replacement_atlas,NULL,&n->replacement_atlas_uav);
        if(SUCCEEDED(hr)) {
            D3D11_SHADER_RESOURCE_VIEW_DESC view;ID3D11ShaderResourceView_GetDesc(n->replacement_atlas_view,&view);view.Texture2D.MipLevels=1;
            hr=ID3D11Device_CreateShaderResourceView(device,(ID3D11Resource*)n->replacement_atlas,&view,&n->replacement_atlas_base);
        }
        if(FAILED(hr)){pilot_replacement_atlas_release(n);return;}
    }
    ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();ID3D11ComputeShader *old_shader=NULL;
    ID3D11Buffer *old_constants=NULL;ID3D11UnorderedAccessView *old_uav=NULL;
    ID3D11ShaderResourceView *old_source=NULL,*old_pixel[4]={0},*empty[4]={0};
    ID3D11DeviceContext_CSGetShader(ctx,&old_shader,NULL,NULL);
    ID3D11DeviceContext_CSGetConstantBuffers(ctx,0,1,&old_constants);
    ID3D11DeviceContext_CSGetShaderResources(ctx,0,1,&old_source);
    ID3D11DeviceContext_CSGetUnorderedAccessViews(ctx,0,1,&old_uav);
    ID3D11DeviceContext_PSGetShaderResources(ctx,0,4,old_pixel);
    ID3D11DeviceContext_PSSetShaderResources(ctx,0,4,empty);
    ID3D11DeviceContext_CSSetShader(ctx,pilot_replacement_upscale,NULL,0);
    ID3D11DeviceContext_CSSetConstantBuffers(ctx,0,1,&pilot_replacement_constants);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx,0,1,&n->replacement_atlas_uav,NULL);
#ifdef PILOT_D3D_PHOBOS
    ID3D11ShaderResourceView *native=(ID3D11ShaderResourceView*)rhi_d3d11_native_view(t->srv);
#else
    ID3D11ShaderResourceView *native=t->srv;
#endif
    pilot_replacement_dispatch(ctx,native,scale,t->width*scale,t->height*scale,0,0);
    for(unsigned i=0;i<count;++i) {
        struct PilotReplacementRecord *r=&pilot_replacements[patches[i].index].record;
        ID3D11ShaderResourceView *view=pilot_replacement_pool(patches[i].index,0);if(!view)continue;
        unsigned density=r->hd_width/r->width;
        pilot_replacement_dispatch(ctx,view,scale/density,r->width*scale,r->height*scale,patches[i].x*scale,patches[i].y*scale);
    }
    ID3D11ShaderResourceView *null_source=NULL;ID3D11UnorderedAccessView *null_uav=NULL;
    ID3D11DeviceContext_CSSetShaderResources(ctx,0,1,&null_source);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx,0,1,&null_uav,NULL);
    ID3D11DeviceContext_GenerateMips(ctx,n->replacement_atlas_view);
    ID3D11DeviceContext_CSSetShader(ctx,old_shader,NULL,0);
    ID3D11DeviceContext_CSSetConstantBuffers(ctx,0,1,&old_constants);
    ID3D11DeviceContext_CSSetShaderResources(ctx,0,1,&old_source);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx,0,1,&old_uav,NULL);
    ID3D11DeviceContext_PSSetShaderResources(ctx,0,4,old_pixel);
    if(old_shader)ID3D11ComputeShader_Release(old_shader);
    if(old_constants)ID3D11Buffer_Release(old_constants);
    if(old_source)ID3D11ShaderResourceView_Release(old_source);
    if(old_uav)ID3D11UnorderedAccessView_Release(old_uav);
    for(unsigned i=0;i<4;++i)if(old_pixel[i])ID3D11ShaderResourceView_Release(old_pixel[i]);
    pilot_sampling_views_dirty=15;
    fprintf(stderr,"[HD ATLAS] handle=%u native=%ux%u detail=%ux patches=%u\n",handle,t->width,t->height,scale,count);
}
