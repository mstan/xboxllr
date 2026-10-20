/* Authored allocator callbacks verify routing/transport only. Actual original
 * heap execution is checked separately by dashboard navigation. */
#if defined(PILOT_D3D_RESOURCE_RETIRE_VERSION) && !defined(PILOT_SCHEDULING_LLE)
static uint32_t retire_test_header,retire_test_data;
static unsigned retire_test_unregistered,retire_test_freed;
static uint64_t retire_test_serial;
static const char *retire_test_failure;
static void test_original_resource_unregister(void)
{
    uint32_t sp=g_esp,header=MEM32(sp+4);CHECK(header==retire_test_header && MEM32(sp)==0);
    CHECK(sp>=0x17000 && sp<0x20000 && graphics_resource_is_retiring(header));
    CHECK(TryAcquireSRWLockExclusive(&graphics_lock));ReleaseSRWLockExclusive(&graphics_lock);
    CHECK(!pilot_gfx_checkpoint_busy(retire_test_serial));
    CHECK(graphics_vertex_find(header)<0 && graphics_index_find(header)<0);
    for(unsigned i=0;i<graphics_texture_capacity;++i)CHECK(graphics_textures[i].header!=header);
    if(retire_test_failure && !strcmp(retire_test_failure,"retire-reset")) {
        uint32_t p=0x19000;g_esp=sp-8;MEM32(g_esp)=0;MEM32(g_esp+4)=p;
        pilot_d3d_dispatch_frame(PILOT_D3D_D3DDevice_Reset,&p,1,4);CHECK(0);
    }
    /* Nested register-frame service on the real callback stack. */
    uint32_t output=0x19500;g_esp=sp-8;MEM32(g_esp)=0;MEM32(g_esp+4)=output;
    pilot_d3d_dispatch_frame(PILOT_D3D_D3DDevice_GetDeviceCaps,&output,1,4);CHECK(g_esp==sp);
    ++retire_test_unregistered;g_esp+=8;
    if(retire_test_failure && !strcmp(retire_test_failure,"retire-abi"))g_esp-=4;
    g_eax=0xaabbccdd;g_ecx=0xbbaaccdd;g_edx=0xccbbaadd;g_ebp=0xdeadbeef;
    g_fp_top=7;g_fp_stack[0]=-17.5;g_fp_control_word=0x0c7f;memset(&g_xmm0,0xab,sizeof(g_xmm0));
    CHECK(!fesetround(FE_UPWARD));
}
static void test_original_resource_free(void)
{
    uint32_t pointer=MEM32(g_esp+4),tag=MEM32(g_esp+8);CHECK(retire_test_unregistered==1);
    CHECK(graphics_resource_is_retiring(retire_test_header));
    CHECK(TryAcquireSRWLockExclusive(&graphics_lock));ReleaseSRWLockExclusive(&graphics_lock);
    if(tag==0xa1800000u) {
        CHECK(pointer==retire_test_data && physical_find(pointer,1) && !physical_find(pointer,1)->borrowers);
        physical_free(pointer);
    } else {
        CHECK(tag==0x24800000u && pointer==retire_test_header);
        uint32_t base=pointer,bytes=0;CHECK(!vm_free(&base,&bytes,MEM_RELEASE));
    }
    ++retire_test_freed;g_esp+=12;g_eax=g_ecx=g_edx=0xeeeeeeee;g_ebp=0xdeadbeef;
}
#endif
static void test_graphics_original_retirement(const char *failure)
{
#if defined(PILOT_D3D_RESOURCE_RETIRE_VERSION) && !defined(PILOT_SCHEDULING_LLE)
    for(unsigned variant=0;variant<4;++variant) {
        uint32_t header=0,bytes=4096;CHECK(!vm_allocate(&header,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        uint32_t data=variant==1?0:physical_allocate(4096,0,0x03ffffff,4096,PAGE_READWRITE);CHECK(variant==1 || data);
        MEM32(header)=variant==1?0x01010001:variant==2?0x01040001:0x01000001;
        MEM32(header+4)=variant==1?header+12:data&0x0fffffffu;MEM32(header+8)=0;
        retire_test_header=header;retire_test_data=data;retire_test_freed=retire_test_unregistered=0;retire_test_failure=failure;retire_test_serial=0;
        struct GraphicsResourceWait lease;
        if(variant==0 || variant==3) {
            AcquireSRWLockExclusive(&graphics_lock);CHECK(graphics_vertex(header)->external);
            ReleaseSRWLockExclusive(&graphics_lock);
            const float positions[3][3]={{-.7f,.6f,.5f},{.6f,.6f,.5f},{0,-.6f,.5f}};
            memset(memory+data,0,3*32);
            for(unsigned i=0;i<3;++i)memcpy(memory+data+i*32,positions[i],12);
            uint32_t stream[]={0,header,32},draw[]={5,0,3};
            call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
            call_graphics(PILOT_D3D_D3DDevice_DrawVertices,draw);
            AcquireSRWLockExclusive(&graphics_lock);
            retire_test_serial=pilot_gfx_resource_serial(PILOT_GFX_VERTEX,graphics_vertex(header)->native);
            CHECK(retire_test_serial);
            ReleaseSRWLockExclusive(&graphics_lock);
            stream[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
            if(variant==3){AcquireSRWLockExclusive(&graphics_lock);graphics_resource_wait_retain(header,&lease);ReleaseSRWLockExclusive(&graphics_lock);}
        }
        if(failure && !strcmp(failure,"retire-shared"))++physical_find(data,1)->borrowers;
        if(variant==2){MEM32(header+12)=0x0221062au;MEM32(header+16)=0;uint32_t binding[]={0,header};call_graphics(PILOT_D3D_D3DDevice_SetTexture,binding);}
        g_fp_top=3;g_fp_stack[0]=12.5;g_fp_control_word=0x027f;memset(&g_xmm0,0x23,sizeof(g_xmm0));RecompXmm xmm=g_xmm0;
        int rounding=fegetround();
        if(variant<2) {
            g_esp=0x1c000;MEM32(g_esp)=0x12340;g_ebx=0x12345678;g_esi=0x23456789;g_edi=0x3456789a;g_ebp=0x456789ab;
            pilot_d3d_dispatch_frame(PILOT_D3D_D3D_DestroyResource,&header,1,0);
            CHECK(g_esp==0x1c004 && g_ebx==0x12345678 && g_esi==0x23456789 && g_edi==0x3456789a && g_ebp==0x456789ab);
        } else {
            CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&header));
            CHECK(!retire_test_freed && vm_find(header,12) && physical_find(data,1));
            if(variant==2){uint32_t binding[]={0,0};call_graphics(PILOT_D3D_D3DDevice_SetTexture,binding);}
            else {
                AcquireSRWLockExclusive(&graphics_lock);graphics_resource_wait_release(&lease);graphics_original_retire_drain();ReleaseSRWLockExclusive(&graphics_lock);
            }
        }
        CHECK(!failure && retire_test_unregistered==1 && retire_test_freed==(variant==1?1:2));
        CHECK(!vm_find(header,12) && (variant==1 || !physical_find(data,1)) && !graphics_resource_retiring && !graphics_retirements);
        CHECK(g_fp_top==3 && g_fp_stack[0]==12.5 && g_fp_control_word==0x027f && !memcmp(&g_xmm0,&xmm,sizeof(xmm)) && fegetround()==rounding);
    }
    puts("PASS original resource retirement: allocator provenance, real submitted GPU completion, unconsumed index, explicit/last-release/binding/private-wait routes, unlocked nested original-stack bridge, FP/SIMD preservation and real backing retirement");
#else
    (void)failure;puts("SKIP original resource allocator: no proved native profile callables");
#endif
}
