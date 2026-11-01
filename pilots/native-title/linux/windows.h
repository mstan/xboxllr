/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native Linux host primitives for the existing title service interfaces.
 * DXVK Native supplies only the D3D/COM type vocabulary. All OS operations
 * declared here execute through POSIX, SDL and OpenSSL in native ELF code.
 */
#pragma once
#include <windows_base.h>
#include <unknwn.h>
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <strings.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#define CALLBACK
#define NTAPI
#define __cdecl
#define __fastcall
#define __declspec(x)
#define MAX_PATH 4096
#define INFINITE UINT32_MAX
#define WAIT_IO_COMPLETION 0xc0u
#define CREATE_SUSPENDED 4u
#define STILL_ACTIVE 259u
#define ERROR_SUCCESS 0u
#define ERROR_INVALID_FUNCTION 1u
#define ERROR_FILE_NOT_FOUND 2u
#define ERROR_PATH_NOT_FOUND 3u
#define ERROR_ACCESS_DENIED 5u
#define ERROR_INVALID_HANDLE 6u
#define ERROR_NOT_ENOUGH_MEMORY 8u
#define ERROR_INVALID_DATA 13u
#define ERROR_NOT_SUPPORTED 50u
#define ERROR_INVALID_PARAMETER 87u
#define ERROR_INSUFFICIENT_BUFFER 122u
#define ERROR_ALREADY_EXISTS 183u
#define ERROR_NO_MORE_FILES 18u
#define ERROR_TIMEOUT 1460u
#define ERROR_DEVICE_NOT_CONNECTED 1167u
#define ERROR_SHARING_VIOLATION 32u
#define ERROR_WRITE_FAULT 29u
#define ERROR_DISK_FULL 112u
#define ERROR_OUTOFMEMORY 14u
#define ERROR_ALREADY_INITIALIZED 1247u
#define MEM_COMMIT 0x1000u
#define MEM_RESERVE 0x2000u
#define MEM_DECOMMIT 0x4000u
#define MEM_RELEASE 0x8000u
#define MEM_FREE 0x10000u
#define MEM_PRIVATE 0x20000u
#define MEM_TOP_DOWN 0x100000u
#define PAGE_NOACCESS 1u
#define PAGE_READONLY 2u
#define PAGE_READWRITE 4u
#define PAGE_WRITECOPY 8u
#define PAGE_EXECUTE 16u
#define PAGE_EXECUTE_READ 32u
#define PAGE_EXECUTE_READWRITE 64u
#define PAGE_EXECUTE_WRITECOPY 128u
#define PAGE_GUARD 256u
#define PAGE_NOCACHE 512u
#define PAGE_WRITECOMBINE 1024u
#define CP_UTF8 65001u
#define MB_ERR_INVALID_CHARS 8u
#define WC_ERR_INVALID_CHARS 128u
#define HEAP_ZERO_MEMORY 8u
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define GENERIC_ALL 0x10000000u
#define MAXIMUM_ALLOWED 0x02000000u
#define WRITE_DAC 0x40000u
#define WRITE_OWNER 0x80000u
#define DELETE 0x10000u
#define SYNCHRONIZE 0x100000u
#define FILE_READ_DATA 1u
#define FILE_LIST_DIRECTORY 1u
#define FILE_WRITE_DATA 2u
#define FILE_ADD_FILE 2u
#define FILE_APPEND_DATA 4u
#define FILE_ADD_SUBDIRECTORY 4u
#define FILE_READ_EA 8u
#define FILE_WRITE_EA 16u
#define FILE_EXECUTE 32u
#define FILE_TRAVERSE 32u
#define FILE_READ_ATTRIBUTES 128u
#define FILE_WRITE_ATTRIBUTES 256u
#define FILE_GENERIC_READ 0x120089u
#define FILE_GENERIC_WRITE 0x120116u
#define FILE_SHARE_READ 1u
#define FILE_SHARE_WRITE 2u
#define FILE_SHARE_DELETE 4u
#define FILE_ATTRIBUTE_READONLY 1u
#define FILE_ATTRIBUTE_HIDDEN 2u
#define FILE_ATTRIBUTE_SYSTEM 4u
#define FILE_ATTRIBUTE_DIRECTORY 16u
#define FILE_ATTRIBUTE_ARCHIVE 32u
#define FILE_ATTRIBUTE_NORMAL 128u
#define FILE_ATTRIBUTE_DEVICE 64u
#define FILE_ATTRIBUTE_REPARSE_POINT 1024u
#define INVALID_FILE_ATTRIBUTES UINT32_MAX
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000u
#define FILE_FLAG_OPEN_REPARSE_POINT 0x00200000u
#define FILE_FLAG_OVERLAPPED 0x40000000u
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000u
#define FILE_FLAG_NO_BUFFERING 0x20000000u
#define FILE_FLAG_WRITE_THROUGH 0x80000000u
#define CREATE_NEW 1u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define TRUNCATE_EXISTING 5u
#define FILE_BEGIN 0u
#define FILE_CURRENT 1u
#define FILE_END 2u
#define FILE_NAME_NORMALIZED 0u
#define FILE_MAP_READ 4u
#define VK_BACK 8u
#define VK_RETURN 13u
#define VK_SHIFT 16u
#define VK_SPACE 32u
#define VK_LEFT 37u
#define VK_UP 38u
#define VK_RIGHT 39u
#define VK_DOWN 40u
#define MOVEFILE_REPLACE_EXISTING 1u
#define MOVEFILE_WRITE_THROUGH 8u
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_IDLE (-15)
#define THREAD_PRIORITY_LOWEST (-2)
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_SET_CONTEXT 16u
#define THREAD_PRIORITY_ERROR_RETURN INT32_MAX
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define CONDITION_VARIABLE_LOCKMODE_SHARED 1u
#define TIMER_ALL_ACCESS 0x1f0003u
#define EVENT_ALL_ACCESS 0x1f0003u
#define EVENT_MODIFY_STATE 2u
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 2u
#define STD_ERROR_HANDLE ((DWORD)-12)
#define SEM_FAILCRITICALERRORS 1u
#define SEM_NOGPFAULTERRORBOX 2u
#define SEM_NOOPENFILEERRORBOX 0x8000u
#define E_UNEXPECTED ((HRESULT)0x8000ffffu)
#define HRESULT_FROM_WIN32(x) ((HRESULT)((x)?((x)&0xffffu)|0x80070000u:0))
#define FIELD_OFFSET(t,m) ((LONG)offsetof(t,m))
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MemoryBarrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#if defined(__x86_64__) || defined(__i386__)
#define YieldProcessor() __asm__ __volatile__("pause")
#elif defined(__aarch64__)
#define YieldProcessor() __asm__ __volatile__("yield")
#else
#define YieldProcessor() __atomic_signal_fence(__ATOMIC_SEQ_CST)
#endif
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _wcsicmp wcscasecmp
#define _wcsnicmp wcsncasecmp
#define _strdup strdup
#define _wcsdup wcsdup
#define _fseeki64 fseeko
#define _ftelli64 ftello
#define SecureZeroMemory(p,n) explicit_bzero((p),(n))
#define CopyMemory(d,s,n) memcpy((d),(s),(n))
typedef uint8_t BOOLEAN;
typedef int16_t INT16;
typedef int64_t LONG64;
typedef uint64_t DWORD64,ULONG64;
typedef uintptr_t DWORD_PTR;
typedef uint32_t ACCESS_MASK;
typedef LONG NTSTATUS;
typedef ULONG *PULONG;
typedef LONG *PLONG;
typedef DWORD *PDWORD;
typedef SIZE_T *PSIZE_T;
typedef HANDLE *PHANDLE;
typedef LARGE_INTEGER *PLARGE_INTEGER;
typedef const WCHAR *LPCWSTR;
typedef pthread_mutex_t SRWLOCK;
typedef struct {pthread_mutex_t mutex;LONG RecursionCount;} CRITICAL_SECTION;
typedef SRWLOCK *PSRWLOCK;
typedef CRITICAL_SECTION *PCRITICAL_SECTION,*LPCRITICAL_SECTION;
typedef pthread_cond_t CONDITION_VARIABLE,*PCONDITION_VARIABLE;
#define SRWLOCK_INIT PTHREAD_MUTEX_INITIALIZER
#define CONDITION_VARIABLE_INIT PTHREAD_COND_INITIALIZER
typedef struct {pthread_mutex_t mutex;int done;} INIT_ONCE,*PINIT_ONCE;
#define INIT_ONCE_STATIC_INIT {PTHREAD_MUTEX_INITIALIZER,0}
typedef BOOL (*PINIT_ONCE_FN)(PINIT_ONCE,void*,void**);
typedef DWORD (*LPTHREAD_START_ROUTINE)(void*);
typedef void (*PAPCFUNC)(ULONG_PTR);
typedef struct {DWORD dwLowDateTime,dwHighDateTime;} FILETIME,*PFILETIME;
typedef struct {WORD wYear,wMonth,wDayOfWeek,wDay,wHour,wMinute,wSecond,wMilliseconds;} SYSTEMTIME;
typedef struct {void *BaseAddress,*AllocationBase;DWORD AllocationProtect;SIZE_T RegionSize;DWORD State,Protect,Type;} MEMORY_BASIC_INFORMATION,*PMEMORY_BASIC_INFORMATION;
typedef struct {DWORD dwPageSize,dwAllocationGranularity;} SYSTEM_INFO;
typedef struct {DWORD dwLength,dwMemoryLoad;uint64_t ullTotalPhys,ullAvailPhys,ullTotalPageFile,ullAvailPageFile,ullTotalVirtual,ullAvailVirtual,ullAvailExtendedVirtual;} MEMORYSTATUSEX;
typedef struct {DWORD dwFileAttributes;FILETIME ftCreationTime,ftLastAccessTime,ftLastWriteTime;DWORD dwVolumeSerialNumber,nFileSizeHigh,nFileSizeLow,nNumberOfLinks,nFileIndexHigh,nFileIndexLow;} BY_HANDLE_FILE_INFORMATION;
typedef struct {DWORD dwFileAttributes;FILETIME ftCreationTime,ftLastAccessTime,ftLastWriteTime;DWORD nFileSizeHigh,nFileSizeLow;} WIN32_FILE_ATTRIBUTE_DATA;
enum {GetFileExInfoStandard=0};
typedef struct {DWORD dwFileAttributes;FILETIME ftCreationTime,ftLastAccessTime,ftLastWriteTime;DWORD nFileSizeHigh,nFileSizeLow,dwReserved0,dwReserved1;WCHAR cFileName[260],cAlternateFileName[14];} WIN32_FIND_DATAW;
typedef struct {DWORD_PTR Mask;WORD Group,Reserved[3];} GROUP_AFFINITY;
typedef struct {BOOL ReplaceIfExists;HANDLE RootDirectory;DWORD FileNameLength;WCHAR FileName[1];} FILE_RENAME_INFO;
typedef enum {FileBasicInfo,FileStandardInfo,FileNameInfo,FileRenameInfo,FileDispositionInfo} FILE_INFO_BY_HANDLE_CLASS;
typedef struct {BOOL DeleteFile;} FILE_DISPOSITION_INFO;
typedef void *DLL_DIRECTORY_COOKIE;
typedef void *PTP_CALLBACK_INSTANCE;
typedef DWORD TP_WAIT_RESULT;
typedef struct PilotPosixWait *PTP_WAIT;
typedef void (*PTP_WAIT_CALLBACK)(PTP_CALLBACK_INSTANCE,void*,PTP_WAIT,TP_WAIT_RESULT);
#ifdef __cplusplus
extern "C" {
#endif
DWORD GetLastError(void);void SetLastError(DWORD);
BOOL CreateDirectoryA(LPCSTR,void*);
LONG InterlockedIncrement(volatile LONG*);LONG InterlockedDecrement(volatile LONG*);
LONG InterlockedExchange(volatile LONG*,LONG);LONG InterlockedCompareExchange(volatile LONG*,LONG,LONG);
LONG64 InterlockedIncrement64(volatile LONG64*);LONG64 InterlockedExchange64(volatile LONG64*,LONG64);
LONG64 InterlockedCompareExchange64(volatile LONG64*,LONG64,LONG64);LONG64 InterlockedAdd64(volatile LONG64*,LONG64);
void InitializeSRWLock(PSRWLOCK);void AcquireSRWLockExclusive(PSRWLOCK);void AcquireSRWLockShared(PSRWLOCK);
void ReleaseSRWLockExclusive(PSRWLOCK);void ReleaseSRWLockShared(PSRWLOCK);BOOLEAN TryAcquireSRWLockExclusive(PSRWLOCK);
void InitializeCriticalSection(LPCRITICAL_SECTION);void EnterCriticalSection(LPCRITICAL_SECTION);void LeaveCriticalSection(LPCRITICAL_SECTION);
BOOL SleepConditionVariableSRW(PCONDITION_VARIABLE,PSRWLOCK,DWORD,ULONG);void WakeAllConditionVariable(PCONDITION_VARIABLE);
BOOL InitOnceExecuteOnce(PINIT_ONCE,PINIT_ONCE_FN,void*,void**);
HANDLE CreateEventA(void*,BOOL,BOOL,LPCSTR);HANDLE CreateEventW(void*,BOOL,BOOL,LPCWSTR);
BOOL SetEvent(HANDLE);BOOL ResetEvent(HANDLE);HANDLE OpenEventA(DWORD,BOOL,LPCSTR);
HANDLE CreateSemaphoreW(void*,LONG,LONG,LPCWSTR);BOOL ReleaseSemaphore(HANDLE,LONG,LONG*);
HANDLE CreateThread(void*,SIZE_T,LPTHREAD_START_ROUTINE,void*,DWORD,DWORD*);DWORD ResumeThread(HANDLE);
void ExitThread(DWORD) __attribute__((noreturn));void ExitProcess(UINT) __attribute__((noreturn));
HANDLE GetCurrentProcess(void);HANDLE GetCurrentThread(void);DWORD GetCurrentProcessId(void);DWORD GetCurrentThreadId(void);DWORD GetThreadId(HANDLE);
DWORD WaitForSingleObject(HANDLE,DWORD);DWORD WaitForMultipleObjects(DWORD,const HANDLE*,BOOL,DWORD);
BOOL CloseHandle(HANDLE);BOOL DuplicateHandle(HANDLE,HANDLE,HANDLE,HANDLE*,DWORD,BOOL,DWORD);
HANDLE CreateWaitableTimerW(void*,BOOL,LPCWSTR);HANDLE CreateWaitableTimerExW(void*,LPCWSTR,DWORD,DWORD);
BOOL SetWaitableTimer(HANDLE,const LARGE_INTEGER*,LONG,void*,void*,BOOL);BOOL CancelWaitableTimer(HANDLE);
BOOL QueryPerformanceCounter(LARGE_INTEGER*);BOOL QueryPerformanceFrequency(LARGE_INTEGER*);uint64_t GetTickCount64(void);
void GetSystemTimePreciseAsFileTime(FILETIME*);BOOL FileTimeToSystemTime(const FILETIME*,SYSTEMTIME*);BOOL SystemTimeToFileTime(const SYSTEMTIME*,FILETIME*);
void Sleep(DWORD);BOOL GetThreadTimes(HANDLE,FILETIME*,FILETIME*,FILETIME*,FILETIME*);
int GetThreadPriority(HANDLE);BOOL SetThreadPriority(HANDLE,int);BOOL GetThreadPriorityBoost(HANDLE,BOOL*);BOOL SetThreadPriorityBoost(HANDLE,BOOL);
BOOL GetProcessAffinityMask(HANDLE,DWORD_PTR*,DWORD_PTR*);BOOL GetThreadGroupAffinity(HANDLE,GROUP_AFFINITY*);BOOL SetThreadGroupAffinity(HANDLE,const GROUP_AFFINITY*,GROUP_AFFINITY*);
DWORD QueueUserAPC(PAPCFUNC,HANDLE,ULONG_PTR);
PTP_WAIT CreateThreadpoolWait(PTP_WAIT_CALLBACK,void*,void*);void SetThreadpoolWait(PTP_WAIT,HANDLE,const FILETIME*);void CloseThreadpoolWait(PTP_WAIT);
void *VirtualAlloc(void*,SIZE_T,DWORD,DWORD);BOOL VirtualFree(void*,SIZE_T,DWORD);BOOL VirtualProtect(void*,SIZE_T,DWORD,DWORD*);SIZE_T VirtualQuery(const void*,MEMORY_BASIC_INFORMATION*,SIZE_T);
void GetSystemInfo(SYSTEM_INFO*);BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX*);
HANDLE GetProcessHeap(void);void *HeapAlloc(HANDLE,DWORD,SIZE_T);BOOL HeapFree(HANDLE,DWORD,void*);
int MultiByteToWideChar(UINT,DWORD,LPCSTR,int,LPWSTR,int);int WideCharToMultiByte(UINT,DWORD,LPCWSTR,int,LPSTR,int,LPCSTR,BOOL*);
DWORD GetEnvironmentVariableA(LPCSTR,LPSTR,DWORD);DWORD GetEnvironmentVariableW(LPCWSTR,LPWSTR,DWORD);
BOOL SetEnvironmentVariableA(LPCSTR,LPCSTR);DWORD GetFullPathNameA(LPCSTR,DWORD,LPSTR,LPSTR*);
FILE *_wfopen(LPCWSTR,LPCWSTR);
HANDLE CreateFileA(LPCSTR,DWORD,DWORD,void*,DWORD,DWORD,HANDLE);HANDLE CreateFileW(LPCWSTR,DWORD,DWORD,void*,DWORD,DWORD,HANDLE);
BOOL ReadFile(HANDLE,void*,DWORD,DWORD*,void*);BOOL WriteFile(HANDLE,const void*,DWORD,DWORD*,void*);BOOL FlushFileBuffers(HANDLE);
DWORD GetFileAttributesA(LPCSTR);DWORD GetFileAttributesW(LPCWSTR);BOOL GetFileAttributesExA(LPCSTR,int,void*);
BOOL GetFileSizeEx(HANDLE,LARGE_INTEGER*);BOOL GetFileInformationByHandle(HANDLE,BY_HANDLE_FILE_INFORMATION*);DWORD GetFinalPathNameByHandleW(HANDLE,LPWSTR,DWORD,DWORD);
BOOL DeleteFileW(LPCWSTR);BOOL MoveFileExW(LPCWSTR,LPCWSTR,DWORD);BOOL SetFileInformationByHandle(HANDLE,FILE_INFO_BY_HANDLE_CLASS,void*,DWORD);
HANDLE FindFirstFileW(LPCWSTR,WIN32_FIND_DATAW*);BOOL FindNextFileW(HANDLE,WIN32_FIND_DATAW*);BOOL FindClose(HANDLE);
HANDLE GetStdHandle(DWORD);DWORD SetErrorMode(DWORD);
DWORD GetModuleFileNameA(HMODULE,LPSTR,DWORD);DWORD GetModuleFileNameW(HMODULE,LPWSTR,DWORD);
HMODULE GetModuleHandleW(LPCWSTR);void *GetProcAddress(HMODULE,LPCSTR);
void *OpenFileMappingA(DWORD,BOOL,LPCSTR);void *MapViewOfFile(HANDLE,DWORD,DWORD,DWORD,SIZE_T);BOOL UnmapViewOfFile(const void*);
BOOL GetClientRect(HWND,RECT*);HWND GetForegroundWindow(void);DWORD GetWindowThreadProcessId(HWND,DWORD*);SHORT GetAsyncKeyState(int);
void pilot_posix_keyboard_snapshot(void);
#ifdef __cplusplus
}
#endif
