/* SPDX-License-Identifier: GPL-3.0-or-later
 * Explicit UTF-8 conversion, independent of the process locale. Host WCHAR
 * follows the DXVK Native SDK (wchar_t); guest UTF-16 remains guest data.
 */
#define _GNU_SOURCE
#include "windows.h"
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>

int MultiByteToWideChar(UINT page,DWORD flags,LPCSTR source,int bytes,LPWSTR out,int capacity)
{
    if(page!=CP_UTF8 || flags&~MB_ERR_INVALID_CHARS || !source || bytes==0 || bytes< -1 || capacity<0 || (!out && capacity))goto invalid;
    size_t size=bytes<0?strlen(source)+1:(size_t)bytes;int count=0;
    for(size_t at=0;at<size;) {
        uint32_t c=(unsigned char)source[at++];unsigned continuation=0;
        if(c>=0xc2 && c<=0xdf){continuation=1;c&=31;}
        else if(c>=0xe0 && c<=0xef){continuation=2;c&=15;}
        else if(c>=0xf0 && c<=0xf4){continuation=3;c&=7;}
        else if(c>=0x80)goto invalid;
        if(continuation>size-at)goto invalid;
        for(unsigned i=0;i<continuation;++i){unsigned next=(unsigned char)source[at++];if((next&0xc0)!=0x80)goto invalid;c=(c<<6)|(next&63);}
        if((continuation==1 && c<0x80) || (continuation==2 && c<0x800) || (continuation==3 && c<0x10000) || c>0x10ffff || (c>=0xd800 && c<=0xdfff))goto invalid;
        if(out && capacity){if(count>=capacity){SetLastError(ERROR_INSUFFICIENT_BUFFER);return 0;}out[count]=(wchar_t)c;}++count;
    }
    return count;
invalid:
    SetLastError(ERROR_INVALID_PARAMETER);return 0;
}
int WideCharToMultiByte(UINT page,DWORD flags,LPCWSTR source,int characters,LPSTR out,int capacity,LPCSTR replacement,BOOL *used)
{
    if(page!=CP_UTF8 || flags&~WC_ERR_INVALID_CHARS || !source || characters==0 || characters< -1 || capacity<0 || replacement || used || (!out && capacity))goto invalid;
    size_t size=characters<0?wcslen(source)+1:(size_t)characters;int count=0;
    for(size_t at=0;at<size;++at) {
        uint32_t c=(uint32_t)source[at];unsigned bytes=c<0x80?1:c<0x800?2:c<0x10000?3:4;unsigned char encoded[4];
        if(c>0x10ffff || (c>=0xd800 && c<=0xdfff))goto invalid;
        uint32_t rest=c;for(unsigned i=bytes-1;i>0;--i){encoded[i]=0x80|(rest&63);rest>>=6;}
        encoded[0]=(bytes==1?0:bytes==2?0xc0:bytes==3?0xe0:0xf0)|rest;
        if(count>INT_MAX-(int)bytes)goto invalid;
        if(out && capacity){if((int)bytes>capacity-count){SetLastError(ERROR_INSUFFICIENT_BUFFER);return 0;}memcpy(out+count,encoded,bytes);}count+=(int)bytes;
    }
    return count;
invalid:
    SetLastError(ERROR_INVALID_PARAMETER);return 0;
}
DWORD GetEnvironmentVariableA(LPCSTR name,LPSTR out,DWORD size)
{
    const char *value=getenv(name);if(!value)return 0;size_t bytes=strlen(value);
    if(bytes>=UINT32_MAX)return 0;if(!out || bytes>=size)return (DWORD)bytes+1;memcpy(out,value,bytes+1);return (DWORD)bytes;
}
DWORD GetEnvironmentVariableW(LPCWSTR name,LPWSTR out,DWORD size)
{
    char key[1024];if(!WideCharToMultiByte(CP_UTF8,0,name,-1,key,sizeof(key),NULL,NULL))return 0;
    const char *value=getenv(key);if(!value)return 0;int count=MultiByteToWideChar(CP_UTF8,0,value,-1,NULL,0);
    if(!count)return 0;if(!out || (DWORD)count>size)return (DWORD)count;
    if(!MultiByteToWideChar(CP_UTF8,0,value,-1,out,(int)size))return 0;return (DWORD)count-1;
}
BOOL SetEnvironmentVariableA(LPCSTR name,LPCSTR value)
{if((value?setenv(name,value,1):unsetenv(name))==0)return TRUE;SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
DWORD GetFullPathNameA(LPCSTR path,DWORD size,LPSTR out,LPSTR *part)
{
    char *resolved=realpath(path,NULL);if(!resolved){SetLastError(ERROR_PATH_NOT_FOUND);return 0;}
    size_t bytes=strlen(resolved);if(size<=bytes){free(resolved);return (DWORD)bytes+1;}
    memcpy(out,resolved,bytes+1);if(part){char *slash=strrchr(out,'/');*part=slash?slash+1:out;}free(resolved);return (DWORD)bytes;
}
DWORD GetModuleFileNameA(HMODULE module,LPSTR out,DWORD size)
{
    if(module){SetLastError(ERROR_NOT_SUPPORTED);return 0;}
    if(!out || size<2){SetLastError(ERROR_INSUFFICIENT_BUFFER);return 0;}
    ssize_t bytes=readlink("/proc/self/exe",out,size-1);if(bytes<0)return 0;out[bytes]=0;return (DWORD)bytes;
}
DWORD GetModuleFileNameW(HMODULE module,LPWSTR out,DWORD size)
{char path[PATH_MAX];if(!GetModuleFileNameA(module,path,sizeof(path)))return 0;int count=MultiByteToWideChar(CP_UTF8,0,path,-1,out,(int)size);return count?(DWORD)count-1:0;}
FILE *_wfopen(LPCWSTR path,LPCWSTR mode)
{char utf8[PATH_MAX],flags[32];if(!WideCharToMultiByte(CP_UTF8,0,path,-1,utf8,sizeof(utf8),NULL,NULL) || !WideCharToMultiByte(CP_UTF8,0,mode,-1,flags,sizeof(flags),NULL,NULL))return NULL;return fopen(utf8,flags);}
