#include <windows.h>
#include <winternl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *media_content_path;
struct ResidentFile;
struct MediaFile {HANDLE host;unsigned asynchronous;const struct ResidentFile *resident;unsigned nonbuffered;};
typedef LONG (NTAPI *MediaInfo)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,ULONG);
static MediaInfo media_nt_set;
static uint64_t dispatch_monotonic(void){return GetTickCount64()*10000;}
#include "resident_media.inc"
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d: %s error=%lu\n",__LINE__,#c,GetLastError());ExitProcess(1);}}while(0)
static void fixture(const WCHAR *path,unsigned bytes)
{
    HANDLE h=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);CHECK(h!=INVALID_HANDLE_VALUE);
    unsigned char block[4096];for(unsigned i=0;i<sizeof(block);++i)block[i]=(unsigned char)i;
    for(unsigned at=0;at<bytes;){DWORD count=bytes-at>sizeof(block)?sizeof(block):bytes-at,done;CHECK(WriteFile(h,block,count,&done,NULL) && done==count);at+=count;}
    CHECK(CloseHandle(h));
}
int main(void)
{
    char root[MAX_PATH];WCHAR directory[MAX_PATH],asset[MAX_PATH],stream[MAX_PATH],large[MAX_PATH];
    CHECK(GetTempPathA(MAX_PATH,root));size_t n=strlen(root);CHECK(n<MAX_PATH-40);
    snprintf(root+n,MAX_PATH-n,"xboxllr-resident-%lu",GetCurrentProcessId());
    CHECK(MultiByteToWideChar(CP_UTF8,0,root,-1,directory,MAX_PATH));CHECK(CreateDirectoryW(directory,NULL));
    swprintf(asset,MAX_PATH,L"%ls\\asset.bin",directory);swprintf(stream,MAX_PATH,L"%ls\\music.adx",directory);
    swprintf(large,MAX_PATH,L"%ls\\large.bin",directory);fixture(asset,9000);fixture(stream,12345);
    media_content_path=root;media_nt_set=(MediaInfo)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtSetInformationFile");CHECK(media_nt_set);
    const char *extensions[]={".adx"};CHECK(media_resident_configure(1,extensions,1));media_resident_prepare();
    CHECK(media_resident_ready() && resident_count==1 && resident_total==9000);
    const char *view_setting=getenv("XLLE_PILOT_RESIDENT_FILE_VIEWS");
    unsigned expect_view=PILOT_RESIDENT_FILE_VIEWS_AVAILABLE && (!view_setting || !strcmp(view_setting,"1"));
    CHECK(resident_files[0].blob.mapped==expect_view);
    HANDLE h=CreateFileW(asset,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);CHECK(h!=INVALID_HANDLE_VALUE);
    struct MediaFile file={h,0,media_resident_find(h)};CHECK(file.resident);
    HANDLE writer=CreateFileW(asset,GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(writer==INVALID_HANDLE_VALUE && GetLastError()==ERROR_SHARING_VIOLATION);
    unsigned char data[40];IO_STATUS_BLOCK io={0};LARGE_INTEGER offset;uint32_t status=123;
    offset.QuadPart=4090;CHECK(media_resident_read(&file,&io,data,40,&offset,&status) && !status && io.Information==40);
    for(unsigned i=0;i<40;++i)CHECK(data[i]==(unsigned char)(4090+i));
    LARGE_INTEGER zero={0},position;CHECK(SetFilePointerEx(h,zero,&position,FILE_CURRENT) && position.QuadPart==4130);
    offset.QuadPart=8990;CHECK(media_resident_read(&file,&io,data,40,&offset,&status) && !status && io.Information==10);
    offset.QuadPart=9000;CHECK(media_resident_read(&file,&io,data,40,&offset,&status) && status==0xc0000011u);
    offset.QuadPart=-1;CHECK(!media_resident_read(&file,&io,data,40,&offset,&status));
    file.nonbuffered=1;offset.QuadPart=12;CHECK(!media_resident_read(&file,&io,data,40,&offset,&status));file.nonbuffered=0;
    file.asynchronous=1;offset.QuadPart=12;CHECK(media_resident_read(&file,&io,data,40,&offset,&status) && !status);
    CHECK(SetFilePointerEx(h,zero,&position,FILE_CURRENT) && position.QuadPart==9000);
    CHECK(CloseHandle(h));
    h=CreateFileW(asset,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    CHECK(h!=INVALID_HANDLE_VALUE && media_resident_find(h));
    writer=CreateFileW(asset,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    CHECK(writer==INVALID_HANDLE_VALUE && GetLastError()==ERROR_SHARING_VIOLATION);
    CHECK(CloseHandle(h));media_resident_close();
    writer=CreateFileW(asset,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);CHECK(writer!=INVALID_HANDLE_VALUE);CHECK(CloseHandle(writer));
    fixture(large,2*1024*1024);CHECK(media_resident_configure(1,extensions,1));media_resident_prepare();
    CHECK(!media_resident_ready() && !resident_files && !resident_count && !resident_total);
    CHECK(media_resident_configure(0,NULL,0));media_resident_prepare();CHECK(!media_resident_ready());
    CHECK(!media_resident_configure(1537,NULL,0));CHECK(!media_resident_configure(1,(const char*[]){"../bad"},1));
    CHECK(DeleteFileW(large));CHECK(DeleteFileW(stream));CHECK(DeleteFileW(asset));CHECK(RemoveDirectoryW(directory));
    puts("PASS: resident bytes, real read pins/writer exclusion, offsets, EOF, nonbuffered fallback, streams, atomic budget fallback and cleanup");return 0;
}
