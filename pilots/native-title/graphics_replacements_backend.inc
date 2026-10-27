/* Optional presentation-only BC2 replacements. Guest storage, resource
 * descriptors, UV normalization, readback and CPU writes remain native.
 * SHA256 binds an attachment to the actual changed texture bytes, not a
 * reused handle. Disk I/O and hashing occur only on a candidate CPU upload.
 * Both D3D donors use this same D3D11 interface and native fallback. */
#include <bcrypt.h>
#include <d3dcompiler.h>
struct PilotReplacementRecord {
    uint32_t format,width,height,bytes,hd_width,hd_height,hd_bytes;
    unsigned char prefix[16],source_sha[32],pixel_sha[32];
};
struct PilotReplacement {
    struct PilotReplacementRecord record;
    ID3D11ShaderResourceView *view,*base;
    uint64_t used;
    unsigned rejected;
    unsigned char *source;
    unsigned probe,source_checked;
};
static struct PilotReplacement *pilot_replacements;
static unsigned pilot_replacement_count,pilot_replacement_initialized;
static uint64_t pilot_replacement_bytes,pilot_replacement_clock;
static WCHAR pilot_replacement_folder[32768];
static ID3D11ComputeShader *pilot_replacement_upscale;
static ID3D11Buffer *pilot_replacement_constants;
static uint64_t pilot_replacement_atlas_bytes;
static void pilot_replacement_atlas_release(struct PilotNativeTexture *native);
static void pilot_replacement_atlas_update(uint32_t handle,const void *data,unsigned bytes);
static int pilot_replacement_hash(const void *data,unsigned bytes,unsigned char out[32])
{return BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(PUCHAR)data,bytes,out,32)>=0;}
static void pilot_replacement_release(unsigned index)
{
    struct PilotReplacement *p=&pilot_replacements[index];
    if(p->base)ID3D11ShaderResourceView_Release(p->base);
    if(p->view) {
        ID3D11ShaderResourceView_Release(p->view);
        pilot_replacement_bytes-=(uint64_t)p->record.hd_bytes*2;
    }
    p->view=p->base=NULL;pilot_sampling_views_dirty=15;
}
static void pilot_replacement_close(void)
{
    for(unsigned i=0;i<pilot_replacement_count;++i){pilot_replacement_release(i);free(pilot_replacements[i].source);}
    for(unsigned i=0;i<pilot_texture_capacity;++i)pilot_replacement_atlas_release(&pilot_textures[i]);
    if(pilot_replacement_upscale)ID3D11ComputeShader_Release(pilot_replacement_upscale);
    if(pilot_replacement_constants)ID3D11Buffer_Release(pilot_replacement_constants);
    pilot_replacement_upscale=NULL;pilot_replacement_constants=NULL;
    free(pilot_replacements);pilot_replacements=NULL;
    pilot_replacement_count=pilot_replacement_initialized=0;
    pilot_replacement_bytes=pilot_replacement_clock=0;pilot_replacement_folder[0]=0;
}
static void pilot_replacement_initialize(void)
{
    if(pilot_replacement_initialized)return;
    pilot_replacement_initialized=1;
    const char *path=getenv("XLLE_PILOT_TEXTURE_REPLACEMENTS");if(!path || !*path)return;
    WCHAR wide[32768];FILE *file=NULL;unsigned char header[12];
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,32768))goto rejected;
    file=_wfopen(wide,L"rb");if(!file)goto rejected;
    if(fread(header,1,12,file)!=12 || memcmp(header,"XLLEHD1\0",8))goto rejected;
    unsigned count;memcpy(&count,header+8,4);
    if(count>2048 || sizeof(struct PilotReplacementRecord)!=108)goto rejected;
    pilot_replacements=calloc(count?count:1,sizeof(*pilot_replacements));if(!pilot_replacements)goto rejected;
    for(unsigned i=0;i<count;++i) {
        struct PilotReplacementRecord *r=&pilot_replacements[i].record;
        if(fread(r,1,sizeof(*r),file)!=sizeof(*r) || (r->format!=0xe && r->format!=0x11 && r->format!=0x12) ||
           r->width<4 || r->height<4 || r->width>1024 || r->height>1024 ||
           (r->format==0xe && ((r->width&(r->width-1)) || (r->height&(r->height-1)))) ||
           (r->format==0x11 && r->width%32) || r->bytes!=r->width*r->height*(r->format==0x12?4:r->format==0x11?2:1) ||
           r->hd_width>4096 || r->hd_height>4096 ||
           (r->hd_width!=r->width*2 && r->hd_width!=r->width*4) ||
           r->hd_width*r->height!=r->hd_height*r->width ||
           r->hd_bytes!=r->hd_width*r->hd_height*4)goto rejected;
    }
    if(fgetc(file)!=EOF || ferror(file))goto rejected;
    fclose(file);file=NULL;
    WCHAR *last=NULL;
    for(WCHAR *p=wide;*p;++p)if(*p==L'/' || *p==L'\\')last=p;
    if(!last)goto rejected;
    last[1]=0;wcscpy(pilot_replacement_folder,wide);pilot_replacement_count=count;
    fprintf(stderr,"[HD TEXTURES] candidates=%u lazy-gpu-budget=256MiB guest-storage=unchanged\n",count);return;
rejected:
    if(file)fclose(file);
    free(pilot_replacements);pilot_replacements=NULL;
    fprintf(stderr,"[HD TEXTURES] invalid catalog; using native textures\n");
}
static void pilot_replacement_match(uint32_t handle,const void *data,unsigned bytes)
{
    struct PilotNativeTexture *native=&pilot_textures[handle-1];native->replacement=0;
    pilot_replacement_initialize();if(!pilot_replacement_count || native->rtv || native->dsv || bytes<16)return;
    D3D8Texture *texture=(D3D8Texture*)native->object;
    if(native->guest_format==0x12){pilot_replacement_atlas_update(handle,data,bytes);return;}
    unsigned char digest[32];unsigned hashed=0;
    for(unsigned i=0;i<pilot_replacement_count;++i) {
        struct PilotReplacementRecord *r=&pilot_replacements[i].record;
        if(r->format!=native->guest_format || r->width!=texture->width || r->height!=texture->height ||
           r->bytes!=bytes || memcmp(r->prefix,data,16))continue;
        if(!hashed){if(!pilot_replacement_hash(data,bytes,digest))return;hashed=1;}
        if(memcmp(digest,r->source_sha,32))continue;
        native->replacement=i+1;
        fprintf(stderr,"[HD TEXTURE MATCH] handle=%u native=%ux%u artwork=%ux%u\n",handle,r->width,r->height,r->hd_width,r->hd_height);
        return;
    }
}
static ID3D11ShaderResourceView *pilot_replacement_pool(unsigned index,unsigned base_only)
{
    struct PilotReplacement *p=&pilot_replacements[index];
    struct PilotReplacementRecord *r=&p->record;
    if(p->rejected)return NULL;
    p->used=++pilot_replacement_clock;
    if(!p->view) {
        const uint64_t charge=(uint64_t)r->hd_bytes*2;
        while(pilot_replacement_bytes+charge>256ull*1024*1024) {
            unsigned oldest=UINT_MAX;
            for(unsigned i=0;i<pilot_replacement_count;++i)
                if(i!=index && pilot_replacements[i].view &&
                   (oldest==UINT_MAX || pilot_replacements[i].used<pilot_replacements[oldest].used))oldest=i;
            if(oldest==UINT_MAX)return NULL;
            pilot_replacement_release(oldest);
        }
        size_t length=wcslen(pilot_replacement_folder);WCHAR path[32768];
        if(length+70>=32768){p->rejected=1;return NULL;}
        wcscpy(path,pilot_replacement_folder);
        const WCHAR *hex=L"0123456789abcdef";
        for(unsigned i=0;i<32;++i){path[length+i*2]=hex[r->pixel_sha[i]>>4];path[length+i*2+1]=hex[r->pixel_sha[i]&15];}
        wcscpy(path+length+64,L".rgba");
        FILE *file=_wfopen(path,L"rb");unsigned char *pixels=malloc(r->hd_bytes),digest[32];
        int valid=file && pixels && fread(pixels,1,r->hd_bytes,file)==r->hd_bytes && fgetc(file)==EOF && !ferror(file) &&
            pilot_replacement_hash(pixels,r->hd_bytes,digest) && !memcmp(digest,r->pixel_sha,32);
        if(file)fclose(file);
        if(!valid){free(pixels);p->rejected=1;fprintf(stderr,"[HD TEXTURES] damaged attachment; using native texture\n");return NULL;}
        D3D11_TEXTURE2D_DESC desc={0};desc.Width=r->hd_width;desc.Height=r->hd_height;
        desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ID3D11Texture2D *image=NULL;ID3D11Device *device=d3d8_GetD3D11Device();
        HRESULT result=ID3D11Device_CreateTexture2D(device,&desc,NULL,&image);
        if(SUCCEEDED(result))result=ID3D11Device_CreateShaderResourceView(device,(ID3D11Resource*)image,NULL,&p->view);
        if(SUCCEEDED(result)) {
            ID3D11DeviceContext *context=d3d8_GetD3D11Context();
            ID3D11DeviceContext_UpdateSubresource(context,(ID3D11Resource*)image,0,NULL,pixels,r->hd_width*4,0);
            ID3D11DeviceContext_GenerateMips(context,p->view);
            D3D11_SHADER_RESOURCE_VIEW_DESC view;ID3D11ShaderResourceView_GetDesc(p->view,&view);view.Texture2D.MipLevels=1;
            result=ID3D11Device_CreateShaderResourceView(device,(ID3D11Resource*)image,&view,&p->base);
        }
        if(image)ID3D11Texture2D_Release(image);free(pixels);
        if(FAILED(result)) {
            if(p->view)ID3D11ShaderResourceView_Release(p->view);
            p->view=NULL;p->rejected=1;return NULL;
        }
        pilot_replacement_bytes+=charge;
    }
    return base_only?p->base:p->view;
}
static ID3D11ShaderResourceView *pilot_replacement_view(uint32_t handle,unsigned base_only)
{
    struct PilotNativeTexture *native=&pilot_textures[handle-1];
    if(native->gpu_written)return NULL;
    if(native->replacement_atlas_view)
        return base_only?native->replacement_atlas_base:native->replacement_atlas_view;
    return native->replacement?pilot_replacement_pool(native->replacement-1,base_only):NULL;
}
#include "graphics_replacement_atlas_backend.inc"
