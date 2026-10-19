struct TestMutantPeer {uint32_t id,status;int release;};
static DWORD WINAPI test_mutant_peer(void *parameter)
{
    struct TestMutantPeer *peer=parameter;uint32_t status;
    HANDLE host=wait_duplicate(peer->id,PILOT_WAIT_MUTANT,&status);CHECK(host && !status);
    LARGE_INTEGER zero;zero.QuadPart=0;
    peer->status=(uint32_t)event_nt_wait(host,FALSE,&zero);
    if(!peer->status && peer->release)CHECK(!mutant_nt_release(host,NULL));
    CHECK(CloseHandle(host));return 0;
}
static void test_mutant_query(uint32_t id,int32_t count,unsigned owned,unsigned abandoned)
{
    const uint32_t output=0x19310;memset(memory+output-4,0xcc,16);
    uint32_t query[]={id,output};call_kernel(213,query,2);
    CHECK(!g_eax && MEM32(output)==(uint32_t)count && MEM8(output+4)==owned && MEM8(output+5)==abandoned);
    CHECK(MEM32(output-4)==0xcccccccc && MEM8(output+6)==0xcc && MEM8(output+7)==0xcc && MEM32(output+8)==0xcccccccc);
}
static void test_mutant_objects(void)
{
    const uint32_t output=0x19300,previous=0x19308,timeout=0x19320,array=0x19330;
    DWORD before,after;CHECK(GetProcessHandleCount(GetCurrentProcess(),&before));
    const uint32_t owners[]={0,1,0x100,0x101,0xff};
    for(unsigned i=0;i<sizeof(owners)/sizeof(*owners);++i) {
        uint32_t create[]={output,0,owners[i]};call_kernel(192,create,3);CHECK(!g_eax);
        uint32_t id=MEM32(output),release[]={id,previous};
        unsigned owned=(owners[i]&255)!=0;test_mutant_query(id,owned?0:1,owned,0);
        if(owned){call_kernel(221,release,2);CHECK(!g_eax && !MEM32(previous));}
        call_kernel(187,&id,1);CHECK(!g_eax);
    }
    uint32_t create[]={output,0,0};call_kernel(192,create,3);CHECK(!g_eax);
    uint32_t id=MEM32(output),wait[]={id,0,timeout},wait_ex[]={id,1,0,timeout},release[]={id,previous};
    MEM32(previous)=0xcccccccc;call_kernel(221,release,2);
    CHECK(g_eax==0xc0000046u && MEM32(previous)==0xcccccccc);test_mutant_query(id,1,0,0);
    MEM64(timeout)=0;call_kernel(233,wait,3);CHECK(!g_eax);test_mutant_query(id,0,1,0);
    call_kernel(234,wait_ex,4);CHECK(!g_eax);test_mutant_query(id,-1,1,0);
    call_kernel(221,release,2);CHECK(!g_eax && MEM32(previous)==UINT32_MAX);
    struct TestMutantPeer peer={id,0,1};HANDLE thread=CreateThread(NULL,0,test_mutant_peer,&peer,0,NULL);
    CHECK(thread && WaitForSingleObject(thread,5000)==WAIT_OBJECT_0 && CloseHandle(thread) && peer.status==0x102);
    call_kernel(221,release,2);CHECK(!g_eax && !MEM32(previous));test_mutant_query(id,1,0,0);
    peer.release=0;thread=CreateThread(NULL,0,test_mutant_peer,&peer,0,NULL);
    CHECK(thread && WaitForSingleObject(thread,5000)==WAIT_OBJECT_0 && CloseHandle(thread) && !peer.status);
    test_mutant_query(id,1,0,1);call_kernel(233,wait,3);CHECK(g_eax==0x80);test_mutant_query(id,0,1,0);
    call_kernel(221,release,2);CHECK(!g_eax);
    uint32_t event_create[]={output,0,1,0};call_kernel(189,event_create,4);CHECK(!g_eax);
    uint32_t event=MEM32(output),query[]={event,previous};MEM32(previous)=0xcccccccc;
    call_kernel(213,query,2);CHECK(g_eax==0xc0000024u && MEM32(previous)==0xcccccccc);
    query[0]=id;call_kernel(209,query,2);CHECK(g_eax==0xc0000024u);
    uint32_t status;CHECK(!event_duplicate(id,&status) && status==0xc0000024u);
    MEM32(array)=event;MEM32(array+4)=id;
    uint32_t multi[]={2,array,1,1,0,timeout};call_kernel(235,multi,6);CHECK(g_eax==1);test_mutant_query(id,0,1,0);
    call_kernel(221,release,2);CHECK(!g_eax);
    multi[2]=0;call_kernel(235,multi,6);CHECK(g_eax==0x102);test_mutant_query(id,1,0,0);
    uint32_t signal[]={event,0};call_kernel(225,signal,2);CHECK(!g_eax);
    call_kernel(235,multi,6);CHECK(!g_eax);test_mutant_query(id,0,1,0);
    uint32_t event_wait[]={event,0,timeout};call_kernel(233,event_wait,3);CHECK(g_eax==0x102);
    /* A failed later handle must not acquire an earlier available mutant. */
    call_kernel(221,release,2);CHECK(!g_eax);
    MEM32(array)=id;MEM32(array+4)=0x300fffffu;multi[2]=1;
    call_kernel(235,multi,6);CHECK(g_eax==0xc0000008u);test_mutant_query(id,1,0,0);
    HANDLE retained=wait_duplicate(id,PILOT_WAIT_MUTANT,&status);CHECK(retained && !status);
    call_kernel(187,&id,1);CHECK(!g_eax);
    LARGE_INTEGER zero;zero.QuadPart=0;CHECK(!event_nt_wait(retained,FALSE,&zero) && !mutant_nt_release(retained,NULL) && CloseHandle(retained));
    query[0]=id;call_kernel(213,query,2);CHECK(g_eax==0xc0000008u);
    call_kernel(187,&id,1);CHECK(g_eax==0xc0000008u);
    call_kernel(187,&event,1);CHECK(!g_eax);
    CHECK(!wait_handles && GetProcessHandleCount(GetCurrentProcess(),&after) && before==after);
    CHECK(scheduler_kernel_blocks(235));
    fprintf(stderr,"PASS native mutant ABI, exact six-byte query, Boolean width, recursion, contention/abandonment, mixed atomic waits, typed references and no handle growth\n");
}
static void test_mutant_dpc_wait(unsigned extended)
{
    uint32_t create[]={0x19300,0,0};call_kernel(192,create,3);CHECK(!g_eax);
    uint32_t wait[]={MEM32(create[0]),0,0,0};dispatch_in_dpc=1;
    call_kernel(extended?234:233,wait,extended?4:3);CHECK(0);
}
