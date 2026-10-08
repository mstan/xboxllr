static void test_physical_memory(void)
{
#ifdef PILOT_USB_LLE
    test_original_physical_memory();
#else
    uint64_t before=vm_committed(0,0x100000000ull);
    uint32_t args[]={4097,0x30001,0x4ffff,0x10000,0x404};
    call_kernel(166,args,5);uint32_t first=g_eax;
    CHECK(first==0x80040000u && !MEM32(first) && !MEM32(first+4096));
    CHECK(vm_committed(0,0x100000000ull)==before+8192);
    call_kernel(180,&first,1); CHECK(g_eax==8192);
    MEM32(first)=0x12345678;MEM32(first+4096)=0xdeadbeef;
    uint32_t persist[]={first+4095,2,1},first_page=(first&0x0fffffff)/4096;
    call_kernel(178,persist,3);CHECK(physical_persistent[first_page] && physical_persistent[first_page+1]);
    persist[0]=first;persist[1]=4096;persist[2]=0;
    call_kernel(178,persist,3);CHECK(!physical_persistent[first_page] && physical_persistent[first_page+1]);
    call_kernel(4,&first,1);call_kernel(1,NULL,0);CHECK(g_eax==first);
    uint32_t zero=0;call_kernel(4,&zero,1);call_kernel(1,NULL,0);CHECK(!g_eax);
    call_kernel(179,&first,1);CHECK(g_eax==0x404);
    uint32_t protection[]={first+4095,2,0x202};
    uint32_t saved_bx=g_ebx,saved_si=g_esi,saved_di=g_edi,saved_bp=g_ebp;
    call_kernel(182,protection,3);
    CHECK(g_ebx==saved_bx && g_esi==saved_si && g_edi==saved_di && g_ebp==saved_bp);
    CHECK(test_write_faults(first) && test_write_faults(first+4096));
    CHECK(MEM32(first)==0x12345678 && MEM32(first+4096)==0xdeadbeef);
    call_kernel(179,&first,1);CHECK(g_eax==0x202);
    protection[0]=first+1;protection[1]=4095;protection[2]=4;
    call_kernel(182,protection,3);
    CHECK(!test_write_faults(first) && test_write_faults(first+4096));
    MEM32(first)=0x12345678;
    call_kernel(179,&first,1);CHECK(g_eax==4);
    uint32_t page=first+4096;call_kernel(179,&page,1);CHECK(g_eax==0x202);
    protection[0]=first;protection[1]=8192;protection[2]=1; /* invalid for Mm, unlike Nt */
    call_kernel(182,protection,3);call_kernel(179,&page,1);CHECK(g_eax==0x202);
    protection[2]=0x404;call_kernel(182,protection,3);
    CHECK(!test_write_faults(page));MEM32(page)=0xdeadbeef;
    call_kernel(179,&page,1);CHECK(g_eax==0x404);
    CHECK(vm_committed(0,0x100000000ull)==before+8192);
    call_kernel(166,args,5);CHECK(!g_eax && MEM32(first)==0x12345678);
    args[2]=0x7ffff;call_kernel(166,args,5);uint32_t second=g_eax;
    CHECK(second==0x80050000u && MEM32(first+4096)==0xdeadbeef);
    uint32_t exhausted[]={64u*1024*1024,0,0xffffffffu,0,4};
    call_kernel(166,exhausted,5);CHECK(!g_eax && MEM32(first)==0x12345678);
    call_kernel(171,&first,1);CHECK(test_write_faults(first));
    CHECK(!physical_persistent[first_page] && !physical_persistent[first_page+1]);
    call_kernel(166,args,5);CHECK(g_eax==first && !MEM32(first) && !MEM32(first+4096));
    call_kernel(171,&first,1);call_kernel(171,&second,1);
    uint32_t length=1;call_kernel(165,&length,1);first=g_eax;
    CHECK(first && !test_write_faults(first));
    call_kernel(180,&first,1); CHECK(g_eax==4096);
    call_kernel(171,&first,1);
    CHECK(vm_committed(0,0x100000000ull)==before);
    fprintf(stderr,"PASS: contiguous allocation ABI, alignment/bounds/non-overlap, actual writes, RAM budget, release protection and zeroed reuse\n");
#endif
}
