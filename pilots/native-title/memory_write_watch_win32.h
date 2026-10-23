#ifndef PILOT_MEMORY_WRITE_WATCH_WIN32_H
#define PILOT_MEMORY_WRITE_WATCH_WIN32_H
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
BOOL WINAPI pilot_write_watch_virtual_protect(LPVOID,SIZE_T,DWORD,PDWORD);
LPVOID WINAPI pilot_write_watch_virtual_alloc(LPVOID,SIZE_T,DWORD,DWORD);
BOOL WINAPI pilot_write_watch_virtual_free(LPVOID,SIZE_T,DWORD);
SIZE_T WINAPI pilot_write_watch_virtual_query(LPCVOID,PMEMORY_BASIC_INFORMATION,SIZE_T);
#ifdef __cplusplus
}
#endif
#endif
