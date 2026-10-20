#include "file_async_apc_contract_test.inc"
/* Actual host IO, independently checked against fixture bytes. Both immediate
 * and pending completion are legal; every event must expose completed data. */
static void test_media_async(const unsigned char *content,unsigned length)
{
    const uint32_t out=0x1d000,iosb=0x1d010,offset=0x1d020,buffer=0x1e000,timeout=0x1d030;
    uint32_t create[]={out,GENERIC_READ|SYNCHRONIZE,0x1a000,iosb,0,0,1,1,0x840};
    test_media_name("D:\\asset.bin",0);
    uint32_t event_create[]={out+4,0,0,1};call_kernel(189,event_create,4);CHECK(!g_eax);
    uint32_t event=MEM32(out+4),wait[]={event,0,timeout};MEM64(timeout)=(uint64_t)-50000000;
    for(unsigned cycle=0;cycle<64;++cycle) {
        call_kernel(190,create,9);CHECK(!g_eax);uint32_t file=MEM32(out);
        struct MediaFile *owner=media_owned(file);CHECK(owner && owner->asynchronous && !owner->cacheable);
        uint32_t read[]={file,event,0,0,iosb,buffer,8,offset};MEM64(offset)=3;
        memset(memory+buffer,0xcc,32);call_kernel(219,read,8);CHECK(!g_eax || g_eax==0x103);
        if(cycle&1){call_kernel(187,&file,1);CHECK(!g_eax && !media_owned(file));}
        call_kernel(233,wait,3);CHECK(!g_eax);
        CHECK(!MEM32(iosb) && MEM32(iosb+4)==8 && !memcmp(memory+buffer,content+3,8) && MEM8(buffer+8)==0xcc);
        if(!(cycle&1)) {
            CHECK(WaitForSingleObject(owner->async_signal,0)==WAIT_TIMEOUT);
            /* An immediate rejection is not a completed request. Original
             * NtReadFile/FATX preserves IOSB and clears event/file signals;
             * an accepted pending EOF must still publish its real completion. */
            uint32_t set_event[]={event,0};call_kernel(225,set_event,2);CHECK(!g_eax);
            MEM32(iosb)=0xdeadbeef;MEM32(iosb+4)=0x12345678;
            MEM64(offset)=length;call_kernel(219,read,8);CHECK(g_eax==0xc0000011u || g_eax==0x103);
            if(g_eax==0x103) {
                call_kernel(233,wait,3);CHECK(!g_eax && MEM32(iosb)==0xc0000011u && !MEM32(iosb+4));
            } else {
                CHECK(MEM32(iosb)==0xdeadbeef && MEM32(iosb+4)==0x12345678);
                MEM64(timeout)=0;call_kernel(233,wait,3);CHECK(g_eax==0x102);
                CHECK(WaitForSingleObject(owner->async_signal,0)==WAIT_TIMEOUT);
                MEM64(timeout)=(uint64_t)-50000000;
            }
            call_kernel(225,set_event,2);CHECK(!g_eax);
            MEM32(iosb)=0xdeadbeef;MEM32(iosb+4)=0x12345678;MEM64(offset)=(uint64_t)-1;
            call_kernel(219,read,8);CHECK(g_eax==MEDIA_INVALID);
            CHECK(MEM32(iosb)==0xdeadbeef && MEM32(iosb+4)==0x12345678);
            MEM64(timeout)=0;call_kernel(233,wait,3);CHECK(g_eax==0x102);
            CHECK(WaitForSingleObject(owner->async_signal,0)==WAIT_TIMEOUT && !owner->pending);
            MEM64(timeout)=(uint64_t)-50000000;
            call_kernel(187,&file,1);CHECK(!g_eax);
        }
        CHECK(!owner->host && !owner->async_idle && !owner->pending);
    }
    /* Exact movie open mode and a sector-aligned short final read. */
    create[8]=0x848;call_kernel(190,create,9);CHECK(!g_eax);uint32_t file=MEM32(out);
    uint32_t read[]={file,event,0,0,iosb,buffer,4096,offset};MEM64(offset)=0;
    memset(memory+buffer,0xcc,4096);call_kernel(219,read,8);CHECK(!g_eax || g_eax==0x103);
    call_kernel(233,wait,3);CHECK(!g_eax && !MEM32(iosb) && MEM32(iosb+4)==length && !memcmp(memory+buffer,content,length));
    /* A real outstanding operation owns a reference to its event and file;
     * closing the guest event first must still permit completion and drain. */
    uint32_t status;HANDLE retained=event_duplicate(event,&status);CHECK(retained && !status);
    call_kernel(219,read,8);CHECK(!g_eax || g_eax==0x103);
    call_kernel(187,&event,1);CHECK(!g_eax);
    call_kernel(187,&file,1);CHECK(!g_eax);
    CHECK(WaitForSingleObject(retained,5000)==WAIT_OBJECT_0 && !MEM32(iosb) && MEM32(iosb+4)==length);
    CHECK(CloseHandle(retained) && !wait_handles);
    test_file_apcs();
    for(unsigned i=0;i<256;++i)CHECK(!media_files[i].host && !media_files[i].pending && !media_files[i].async_idle);
    fprintf(stderr,"PASS: real async movie reads, event/IOSB/data ordering, EOF, nonbuffered short read, 64 close/reopen drains and retained completion lifetime\n");
}
