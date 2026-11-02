/* Keep the original donor image canonical. Only its optional enhanced
 * companion is allocated here; failure never changes the source. The owning
 * graphics lock serializes the registry and references. */
#ifdef PILOT_D3D_PHOBOS
#include <dxgi1_4.h>
struct PilotNativeViewCache {
    D3D11_SHADER_RESOURCE_VIEW_DESC desc;
    ID3D11ShaderResourceView *view;
    struct PilotNativeViewCache *next;
};
struct PilotNativeMap {
    ID3D11Resource *source;
    struct PilotNativePlane *plane;
    unsigned refs;
    /* Facts belong to this registration generation, not to a raw pointer.
     * Alias refs share it; final unmap clears it even if GPU jobs retain plane. */
    struct PilotNativeAdmissionView *admission;
    unsigned admission_count;
    struct PilotNativeMap *next;
};
static struct PilotNativeMap *pilot_native_maps;
static uint64_t pilot_native_admitted_bytes;
enum { PILOT_NATIVE_ADMISSION_RTV=1,PILOT_NATIVE_ADMISSION_DSV=2,PILOT_NATIVE_ADMISSION_SRV=3,
       PILOT_NATIVE_ADMISSION_VIEWS=256,PILOT_NATIVE_ADMISSION_PER_MAP=16,PILOT_NATIVE_ADMISSION_STATES=32 };
struct PilotNativeAdmissionView {
    ID3D11View *original,*paired;
    struct PilotNativeMap *owner;
    unsigned kind,compatible;
    union {D3D11_RENDER_TARGET_VIEW_DESC rtv;D3D11_DEPTH_STENCIL_VIEW_DESC dsv;D3D11_SHADER_RESOURCE_VIEW_DESC srv;} desc;
    D3D11_TEXTURE2D_DESC shape;HRESULT shape_result;
    ID3D11ShaderResourceView *enhanced;
    struct PilotNativeAdmissionView *next;
};
/* Index refs are weak; the map owns each entry and clears its exact index
 * pointer before destruction. Owned original views prevent pointer ABA. */
static struct PilotNativeAdmissionView *pilot_native_admission_index[PILOT_NATIVE_ADMISSION_VIEWS];
static unsigned pilot_native_admission_count;
static struct {ID3D11DepthStencilState *state;D3D11_DEPTH_STENCIL_DESC desc;} pilot_native_admission_depth[PILOT_NATIVE_ADMISSION_STATES];
static struct {ID3D11BlendState *state;D3D11_BLEND_DESC desc;} pilot_native_admission_blend[PILOT_NATIVE_ADMISSION_STATES];
static unsigned pilot_native_admission_depth_next,pilot_native_admission_blend_next;
static int pilot_native_admission_enabled(void)
{
    static int enabled=-1;
    if(enabled<0){const char *value=getenv("XLLE_PILOT_NATIVE_ADMISSION_CACHE");enabled=value && !strcmp(value,"1");}
    return enabled;
}
static unsigned pilot_native_admission_slot(ID3D11View *view,unsigned kind)
{
    uintptr_t key=(uintptr_t)view;return (unsigned)((key>>4)^(key>>12)^(kind*17u))&(PILOT_NATIVE_ADMISSION_VIEWS-1);
}
static void pilot_native_admission_clear(struct PilotNativeMap *map)
{
    while(map->admission) {
        struct PilotNativeAdmissionView *view=map->admission;map->admission=view->next;
        unsigned slot=pilot_native_admission_slot(view->original,view->kind);
        if(pilot_native_admission_index[slot]==view)pilot_native_admission_index[slot]=NULL;
        prepared_state_forget_view(view->original);
        if(view->paired)prepared_state_forget_view(view->paired);
        if(view->enhanced)prepared_state_forget_view((ID3D11View*)view->enhanced);
        if(view->enhanced)ID3D11ShaderResourceView_Release(view->enhanced);
        if(view->paired)ID3D11View_Release(view->paired);
        ID3D11View_Release(view->original);free(view);--pilot_native_admission_count;
    }
    map->admission_count=0;
    if(!pilot_native_maps) {
        for(unsigned i=0;i<PILOT_NATIVE_ADMISSION_STATES;++i) {
            if(pilot_native_admission_depth[i].state)ID3D11DepthStencilState_Release(pilot_native_admission_depth[i].state);
            if(pilot_native_admission_blend[i].state)ID3D11BlendState_Release(pilot_native_admission_blend[i].state);
        }
        memset(pilot_native_admission_depth,0,sizeof(pilot_native_admission_depth));
        memset(pilot_native_admission_blend,0,sizeof(pilot_native_admission_blend));
        pilot_native_admission_depth_next=pilot_native_admission_blend_next=0;
    }
}
static void pilot_native_admission_depth_desc(ID3D11DepthStencilState *state,D3D11_DEPTH_STENCIL_DESC *desc)
{
    if(!pilot_native_admission_enabled() || !pilot_native_maps){ID3D11DepthStencilState_GetDesc(state,desc);return;}
    for(unsigned i=0;i<PILOT_NATIVE_ADMISSION_STATES;++i)if(pilot_native_admission_depth[i].state==state){*desc=pilot_native_admission_depth[i].desc;return;}
    ID3D11DepthStencilState_GetDesc(state,desc);
    unsigned slot=pilot_native_admission_depth_next++%PILOT_NATIVE_ADMISSION_STATES;
    if(pilot_native_admission_depth[slot].state)ID3D11DepthStencilState_Release(pilot_native_admission_depth[slot].state);
    ID3D11DepthStencilState_AddRef(state);pilot_native_admission_depth[slot].state=state;pilot_native_admission_depth[slot].desc=*desc;
}
static void pilot_native_admission_blend_desc(ID3D11BlendState *state,D3D11_BLEND_DESC *desc)
{
    if(!pilot_native_admission_enabled() || !pilot_native_maps){ID3D11BlendState_GetDesc(state,desc);return;}
    for(unsigned i=0;i<PILOT_NATIVE_ADMISSION_STATES;++i)if(pilot_native_admission_blend[i].state==state){*desc=pilot_native_admission_blend[i].desc;return;}
    ID3D11BlendState_GetDesc(state,desc);
    unsigned slot=pilot_native_admission_blend_next++%PILOT_NATIVE_ADMISSION_STATES;
    if(pilot_native_admission_blend[slot].state)ID3D11BlendState_Release(pilot_native_admission_blend[slot].state);
    ID3D11BlendState_AddRef(state);pilot_native_admission_blend[slot].state=state;pilot_native_admission_blend[slot].desc=*desc;
}

struct PilotNativeBudget {
    uint64_t budget,usage,headroom,estimate;
    unsigned limit;
    const char *source;
};
static int pilot_native_budget(ID3D11Device *device,struct PilotNativeBudget *budget)
{
    IDXGIDevice *dxgi=NULL;IDXGIAdapter *adapter=NULL;IDXGIAdapter3 *modern=NULL;
    int available=0;
    if(FAILED(ID3D11Device_QueryInterface(device,&IID_IDXGIDevice,(void**)&dxgi)))goto done;
    if(FAILED(IDXGIDevice_GetAdapter(dxgi,&adapter)))goto done;
    if(SUCCEEDED(IDXGIAdapter_QueryInterface(adapter,&IID_IDXGIAdapter3,(void**)&modern))) {
        DXGI_QUERY_VIDEO_MEMORY_INFO info={0};
        if(SUCCEEDED(IDXGIAdapter3_QueryVideoMemoryInfo(modern,0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info)) && info.Budget) {
            budget->budget=info.Budget;budget->usage=info.CurrentUsage;
            budget->source="DXGI local budget";available=1;
        }
    }
    if(!available) {
        DXGI_ADAPTER_DESC desc;
        if(FAILED(IDXGIAdapter_GetDesc(adapter,&desc)))goto done;
        /* Older DXGI cannot report current VRAM pressure. Start with only
         * half dedicated capacity, or a quarter of shared/available RAM;
         * the separate quarter-budget cap below is stricter again. */
        if(desc.DedicatedVideoMemory) {
            budget->budget=(uint64_t)desc.DedicatedVideoMemory/2;
            budget->source="conservative dedicated estimate";
        } else {
            MEMORYSTATUSEX memory={0};memory.dwLength=sizeof(memory);
            if(!GlobalMemoryStatusEx(&memory))goto done;
            uint64_t shared=desc.SharedSystemMemory;
            if(shared>memory.ullAvailPhys)shared=memory.ullAvailPhys;
            budget->budget=shared/4;
            budget->source="conservative shared/available RAM estimate";
        }
        budget->usage=pilot_native_admitted_bytes;available=budget->budget!=0;
    }
    budget->headroom=budget->usage<budget->budget?budget->budget-budget->usage:0;
done:
    if(modern)IDXGIAdapter3_Release(modern);
    if(adapter)IDXGIAdapter_Release(adapter);
    if(dxgi)IDXGIDevice_Release(dxgi);
    return available;
}
static unsigned pilot_native_format_bytes(const D3D11_TEXTURE2D_DESC *desc)
{
    unsigned target=desc->BindFlags&(D3D11_BIND_RENDER_TARGET|D3D11_BIND_DEPTH_STENCIL);
    /* Retained front images are sampled copy destinations without RT binds.
     * Their companions preserve that copy-only contract. */
    if(target==D3D11_BIND_RENDER_TARGET || (!target && (desc->BindFlags&D3D11_BIND_SHADER_RESOURCE))) {
        switch(desc->Format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_TYPELESS:return 4;
        default:return 0;
        }
    }
    if(target==D3D11_BIND_DEPTH_STENCIL) {
        switch(desc->Format) {
        case DXGI_FORMAT_D16_UNORM:case DXGI_FORMAT_R16_TYPELESS:return 2;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT:case DXGI_FORMAT_R32_TYPELESS:return 4;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:case DXGI_FORMAT_R32G8X24_TYPELESS:return 8;
        default:return 0;
        }
    }
    return 0;
}

static void pilot_native_plane_retain(struct PilotNativePlane *plane)
{
    if(plane)++plane->refs;
}
static void pilot_native_plane_release(struct PilotNativePlane *plane)
{
    if(!plane || --plane->refs)return;
    while(plane->views) {
        struct PilotNativeViewCache *view=plane->views;
        plane->views=view->next;
        prepared_state_forget_view((ID3D11View*)view->view);
        ID3D11ShaderResourceView_Release(view->view);
        free(view);
    }
    rhi_view_destroy(plane->presentation_view);
    rhi_image_destroy(plane->presentation_image);
    if(plane->dsv) {
        prepared_state_forget_view((ID3D11View*)plane->dsv);
        ID3D11DepthStencilView_Release(plane->dsv);
    }
    if(plane->rtv) {
        prepared_state_forget_view((ID3D11View*)plane->rtv);
        ID3D11RenderTargetView_Release(plane->rtv);
    }
    if(plane->enhanced)ID3D11Texture2D_Release(plane->enhanced);
    if(plane->image)ID3D11Texture2D_Release(plane->image);
    pilot_native_admitted_bytes-=plane->admitted_bytes;
    free(plane);
}

/* A texture descriptor does not encode the interpretation of a typeless RTV.
 * These are the default typed families used by the backend's targets. SRVs
 * below always retain the original view's explicit interpretation instead. */
static DXGI_FORMAT pilot_native_rtv_format(DXGI_FORMAT format)
{
    switch(format) {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R32G32B32_TYPELESS:return DXGI_FORMAT_R32G32B32_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:return DXGI_FORMAT_R16G16B16A16_UNORM;
    case DXGI_FORMAT_R32G32_TYPELESS:return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R16G16_TYPELESS:return DXGI_FORMAT_R16G16_UNORM;
    case DXGI_FORMAT_R32_TYPELESS:return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R8G8_TYPELESS:return DXGI_FORMAT_R8G8_UNORM;
    case DXGI_FORMAT_R16_TYPELESS:return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_R8_TYPELESS:return DXGI_FORMAT_R8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:return DXGI_FORMAT_B8G8R8X8_UNORM;
    default:return format;
    }
}
static DXGI_FORMAT pilot_native_dsv_format(DXGI_FORMAT format)
{
    switch(format) {
    case DXGI_FORMAT_R32G8X24_TYPELESS:return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    case DXGI_FORMAT_R32_TYPELESS:return DXGI_FORMAT_D32_FLOAT;
    case DXGI_FORMAT_R24G8_TYPELESS:return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case DXGI_FORMAT_R16_TYPELESS:return DXGI_FORMAT_D16_UNORM;
    default:return format;
    }
}
static struct PilotNativePlane *pilot_native_plane_create(ID3D11Texture2D *source,unsigned width,unsigned height)
{
    if(!source || !width || !height) {
        fprintf(stderr,"[NATIVE PLANE] requested=%ux%u rejected: invalid source/dimensions; native image retained\n",width,height);
        return NULL;
    }
    struct PilotNativePlane *plane=calloc(1,sizeof(*plane));
    if(!plane) {
        fprintf(stderr,"[NATIVE PLANE] requested=%ux%u rejected: owner allocation failed; native image retained\n",width,height);
        return NULL;
    }
    plane->width=width;plane->height=height;plane->refs=1;
    D3D11_TEXTURE2D_DESC desc;
    ID3D11Texture2D_GetDesc(source,&desc);
    plane->native_width=desc.Width;plane->native_height=desc.Height;
    plane->image=source;ID3D11Texture2D_AddRef(source);
    /* Neither a partial draw nor undefined initial pixels establish a fully
     * populated enhanced image. The owner publishes coherence after a full
     * paired clear/copy or successful complete color refresh. */
    plane->coherent=0;
    desc.Width=width;desc.Height=height;
    ID3D11Device *device=NULL;
    struct PilotNativeBudget budget={0};budget.source="unavailable";
    const char *reason="native device unavailable";HRESULT result=E_FAIL;
    ID3D11Texture2D_GetDevice(source,&device);
    if(!device)goto failed;
    D3D_FEATURE_LEVEL feature=ID3D11Device_GetFeatureLevel(device);
    budget.limit=feature>=D3D_FEATURE_LEVEL_11_0?16384:feature>=D3D_FEATURE_LEVEL_10_0?8192:
        feature>=D3D_FEATURE_LEVEL_9_3?4096:2048;
    reason="unsupported source shape/bind flags";
    if(desc.MipLevels!=1 || desc.ArraySize!=1 || desc.SampleDesc.Count!=1 || desc.SampleDesc.Quality ||
       desc.Usage!=D3D11_USAGE_DEFAULT || desc.CPUAccessFlags || desc.MiscFlags ||
       (desc.BindFlags&~(D3D11_BIND_RENDER_TARGET|D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE)))goto failed;
    reason="unsupported color/depth format";
    unsigned bytes=pilot_native_format_bytes(&desc);if(!bytes)goto failed;
    reason="device texture dimension limit";
    if(width>budget.limit || height>budget.limit)goto failed;
    /* Account for aligned rows/allocation granularity and a 25% allowance for
     * driver metadata. This remains an estimate, independently bounded by
     * current DXGI headroom and the total admitted companion budget. */
    uint64_t row=((uint64_t)width*bytes+255)&~UINT64_C(255);
    budget.estimate=((row*height+65535)&~UINT64_C(65535));
    budget.estimate=((budget.estimate+budget.estimate/4+65535)&~UINT64_C(65535));
    reason="adapter memory budget unavailable";
    if(!pilot_native_budget(device,&budget))goto failed;
    reason="companion memory budget/headroom";
    if(pilot_native_admitted_bytes>budget.budget/4 ||
       budget.estimate>budget.budget/4-pilot_native_admitted_bytes || budget.estimate>budget.headroom/2)goto failed;
    /* Native allocation, initial data, and every existing canonical writer
     * remain untouched. Paired clears/draws or an explicit color refresh
     * subsequently populate the enhanced companion. */
    /* Copy-only color fronts also need one GPU seed before their first use.
     * Only the private companion gains a render view; canonical binds stay
     * exactly as allocated by the original donor. */
    if(!(desc.BindFlags&D3D11_BIND_DEPTH_STENCIL))desc.BindFlags|=D3D11_BIND_RENDER_TARGET;
    reason="enhanced texture allocation failed";
    result=ID3D11Device_CreateTexture2D(device,&desc,NULL,&plane->enhanced);
    if(FAILED(result))goto failed;
    if(desc.BindFlags&D3D11_BIND_RENDER_TARGET) {
        D3D11_RENDER_TARGET_VIEW_DESC view={0};
        view.Format=pilot_native_rtv_format(desc.Format);
        if(desc.SampleDesc.Count>1) {
            if(desc.ArraySize>1) {
                view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
                view.Texture2DMSArray.ArraySize=desc.ArraySize;
            } else view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DMS;
        } else if(desc.ArraySize>1) {
            view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.ArraySize=desc.ArraySize;
        } else view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        reason="enhanced render-target view allocation failed";
        result=ID3D11Device_CreateRenderTargetView(device,(ID3D11Resource*)plane->enhanced,&view,&plane->rtv);
        if(FAILED(result))goto failed;
    }
    if(desc.BindFlags&D3D11_BIND_DEPTH_STENCIL) {
        D3D11_DEPTH_STENCIL_VIEW_DESC view={0};
        view.Format=pilot_native_dsv_format(desc.Format);
        if(desc.SampleDesc.Count>1) {
            if(desc.ArraySize>1) {
                view.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;
                view.Texture2DMSArray.ArraySize=desc.ArraySize;
            } else view.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DMS;
        } else if(desc.ArraySize>1) {
            view.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.ArraySize=desc.ArraySize;
        } else view.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        reason="enhanced depth/stencil view allocation failed";
        result=ID3D11Device_CreateDepthStencilView(device,(ID3D11Resource*)plane->enhanced,&view,&plane->dsv);
        if(FAILED(result))goto failed;
    }
    plane->admitted_bytes=budget.estimate;pilot_native_admitted_bytes+=budget.estimate;
    fprintf(stderr,"[NATIVE PLANE] requested=%ux%u admitted estimate=%llu total=%llu budget=%llu usage=%llu headroom=%llu limit=%u source=%s\n",
        width,height,(unsigned long long)budget.estimate,(unsigned long long)pilot_native_admitted_bytes,
        (unsigned long long)budget.budget,(unsigned long long)budget.usage,(unsigned long long)budget.headroom,budget.limit,budget.source);
    ID3D11Device_Release(device);
    return plane;
failed:
    fprintf(stderr,"[NATIVE PLANE] requested=%ux%u rejected: %s hr=%08lx estimate=%llu total=%llu budget=%llu usage=%llu headroom=%llu limit=%u source=%s; native image retained\n",
        width,height,reason,(unsigned long)result,(unsigned long long)budget.estimate,(unsigned long long)pilot_native_admitted_bytes,
        (unsigned long long)budget.budget,(unsigned long long)budget.usage,(unsigned long long)budget.headroom,budget.limit,budget.source);
    if(device)ID3D11Device_Release(device);
    pilot_native_plane_release(plane);
    return NULL;
}

static struct PilotNativeMap *pilot_native_map_create(ID3D11Texture2D *source,struct PilotNativePlane *plane)
{
    if(!source || !plane)return NULL;
    ID3D11Resource *resource=NULL;
    if(FAILED(ID3D11Texture2D_QueryInterface(source,&IID_ID3D11Resource,(void**)&resource)))return NULL;
    for(struct PilotNativeMap *map=pilot_native_maps;map;map=map->next)if(map->source==resource) {
        ID3D11Resource_Release(resource);
        /* An alias may retain an existing registration, but may never take
         * the physical image away from another canonical owner. */
        if(map->plane!=plane || map->refs==UINT_MAX)return NULL;
        ++map->refs;
        return map;
    }
    if(plane->refs==UINT_MAX) {
        ID3D11Resource_Release(resource);
        return NULL;
    }
    struct PilotNativeMap *map=calloc(1,sizeof(*map));
    if(!map) {
        ID3D11Resource_Release(resource);
        return NULL;
    }
    map->source=resource;map->plane=plane;map->refs=1;
    pilot_native_plane_retain(plane);
    map->next=pilot_native_maps;pilot_native_maps=map;
    return map;
}
static void pilot_native_map_release(struct PilotNativeMap *map)
{
    if(!map || --map->refs)return;
    struct PilotNativeMap **entry=&pilot_native_maps;
    while(*entry && *entry!=map)entry=&(*entry)->next;
    if(*entry)*entry=map->next;
    pilot_native_admission_clear(map);
    ID3D11Resource_Release(map->source);
    pilot_native_plane_release(map->plane);
    free(map);
}
static struct PilotNativePlane *pilot_native_plane_find(ID3D11Resource *source)
{
    if(!source)return NULL;
    for(struct PilotNativeMap *map=pilot_native_maps;map;map=map->next)
        if(map->source==source)return map->plane;
    return NULL;
}

static HRESULT pilot_native_plane_view(struct PilotNativePlane *plane,ID3D11ShaderResourceView *original,ID3D11ShaderResourceView **output)
{
    if(!output)return E_POINTER;
    *output=NULL;
    if(!plane || !original)return E_INVALIDARG;
    D3D11_SHADER_RESOURCE_VIEW_DESC desc={0};
    ID3D11ShaderResourceView_GetDesc(original,&desc);
    unsigned cached=0;
    for(struct PilotNativeViewCache *entry=plane->views;entry;entry=entry->next) {
        ++cached;
        if(!memcmp(&entry->desc,&desc,sizeof(desc))) {
            ID3D11ShaderResourceView_AddRef(entry->view);
            *output=entry->view;
            return S_OK;
        }
    }
    ID3D11Device *device=NULL;
    ID3D11Texture2D_GetDevice(plane->enhanced,&device);
    if(!device)return E_FAIL;
    ID3D11ShaderResourceView *view=NULL;
    HRESULT result=ID3D11Device_CreateShaderResourceView(device,(ID3D11Resource*)plane->enhanced,&desc,&view);
    ID3D11Device_Release(device);
    if(FAILED(result))return result;
    /* Keep cache memory bounded even if a title continually changes view
     * ranges. Uncached views retain their normal caller-owned COM lifetime. */
    if(cached<8) {
        struct PilotNativeViewCache *entry=malloc(sizeof(*entry));
        if(entry) {
            entry->desc=desc;entry->view=view;
            entry->next=plane->views;plane->views=entry;
            ID3D11ShaderResourceView_AddRef(view);
        }
    }
    *output=view;
    return S_OK;
}
static void pilot_native_admission_pair(struct PilotNativeAdmissionView *view)
{
    struct PilotNativePlane *plane=view->owner->plane;
    ID3D11View *paired=view->kind==PILOT_NATIVE_ADMISSION_RTV?(ID3D11View*)plane->rtv:
        view->kind==PILOT_NATIVE_ADMISSION_DSV?(ID3D11View*)plane->dsv:NULL;
    if(view->paired==paired)return;
    if(view->paired)ID3D11View_Release(view->paired);
    view->paired=paired;view->compatible=0;if(!paired)return;
    ID3D11View_AddRef(paired);
    if(view->kind==PILOT_NATIVE_ADMISSION_RTV) {
        D3D11_RENDER_TARGET_VIEW_DESC b={0},*a=&view->desc.rtv;
        ID3D11RenderTargetView_GetDesc((ID3D11RenderTargetView*)paired,&b);
        view->compatible=a->Format==b.Format && a->ViewDimension==b.ViewDimension &&
            (a->ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2DMS ||
             (a->ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2D && !a->Texture2D.MipSlice && !b.Texture2D.MipSlice));
    } else {
        D3D11_DEPTH_STENCIL_VIEW_DESC b={0},*a=&view->desc.dsv;
        ID3D11DepthStencilView_GetDesc((ID3D11DepthStencilView*)paired,&b);
        view->compatible=a->Format==b.Format && a->ViewDimension==b.ViewDimension && a->Flags==b.Flags &&
            (a->ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2DMS ||
             (a->ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2D && !a->Texture2D.MipSlice && !b.Texture2D.MipSlice));
    }
}
/* Resource is borrowed on a cache hit, owned on a cold/uncached lookup. No
 * negative membership is cached: an ordinary SRV may acquire a map later. */
static struct PilotNativeAdmissionView *pilot_native_admission_get(ID3D11View *original,unsigned kind,
    ID3D11Resource **resource,unsigned *owned)
{
    *resource=NULL;*owned=0;
    unsigned slot=pilot_native_admission_slot(original,kind);
    struct PilotNativeAdmissionView *view=pilot_native_admission_index[slot];
    if(!view || view->original!=original || view->kind!=kind) {
        view=NULL;
        for(struct PilotNativeMap *map=pilot_native_maps;map && !view;map=map->next)
            for(struct PilotNativeAdmissionView *p=map->admission;p;p=p->next)
                if(p->original==original && p->kind==kind){view=p;break;}
    }
    if(view) {
        pilot_native_admission_index[slot]=view;*resource=view->owner->source;
        pilot_native_admission_pair(view);return view;
    }
    ID3D11View_GetResource(original,resource);*owned=1;
    struct PilotNativeMap *owner=pilot_native_maps;
    while(owner && owner->source!=*resource)owner=owner->next;
    if(!owner || owner->admission_count>=PILOT_NATIVE_ADMISSION_PER_MAP ||
       pilot_native_admission_count>=PILOT_NATIVE_ADMISSION_VIEWS)return NULL;
    view=calloc(1,sizeof(*view));if(!view)return NULL;
    view->original=original;ID3D11View_AddRef(original);view->kind=kind;view->owner=owner;
    if(kind==PILOT_NATIVE_ADMISSION_SRV)ID3D11ShaderResourceView_GetDesc((ID3D11ShaderResourceView*)original,&view->desc.srv);
    else {
        if(kind==PILOT_NATIVE_ADMISSION_RTV)ID3D11RenderTargetView_GetDesc((ID3D11RenderTargetView*)original,&view->desc.rtv);
        else ID3D11DepthStencilView_GetDesc((ID3D11DepthStencilView*)original,&view->desc.dsv);
        ID3D11Texture2D *image=NULL;
        view->shape_result=ID3D11Resource_QueryInterface(*resource,&IID_ID3D11Texture2D,(void**)&image);
        if(SUCCEEDED(view->shape_result)) {
            ID3D11Texture2D_GetDesc(image,&view->shape);ID3D11Texture2D_Release(image);
            if(!view->shape.Width || !view->shape.Height || view->shape.ArraySize!=1 || view->shape.MipLevels!=1)view->shape_result=E_NOTIMPL;
        }
        pilot_native_admission_pair(view);
    }
    view->next=owner->admission;owner->admission=view;++owner->admission_count;++pilot_native_admission_count;
    pilot_native_admission_index[slot]=view;return view;
}
/* Borrowed only within this graphics-locked submission; the map owns the
 * view ref. Queued consumers must take their own refs, as they already do. */
static HRESULT pilot_native_admission_srv(struct PilotNativeAdmissionView *view,ID3D11ShaderResourceView **output)
{
    if(!view->enhanced) {
        HRESULT result=pilot_native_plane_view(view->owner->plane,(ID3D11ShaderResourceView*)view->original,&view->enhanced);
        if(FAILED(result))return result;
    }
    *output=view->enhanced;return S_OK;
}
#endif
