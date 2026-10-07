/* Native implementation of the bounded NV097/XDK visibility contract.
 * Real D3D11 samples and GPU timestamps. Independent of the CPU reference;
 * production selects one renderer and never executes both. See pilot README. */
struct PilotVisibilityCounter { ID3D11Query *query; unsigned refs; int ended,ready; uint32_t value; };
struct PilotVisibilityReport {
    uint32_t index;struct PilotVisibilityCounter *counter;
    ID3D11Query *begin_stamp,*end_stamp,*clock;
    int ready;uint64_t stamp;
    struct PilotVisibilityReport *next;
};
static struct PilotVisibilityCounter *pilot_visibility_current;
static struct PilotVisibilityReport *pilot_visibility_reports;
static unsigned pilot_visibility_counter_count,pilot_visibility_report_count;
static void pilot_visibility_unref(struct PilotVisibilityCounter *c)
{
    if(c && !--c->refs){ID3D11Query_Release(c->query);free(c);--pilot_visibility_counter_count;}
}
static void pilot_visibility_drop(struct PilotVisibilityReport *r)
{
    if(r->begin_stamp)ID3D11Query_Release(r->begin_stamp);
    if(r->end_stamp)ID3D11Query_Release(r->end_stamp);
    if(r->clock)ID3D11Query_Release(r->clock);
    pilot_visibility_unref(r->counter);free(r);--pilot_visibility_report_count;
}
static HRESULT pilot_visibility_query(D3D11_QUERY type,ID3D11Query **out)
{D3D11_QUERY_DESC d={type,0};return ID3D11Device_CreateQuery(d3d8_GetD3D11Device(),&d,out);}
int32_t pilot_gfx_visibility_begin(void)
{
    if(!pilot_device)return E_FAIL;
    struct PilotVisibilityCounter *c=calloc(1,sizeof(*c));if(!c)return E_OUTOFMEMORY;
    HRESULT hr=pilot_visibility_query(D3D11_QUERY_OCCLUSION,&c->query);
    if(FAILED(hr)){free(c);return hr;}c->refs=1;++pilot_visibility_counter_count;
    ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();
    /* Begin is CLEAR+ENABLE, not a nested query stack. Earlier draws remain
     * submitted; only their discarded measurement loses its ownership. */
    if(pilot_visibility_current && !pilot_visibility_current->ended)
        ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous*)pilot_visibility_current->query);
    pilot_visibility_unref(pilot_visibility_current);pilot_visibility_current=c;
    ID3D11DeviceContext_Begin(ctx,(ID3D11Asynchronous*)c->query);return S_OK;
}
int32_t pilot_gfx_visibility_end(uint32_t index)
{
    if(!pilot_device)return E_FAIL;
    if(!pilot_visibility_current){HRESULT hr=pilot_gfx_visibility_begin();if(FAILED(hr))return hr;}
    struct PilotVisibilityReport *r=calloc(1,sizeof(*r));if(!r)return E_OUTOFMEMORY;
    ++pilot_visibility_report_count;r->index=index;
    HRESULT hr=pilot_visibility_query(D3D11_QUERY_TIMESTAMP,&r->begin_stamp);
    if(SUCCEEDED(hr))hr=pilot_visibility_query(D3D11_QUERY_TIMESTAMP,&r->end_stamp);
    if(SUCCEEDED(hr))hr=pilot_visibility_query(D3D11_QUERY_TIMESTAMP_DISJOINT,&r->clock);
    if(FAILED(hr)){pilot_visibility_drop(r);return hr;}
    ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();
    r->counter=pilot_visibility_current;++r->counter->refs;
    if(!r->counter->ended){ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous*)r->counter->query);r->counter->ended=1;}
    ID3D11DeviceContext_Begin(ctx,(ID3D11Asynchronous*)r->clock);
    ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous*)r->begin_stamp);
    ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous*)r->end_stamp);
    ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous*)r->clock);
    struct PilotVisibilityReport **at=&pilot_visibility_reports;
    while(*at && (*at)->index!=index)at=&(*at)->next;
    if(*at){struct PilotVisibilityReport *old=*at;r->next=old->next;*at=r;pilot_visibility_drop(old);}
    else {*at=r;}
    /* One submission per report. Subsequent polls never force another flush. */
    ID3D11DeviceContext_Flush(ctx);pilot_mark_work();return S_OK;
}
int32_t pilot_gfx_visibility_result(uint32_t index,uint32_t *count,uint64_t *stamp)
{
    struct PilotVisibilityReport *r=pilot_visibility_reports;
    while(r && r->index!=index)r=r->next;
    if(!r)return (int32_t)0x88760828u;
    ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();HRESULT hr;
    if(!r->counter->ready){
        UINT64 samples;
        hr=ID3D11DeviceContext_GetData(ctx,(ID3D11Asynchronous*)r->counter->query,&samples,sizeof(samples),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(hr!=S_OK)return hr==S_FALSE?(int32_t)0x88760828u:hr;
        r->counter->value=(uint32_t)samples;r->counter->ready=1;
    }
    if(!r->ready){
        UINT64 begin,end;D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock;
        hr=ID3D11DeviceContext_GetData(ctx,(ID3D11Asynchronous*)r->clock,&clock,sizeof(clock),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(hr==S_OK)hr=ID3D11DeviceContext_GetData(ctx,(ID3D11Asynchronous*)r->begin_stamp,&begin,sizeof(begin),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(hr==S_OK)hr=ID3D11DeviceContext_GetData(ctx,(ID3D11Asynchronous*)r->end_stamp,&end,sizeof(end),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(hr!=S_OK)return hr==S_FALSE?(int32_t)0x88760828u:hr;
        if(clock.Disjoint || !clock.Frequency || end<begin)return E_FAIL;
        /* Report epoch is backend-specific; units are actual nanoseconds.
         * Quotient/remainder avoids overflowing the full timestamp product. */
        r->stamp=(end/clock.Frequency)*1000000000ull+(uint64_t)((long double)(end%clock.Frequency)*1000000000.0L/clock.Frequency);
        r->ready=1;
    }
    if(count)*count=r->counter->value;if(stamp)*stamp=r->stamp;return S_OK;
}
static void pilot_visibility_close(void)
{
    if(pilot_visibility_current && !pilot_visibility_current->ended)
        ID3D11DeviceContext_End(d3d8_GetD3D11Context(),(ID3D11Asynchronous*)pilot_visibility_current->query);
    while(pilot_visibility_reports){struct PilotVisibilityReport *r=pilot_visibility_reports;pilot_visibility_reports=r->next;pilot_visibility_drop(r);}
    pilot_visibility_unref(pilot_visibility_current);pilot_visibility_current=NULL;
    if(pilot_visibility_counter_count || pilot_visibility_report_count)pilot_d3d_backend_stop("visibility ownership leak");
}
