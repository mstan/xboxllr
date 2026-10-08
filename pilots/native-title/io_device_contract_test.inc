/* Expectations observed independently with native_io_device_reference.py over
 * the owned kernel. Exercise native caller writes across repeated LLE calls. */
static uint32_t io_test_create(unsigned extension,unsigned kind,unsigned exclusive,const char *name)
{
    uint32_t name_va=0;
    if(name) {
        size_t length=strlen(name);CHECK(length<240);
        memcpy(memory+0x19500,name,length+1);
        MEM16(0x19400)=(uint16_t)length;MEM16(0x19402)=(uint16_t)(length+1);
        MEM32(0x19404)=0x19500;name_va=0x19400;
    }
    MEM32(0x193fc)=0xaabbccdd;MEM32(0x1940c)=0x11223344;
    MEM32(0x19408)=0xcdcdcdcd;
    uint32_t args[]={0x19300,extension,name_va,kind,exclusive,0x19408};
    call_kernel(65,args,6);
    CHECK(MEM32(0x193fc)==0xaabbccdd && MEM32(0x1940c)==0x11223344);
    return MEM32(0x19408);
}
static void test_io_device_lle(void)
{
    install_fault_handler();
    uint32_t plain=io_test_create(0,0x22,0,NULL);
    CHECK(g_eax==0 && plain && MEM16(plain)==3 && MEM16(plain+2)==0x48);
    CHECK(MEM32(plain+4)==0 && MEM32(plain+8)==0x19300 && MEM32(plain+12)==plain);
    CHECK(MEM32(plain+0x14)==0x10 && MEM32(plain+0x18)==0 && MEM8(plain+0x1e)==1);
    uint32_t exclusive=io_test_create(12,0x22,1,NULL);
    CHECK(g_eax==0 && MEM16(exclusive+2)==0x54 && MEM32(exclusive+0x14)==0x12);
    CHECK(MEM32(exclusive+0x18)==exclusive+0x48);
    for(unsigned i=0;i<12;++i)CHECK(MEM8(exclusive+0x48+i)==0);
    uint32_t nested_args[]={0x19300,0,0,0x22,0,exclusive+0x48};
    call_kernel(65,nested_args,6);CHECK(g_eax==0);
    uint32_t nested=MEM32(exclusive+0x48);CHECK(nested && MEM16(nested)==3);
    call_kernel(68,&nested,1);
    const char *name="\\Device\\XLLE_InputReference";
    uint32_t named=io_test_create(0x170,0x3a,0,name);
    CHECK(g_eax==0 && named && MEM16(named+2)==0x1b8 && MEM32(named+0x14)==0x18);
    CHECK(MEM32(named+0xc)==0 && MEM8(named+0x1c)==0x3a);
    CHECK(MEM16(named+0x28)==0x14 && MEM16(named+0x2a)==0xc);
    CHECK(MEM32(named+0x2c)==named+0x2c && MEM32(named+0x30)==named+0x2c);
    CHECK(MEM8(named+0x34)==1 && MEM8(named+0x36)==4 && MEM32(named+0x38)==1);
    CHECK(MEM32(named+0x3c)==named+0x3c && MEM32(named+0x40)==named+0x3c);
    for(unsigned i=0;i<0x170;++i)CHECK(MEM8(named+0x48+i)==0);
    /* A native driver owns the extension. Later original calls must see and
     * preserve these writes, including allocations sharing the same page. */
    memset(memory+named+0x48,0x5a,0x170);
    CHECK(io_test_create(0x170,0x3a,0,name)==0 && g_eax==0xc0000035u);
    for(unsigned i=0;i<0x170;++i)CHECK(MEM8(named+0x48+i)==0x5a);
    CHECK(io_test_create(8,0x22,0,"\\MissingParent\\XLLE_InputReference")==0 && g_eax==0xc000003au);
    call_kernel(68,&plain,1);call_kernel(68,&exclusive,1);call_kernel(68,&named,1);
    /* Repeated real namespace insertion/removal must not leak or lose pages. */
    for(unsigned i=0;i<128;++i) {
        named=io_test_create(0x170,0x3a,0,name);CHECK(g_eax==0 && named);
        CHECK(MEM32(named+0x48)==0);
        call_kernel(68,&named,1);
    }
    /* Deletion with an outstanding driver reference sets DeletePending but
     * retains the live object. The real final teardown runs after release. */
    named=io_test_create(12,0x22,0,NULL);CHECK(g_eax==0 && named);
    MEM32(named+4)=1;MEM32(named+0x48)=0x12345678;
    call_kernel(68,&named,1);
    CHECK(MEM8(named+0x1f)==1 && MEM32(named+4)==1 && MEM32(named+0x48)==0x12345678);
    MEM32(named+4)=0;call_kernel(68,&named,1);
    uint32_t live[32];
    for(unsigned i=0;i<32;++i) {
        char unique[64];sprintf(unique,"\\Device\\XLLE_Concurrent%u",i);
        live[i]=io_test_create(0x170,0x3a,0,unique);CHECK(g_eax==0 && live[i]);
        MEM32(live[i]+0x48)=0x12345678u+i;
    }
    for(unsigned i=0;i<32;++i) {
        CHECK(MEM32(live[i]+0x48)==0x12345678u+i);call_kernel(68,live+i,1);
    }
    named=io_test_create(0x170,0x3a,0,"\\Device\\XLLE_Concurrent0");CHECK(g_eax==0 && named);
    call_kernel(68,&named,1);
    fprintf(stderr,"PASS: original device creation/queues, named errors, shared extension writes, deferred deletion, 128 delete/recreate cycles, 32 simultaneous devices\n");
}
