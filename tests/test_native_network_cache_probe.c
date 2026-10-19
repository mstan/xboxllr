#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s error%lu\n",__LINE__,#x,GetLastError());ExitProcess(1);}}while(0)
typedef LONG (NTAPI *QueryFn)(HANDLE,IO_STATUS_BLOCK *,void *,ULONG,ULONG);
typedef LONG (NTAPI *OpenFn)(HANDLE *,ACCESS_MASK,OBJECT_ATTRIBUTES *,IO_STATUS_BLOCK *,ULONG,ULONG);
typedef LONG (NTAPI *CreateFn)(HANDLE *,ACCESS_MASK,OBJECT_ATTRIBUTES *,IO_STATUS_BLOCK *,LARGE_INTEGER *,ULONG,ULONG,ULONG,ULONG,void *,ULONG);
static QueryFn media_nt_query,real_query;
static OpenFn nt_open;
static CreateFn nt_create;
static SRWLOCK media_lock=SRWLOCK_INIT;
static HANDLE storage_roots[6];
static INIT_ONCE media_once=INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK media_start(PINIT_ONCE once,void *parameter,void **context)
{(void)once;(void)parameter;(void)context;return TRUE;}
static void stop(const char *why,uint32_t value)
{fprintf(stderr,"unexpected stop:%s %u\n",why,value);ExitProcess(2);}
/* Exact existing media_open_host NT transport, isolated from guest machinery. */
static uint32_t media_open_host(HANDLE parent,const WCHAR *path,uint32_t access,
    uint32_t share,uint32_t options,HANDLE *out,IO_STATUS_BLOCK *io)
{
    UNICODE_STRING name={(USHORT)(wcslen(path)*2),(USHORT)(wcslen(path)*2),(PWSTR)path};
    OBJECT_ATTRIBUTES attr={sizeof(attr),parent,&name,0x1040,NULL,NULL};
    return (uint32_t)nt_open(out,access,&attr,io,share,options);
}
#include "network_cache_probe.inc"
static unsigned injected;
static uint32_t query_failure=0xc0000022u;
static LONG NTAPI fail_query(HANDLE file,IO_STATUS_BLOCK *io,void *out,ULONG size,ULONG kind)
{
    if(kind==71) {++injected;return (LONG)query_failure;}
    return real_query(file,io,out,size,kind);
}
static LONG NTAPI sensitive_query(HANDLE file,IO_STATUS_BLOCK *io,void *out,ULONG size,ULONG kind)
{
    if(kind==71) {CHECK(size==4);*(uint32_t *)out=1;return 0;}
    return real_query(file,io,out,size,kind);
}
static LONG NTAPI final_failure(HANDLE file,IO_STATUS_BLOCK *io,void *out,ULONG size,ULONG kind)
{
    if(kind==5 && ++injected==3) {memset(out,0xab,size);return (LONG)query_failure;}
    return real_query(file,io,out,size,kind);
}
static LONG NTAPI null_open(HANDLE *out,ACCESS_MASK access,OBJECT_ATTRIBUTES *attributes,
    IO_STATUS_BLOCK *io,ULONG share,ULONG options)
{(void)access;(void)attributes;(void)io;(void)share;(void)options;*out=NULL;return 0;}
int main(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    HMODULE nt=GetModuleHandleW(L"ntdll.dll");
    media_nt_query=(QueryFn)(void *)GetProcAddress(nt,"NtQueryInformationFile");real_query=media_nt_query;
    nt_open=(OpenFn)(void *)GetProcAddress(nt,"NtOpenFile");CHECK(nt_open && media_nt_query);
    nt_create=(CreateFn)(void *)GetProcAddress(nt,"NtCreateFile");CHECK(nt_create);
    WCHAR temporary[512],root[512],cache[512],file[512],link[512];
    DWORD temporary_length=GetTempPathW(512,temporary);CHECK(temporary_length && temporary_length<400);
    CHECK(swprintf(root,512,L"%lscache-probe-%lu",temporary,GetCurrentProcessId())>0);
    CHECK(CreateDirectoryW(root,NULL));
    CHECK(swprintf(cache,512,L"%ls\\cAcHe",root)>0);
    CHECK(swprintf(file,512,L"%ls\\localcache08.BIN",cache)>0);
    CHECK(swprintf(link,512,L"%ls\\junction",root)>0);
    /* CreateFileW may add READ_ATTRIBUTES internally. Use the actual mounted
     * partition's NT creation transport, including its access/share/options. */
    HANDLE fixture_parent=CreateFileW(temporary,FILE_LIST_DIRECTORY|FILE_TRAVERSE|SYNCHRONIZE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
    CHECK(fixture_parent!=INVALID_HANDLE_VALUE);
    WCHAR *relative=root+temporary_length;
    UNICODE_STRING mount_name={(USHORT)(wcslen(relative)*2),(USHORT)(wcslen(relative)*2),relative};
    OBJECT_ATTRIBUTES mount_attributes={sizeof(mount_attributes),fixture_parent,&mount_name,0x1040,NULL,NULL};
    IO_STATUS_BLOCK mount_io={0};uint32_t granted=0,case_flags=0;
    const ULONG old_access=FILE_LIST_DIRECTORY|FILE_TRAVERSE|SYNCHRONIZE;
    CHECK(!nt_create(&storage_roots[1],old_access,&mount_attributes,&mount_io,NULL,
        FILE_ATTRIBUTE_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE,3,0x21,NULL,0));
    CHECK(!media_nt_query(storage_roots[1],&mount_io,&granted,4,8) && !(granted&FILE_READ_ATTRIBUTES));
    CHECK((uint32_t)media_nt_query(storage_roots[1],&mount_io,&case_flags,4,71)==0xc0000022u);
    CHECK(network_cache_probe(NULL,1,"CACHE\\LocalCache08.bin")==0xc0000022u);
    CHECK(CloseHandle(storage_roots[1]));storage_roots[1]=NULL;
    CHECK(!nt_create(&storage_roots[1],old_access|FILE_READ_ATTRIBUTES,&mount_attributes,&mount_io,NULL,
        FILE_ATTRIBUTE_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE,3,0x21,NULL,0));
    CHECK(!media_nt_query(storage_roots[1],&mount_io,&granted,4,8) && (granted&FILE_READ_ATTRIBUTES));
    CHECK(!media_nt_query(storage_roots[1],&mount_io,&case_flags,4,71) && !case_flags);
    CHECK(CloseHandle(fixture_parent));
    DWORD before,after;CHECK(GetProcessHandleCount(GetCurrentProcess(),&before));
    CHECK(network_cache_probe(NULL,2,"CACHE\\LocalCache08.bin")==0xc0000008u);
    CHECK(network_cache_probe(NULL,1,"CACHE\\LocalCache08.bin")==0xc0000034u);
    CHECK(CreateDirectoryW(cache,NULL));
    CHECK(network_cache_probe(NULL,1,"CACHE\\LocalCache08.bin")==0xc0000034u);
    HANDLE created=CreateFileW(file,GENERIC_WRITE,7,NULL,CREATE_NEW,0,NULL);CHECK(created!=INVALID_HANDLE_VALUE);CHECK(CloseHandle(created));
    for(unsigned i=0;i<32;++i)CHECK(network_cache_probe(NULL,1,"CACHE\\LocalCache08.bin")==0);
    CHECK(network_cache_probe(NULL,1,"CACHE")==0xc00000bau);
    uint32_t status=network_cache_probe(NULL,1,"CACHE\\LocalCache08.bin\\child");CHECK(status && status!=0xc0000034u && status!=0xc000003au);
    const char *bad[]={"..\\x","CACHE\\..\\x","CACHE\\x:stream","\\CACHE\\x","CACHE//x","CACHE\\","CACHE\\.\\x"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i)CHECK(network_cache_probe(NULL,1,bad[i])==0xc0000033u);
    media_nt_query=fail_query;CHECK(network_cache_probe(NULL,1,"CACHE\\LocalCache08.bin")==0xc0000022u && injected==1);
    media_nt_query=sensitive_query;CHECK(network_cache_probe(NULL,1,"CACHE\\LocalCache08.bin")==0xc00000bbu);media_nt_query=real_query;
    injected=0;media_nt_query=final_failure;
    CHECK(network_cache_probe(NULL,1,"CACHE\\LocalCache08.bin")==0xc0000022u && injected==3);media_nt_query=real_query;
    const uint32_t misleading[]={0xc0000034u,0xc000003au};
    for(unsigned i=0;i<2;++i) {
        query_failure=misleading[i];injected=0;media_nt_query=fail_query;
        CHECK(network_cache_probe(NULL,1,"CACHE\\LocalCache08.bin")==0xc0000184u && injected==1);
        injected=0;media_nt_query=final_failure;
        CHECK(network_cache_probe(NULL,1,"CACHE\\LocalCache08.bin")==0xc0000184u && injected==3);
    }
    query_failure=0xc0000022u;media_nt_query=real_query;
    OpenFn real_open=nt_open;nt_open=null_open;CHECK(network_cache_probe(NULL,1,"CACHE\\LocalCache08.bin")==0xc0000008u);nt_open=real_open;
    /* Actual NTFS junction: directory mount points need no symlink privilege. */
    CHECK(CreateDirectoryW(link,NULL));
    HANDLE junction=CreateFileW(link,GENERIC_WRITE,7,NULL,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    CHECK(junction!=INVALID_HANDLE_VALUE);
    WCHAR target[600];CHECK(swprintf(target,600,L"\\??\\%ls",cache)>0);
    union {uint64_t alignment;unsigned char bytes[4096];} reparse={0};
    uint32_t tag=0xa0000003u;uint16_t sub=(uint16_t)(wcslen(target)*2),print=(uint16_t)(wcslen(cache)*2);
    uint16_t data=(uint16_t)(8+sub+2+print+2),print_offset=(uint16_t)(sub+2);
    memcpy(reparse.bytes,&tag,4);memcpy(reparse.bytes+4,&data,2);
    memcpy(reparse.bytes+10,&sub,2);memcpy(reparse.bytes+12,&print_offset,2);memcpy(reparse.bytes+14,&print,2);
    memcpy(reparse.bytes+16,target,sub);memcpy(reparse.bytes+16+print_offset,cache,print);
    DWORD returned=0;CHECK(DeviceIoControl(junction,0x000900a4u,reparse.bytes,(DWORD)data+8,NULL,0,&returned,NULL));
    CHECK(CloseHandle(junction));
    status=network_cache_probe(NULL,1,"junction\\LocalCache08.bin");CHECK(status && status!=0xc0000034u && status!=0xc000003au);
    CHECK(RemoveDirectoryW(link));
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&after) && before==after);
    CHECK(CloseHandle(storage_roots[1]));CHECK(DeleteFileW(file));CHECK(RemoveDirectoryW(cache));CHECK(RemoveDirectoryW(root));
    puts("native mounted cache probe PASS: real NTFS absence/case/file/directory/reparse; errors fail closed; balanced handles");
    return 0;
}
