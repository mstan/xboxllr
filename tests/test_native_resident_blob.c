/* SDK19041 emits C5105 in winbase.h with MSVC1929. Keep /W4 /WX on
 * authored code while scoping that SDK-only diagnostic to its own include. */
#if defined(_MSC_VER) && _MSC_VER==1929
#pragma warning(push)
#pragma warning(disable:5105)
#include <windows.h>
#pragma warning(pop)
#else
#include <windows.h>
#endif
/* Test-only map admission faults. The prefault case uses a real inaccessible
 * mapped page; no production fault-injection switches are introduced. */
#if defined(_MSC_VER)
static unsigned test_map_fault;
static LPVOID test_map_view(HANDLE mapping,DWORD access,DWORD high,DWORD low,SIZE_T count)
{
    if(test_map_fault==1){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return NULL;}
    LPVOID bytes=MapViewOfFile(mapping,access,high,low,count);
    if(bytes && test_map_fault==2){DWORD previous;if(!VirtualProtect(bytes,count,PAGE_NOACCESS,&previous)){UnmapViewOfFile(bytes);return NULL;}}
    return bytes;
}
#define MapViewOfFile test_map_view
#endif
#include "../pilots/native-title/resident_blob.h"
#undef MapViewOfFile
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"resident blob line %d: %s winerror=%lu\n",__LINE__,#x,GetLastError());exit(1);}}while(0)
static unsigned char expected(uint64_t i)
{i&=65535;return (unsigned char)((i*37)^(i>>8)^0xa5);}
static void fixture(const char *path,uint64_t size)
{
    HANDLE file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);CHECK(file!=INVALID_HANDLE_VALUE);
    unsigned char pattern[65536];for(unsigned i=0;i<sizeof(pattern);++i)pattern[i]=expected(i);
    while(size){DWORD written=0,count=(DWORD)(size>sizeof(pattern)?sizeof(pattern):size);CHECK(WriteFile(file,pattern,count,&written,NULL) && written==count);size-=count;}
    CHECK(CloseHandle(file));
}
static HANDLE open_fixture(const char *path)
{HANDLE file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,NULL);CHECK(file!=INVALID_HANDLE_VALUE);return file;}
static uint64_t cpu_ticks(void)
{FILETIME c,e,k,u;CHECK(GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u));return ((uint64_t)k.dwHighDateTime<<32)+k.dwLowDateTime+((uint64_t)u.dwHighDateTime<<32)+u.dwLowDateTime;}
static uint64_t thread_cycles(void)
{ULONG64 value=0;CHECK(QueryThreadCycleTime(GetCurrentThread(),&value));return (uint64_t)value;}
static PROCESS_MEMORY_COUNTERS_EX usage(void)
{PROCESS_MEMORY_COUNTERS_EX value={0};value.cb=sizeof(value);CHECK(GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&value,sizeof(value)));return value;}
#if PILOT_RESIDENT_FILE_VIEWS_AVAILABLE
static void fault_contract(void)
{
    unsigned char *page=VirtualAlloc(NULL,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);CHECK(page);
    EXCEPTION_RECORD record={0};EXCEPTION_POINTERS exception={&record,NULL};
    record.ExceptionCode=EXCEPTION_IN_PAGE_ERROR;record.NumberParameters=3;record.ExceptionInformation[1]=(ULONG_PTR)page;
    CHECK(pilot_resident_blob_fault(&exception,page,8192)==EXCEPTION_EXECUTE_HANDLER);
    record.ExceptionCode=EXCEPTION_ACCESS_VIOLATION;
    CHECK(pilot_resident_blob_fault(&exception,page,8192)==EXCEPTION_EXECUTE_HANDLER);
    record.ExceptionInformation[0]=1;CHECK(pilot_resident_blob_fault(&exception,page,8192)==EXCEPTION_CONTINUE_SEARCH);
    record.ExceptionInformation[0]=0;record.ExceptionInformation[1]=(ULONG_PTR)(page+8192);
    CHECK(pilot_resident_blob_fault(&exception,page,8192)==EXCEPTION_CONTINUE_SEARCH);
    record.ExceptionInformation[1]=(ULONG_PTR)page;record.ExceptionCode=EXCEPTION_INT_DIVIDE_BY_ZERO;
    CHECK(pilot_resident_blob_fault(&exception,page,8192)==EXCEPTION_CONTINUE_SEARCH);
    DWORD previous;CHECK(VirtualProtect(page+4096,4096,PAGE_NOACCESS,&previous));
    CHECK(!pilot_resident_blob_prefault(page,8192));
    PilotResidentBlob inaccessible={page,8192,1};unsigned char output[32];
    CHECK(!pilot_resident_blob_copy(&inaccessible,4090,output,sizeof(output)));
    CHECK(VirtualFree(page,0,MEM_RELEASE));
}
#endif
static void benchmark(const char *path,int mode)
{
    HANDLE file=open_fixture(path);LARGE_INTEGER size,frequency,a,b;CHECK(GetFileSizeEx(file,&size) && size.QuadPart>0);CHECK(QueryPerformanceFrequency(&frequency));
    PilotResidentBlob blob={0};PROCESS_MEMORY_COUNTERS_EX before=usage();uint64_t cpu=cpu_ticks();
    CHECK(QueryPerformanceCounter(&a));uint64_t c=thread_cycles();
    CHECK(pilot_resident_blob_load(file,(uint64_t)size.QuadPart,mode,&blob));
    uint64_t d=thread_cycles();CHECK(QueryPerformanceCounter(&b));cpu=cpu_ticks()-cpu;
    PROCESS_MEMORY_COUNTERS_EX loaded=usage();CHECK(d>=c);
    unsigned char byte=0;uint64_t witness=0;
    for(uint64_t at=0;at<blob.size;at+=4096){CHECK(pilot_resident_blob_copy(&blob,at,&byte,1) && byte==expected(at));witness+=byte;}
    CHECK(pilot_resident_blob_copy(&blob,blob.size-1,&byte,1) && byte==expected(blob.size-1));
    printf("{\"mode\":%d,\"views_available\":%d,\"mapped\":%u,\"bytes\":%llu,\"load_wall_ms\":%.6f,\"load_cpu_ms\":%.6f,\"load_thread_cycles\":%llu,\"private_before\":%llu,\"private_loaded\":%llu,\"working_before\":%llu,\"working_loaded\":%llu,\"witness\":%llu}\n",
        mode,PILOT_RESIDENT_FILE_VIEWS_AVAILABLE,blob.mapped,(unsigned long long)blob.size,
        1000.0*(double)(b.QuadPart-a.QuadPart)/(double)frequency.QuadPart,(double)cpu/10000.0,(unsigned long long)(d-c),
        (unsigned long long)before.PrivateUsage,(unsigned long long)loaded.PrivateUsage,
        (unsigned long long)before.WorkingSetSize,(unsigned long long)loaded.WorkingSetSize,(unsigned long long)witness);
    pilot_resident_blob_release(&blob);CHECK(!blob.bytes && !blob.size && !blob.mapped);CHECK(CloseHandle(file));
}
int main(int argc,char **argv)
{
    if(argc==4 && !strcmp(argv[1],"--fixture")){fixture(argv[2],strtoull(argv[3],NULL,10));return 0;}
    if(argc==4 && !strcmp(argv[1],"--bench")){benchmark(argv[3],atoi(argv[2]));return 0;}
    CHECK(argc==1);
#if PILOT_RESIDENT_FILE_VIEWS_AVAILABLE
    fault_contract();
#endif
    char temporary[MAX_PATH],path[MAX_PATH];CHECK(GetTempPathA(MAX_PATH,temporary));CHECK(GetTempFileNameA(temporary,"xrv",0,path));
    const uint64_t size=32785;fixture(path,size);
    for(int mode=0;mode<2;++mode){
        HANDLE file=open_fixture(path);PilotResidentBlob blob={0};CHECK(pilot_resident_blob_load(file,size,mode,&blob));
        CHECK(blob.size==size && blob.mapped==(unsigned)(mode && PILOT_RESIDENT_FILE_VIEWS_AVAILABLE));
        MEMORY_BASIC_INFORMATION info;CHECK(VirtualQuery(blob.bytes,&info,sizeof(info))==sizeof(info));
        CHECK(info.Type==(DWORD)(blob.mapped?MEM_MAPPED:MEM_PRIVATE) && info.Protect==PAGE_READONLY);
        unsigned char output[32785];CHECK(pilot_resident_blob_copy(&blob,0,output,sizeof(output)));
        for(unsigned i=0;i<sizeof(output);++i)CHECK(output[i]==expected(i));
        CHECK(!pilot_resident_blob_copy(&blob,size-1,output,2));CHECK(!pilot_resident_blob_copy(&blob,size+1,output,0));
        CHECK(!pilot_resident_blob_copy(&blob,0,NULL,1));CHECK(pilot_resident_blob_copy(&blob,size,NULL,0));
        CHECK(!pilot_resident_blob_load(file,size,mode,&blob));
        HANDLE writer=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(writer==INVALID_HANDLE_VALUE && GetLastError()==ERROR_SHARING_VIOLATION);
        pilot_resident_blob_release(&blob);pilot_resident_blob_release(&blob);CHECK(!blob.bytes && !blob.size && !blob.mapped);
        LARGE_INTEGER borrowed_size;CHECK(GetFileSizeEx(file,&borrowed_size) && (uint64_t)borrowed_size.QuadPart==size);CHECK(CloseHandle(file));
    }
#if PILOT_RESIDENT_FILE_VIEWS_AVAILABLE
    for(test_map_fault=1;test_map_fault<=2;++test_map_fault){
        HANDLE file=open_fixture(path);PilotResidentBlob blob={0};
        CHECK(pilot_resident_blob_load(file,size,1,&blob) && !blob.mapped && blob.size==size);
        unsigned char output[32785];CHECK(pilot_resident_blob_copy(&blob,0,output,sizeof(output)));
        for(unsigned i=0;i<sizeof(output);++i)CHECK(output[i]==expected(i));
        pilot_resident_blob_release(&blob);CHECK(CloseHandle(file));
    }
    test_map_fault=0;
#endif
    CHECK(DeleteFileA(path));
    PilotResidentBlob invalid={0};CHECK(!pilot_resident_blob_load(INVALID_HANDLE_VALUE,1,1,&invalid));CHECK(!invalid.bytes);
    printf("PASS immutable resident data: exact bytes, read-only ownership, bounds, retained writer denial, release and %s path\n",PILOT_RESIDENT_FILE_VIEWS_AVAILABLE?"MSVC mapped":"private-copy fallback");
    return 0;
}
