/* Authored empty-board ABI/namespace fixture; original oracle is separate. */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
static unsigned char memory[65536],before[65536];
#define MEM32(a) (*(uint32_t *)(memory+(uint32_t)(a)))
#define MEM16(a) (*(uint16_t *)(memory+(uint32_t)(a)))
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);ExitProcess(1);}}while(0)
static uint32_t g_esp,g_eax;
static jmp_buf guard;
static int expecting_guard;
static void stop(const char *reason,uint32_t value)
{
    if(!expecting_guard){fprintf(stderr,"unexpected guard: %s/%u\n",reason,value);ExitProcess(2);}
    longjmp(guard,1);
}
#define EXPECT_GUARD(call) do{memcpy(before,memory,sizeof(memory));uint32_t saved_esp=g_esp,saved_eax=g_eax;expecting_guard=1;if(!setjmp(guard)){call;CHECK(0);}expecting_guard=0;CHECK(!memcmp(before,memory,sizeof(memory)) && saved_esp==g_esp && saved_eax==g_eax);}while(0)
#define PILOT_OPTICAL_EMPTY_NATIVE 1
#include "optical_empty.inc"
#define MEDIA_INVALID 0xc000000du
#define MEDIA_BAD_HANDLE 0xc0000008u
static SRWLOCK media_lock=SRWLOCK_INIT;
#include "namespace.inc"
static const char *test_device="\\Device\\Harddisk0\\Partition2";
#define PILOT_MEDIA_HOST 1
#define PILOT_TITLE_DEVICE test_device
#include "media_optical.inc"
#define MEDIA_NO_MEDIA 0xc0000013u
static HANDLE media_root=(HANDLE)(uintptr_t)0x1111,storage_roots[6];
/* This fixture uses absolute paths; handle-relative ownership is not mocked
 * into success and is outside these regression cases. */
static HANDLE media_handle(uint32_t id){(void)id;return NULL;}
static unsigned media_partition(uint32_t id){(void)id;return 0;}
#include "media_path.inc"
static void frame(uint32_t state,uint32_t changes)
{
    g_esp=0xf000;g_eax=0x12345678;MEM32(g_esp)=0x13579;
    MEM32(g_esp+4)=state;MEM32(g_esp+8)=changes;
}
static uint32_t name(const char *text)
{
    size_t length=strlen(text);CHECK(length<1024);
    MEM16(0x1000)=(uint16_t)length;MEM16(0x1002)=(uint16_t)length;
    MEM32(0x1004)=0x1100;memcpy(memory+0x1100,text,length);return 0x1000;
}
static void path_input(const char *text)
{
    MEM32(0x3000)=0;MEM32(0x3004)=name(text);MEM32(0x3008)=0x40;
}
static void reject_path(const char *text)
{
    WCHAR output[1024];memset(output,0xcc,sizeof(output));
    HANDLE parent=(HANDLE)(uintptr_t)0x9876;unsigned partition=123;
    path_input(text);EXPECT_GUARD(media_path(0x3000,output,&parent,&partition));
    CHECK(parent==(HANDLE)(uintptr_t)0x9876 && partition==123);
    for(unsigned i=0;i<1024;++i)CHECK(output[i]==0xcccc);
}
int main(void)
{
    memset(memory,0xcd,sizeof(memory));frame(0x2000,0x2004);
    CHECK(!kernel_optical_is_empty());CHECK(!kernel_optical_dispatch(10));
    EXPECT_GUARD(kernel_optical_dispatch(9));
    name("\\Device\\CdRom0");EXPECT_GUARD(media_dismount_empty_optical(0x1000));
    kernel_optical_initialize();CHECK(kernel_optical_is_empty());
    EXPECT_GUARD(kernel_optical_initialize());
    for(unsigned i=0;i<3;++i){
        frame(0x2000,0x2004);CHECK(kernel_optical_dispatch(9));
        CHECK(!g_eax && g_esp==0xf00c && MEM32(0xf000)==0x13579);
        CHECK(MEM32(0x2000)==0x10 && MEM32(0x2004)==0);
        CHECK(MEM32(0x1ffc)==0xcdcdcdcd && MEM32(0x2008)==0xcdcdcdcd);
    }
    frame(0x2000,0);MEM32(0x2004)=0xcdcdcdcd;
    CHECK(kernel_optical_dispatch(9) && MEM32(0x2000)==0x10 && MEM32(0x2004)==0xcdcdcdcd);
    frame(0x2000,0x2000);CHECK(kernel_optical_dispatch(9) && MEM32(0x2000)==0);
    frame(0x2000,0xfffffffdu);EXPECT_GUARD(kernel_optical_dispatch(9));
    frame(0,0x2004);EXPECT_GUARD(kernel_optical_dispatch(9));
    frame(0xfffffffdu,0);EXPECT_GUARD(kernel_optical_dispatch(9));
    g_esp=0xfffffff8u;EXPECT_GUARD(kernel_optical_dispatch(9));frame(0x2000,0x2004);
    const char *names[]={"\\Device\\CdRom0","\\device\\cdrom0","D:",
        "\\??\\CdRom0:","\\??\\cdrom0:","\\??\\CDROM0:","O:"};
    CHECK(!namespace_create("\\??\\D:","\\Device\\CdRom0"));
    CHECK(namespace_find("\\??\\CdRom0:")==-1); /* Actual caller's missing name. */
    CHECK(!namespace_create("\\??\\O:","\\??\\CdRom0:"));
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i){uint32_t string=name(names[i]);memcpy(before,memory,sizeof(memory));
        CHECK(media_dismount_empty_optical(string)==0xc0000034u && !memcmp(before,memory,sizeof(memory)));}
    CHECK(namespace_find("\\??\\CdRom0:")==-1); /* Never manufacture an alias. */
    CHECK(!namespace_create("\\??\\CdRom0:","\\Device\\Harddisk0\\Partition2"));
    name("\\??\\cdrom0:");EXPECT_GUARD(media_dismount_empty_optical(0x1000));
    name("O:");EXPECT_GUARD(media_dismount_empty_optical(0x1000));
    namespace_links[namespace_find("\\??\\CdRom0:")].named=0;
    CHECK(!namespace_create("\\??\\CdRom0:","\\Device\\CdRom0"));
    CHECK(media_dismount_empty_optical(name("\\??\\CdRom0:"))==0xc0000034u);
    namespace_links[namespace_find("\\??\\CdRom0:")].named=0;
    CHECK(media_dismount_empty_optical(0)==MEDIA_INVALID);
    EXPECT_GUARD(media_dismount_empty_optical(0xfffffffcu));
    MEM16(0x1000)=16;MEM32(0x1004)=0xfffffff8u;
    EXPECT_GUARD(media_dismount_empty_optical(0x1000));
    uint32_t string=name("");CHECK(media_dismount_empty_optical(string)==0xc0000033u);
    name("\\Device\\CdRom0-absent");EXPECT_GUARD(media_dismount_empty_optical(0x1000));
    name("\\??\\CdRom00:");EXPECT_GUARD(media_dismount_empty_optical(0x1000));
    name("\\??\\CdRom0:\\asset.bin");EXPECT_GUARD(media_dismount_empty_optical(0x1000));
    name("\\Device\\Harddisk0\\Partition2");EXPECT_GUARD(media_dismount_empty_optical(0x1000));
    CHECK(!namespace_create("\\Device\\CdRom0","\\Device\\Harddisk0\\Partition2"));
    name("\\Device\\CdRom0");EXPECT_GUARD(media_dismount_empty_optical(0x1000));
    /* The production path resolver must reject optical ancestry BEFORE an
     * otherwise valid rewrite can erase it. Ordinary resolver stays intact. */
    char resolved[1024];CHECK(!namespace_resolve("D:\\asset.bin",resolved));
    CHECK(!_stricmp(resolved,"\\Device\\Harddisk0\\Partition2\\asset.bin"));
    reject_path("\\Device\\CdRom0\\asset.bin");
    reject_path("D:\\asset.bin");
    CHECK(!namespace_create("\\??\\E:","\\??\\D:"));reject_path("E:\\asset.bin");
    CHECK(!namespace_create("\\??\\C:","\\Device\\Harddisk0\\Partition2"));
    WCHAR path[1024];HANDLE parent=NULL;unsigned partition=123;
    path_input("C:\\asset.bin");CHECK(!media_path(0x3000,path,&parent,&partition));
    CHECK(!wcscmp(path,L"asset.bin") && parent==media_root && !partition);
    namespace_links[namespace_find("\\Device\\CdRom0")].named=0;
    test_device="\\Device\\CdRom0";EXPECT_GUARD(media_dismount_empty_optical(0x1000));
    name("\\??\\CdRom0:");EXPECT_GUARD(media_dismount_empty_optical(0x1000));
    test_device="\\Device\\CdRom0\\content";EXPECT_GUARD(media_dismount_empty_optical(0x1000));
    test_device="\\Device\\Harddisk0\\Partition2";
    EXPECT_GUARD(media_guard_empty_optical_path("\\device\\cdrom0\\asset.bin"));
    EXPECT_GUARD(media_guard_empty_optical_path("\\Device\\CdRom0"));
    media_guard_empty_optical_path("\\Device\\CdRom00\\asset.bin");
    media_guard_empty_optical_path(test_device);
    /* Namespace operations are real shared code, not substitute resolvers. */
    CHECK(!namespace_create("\\??\\loop","\\??\\loop"));
    CHECK(media_dismount_empty_optical(name("\\??\\loop"))==0xc0000039u);
    (void)namespace_close;(void)namespace_kernel;
    puts("PASS: empty optical original-shaped ABI, immutable owner, optional/alias outputs, absence and guarded content namespaces");
    return 0;
}
