/* First-chance language-exception diagnostics only. Never consume an exception
 * or call an emulated service. Unicorn's lazy code-page faults and genuine
 * native/guest access faults keep their existing handler order. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
static volatile LONG native_language_exception_records;
static LONG CALLBACK native_language_exception_observer(EXCEPTION_POINTERS *exception)
{
    DWORD code=exception->ExceptionRecord->ExceptionCode;
    if(code!=0x20474343u && code!=0xe06d7363u)return EXCEPTION_CONTINUE_SEARCH;
    LONG sequence=InterlockedIncrement(&native_language_exception_records);
    if(sequence<=64) {
        char line[4096];void *frames[16];unsigned used=0;
        int n=snprintf(line,sizeof(line),
            "[HOST LANGUAGE EXCEPTION] first_chance=1 sequence=%ld code=%08lx thread=%lu pc=%p parameters=%lu",
            sequence,(unsigned long)code,(unsigned long)GetCurrentThreadId(),
            exception->ExceptionRecord->ExceptionAddress,
            (unsigned long)exception->ExceptionRecord->NumberParameters);
        if(n>0 && (unsigned)n<sizeof(line))used=(unsigned)n;
        for(unsigned i=0;i<exception->ExceptionRecord->NumberParameters && i<4;++i) {
            n=snprintf(line+used,sizeof(line)-used," p%u=%llx",i,
                (unsigned long long)exception->ExceptionRecord->ExceptionInformation[i]);
            if(n<0 || (unsigned)n>=sizeof(line)-used)break;
            used+=(unsigned)n;
        }
        USHORT count=CaptureStackBackTrace(0,16,frames,NULL);
        for(unsigned i=0;i<count && sizeof(line)-used>MAX_PATH+96;++i) {
            HMODULE module=NULL;char name[MAX_PATH]={0};
            if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)frames[i],&module))
                GetModuleFileNameA(module,name,sizeof(name)-1);
            n=snprintf(line+used,sizeof(line)-used,"\n[HOST LANGUAGE FRAME] index=%u pc=%p module=%s rva=%llx",
                i,frames[i],name,(unsigned long long)((uintptr_t)frames[i]-(uintptr_t)module));
            if(n<0 || (unsigned)n>=sizeof(line)-used)break;
            used+=(unsigned)n;
        }
        if(used+1<sizeof(line))line[used++]='\n';
        DWORD written=0;HANDLE output=GetStdHandle(STD_ERROR_HANDLE);
        if(output && output!=INVALID_HANDLE_VALUE)WriteFile(output,line,used,&written,NULL);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static void install_native_language_exception_observer(void)
{
    if(!AddVectoredExceptionHandler(1,native_language_exception_observer))
        fprintf(stderr,"[HOST LANGUAGE EXCEPTION] observer_unavailable=%lu\n",(unsigned long)GetLastError());
}
