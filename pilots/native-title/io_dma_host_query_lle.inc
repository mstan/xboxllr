/* A registered MMIO owner may explicitly promise that the queried guest-backing
 * host mappings/protections stay immutable until its synchronous callback returns.
 * Ordinary unrelated host-container allocation is allowed. CPU/service/guest
 * allocation reentry is forbidden by the borrowed owner contract. No whole
 * compiled continuation, device-work callback or later CPU slice shares this cache.
 *
 * Only VirtualQuery State/Protect is memoized, keyed by the full HOST virtual
 * page (different aliases can have different protections). Original PTE/PFN,
 * ownership, publication, pin/range and both DMA passes are always checked.
 */
#define DMA_HOST_QUERY_SLOTS 64u
struct DmaHostQueryPage {uintptr_t page;DWORD state,protect;unsigned valid;};
static __declspec(thread) struct DmaHostQueryPage dma_host_query_pages[DMA_HOST_QUERY_SLOTS];
static __declspec(thread) unsigned dma_host_query_active;
#ifdef PILOT_IO_DMA_QUERY_TEST
static __declspec(thread) unsigned dma_host_query_enabled=1;
static __declspec(thread) uint64_t dma_host_query_calls,dma_host_query_hits,dma_host_query_epochs;
#endif
static void dma_host_query_begin(unsigned immutable_guest_mapping)
{
    dma_host_query_active=0;
    if(!immutable_guest_mapping || !device_bus_borrow)return;
#ifdef PILOT_IO_DMA_QUERY_TEST
    if(!dma_host_query_enabled)return;
#endif
    /* Generic/audio/USB/work paths never pay this bounded clear. */
    memset(dma_host_query_pages,0,sizeof(dma_host_query_pages));
    dma_host_query_active=1;
#ifdef PILOT_IO_DMA_QUERY_TEST
    ++dma_host_query_epochs;
#endif
}
static void dma_host_query_end(void)
{
    /* Stale slots cannot be used while inactive; next eligible begin clears.
     * Deactivate before errors, native/guest execution or a later device call. */
    dma_host_query_active=0;
}
static int dma_host_page_query(const void *address,MEMORY_BASIC_INFORMATION *info)
{
    const uintptr_t page=(uintptr_t)address&~(uintptr_t)4095;
    struct DmaHostQueryPage *slot=dma_host_query_pages+((page>>12)%DMA_HOST_QUERY_SLOTS);
    if(dma_host_query_active && device_bus_borrow && slot->valid && slot->page==page) {
        info->State=slot->state;info->Protect=slot->protect;
#ifdef PILOT_IO_DMA_QUERY_TEST
        ++dma_host_query_hits;
#endif
        return 1;
    }
#ifdef PILOT_IO_DMA_QUERY_TEST
    ++dma_host_query_calls;
#endif
    if(!VirtualQuery(address,info,sizeof(*info)))return 0;
    if(dma_host_query_active && device_bus_borrow) {
        slot->page=page;slot->state=info->State;slot->protect=info->Protect;slot->valid=1;
    }
    return 1;
}
#ifdef PILOT_IO_DMA_QUERY_TEST
/* Instrumented fixture only. These observations grant no guest DMA ownership. */
int pilot_io_device_dma_query_test_enable(unsigned enabled)
{
    if(device_bus_borrow || dma_host_query_active || enabled>1)return 0;
    dma_host_query_end();dma_host_query_enabled=enabled;return 1;
}
void pilot_io_device_dma_query_test_stats(uint64_t out[4])
{
    out[0]=dma_host_query_calls;out[1]=dma_host_query_hits;
    out[2]=dma_host_query_epochs;out[3]=dma_host_query_active;
}
int pilot_io_device_dma_query_test_host_page(const void *address,int write)
{
    MEMORY_BASIC_INFORMATION info;
    return dma_host_page_query(address,&info) && info.State==MEM_COMMIT &&
        !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) && (!write || info.Protect==PAGE_READWRITE);
}
#endif
