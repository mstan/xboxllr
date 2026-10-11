/* Original CPU CALL frames drive the existing FATX/IDE/PIC owner. Expected
 * bytes and length come from the separately supplied owned comparison file. */
#include "files_lle_config.h"
#define THREAD_FILES_CPU 0x13200u
#define FILES_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static void thread_files_open(struct ThreadChildCode *c,uint8_t attribute)
{
    child_push_field(c,48);child_push(c,1);child_output(c,4);
    child_push_field(c,attribute);child_push(c,0x100001);child_output(c,0);child_call(c,202);
}
static void thread_files_read(struct ThreadChildCode *c,uint8_t offset,int eof)
{
    child_output(c,offset);
    if(eof)child_push(c,512);else child_push_field(c,72);
    child_push_field(c,68);child_output(c,4);
    child_push(c,0);child_push(c,0);child_push_field(c,32);child_push_field(c,0);child_call(c,219);
}
static void thread_files_code(void)
{
    struct ThreadChildCode code={0};struct ThreadChildCode *c=&code;
    FILES_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14);
    thread_files_open(c,96);vm_status(c,12);
    child_push(c,34);child_push(c,56);child_push_field(c,104);child_output(c,4);child_push_field(c,0);child_call(c,211);vm_status(c,20);
    thread_files_read(c,76,0);vm_status(c,16);
    child_push(c,0);child_push(c,0);child_push_field(c,32);child_call(c,233);vm_status(c,36);
    FILES_BYTES(c,0x8b,0x47,4,0x89,0x47,40,0x8b,0x47,8,0x89,0x47,44);
    thread_files_read(c,112,1);vm_status(c,52);
    child_output(c,120);child_push(c,0);child_push_field(c,32);child_call(c,233);vm_status(c,56);
    child_push_field(c,0);child_call(c,187);vm_status(c,24);
    child_push_field(c,0);child_call(c,187);vm_status(c,60);
    thread_files_open(c,100);vm_status(c,28);
    FILES_BYTES(c,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(c->size<512);memcpy(memory+THREAD_FILES_CPU,c->bytes,c->size);
}
static void test_thread_files_cpu_lle(const char *comparison)
{
    FILE *file=fopen(comparison,"rb");CHECK(file);
    unsigned char expected[8192];CHECK(fread(expected,1,sizeof(expected),file)==sizeof(expected));
    CHECK(!fseek(file,0,SEEK_END));long size=ftell(file);CHECK(size>(long)sizeof(expected));fclose(file);
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));thread_files_code();
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    unsigned pending=0,immediate=0;
    for(unsigned life=0;life<6;++life) {
        const uint32_t context=0x1b000,out=0x19800;uint32_t status=0,vm[7],heap=0;
        memset(memory+context,0,0x500);MEM32(context+64)=THREAD_FILES_CPU;
        MEM32(context+72)=sizeof(expected);MEM32(context+112)=((uint32_t)size+511)&~511u;
        MEM32(context+48)=life==3?8:life&1?0:0x20;MEM32(context+104)=context+128;
        MEM32(context+184)=0xa5a55a5a;
        if(life&1) {
            uint32_t inputs[]={0,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE,0};
            CHECK(!pilot_io_device_vm(184,memory,inputs,vm,&status) && !status);heap=vm[0];
        }
        uint32_t buffer=heap?heap:0x1c800;MEM32(context+68)=buffer;
        memset(memory+buffer,0xcd,sizeof(expected));
        char path[1024];snprintf(path,sizeof(path),"%s\\default.xbe",PILOT_FILES_TITLE_ROOT);
        const uint32_t attribute=context+0x200,name=context+0x220,text=context+0x240;
        MEM32(attribute)=0;MEM32(attribute+4)=name;MEM32(attribute+8)=0x40;
        test_string(name,text,path);MEM32(context+96)=attribute;
        snprintf(path,sizeof(path),"%s\\xlle-thread-missing.bin",PILOT_FILES_TITLE_ROOT);
        const uint32_t missing=context+0x3e0;
        MEM32(missing)=0;MEM32(missing+4)=context+0x400;MEM32(missing+8)=0x40;
        test_string(context+0x400,context+0x410,path);MEM32(context+100)=missing;
        uint32_t event_create[]={out,0,1,1};CHECK(!event_lle_call(189,event_create));uint32_t event=MEM32(out);
        MEM32(context+32)=event;
        thread_cpu_create_join(THREAD_FILES_CPU,context,life&1);
        fprintf(stderr,"[FILE CPU RESULTS] life=%u open=%08x query=%08x read=%08x wait=%08x close=%08x eof=%08x poll=%08x missing=%08x iosb=%08x/%u\n",
            life,MEM32(context+12),MEM32(context+20),MEM32(context+16),MEM32(context+36),MEM32(context+24),
            MEM32(context+52),MEM32(context+56),MEM32(context+28),MEM32(context+40),MEM32(context+44));
        CHECK(!MEM32(context+12) && !MEM32(context+20) && !MEM32(context+24) && !MEM32(context+36));
        if(!(life&1))CHECK(!MEM32(context+16));
        else {
            CHECK(MEM32(context+16)==0 || MEM32(context+16)==0x103u);
            if(MEM32(context+16)==0x103u)++pending;else ++immediate;
        }
        CHECK(!MEM32(context+40) && MEM32(context+44)==sizeof(expected));
        CHECK(!memcmp(memory+buffer,expected,sizeof(expected)));
        CHECK(MEM64(context+168)==(uint64_t)size && MEM32(context+184)==0xa5a55a5a);
        CHECK(MEM32(context+52)==0xc0000011u && MEM32(context+56)==0x102u);
        CHECK(MEM32(context+60)==0xc0000008u && MEM32(context+28)==0xc0000034u);
        CHECK(!event_lle_call(187,&event));
        if(heap) {
            uint32_t inputs[]={heap,0,MEM_RELEASE,0,0};
            CHECK(!pilot_io_device_vm(199,memory,inputs,vm,&status) && !status);
        }
        CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
    }
    CHECK(thread_cpu_calls==6 && pending && immediate);
    fprintf(stderr,"PASS: six original CPU file lifetimes, sync/async real events, image/heap DMA bytes, metadata/EOF/missing/close errors and original handle baselines\n");
}
#undef FILES_BYTES
