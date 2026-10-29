#ifndef PILOT_RESIDENT_BLOB_H
#define PILOT_RESIDENT_BLOB_H
/* Windows native-title storage primitive. Borrow the caller's admitted,
 * write/delete-denying handle. GCC retains the original private-copy path;
 * MSVC SEH is required to recover from mapped-file in-page errors. */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#if defined(_MSC_VER)
#define PILOT_RESIDENT_FILE_VIEWS_AVAILABLE 1
#else
#define PILOT_RESIDENT_FILE_VIEWS_AVAILABLE 0
#endif
typedef struct PilotResidentBlob {unsigned char *bytes;uint64_t size;unsigned mapped;} PilotResidentBlob;

static inline void pilot_resident_blob_release(PilotResidentBlob *blob)
{
    if(blob->bytes){
        if(blob->mapped)UnmapViewOfFile(blob->bytes);
        else VirtualFree(blob->bytes,0,MEM_RELEASE);
    }
    memset(blob,0,sizeof(*blob));
}
#if PILOT_RESIDENT_FILE_VIEWS_AVAILABLE
/* Recover only from reads owned by this mapping. A bad destination or an
 * unrelated exception must retain its normal failure behavior. */
static inline int pilot_resident_blob_fault(EXCEPTION_POINTERS *exception,const void *source,SIZE_T size)
{
    EXCEPTION_RECORD *record=exception->ExceptionRecord;
    if((record->ExceptionCode!=EXCEPTION_IN_PAGE_ERROR && record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION) ||
        record->NumberParameters<2 || record->ExceptionInformation[0]!=0)return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t address=(uintptr_t)record->ExceptionInformation[1],begin=(uintptr_t)source;
    return address>=begin && address-begin<size?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH;
}
static inline int pilot_resident_blob_prefault(const unsigned char *bytes,SIZE_T size)
{
    __try {
        const volatile unsigned char *pages=bytes;
        for(SIZE_T at=0;at<size;){
            (void)pages[at];
            if(size-at<=4096)break;
            at+=4096;
        }
        if(size)(void)pages[size-1];
        return 1;
    } __except(pilot_resident_blob_fault(GetExceptionInformation(),bytes,size)){return 0;}
}
#endif
static inline int pilot_resident_blob_load(HANDLE file,uint64_t size,int prefer_views,PilotResidentBlob *out)
{
    if(!out || out->bytes || out->size || out->mapped || !file || file==INVALID_HANDLE_VALUE || size>SIZE_MAX)return 0;
    if(!size)return 1;
#if PILOT_RESIDENT_FILE_VIEWS_AVAILABLE
    if(prefer_views){
        HANDLE mapping=CreateFileMappingW(file,NULL,PAGE_READONLY,0,0,NULL);
        if(mapping){
            unsigned char *bytes=MapViewOfFile(mapping,FILE_MAP_READ,0,0,(SIZE_T)size);
            CloseHandle(mapping);
            if(bytes){
                if(pilot_resident_blob_prefault(bytes,(SIZE_T)size)){
                    out->bytes=bytes;out->size=size;out->mapped=1;return 1;
                }
                UnmapViewOfFile(bytes);
            }
        }
    }
#else
    (void)prefer_views;
#endif
    unsigned char *bytes=VirtualAlloc(NULL,(SIZE_T)size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!bytes)return 0;
    int ok=1;
    for(uint64_t done=0;ok && done<size;){
        DWORD got=0,wanted=(DWORD)(size-done>4*1024*1024?4*1024*1024:size-done);
        if(!ReadFile(file,bytes+done,wanted,&got,NULL) || got!=wanted)ok=0;
        done+=got;
    }
    DWORD previous;
    if(ok && !VirtualProtect(bytes,(SIZE_T)size,PAGE_READONLY,&previous))ok=0;
    if(!ok){VirtualFree(bytes,0,MEM_RELEASE);return 0;}
    out->bytes=bytes;out->size=size;return 1;
}
static inline int pilot_resident_blob_copy(const PilotResidentBlob *blob,uint64_t offset,void *output,SIZE_T size)
{
    if(!blob || offset>blob->size || size>blob->size-offset || (size && (!output || !blob->bytes)))return 0;
    if(!size)return 1;
#if PILOT_RESIDENT_FILE_VIEWS_AVAILABLE
    if(blob->mapped){
        __try {memcpy(output,blob->bytes+offset,size);return 1;}
        __except(pilot_resident_blob_fault(GetExceptionInformation(),blob->bytes+offset,size)){return 0;}
    }
#endif
    memcpy(output,blob->bytes+offset,size);return 1;
}
#endif
