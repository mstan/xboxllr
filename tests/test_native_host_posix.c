/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "windows.h"
#include "bcrypt.h"
#include "winternl.h"
#include <assert.h>
#include <unistd.h>
static DWORD worker(void *event){assert(SetEvent(event));return 37;}
static ULONG_PTR opened_information;
static LONG nt_open(HANDLE root,const WCHAR *path,HANDLE *out,ULONG disposition)
{
    UNICODE_STRING name={(USHORT)(wcslen(path)*2),(USHORT)(wcslen(path)*2),(PWSTR)path};
    OBJECT_ATTRIBUTES attr={sizeof(attr),root,&name,0x1040,NULL,NULL};IO_STATUS_BLOCK io;
    LONG status=pilot_posix_nt_create(out,FILE_GENERIC_READ|FILE_GENERIC_WRITE|DELETE,&attr,&io,NULL,
        FILE_ATTRIBUTE_NORMAL,7,disposition,0x60,NULL,0);
    opened_information=io.Information;return status;
}
int main(void)
{
    /* Recommit preserves data; decommit drops data and permissions. */
    unsigned char *memory=VirtualAlloc(NULL,65536,MEM_RESERVE,PAGE_NOACCESS);assert(memory);
    MEMORY_BASIC_INFORMATION info;assert(VirtualQuery(memory,&info,sizeof(info)) && info.State==MEM_RESERVE && info.RegionSize==65536);
    assert(VirtualAlloc(memory+4096,4096,MEM_COMMIT,PAGE_READWRITE)==memory+4096);memory[4096]=71;
    assert(VirtualAlloc(memory+4096,4096,MEM_COMMIT,PAGE_READWRITE)==memory+4096 && memory[4096]==71);
    DWORD previous;assert(VirtualProtect(memory+4096,4096,PAGE_READONLY,&previous) && previous==PAGE_READWRITE);
    assert(VirtualQuery(memory+4096,&info,sizeof(info)) && info.Protect==PAGE_READONLY && info.RegionSize==4096);
    assert(!VirtualProtect(memory+8192,4096,PAGE_READWRITE,&previous));
    assert(VirtualFree(memory+4096,4096,MEM_DECOMMIT));
    assert(VirtualAlloc(memory+4096,4096,MEM_COMMIT,PAGE_READWRITE) && memory[4096]==0);
    assert(VirtualFree(memory,0,MEM_RELEASE));
    HANDLE a=CreateEventW(NULL,FALSE,TRUE,NULL),b=CreateEventW(NULL,FALSE,FALSE,NULL);assert(a && b);
    HANDLE pair[]={a,b};assert(WaitForMultipleObjects(2,pair,TRUE,0)==WAIT_TIMEOUT);
    assert(WaitForSingleObject(a,0)==WAIT_OBJECT_0);assert(WaitForSingleObject(a,0)==WAIT_TIMEOUT);
    assert(SetEvent(a) && SetEvent(b));assert(WaitForMultipleObjects(2,pair,TRUE,0)==WAIT_OBJECT_0);
    assert(WaitForSingleObject(b,0)==WAIT_TIMEOUT);
    DWORD id=0;HANDLE thread=CreateThread(NULL,0,worker,b,CREATE_SUSPENDED,&id);assert(thread && id);
    assert(WaitForSingleObject(b,10)==WAIT_TIMEOUT);assert(ResumeThread(thread)==1);
    assert(WaitForSingleObject(b,1000)==WAIT_OBJECT_0);assert(WaitForSingleObject(thread,1000)==WAIT_OBJECT_0);
    assert(CloseHandle(thread) && CloseHandle(a) && CloseHandle(b));
    HANDLE timer=CreateWaitableTimerW(NULL,FALSE,NULL);assert(timer);LARGE_INTEGER due;due.QuadPart=-200000;
    uint64_t begin=GetTickCount64();assert(SetWaitableTimer(timer,&due,0,NULL,NULL,FALSE));assert(WaitForSingleObject(timer,1000)==WAIT_OBJECT_0);
    assert(GetTickCount64()-begin>=15);assert(WaitForSingleObject(timer,0)==WAIT_TIMEOUT);assert(CloseHandle(timer));
    wchar_t wide[64];char utf8[128];const char text[]="/tmp/\xc3\xa9-\xf0\x9f\x8e\xae";
    assert(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,wide,64));
    assert(WideCharToMultiByte(CP_UTF8,0,wide,-1,utf8,sizeof(utf8),NULL,NULL) && !strcmp(text,utf8));
    assert(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,"\xc0\xaf",-1,wide,64));
    unsigned char digest[32];assert(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(unsigned char*)"abc",3,digest,32)==0);
    const unsigned char expected[]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};assert(!memcmp(digest,expected,32));
    char directory[]="/tmp/xboxllr-host-XXXXXX";assert(mkdtemp(directory));char path[256];snprintf(path,sizeof(path),"%s/save",directory);
    HANDLE f=CreateFileA(path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);assert(f!=INVALID_HANDLE_VALUE);
    DWORD count;assert(WriteFile(f,"progress",8,&count,NULL) && count==8);assert(FlushFileBuffers(f));
    HANDLE rejected=CreateFileA(path,GENERIC_WRITE,7,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);assert(rejected==INVALID_HANDLE_VALUE);
    assert(CloseHandle(f));f=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);assert(f!=INVALID_HANDLE_VALUE);
    char content[16]={0};assert(ReadFile(f,content,sizeof(content),&count,NULL) && count==8 && !strcmp(content,"progress"));assert(CloseHandle(f));
    assert(unlink(path)==0);
    HANDLE root=CreateFileA(directory,FILE_LIST_DIRECTORY|FILE_ADD_FILE|FILE_ADD_SUBDIRECTORY|FILE_TRAVERSE|FILE_READ_ATTRIBUTES|SYNCHRONIZE,7,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);assert(root!=INVALID_HANDLE_VALUE);
    HANDLE save;assert(nt_open(root,L"Progress.dat",&save,2)==0 && opened_information==2);
    IO_STATUS_BLOCK io;LARGE_INTEGER offset;offset.QuadPart=0;
    DWORD granted;assert(pilot_posix_nt_query(save,&io,&granted,sizeof(granted),8)==0);
    assert((granted&(FILE_READ_DATA|FILE_WRITE_DATA))==(FILE_READ_DATA|FILE_WRITE_DATA) && !(granted&(GENERIC_READ|GENERIC_WRITE)));
    assert(pilot_posix_nt_write(save,NULL,NULL,NULL,&io,"native-save",11,&offset,NULL)==0 && io.Information==11);
    assert(pilot_posix_nt_flush(save,&io)==0);
    uint64_t basic[5]={132537600000000000ull,0,132537600100000000ull,0,FILE_ATTRIBUTE_HIDDEN};
    assert(pilot_posix_nt_set(save,&io,basic,sizeof(basic),4)==0);
    assert(CloseHandle(save));
    /* Open-if must find an existing case-insensitive name before creating. */
    assert(nt_open(root,L"PROGRESS.DAT",&save,3)==0 && opened_information==1);
    unsigned char meta[56];assert(pilot_posix_nt_query(save,&io,meta,sizeof(meta),34)==0);
    uint64_t created,size;DWORD attrs;memcpy(&created,meta,8);memcpy(&size,meta+40,8);memcpy(&attrs,meta+48,4);
    assert(created==basic[0] && size==11 && (attrs&FILE_ATTRIBUTE_HIDDEN));
    memset(content,0,sizeof(content));assert(pilot_posix_nt_read(save,NULL,NULL,NULL,&io,content,11,&offset,NULL)==0 && !strcmp(content,"native-save"));
    HANDLE other;assert((uint32_t)nt_open(root,L"progress.dat",&other,2)==0xc0000035u);
    assert(nt_open(root,L"../escape",&other,3)<0);
    assert(nt_open(root,L"Cache.owner",&other,3)==0 && opened_information==2);
    assert(CloseHandle(other));
    assert(nt_open(root,L"Cache.owner",&other,3)==0 && opened_information==1);
    assert(CloseHandle(other));
    assert(nt_open(root,L"Cache.owner",&other,5)==0 && opened_information==3);
    assert(CloseHandle(other));
    snprintf(path,sizeof(path),"%s/Cache.owner",directory);assert(unlink(path)==0);
    char link[256];snprintf(link,sizeof(link),"%s/link",directory);assert(symlink("/tmp",link)==0);
    assert(nt_open(root,L"link/escape",&other,3)<0);assert(unlink(link)==0);
    WCHAR mask[]=L"*.DAT";UNICODE_STRING pattern={10,10,mask};unsigned char entries[1024];
    assert(pilot_posix_nt_directory(root,NULL,NULL,NULL,&io,entries,sizeof(entries),1,TRUE,&pattern,TRUE)==0);
    DWORD name_bytes;memcpy(&name_bytes,entries+60,4);assert(name_bytes==24);
    const uint16_t *wire=(const uint16_t*)(entries+64);assert(wire[0]=='P' && wire[11]=='t');
    assert((uint32_t)pilot_posix_nt_directory(root,NULL,NULL,NULL,&io,entries,sizeof(entries),1,TRUE,NULL,FALSE)==0x80000006u);
    unsigned char rename_bytes[128]={0};FILE_RENAME_INFO *rename=(FILE_RENAME_INFO*)rename_bytes;
    rename->RootDirectory=root;rename->FileNameLength=18;wcscpy(rename->FileName,L"Saved.dat");
    assert(pilot_posix_nt_set(save,&io,rename,sizeof(rename_bytes),10)==0);
    assert(CloseHandle(save) && CloseHandle(root));
    snprintf(path,sizeof(path),"%s/Saved.dat",directory);assert(unlink(path)==0 && rmdir(directory)==0);
    puts("native Linux memory, waits, threads, timers, Unicode, hashes and file persistence pass");return 0;
}
