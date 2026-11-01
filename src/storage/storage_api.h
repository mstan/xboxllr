// Shared native storage boundary. Integer status values are Xbox NTSTATUS;
// no exceptions, host handles, title-specific saves or key values cross it.
#pragma once
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32) && defined(XLLR_STORAGE_SHARED)
#define XLLR_STORAGE_API __declspec(dllexport)
#else
#define XLLR_STORAGE_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct XllrStorageInfo {
    uint64_t length,position;
    uint32_t attributes,access;
    uint16_t times[6];
    uint32_t directory,delete_pending;
} XllrStorageInfo;
typedef struct XllrStorageDirectoryEntry {
    XllrStorageInfo info;
    char name[43];
} XllrStorageDirectoryEntry;
XLLR_STORAGE_API void* xllr_storage_open(const char* path,int writable,char* error,size_t capacity);
XLLR_STORAGE_API void* xllr_storage_open_ram_cache(unsigned partition,uint64_t bytes,char* error,size_t capacity);
XLLR_STORAGE_API uint32_t xllr_storage_ram_stats(void*,uint64_t* resident,uint64_t* read,uint64_t* written);
XLLR_STORAGE_API uint32_t xllr_storage_destroy(void*);
XLLR_STORAGE_API uint64_t xllr_storage_bytes(void*);
XLLR_STORAGE_API int xllr_storage_is_qcow2(void*);
XLLR_STORAGE_API uint32_t xllr_storage_read_blocks(void*,uint64_t,void*,uint32_t);
XLLR_STORAGE_API uint32_t xllr_storage_write_blocks(void*,uint64_t,const void*,uint32_t);
XLLR_STORAGE_API uint32_t xllr_storage_flush(void*);
XLLR_STORAGE_API uint32_t xllr_storage_file_open(void*,unsigned partition,uint64_t parent,const char* path,
    uint32_t access,uint32_t share,uint32_t options,uint32_t disposition,uint32_t attributes,
    int64_t allocation,uint64_t* handle,uint32_t* result);
XLLR_STORAGE_API uint32_t xllr_storage_file_close(void*,uint64_t);
XLLR_STORAGE_API uint32_t xllr_storage_file_info(void*,uint64_t,XllrStorageInfo*);
XLLR_STORAGE_API uint32_t xllr_storage_file_allocation(void*,uint64_t,uint64_t*);
XLLR_STORAGE_API uint32_t xllr_storage_file_read(void*,uint64_t,int64_t offset,void*,uint32_t,uint32_t*);
XLLR_STORAGE_API uint32_t xllr_storage_file_write(void*,uint64_t,int64_t offset,const void*,uint32_t,uint32_t*);
XLLR_STORAGE_API uint32_t xllr_storage_file_resize(void*,uint64_t,int64_t bytes,int allocation);
XLLR_STORAGE_API uint32_t xllr_storage_file_position(void*,uint64_t,int64_t);
XLLR_STORAGE_API uint32_t xllr_storage_file_metadata(void*,uint64_t,uint32_t,const uint16_t times[6]);
XLLR_STORAGE_API uint32_t xllr_storage_file_delete(void*,uint64_t,int pending);
XLLR_STORAGE_API uint32_t xllr_storage_file_rename(void*,uint64_t,uint64_t parent,const char*,int replace);
XLLR_STORAGE_API uint32_t xllr_storage_directory(void*,uint64_t,const char* mask,int restart,XllrStorageDirectoryEntry*);
XLLR_STORAGE_API uint32_t xllr_storage_capacity(void*,unsigned,uint64_t* total,uint64_t* available,uint32_t* unit);
XLLR_STORAGE_API uint32_t xllr_storage_clear_cache(void*,unsigned partition);
#ifdef __cplusplus
}
#endif
