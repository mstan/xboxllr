/* Instrumented no-HDD extension of the actual original ISR/DPC fixture.
 * Compiled only with PILOT_IO_DMA_QUERY_TEST; never initializes storage.
 * Original allocator/protection/free calls run between separate IRQ scopes.
 */

static unsigned query_action,query_transfers=65536,private_control_action;
static uint32_t private_control_address=0xfe100000;
static uint32_t query_base,query_pa;
static void *alias_rw,*alias_ro;
static uint64_t query_checksum;
static const char *query_operation(uint32_t *value) {
    uint8_t bytes[16];
    if(query_action==1) {
        uint64_t sum=0;
        for(unsigned i=0;i<query_transfers;++i) {
            const unsigned offset=(i&255u)*16u;
            CHECK(pilot_io_device_contiguous_dma(query_pa+offset,bytes,16,0,PILOT_IO_DMA_SYNC));
            CHECK(!memcmp(bytes,guest+query_base+offset,16));sum+=bytes[i&15];
        }
        query_checksum=sum;*value=0x51;return nullptr;
    }
    if(query_action==2) {
        CHECK(pilot_io_device_contiguous_dma(query_pa,bytes,16,0,PILOT_IO_DMA_SYNC));
        uint8_t before[16];memcpy(before,bytes,16);memset(bytes,0x7a,16);
        CHECK(!pilot_io_device_contiguous_dma(query_pa,bytes,16,1,PILOT_IO_DMA_SYNC));
        CHECK(!memcmp(before,guest+query_base,16));*value=0x52;return nullptr;
    }
    if(query_action==3) {
        memcpy(bytes,guest+query_base,16);
        CHECK(pilot_io_device_contiguous_dma(query_pa,bytes,16,1,PILOT_IO_DMA_SYNC));
        memset(bytes,0,16);CHECK(pilot_io_device_contiguous_dma(query_pa,bytes,16,0,PILOT_IO_DMA_SYNC));
        CHECK(!memcmp(bytes,guest+query_base,16));*value=0x53;return nullptr;
    }
    if(query_action==4) {
        uint8_t before[16];memcpy(before,guest+query_base+4092,16);memset(bytes,0x7a,16);
        CHECK(!pilot_io_device_contiguous_dma(query_pa+4092,bytes,16,1,PILOT_IO_DMA_SYNC));
        CHECK(!memcmp(before,guest+query_base+4092,16));*value=0x54;return nullptr;
    }
    if(query_action==5) {
        memset(bytes,0xcc,16);CHECK(!pilot_io_device_contiguous_dma(query_pa,bytes,16,0,PILOT_IO_DMA_SYNC));
        for(auto x:bytes)CHECK(x==0xcc);*value=0x55;return nullptr;
    }
    if(query_action==6) {
        CHECK(pilot_io_device_dma_query_test_host_page(alias_rw,1));CHECK(!pilot_io_device_dma_query_test_host_page(alias_ro,1));
        CHECK(pilot_io_device_dma_query_test_host_page(alias_ro,0));CHECK(pilot_io_device_dma_query_test_host_page(alias_rw,0));
        *value=0x56;return nullptr;
    }
    CHECK(0);return nullptr;
}
static void query_counters(uint64_t s[4]) {pilot_io_device_dma_query_test_stats(s);CHECK(!s[3]);}
static void query_once(unsigned action,uint32_t address=0xfe100000) {
    query_action=action;CHECK(compiled_load(address)==0x50+action);query_action=0;
    uint64_t s[4];query_counters(s);
}
static void query_suite() {
    uint64_t a[4],b[4];uint8_t before[4096];memcpy(before,guest+allocation,sizeof(before));
    query_base=allocation;query_pa=physical_address;
    const unsigned lease=pilot_io_device_dma_query_test_lease(query_pa);CHECK(lease==0);
    // Baseline and candidate use the SAME executable/buffer/checks and alternate
    // within this actual stopped original ISR continuation. Only query reuse differs.
    for(unsigned sample=0;sample<8;++sample) {
        unsigned enabled=(sample==1 || sample==2 || sample==5 || sample==6);
        CHECK(pilot_io_device_dma_query_test_enable(enabled));query_counters(a);
        LARGE_INTEGER start,end,freq;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&start);
        query_once(1);QueryPerformanceCounter(&end);query_counters(b);
        const uint64_t queries=b[0]-a[0],hits=b[1]-a[1];
        CHECK(queries==(enabled?1u:2u*query_transfers));
        CHECK(hits==(enabled?2u*query_transfers-1:0));
        printf("[DMA QUERY BENCH] sample=%u cache=%u transfers=%u bytes=%u queries=%llu hits=%llu seconds=%.9f checksum=%llu\n",
            sample,enabled,query_transfers,16u*query_transfers,(unsigned long long)queries,
            (unsigned long long)hits,double(end.QuadPart-start.QuadPart)/double(freq.QuadPart),
            (unsigned long long)query_checksum);
        CHECK(!memcmp(before,guest+allocation,sizeof(before)) && pilot_io_device_dma_query_test_lease(query_pa)==lease);
    }
    CHECK(pilot_io_device_dma_query_test_enable(1));
    // No explicit callback capability: reuse must stay disabled even in a borrow.
    query_counters(a);query_once(1,0xfe101000);query_counters(b);
    CHECK(b[0]-a[0]==2u*query_transfers && b[1]==a[1]);
    // Outside borrowed callbacks, both DMA validation passes always query fresh.
    uint8_t bytes[16];query_counters(a);
    CHECK(pilot_io_device_contiguous_dma(query_pa,bytes,16,0,PILOT_IO_DMA_SYNC));
    CHECK(pilot_io_device_contiguous_dma(query_pa,bytes,16,0,PILOT_IO_DMA_SYNC));
    query_counters(b);CHECK(b[0]-a[0]==4 && b[1]==a[1]);
    // Two genuine host aliases of the SAME section bytes have different page
    // protections. These query-only fixture calls grant no guest DMA ownership.
    HANDLE section=CreateFileMappingA(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,4096,nullptr);CHECK(section);
    alias_rw=MapViewOfFile(section,FILE_MAP_READ|FILE_MAP_WRITE,0,0,4096);CHECK(alias_rw);
    alias_ro=MapViewOfFile(section,FILE_MAP_READ,0,0,4096);CHECK(alias_ro && alias_ro!=alias_rw);
    memcpy(alias_rw,"alias",6);CHECK(!memcmp(alias_ro,"alias",6));
    query_counters(a);query_once(6);query_counters(b);CHECK(b[0]-a[0]>=2);
    // A 64-slot collision can lower hits but may never conflate distinct aliases.
    CHECK(UnmapViewOfFile(alias_ro) && UnmapViewOfFile(alias_rw) && CloseHandle(section));
    CHECK(!memcmp(before,guest+allocation,sizeof(before)) && pilot_io_device_dma_query_test_lease(physical_address)==lease);
    puts("[DMA QUERY CONTROL] default_uncached outside_uncached alias_separation bytes_leases=PASS");
}

static void query_fire(unsigned action) {
    private_control_action=action;pilot_usb_pic_raise(3,1);int delivered=0;
    const char *error=pilot_io_device_irq(guest,&delivered);
    if(error)fprintf(stderr,"CONTROL IRQ: %s\n",error);
    CHECK(!error && delivered);private_control_action=0;
    uint64_t counts[4];query_counters(counts);
}
static void query_controls_main() {
    uint64_t a[4],b[4];uint8_t before[4096];memcpy(before,guest+allocation,sizeof(before));
    query_base=allocation;query_pa=physical_address;CHECK(pilot_io_device_dma_query_test_enable(1));
    // Real original182 protection changes occur BETWEEN scopes. A RO hit may
    // satisfy reads but never grant writes; restoring RW must issue a new query.
    uint32_t protect[]={allocation,4096,PAGE_READONLY};call(182,protect);
    query_counters(a);query_fire(2);query_counters(b);CHECK(b[0]-a[0]==1);
    protect[2]=PAGE_READWRITE;call(182,protect);
    query_counters(a);query_fire(3);query_counters(b);CHECK(b[0]-a[0]==1);
    CHECK(!memcmp(before,guest+allocation,sizeof(before)));
    // Cross-page prevalidation must still prevent even a first-page store.
    uint32_t create[]={8192,0,0x03ffffff,4096,PAGE_READWRITE};
    query_base=call(166,create);CHECK(query_base);query_pa=call(173,&query_base);
    memset(guest+query_base,0x39,8192);protect[0]=query_base+4096;protect[2]=PAGE_READONLY;call(182,protect);
    query_fire(4);protect[2]=PAGE_READWRITE;call(182,protect);
    uint32_t retired_base=query_base,retired_pa=query_pa;call(171,&retired_base);
    query_fire(5);create[0]=4096;query_base=call(166,create);CHECK(query_base);query_pa=call(173,&query_base);
    memset(guest+query_base,0x67,4096);query_counters(a);query_fire(3);query_counters(b);
    CHECK(b[0]-a[0]==1 && pilot_io_device_dma_query_test_lease(query_pa)==0);
    CHECK(query_pa==retired_pa+4096 && query_base==retired_base+4096);
    printf("[DMA QUERY CONTROL] retired_pa=%u reallocated_pa=%u same_pa=%u reused_former_RO_host_page=1 fresh_queries=%llu\n",retired_pa,query_pa,retired_pa==query_pa,(unsigned long long)(b[0]-a[0]));
    call(171,&query_base);query_base=allocation;query_pa=physical_address;

    CHECK(!memcmp(before,guest+allocation,sizeof(before)) && pilot_io_device_dma_query_test_lease(physical_address)==0);
    puts("[DMA QUERY CONTROL] original182_refresh crosspage_atomic original171_166_retirement_reallocation=PASS");
}
