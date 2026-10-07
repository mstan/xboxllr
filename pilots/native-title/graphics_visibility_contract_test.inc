static void test_graphics_visibility(void)
{
    uint32_t args[]={655,0x19604,0x1960c};
    memset(memory+0x19600,0xcd,24);
    CHECK(call_graphics(PILOT_D3D_D3DDevice_GetVisibilityTestResult,args)==0x88760828u);
    for(unsigned i=0;i<24;++i)CHECK(memory[0x19600+i]==0xcd);
    call_graphics(PILOT_D3D_D3DDevice_BeginVisibilityTest,NULL);
    CHECK(call_graphics(PILOT_D3D_D3DDevice_EndVisibilityTest,args)==0);
    DWORD start=GetTickCount();uint32_t hr;
    do {
        hr=call_graphics(PILOT_D3D_D3DDevice_GetVisibilityTestResult,args);
        if(hr!=0x88760828u)break;
        for(unsigned i=0;i<24;++i)CHECK(memory[0x19600+i]==0xcd);Sleep(1);
    } while(GetTickCount()-start<2000);
    CHECK(hr==0 && MEM32(args[1])==0);
    uint64_t stamp;memcpy(&stamp,memory+args[2],8);
    CHECK(MEM32(0x19600)==0xcdcdcdcdu && MEM32(0x19608)==0xcdcdcdcdu && MEM32(0x19614)==0xcdcdcdcdu);
    CHECK(call_graphics(PILOT_D3D_D3DDevice_GetVisibilityTestResult,args)==0);
    CHECK(!memcmp(&stamp,memory+args[2],8));
    args[2]=0;CHECK(call_graphics(PILOT_D3D_D3DDevice_GetVisibilityTestResult,args)==0);
    fprintf(stderr,"PASS: original visibility stdcall/outputs, pending preservation, optional timestamp and repeatable result ABI\n");
}
