#ifndef PILOT_GPU_FRAMEBUFFER_REQUEST_H
#define PILOT_GPU_FRAMEBUFFER_REQUEST_H
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* Host observation intent only. No guest or renderer services are called.
 * Publication can expose the name while its DELETE-access rename handle
 * remains open. Share namespace access, but exclude all content writers
 * while verifying bytes through this pinned read handle. */
static int gpu_framebuffer_request(const char *directory,unsigned record)
{
    if(!record || record>16)return -1;
    char path[4352],bytes[12];const char marker[]="capture-v1\n";
    if(sprintf_s(path,sizeof(path),"%s/framebuffer-%02u.request",directory,record)<0)return -1;
    HANDLE file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND?0:-1;
    DWORD read=0;int okay=ReadFile(file,bytes,sizeof(bytes),&read,NULL) && read==sizeof(marker)-1 &&
        !memcmp(bytes,marker,sizeof(marker)-1);
    if(!CloseHandle(file))okay=0;
    return okay?1:-1;
}
#endif
