/* SPDX-License-Identifier: GPL-3.0-or-later
 * Link the established native-service table to POSIX implementations. The
 * historical service names are internal ABI keys; no Windows module is loaded.
 */
#include "winternl.h"
static LONG allocate(HANDLE process,void **address,ULONG_PTR zero_bits,SIZE_T *bytes,ULONG type,ULONG protect)
{
    if(process!=GetCurrentProcess() || !address || !bytes || !*bytes || zero_bits)return (LONG)0xc000000du;
    uintptr_t low=(uintptr_t)*address&~(uintptr_t)4095;size_t size=(*bytes+(uintptr_t)*address-low+4095)&~(size_t)4095;
    void *result=VirtualAlloc(*address,size,type,protect);if(!result)return GetLastError()==ERROR_NOT_ENOUGH_MEMORY?(LONG)0xc0000017u:(LONG)0xc000000du;
    *address=result;*bytes=size;return 0;
}
static LONG release(HANDLE process,void **address,SIZE_T *bytes,ULONG type)
{
    if(process!=GetCurrentProcess() || !address || !bytes)return (LONG)0xc000000du;
    uintptr_t low=(uintptr_t)*address&~(uintptr_t)4095;size_t size=*bytes?(*bytes+(uintptr_t)*address-low+4095)&~(size_t)4095:0;
    if(!VirtualFree((void*)low,size,type))return (LONG)0xc000000du;
    if(type==MEM_RELEASE){*address=NULL;*bytes=0;}else {*address=(void*)low;*bytes=size;}return 0;
}
static LONG protect(HANDLE process,void **address,SIZE_T *bytes,ULONG access,PULONG previous)
{
    if(process!=GetCurrentProcess() || !address || !bytes || !*bytes || !previous)return (LONG)0xc000000du;
    uintptr_t low=(uintptr_t)*address&~(uintptr_t)4095;size_t size=(*bytes+(uintptr_t)*address-low+4095)&~(size_t)4095;
    if(!VirtualProtect((void*)low,size,access,previous))return (LONG)0xc000000du;*address=(void*)low;*bytes=size;return 0;
}
static void to_fields(const LARGE_INTEGER *time,void *out)
{
    FILETIME ft={(DWORD)time->QuadPart,(DWORD)((uint64_t)time->QuadPart>>32)};SYSTEMTIME system;
    if(!FileTimeToSystemTime(&ft,&system)){memset(out,0,16);return;}
    int16_t fields[]={system.wYear,system.wMonth,system.wDay,system.wHour,system.wMinute,system.wSecond,system.wMilliseconds,system.wDayOfWeek};memcpy(out,fields,sizeof(fields));
}
static BOOLEAN to_time(const void *source,LARGE_INTEGER *time)
{
    int16_t fields[8];memcpy(fields,source,sizeof(fields));
    for(unsigned i=0;i<7;++i)if(fields[i]<0)return FALSE;
    SYSTEMTIME system={(WORD)fields[0],(WORD)fields[1],0,(WORD)fields[2],(WORD)fields[3],(WORD)fields[4],(WORD)fields[5],(WORD)fields[6]};FILETIME ft;
    if(!SystemTimeToFileTime(&system,&ft))return FALSE;time->QuadPart=(int64_t)(((uint64_t)ft.dwHighDateTime<<32)|ft.dwLowDateTime);return TRUE;
}
HMODULE GetModuleHandleW(LPCWSTR name)
{if(!name || !wcscmp(name,L"ntdll.dll"))return (HMODULE)(uintptr_t)1;SetLastError(ERROR_NOT_SUPPORTED);return NULL;}
void *GetProcAddress(HMODULE module,LPCSTR name)
{
    if(module!=(HMODULE)(uintptr_t)1 || !name)return NULL;
#define SERVICE(key,function) if(!strcmp(name,key))return (void*)(uintptr_t)(function)
    SERVICE("NtAllocateVirtualMemory",allocate);SERVICE("NtFreeVirtualMemory",release);SERVICE("NtProtectVirtualMemory",protect);
    SERVICE("NtCreateFile",pilot_posix_nt_create);SERVICE("NtOpenFile",pilot_posix_nt_open);
    SERVICE("NtReadFile",pilot_posix_nt_read);SERVICE("NtWriteFile",pilot_posix_nt_write);
    SERVICE("NtQueryInformationFile",pilot_posix_nt_query);SERVICE("NtSetInformationFile",pilot_posix_nt_set);
    SERVICE("NtQueryVolumeInformationFile",pilot_posix_nt_volume);SERVICE("NtFlushBuffersFile",pilot_posix_nt_flush);SERVICE("NtQueryDirectoryFile",pilot_posix_nt_directory);
    SERVICE("NtCreateEvent",pilot_posix_nt_create_event);SERVICE("NtSetEvent",pilot_posix_nt_set_event);SERVICE("NtResetEvent",pilot_posix_nt_reset_event);SERVICE("NtQueryEvent",pilot_posix_nt_query_event);
    SERVICE("NtWaitForSingleObject",pilot_posix_nt_wait);SERVICE("NtWaitForMultipleObjects",pilot_posix_nt_wait_many);SERVICE("NtSignalAndWaitForSingleObject",pilot_posix_nt_signal_wait);SERVICE("NtDelayExecution",pilot_posix_nt_delay);
    SERVICE("NtCreateMutant",pilot_posix_nt_create_mutant);SERVICE("NtReleaseMutant",pilot_posix_nt_release_mutant);SERVICE("NtQueryMutant",pilot_posix_nt_query_mutant);
    SERVICE("RtlTimeToTimeFields",to_fields);SERVICE("RtlTimeFieldsToTime",to_time);
#undef SERVICE
    SetLastError(ERROR_NOT_SUPPORTED);return NULL;
}
