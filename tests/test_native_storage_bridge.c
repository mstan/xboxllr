/* ROM-free Windows ABI integration over explicitly supplied disposable images.
 * Exercises the exact native-title adapter, not a second filesystem service. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MEDIA_INVALID 0xc000000du
static HANDLE storage_roots[6];
static void stop(const char *why,uint32_t code) {fprintf(stderr,"%s %08x\n",why,code);exit(1);}
typedef LONG (NTAPI *MediaOpen)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,ULONG,ULONG);
typedef LONG (NTAPI *MediaRead)(HANDLE,HANDLE,PVOID,PVOID,PIO_STATUS_BLOCK,PVOID,ULONG,PLARGE_INTEGER,PULONG);
typedef LONG (NTAPI *MediaInfo)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,ULONG);
typedef LONG (NTAPI *MediaCreate)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,PLARGE_INTEGER,ULONG,ULONG,ULONG,ULONG,PVOID,ULONG);
typedef LONG (NTAPI *MediaFlush)(HANDLE,PIO_STATUS_BLOCK);
typedef LONG (NTAPI *MediaDirectory)(HANDLE,HANDLE,PVOID,PVOID,PIO_STATUS_BLOCK,PVOID,ULONG,ULONG,BOOLEAN,PUNICODE_STRING,BOOLEAN);
static MediaOpen media_nt_open;
static MediaCreate media_nt_create;
static MediaRead media_nt_read,media_nt_write;
static MediaInfo media_nt_query,media_nt_set,media_nt_volume;
static MediaFlush media_nt_flush;
static MediaDirectory media_nt_directory;
#include "../pilots/native-title/storage_image.inc"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"storage bridge check failed line %u: %s\n",__LINE__,#x);exit(1);}} while(0)
static HANDLE create(HANDLE parent,WCHAR *path,ULONG options)
{
    UNICODE_STRING name={(USHORT)(wcslen(path)*2),(USHORT)(wcslen(path)*2),path};
    OBJECT_ATTRIBUTES attributes={sizeof(attributes),parent,&name,0x40,NULL,NULL};
    IO_STATUS_BLOCK io={0};HANDLE file=NULL;
    CHECK(!media_nt_create(&file,GENERIC_READ|GENERIC_WRITE|DELETE|SYNCHRONIZE,&attributes,&io,NULL,
        options&1?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL,7,2,options,NULL,0));
    CHECK(io.Information==2);return file;
}
int main(int argc,char **argv)
{
    if(argc!=3 || strcmp(argv[1],"--fixture"))return 2;
    HMODULE nt=GetModuleHandleW(L"ntdll.dll");
#define IMPORT(name,type,export_name) name=(type)(void*)GetProcAddress(nt,export_name);CHECK(name)
    IMPORT(media_nt_open,MediaOpen,"NtOpenFile");IMPORT(media_nt_create,MediaCreate,"NtCreateFile");
    IMPORT(media_nt_read,MediaRead,"NtReadFile");IMPORT(media_nt_write,MediaRead,"NtWriteFile");
    IMPORT(media_nt_query,MediaInfo,"NtQueryInformationFile");IMPORT(media_nt_set,MediaInfo,"NtSetInformationFile");
    IMPORT(media_nt_volume,MediaInfo,"NtQueryVolumeInformationFile");IMPORT(media_nt_flush,MediaFlush,"NtFlushBuffersFile");
    IMPORT(media_nt_directory,MediaDirectory,"NtQueryDirectoryFile");
    char error[1024]={0};void *inspection=xllr_storage_open(argv[2],0,error,sizeof(error));
    CHECK(inspection);
    uint8_t identity[88],digest[32];uint64_t size=xllr_storage_bytes(inspection);memcpy(identity,&size,8);
    const uint64_t positions[]={0xabe80000,0x8ca80000,0x80000,0x2ee80000,0x5dc80000};
    for(unsigned i=0;i<5;++i)CHECK(!xllr_storage_read_blocks(inspection,positions[i],identity+8+16*i,16));
    CHECK(!xllr_storage_destroy(inspection));
    CHECK(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,identity,sizeof(identity),digest,32)>=0);
    char hex[65];for(unsigned i=0;i<32;++i)sprintf(hex+i*2,"%02x",digest[i]);
    CHECK(!_putenv_s("XLLE_PILOT_HDD_IDENTITY",hex));storage_image_mount(argv[2]);
    HANDLE dir=create(storage_roots[1],L"xllr-bridge",0x21);
    HANDLE file=create(dir,L"payload.dat",0x60);
    LARGE_INTEGER offset;offset.QuadPart=29;IO_STATUS_BLOCK io={0};
    uint8_t payload[19001],loaded[19030];for(unsigned i=0;i<sizeof(payload);++i)payload[i]=(uint8_t)(i*13+i/19);
    CHECK(!media_nt_write(file,NULL,NULL,NULL,&io,payload,sizeof(payload),&offset,NULL) && io.Information==sizeof(payload));
    offset.QuadPart=0;CHECK(!media_nt_read(file,NULL,NULL,NULL,&io,loaded,sizeof(loaded),&offset,NULL));
    CHECK(io.Information==sizeof(loaded) && !memcmp(loaded+29,payload,sizeof(payload)));
    for(unsigned i=0;i<29;++i)CHECK(!loaded[i]);
    union {uint64_t align;uint8_t bytes[128];} info={0};
    CHECK(!media_nt_query(file,&io,info.bytes,56,34) && io.Information==56);
    uint64_t length;memcpy(&length,info.bytes+40,8);CHECK(length==sizeof(loaded));
    CHECK(GetFileSizeEx(file,&offset) && offset.QuadPart==sizeof(loaded));
    uint64_t shorter=37;CHECK(!media_nt_set(file,&io,&shorter,8,20));
    CHECK(GetFileSizeEx(file,&offset) && offset.QuadPart==37);
    unsigned char renamed[sizeof(FILE_RENAME_INFO)+64]={0};FILE_RENAME_INFO *rename=(FILE_RENAME_INFO*)renamed;
    rename->RootDirectory=dir;rename->FileNameLength=22;memcpy(rename->FileName,L"renamed.dat",22);
    CHECK(!media_nt_set(file,&io,rename,sizeof(renamed),10));
    CHECK(!media_nt_flush(file,&io));CHECK(CloseHandle(file));
    UNICODE_STRING name={22,22,L"renamed.dat"};OBJECT_ATTRIBUTES attributes={sizeof(attributes),dir,&name,0x40,NULL,NULL};
    CHECK(!media_nt_open(&file,GENERIC_READ|DELETE|SYNCHRONIZE,&attributes,&io,7,0x60));
    offset.QuadPart=29;memset(loaded,0,sizeof(loaded));
    CHECK(!media_nt_read(file,NULL,NULL,NULL,&io,loaded,sizeof(loaded),&offset,NULL) && io.Information==8 && !memcmp(loaded,payload,8));
    WCHAR mask_data[]=L"*.dat";UNICODE_STRING mask={10,10,mask_data};
    CHECK(!media_nt_directory(dir,NULL,NULL,NULL,&io,info.bytes,sizeof(info.bytes),1,TRUE,&mask,TRUE));
    uint32_t name_bytes;memcpy(&name_bytes,info.bytes+60,4);
    CHECK(name_bytes==22 && !memcmp(info.bytes+64,L"renamed.dat",22));
    CHECK((uint32_t)media_nt_directory(dir,NULL,NULL,NULL,&io,info.bytes,sizeof(info.bytes),1,TRUE,NULL,FALSE)==0x80000006u);
    BOOLEAN remove=TRUE;CHECK(!media_nt_set(file,&io,&remove,1,13));CHECK(CloseHandle(file));
    CHECK(!media_nt_set(dir,&io,&remove,1,13));CHECK(CloseHandle(dir));
    storage_image_shutdown();
    inspection=xllr_storage_open(argv[2],0,error,sizeof(error));CHECK(inspection);
    uint64_t handle=0;uint32_t action=0;
    CHECK(xllr_storage_file_open(inspection,1,0,"xllr-bridge",GENERIC_READ|SYNCHRONIZE,7,0x21,1,0,0,&handle,&action)==0xc0000034u);
    CHECK(!xllr_storage_destroy(inspection));
    puts("actual native NT bridge save/reopen/rename/enumerate/delete passed");
    return 0;
}
