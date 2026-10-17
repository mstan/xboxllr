/* MSVC host dispatch: language, access and thread-name exceptions still reach
 * their real SEH owner; an unhandled exception still reaches the final filter. */
#include <stdint.h>
#include <string.h>
#include "native_exception_diagnostic.inc"
static LONG WINAPI final_filter(EXCEPTION_POINTERS *exception)
{
    ExitProcess(exception->ExceptionRecord->ExceptionCode==0x20474343u?77:78);
}
int main(int argc,char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    install_native_language_exception_observer();
    if(argc==2 && !strcmp(argv[1],"unhandled")) {
        SetUnhandledExceptionFilter(final_filter);
        RaiseException(0x20474343u,0,0,NULL);return 3;
    }
    const DWORD codes[]={0x20474343u,0xe06d7363u,EXCEPTION_ACCESS_VIOLATION,0x406d1388u};
    unsigned caught=0;
    for(unsigned i=0;i<sizeof(codes)/sizeof(codes[0]);++i) {
        __try {RaiseException(codes[i],0,0,NULL);}
        __except(EXCEPTION_EXECUTE_HANDLER) {++caught;}
    }
    if(caught!=4 || native_language_exception_records!=2)return 1;
    char path[MAX_PATH],command[MAX_PATH+32];
    if(!GetModuleFileNameA(NULL,path,sizeof(path)))return 2;
    snprintf(command,sizeof(command),"\"%s\" unhandled",path);
    STARTUPINFOA startup;PROCESS_INFORMATION process;
    memset(&startup,0,sizeof(startup));memset(&process,0,sizeof(process));startup.cb=sizeof(startup);
    if(!CreateProcessA(path,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&process))return 2;
    DWORD status=WaitForSingleObject(process.hProcess,10000);
    if(status!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,79);
    DWORD exit_code=0;BOOL result=GetExitCodeProcess(process.hProcess,&exit_code);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    if(status!=WAIT_OBJECT_0 || !result || exit_code!=77)return 1;
    puts("PASS: language diagnostics preserve caught exceptions, access faults, thread names and final-filter dispatch");
    return 0;
}
