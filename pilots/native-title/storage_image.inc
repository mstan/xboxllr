/* Optional image service under the existing shared file ABI. Installed game
 * content and private console metadata keep their existing host owners.
 * Only mounted HDD partitions dispatch to actual FATX image bytes. */
#include "../../src/storage/storage_api.h"
static void *storage_image;
struct StorageImageHandle {HANDLE event;uint64_t file;unsigned partition;};
static struct StorageImageHandle storage_image_handles[2048];
static SRWLOCK storage_image_lock=SRWLOCK_INIT;
static MediaOpen storage_host_open;
static MediaCreate storage_host_create;
static MediaRead storage_host_read,storage_host_write;
static MediaInfo storage_host_query,storage_host_set,storage_host_volume;
static MediaFlush storage_host_flush;
static MediaDirectory storage_host_directory;
static struct StorageImageHandle *storage_image_find(HANDLE h)
{
    if(!storage_image)return NULL;
    for(unsigned i=0;i<2048;++i)if(storage_image_handles[i].event==h)return &storage_image_handles[i];
    return NULL;
}
static uint32_t storage_image_ascii(const UNICODE_STRING *s,char out[1024])
{
    if(!s || s->Length%2 || s->Length>s->MaximumLength || s->Length/2>=1024)return MEDIA_INVALID;
    for(unsigned i=0;i<s->Length/2;++i) {
        if(!s->Buffer[i] || s->Buffer[i]>=128)return 0xc0000033u;
        out[i]=(char)s->Buffer[i];
    }
    out[s->Length/2]=0;return 0;
}
static LONG NTAPI storage_image_create(PHANDLE out,ACCESS_MASK access,POBJECT_ATTRIBUTES attr,PIO_STATUS_BLOCK io,
    PLARGE_INTEGER allocation,ULONG attributes,ULONG share,ULONG disposition,ULONG options,PVOID ea,ULONG ea_bytes)
{
    AcquireSRWLockExclusive(&storage_image_lock);
    struct StorageImageHandle *parent=attr?storage_image_find(attr->RootDirectory):NULL;
    if(!parent) {ReleaseSRWLockExclusive(&storage_image_lock);return storage_host_create(out,access,attr,io,allocation,attributes,share,disposition,options,ea,ea_bytes);}
    char path[1024];uint32_t status=storage_image_ascii(attr->ObjectName,path);
    unsigned slot=0;while(slot<2048 && storage_image_handles[slot].event)++slot;
    if(!status && slot==2048)status=0xc000009au;
    if(!status && ea_bytes)status=MEDIA_INVALID;
    HANDLE event=NULL;uint64_t file=0;uint32_t action=0;
    if(!status && !(event=CreateEventW(NULL,TRUE,TRUE,NULL)))status=0xc000009au;
    if(!status)status=xllr_storage_file_open(storage_image,parent->partition,parent->file,path,access,share,options,
        disposition,attributes,allocation?allocation->QuadPart:0,&file,&action);
    if(!status) {
        storage_image_handles[slot]=(struct StorageImageHandle){event,file,parent->partition};
        *out=event;io->Status=0;io->Information=action;
    } else if(event)CloseHandle(event);
    ReleaseSRWLockExclusive(&storage_image_lock);return (LONG)status;
}
static LONG NTAPI storage_image_open(PHANDLE out,ACCESS_MASK access,POBJECT_ATTRIBUTES attr,PIO_STATUS_BLOCK io,ULONG share,ULONG options)
{return storage_image_create(out,access,attr,io,NULL,0,share,1,options,NULL,0);}
static LONG storage_image_transfer(HANDLE h,HANDLE event,PVOID apc,PVOID context,PIO_STATUS_BLOCK io,PVOID bytes,
    ULONG length,PLARGE_INTEGER offset,PULONG key,int write)
{
    AcquireSRWLockShared(&storage_image_lock);struct StorageImageHandle *f=storage_image_find(h);
    if(!f) {ReleaseSRWLockShared(&storage_image_lock);return (write?storage_host_write:storage_host_read)(h,event,apc,context,io,bytes,length,offset,key);}
    uint32_t done=0,status=apc||key?MEDIA_INVALID:write?
        xllr_storage_file_write(storage_image,f->file,offset?offset->QuadPart:-2,bytes,length,&done):
        xllr_storage_file_read(storage_image,f->file,offset?offset->QuadPart:-2,bytes,length,&done);
    if(!status){io->Status=0;io->Information=done;}
    ReleaseSRWLockShared(&storage_image_lock);return (LONG)status;
}
static LONG NTAPI storage_image_read(HANDLE h,HANDLE e,PVOID a,PVOID c,PIO_STATUS_BLOCK io,PVOID b,ULONG n,PLARGE_INTEGER o,PULONG k)
{return storage_image_transfer(h,e,a,c,io,b,n,o,k,0);}
static LONG NTAPI storage_image_write(HANDLE h,HANDLE e,PVOID a,PVOID c,PIO_STATUS_BLOCK io,PVOID b,ULONG n,PLARGE_INTEGER o,PULONG k)
{return storage_image_transfer(h,e,a,c,io,b,n,o,k,1);}
static uint64_t storage_image_time(const uint16_t pair[2])
{
    if(!pair[1])return 0;
    SYSTEMTIME s={0};s.wYear=(WORD)(2000+(pair[1]>>9));s.wMonth=(WORD)((pair[1]>>5)&15);s.wDay=(WORD)(pair[1]&31);
    s.wHour=(WORD)(pair[0]>>11);s.wMinute=(WORD)((pair[0]>>5)&63);s.wSecond=(WORD)((pair[0]&31)*2);
    FILETIME f={0};if(!SystemTimeToFileTime(&s,&f))return 0;
    return ((uint64_t)f.dwHighDateTime<<32)|f.dwLowDateTime;
}
static uint32_t storage_image_pack_time(int64_t ticks,uint16_t pair[2])
{
    if(!ticks || ticks==-1){pair[0]=pair[1]=0;return 0;}
    FILETIME f={(DWORD)ticks,(DWORD)((uint64_t)ticks>>32)};SYSTEMTIME s;
    if(ticks<0 || !FileTimeToSystemTime(&f,&s) || s.wYear<2000 || s.wYear>2127)return MEDIA_INVALID;
    pair[0]=(uint16_t)((s.wHour<<11)|(s.wMinute<<5)|(s.wSecond/2));
    pair[1]=(uint16_t)(((s.wYear-2000)<<9)|(s.wMonth<<5)|s.wDay);return 0;
}
static void storage_image_metadata_bytes(const XllrStorageInfo *info,unsigned char bytes[56])
{
    uint64_t created=storage_image_time(info->times),written=storage_image_time(info->times+2),accessed=storage_image_time(info->times+4);
    memset(bytes,0,56);memcpy(bytes,&created,8);memcpy(bytes+8,&accessed,8);memcpy(bytes+16,&written,8);memcpy(bytes+24,&written,8);
    memcpy(bytes+32,&info->length,8);memcpy(bytes+40,&info->length,8);memcpy(bytes+48,&info->attributes,4);
}
static LONG NTAPI storage_image_query(HANDLE h,PIO_STATUS_BLOCK io,PVOID output,ULONG bytes,ULONG kind)
{
    AcquireSRWLockShared(&storage_image_lock);struct StorageImageHandle *f=storage_image_find(h);
    if(!f) {ReleaseSRWLockShared(&storage_image_lock);return storage_host_query(h,io,output,bytes,kind);}
    XllrStorageInfo info;uint32_t status=xllr_storage_file_info(storage_image,f->file,&info),needed=0;
    unsigned char stage[56]={0};
    if(!status) {
        if(kind==34 || kind==4) {
            storage_image_metadata_bytes(&info,stage);needed=kind==34?56:40;
            if(kind==4){memcpy(stage+32,&info.attributes,4);memset(stage+36,0,4);}
        } else if(kind==5) {
            needed=24;memcpy(stage,&info.length,8);memcpy(stage+8,&info.length,8);stage[16]=1;
            stage[20]=(unsigned char)info.delete_pending;stage[21]=(unsigned char)info.directory;
        } else if(kind==14){needed=8;memcpy(stage,&info.position,8);}
        else if(kind==8){needed=4;memcpy(stage,&info.access,4);}
        else if(kind==71){needed=4;} /* FATX is case-insensitive */
        else status=0xc0000003u;
        if(!status && bytes<needed)status=0xc0000004u;
        if(!status){memcpy(output,stage,needed);io->Status=0;io->Information=needed;}
    }
    ReleaseSRWLockShared(&storage_image_lock);return (LONG)status;
}
static LONG NTAPI storage_image_set(HANDLE h,PIO_STATUS_BLOCK io,PVOID input,ULONG bytes,ULONG kind)
{
    AcquireSRWLockShared(&storage_image_lock);struct StorageImageHandle *f=storage_image_find(h);
    if(!f) {ReleaseSRWLockShared(&storage_image_lock);return storage_host_set(h,io,input,bytes,kind);}
    uint32_t status=0,needed=kind==4?40:kind==13?1:kind==14 || kind==19 || kind==20?8:kind==10?20:0;
    if(!needed)status=0xc0000003u;else if(bytes<needed)status=0xc0000004u;
    uint64_t changed=0;
    if(!status) {
        int64_t value=0;if(bytes>=8)memcpy(&value,input,8);
        if(kind==14)status=xllr_storage_file_position(storage_image,f->file,value);
        else if(kind==19 || kind==20) {
            XllrStorageInfo previous;uint64_t before=0,after=0;
            status=xllr_storage_file_info(storage_image,f->file,&previous);
            if(!status)status=xllr_storage_file_allocation(storage_image,f->file,&before);
            if(!status)status=xllr_storage_file_resize(storage_image,f->file,value,kind==19);
            if(!status)status=xllr_storage_file_allocation(storage_image,f->file,&after);
            if(!status)changed=(kind==20?previous.length!=(uint64_t)value:before!=after)?512:0;
        }
        else if(kind==13)status=xllr_storage_file_delete(storage_image,f->file,*(unsigned char*)input!=0);
        else if(kind==4) {
            uint16_t times[6]={0};int64_t created,accessed,written;uint32_t attributes;
            memcpy(&created,input,8);memcpy(&accessed,(unsigned char*)input+8,8);memcpy(&written,(unsigned char*)input+16,8);memcpy(&attributes,(unsigned char*)input+32,4);
            status=storage_image_pack_time(created,times);
            if(!status)status=storage_image_pack_time(written,times+2);
            if(!status)status=storage_image_pack_time(accessed,times+4);
            if(!status)status=xllr_storage_file_metadata(storage_image,f->file,attributes,times);
        } else if(kind==10) {
            FILE_RENAME_INFO *rename=(FILE_RENAME_INFO*)input;char path[1024];
            struct StorageImageHandle *parent=storage_image_find(rename->RootDirectory);
            if(!parent || parent->partition!=f->partition || rename->FileNameLength>bytes-20 || rename->FileNameLength>2046)status=MEDIA_INVALID;
            else {
                UNICODE_STRING name={(USHORT)rename->FileNameLength,(USHORT)rename->FileNameLength,rename->FileName};
                status=storage_image_ascii(&name,path);
                if(!status)status=xllr_storage_file_rename(storage_image,f->file,parent->file,path,rename->ReplaceIfExists);
            }
        }
        if(!status){io->Status=0;io->Information=(ULONG_PTR)changed;}
    }
    ReleaseSRWLockShared(&storage_image_lock);return (LONG)status;
}
static LONG NTAPI storage_image_flush(HANDLE h,PIO_STATUS_BLOCK io)
{
    AcquireSRWLockShared(&storage_image_lock);struct StorageImageHandle *f=storage_image_find(h);
    if(!f) {ReleaseSRWLockShared(&storage_image_lock);return storage_host_flush(h,io);}
    uint32_t status=xllr_storage_flush(storage_image);if(!status){io->Status=0;io->Information=0;}
    ReleaseSRWLockShared(&storage_image_lock);return (LONG)status;
}
static LONG NTAPI storage_image_volume(HANDLE h,PIO_STATUS_BLOCK io,PVOID output,ULONG bytes,ULONG kind)
{
    AcquireSRWLockShared(&storage_image_lock);struct StorageImageHandle *f=storage_image_find(h);
    if(!f) {ReleaseSRWLockShared(&storage_image_lock);return storage_host_volume(h,io,output,bytes,kind);}
    uint64_t total,free;uint32_t unit,status=kind!=3?0xc0000003u:bytes<24?0xc0000004u:
        xllr_storage_capacity(storage_image,f->partition,&total,&free,&unit);
    if(!status){uint32_t sectors=unit/512,sector_bytes=512;memcpy(output,&total,8);memcpy((char*)output+8,&free,8);memcpy((char*)output+16,&sectors,4);memcpy((char*)output+20,&sector_bytes,4);io->Status=0;io->Information=24;}
    ReleaseSRWLockShared(&storage_image_lock);return (LONG)status;
}
static LONG NTAPI storage_image_directory(HANDLE h,HANDLE event,PVOID apc,PVOID context,PIO_STATUS_BLOCK io,PVOID output,
    ULONG bytes,ULONG kind,BOOLEAN single,PUNICODE_STRING pattern,BOOLEAN restart)
{
    AcquireSRWLockShared(&storage_image_lock);struct StorageImageHandle *f=storage_image_find(h);
    if(!f) {ReleaseSRWLockShared(&storage_image_lock);return storage_host_directory(h,event,apc,context,io,output,bytes,kind,single,pattern,restart);}
    char mask[1024];uint32_t status=event || apc || kind!=1?MEDIA_INVALID:bytes<64?0xc0000004u:pattern?storage_image_ascii(pattern,mask):0;
    XllrStorageDirectoryEntry e;
    if(!status)status=xllr_storage_directory(storage_image,f->file,pattern?mask:NULL,restart,&e);
    if(!status) {
        unsigned n=(unsigned)strlen(e.name),copy=n<(bytes-64)/2?n:(bytes-64)/2;
        unsigned char metadata[56];storage_image_metadata_bytes(&e.info,metadata);
        memset(output,0,64);memcpy((char*)output+8,metadata,48);memcpy((char*)output+56,&e.info.attributes,4);
        uint32_t name_bytes=n*2;memcpy((char*)output+60,&name_bytes,4);
        for(unsigned i=0;i<copy;++i)((WCHAR*)((char*)output+64))[i]=(WCHAR)(unsigned char)e.name[i];
        if(copy<n)status=0x80000005u;io->Status=(LONG)status;io->Information=64+copy*2;
    } else if(status==0x80000006u){io->Status=(LONG)status;io->Information=0;}
    ReleaseSRWLockShared(&storage_image_lock);return (LONG)status;
}
static BOOL storage_image_close(HANDLE h)
{
    if(!storage_image)return CloseHandle(h);
    AcquireSRWLockExclusive(&storage_image_lock);struct StorageImageHandle *f=storage_image_find(h);uint32_t status=0;
    if(f){status=xllr_storage_file_close(storage_image,f->file);if(!status)memset(f,0,sizeof(*f));}
    ReleaseSRWLockExclusive(&storage_image_lock);
    if(status){SetLastError(ERROR_WRITE_FAULT);return FALSE;}return CloseHandle(h);
}
static BOOL storage_image_file_size(HANDLE h,PLARGE_INTEGER size)
{
    AcquireSRWLockShared(&storage_image_lock);struct StorageImageHandle *f=storage_image_find(h);
    if(!f){ReleaseSRWLockShared(&storage_image_lock);return GetFileSizeEx(h,size);}
    XllrStorageInfo info;uint32_t status=xllr_storage_file_info(storage_image,f->file,&info);
    if(!status)size->QuadPart=(LONGLONG)info.length;
    ReleaseSRWLockShared(&storage_image_lock);return status==0;
}
static void storage_image_shutdown(void)
{
    if(!storage_image)return;
    for(unsigned i=0;i<2048;++i)if(storage_image_handles[i].event)storage_image_close(storage_image_handles[i].event);
    uint32_t status=xllr_storage_destroy(storage_image);storage_image=NULL;
    if(status)fprintf(stderr,"[STORAGE] final image flush failed status=%08x\n",status);
}
static void storage_image_mount(const char *path)
{
    char error[1024]={0};storage_image=xllr_storage_open(path,1,error,sizeof(error));
    if(!storage_image){fprintf(stderr,"[STORAGE] %s\n",error);stop("HDD image admission failed",0);}
    const char *expected=getenv("XLLE_PILOT_HDD_IDENTITY");
    if(!expected || strlen(expected)!=64)stop("HDD image requires a checked console identity",0);
    unsigned char identity[88],digest[32];uint64_t capacity=xllr_storage_bytes(storage_image);memcpy(identity,&capacity,8);
    const uint64_t positions[5]={0xabe80000,0x8ca80000,0x80000,0x2ee80000,0x5dc80000};
    for(unsigned i=0;i<5;++i)if(xllr_storage_read_blocks(storage_image,positions[i],identity+8+16*i,16))stop("HDD identity read failed",i);
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,identity,sizeof(identity),digest,32)<0)stop("HDD identity digest failed",0);
    char actual[65];for(unsigned i=0;i<32;++i)sprintf(actual+i*2,"%02x",digest[i]);
    if(strcmp(actual,expected))stop("HDD volume identity changed after setup",0);
    for(unsigned i=1;i<=5;++i) {
        uint64_t file=0;uint32_t action=0,status=xllr_storage_file_open(storage_image,i,0,"",FILE_LIST_DIRECTORY|FILE_TRAVERSE|FILE_READ_ATTRIBUTES|SYNCHRONIZE,7,0x21,1,0,0,&file,&action);
        if(status)stop("HDD image root mount failed",status);
        HANDLE event=CreateEventW(NULL,TRUE,TRUE,NULL);if(!event)stop("HDD root event unavailable",i);
        CloseHandle(storage_roots[i]);storage_roots[i]=event;
        storage_image_handles[i]=(struct StorageImageHandle){event,file,i};
    }
    storage_host_open=media_nt_open;storage_host_create=media_nt_create;storage_host_read=media_nt_read;storage_host_write=media_nt_write;
    storage_host_query=media_nt_query;storage_host_set=media_nt_set;storage_host_volume=media_nt_volume;
    storage_host_flush=media_nt_flush;storage_host_directory=media_nt_directory;
    media_nt_open=storage_image_open;media_nt_create=storage_image_create;media_nt_read=storage_image_read;media_nt_write=storage_image_write;
    media_nt_query=storage_image_query;media_nt_set=storage_image_set;media_nt_volume=storage_image_volume;
    media_nt_flush=storage_image_flush;media_nt_directory=storage_image_directory;
    atexit(storage_image_shutdown);fprintf(stderr,"[STORAGE] verified existing %s HDD; actual shared FATX partitions\n",xllr_storage_is_qcow2(storage_image)?"QCOW2":"raw");
}
/* Include before the remaining runtime services so closes release their actual
 * FATX owner. Other kernel objects retain their ordinary Windows lifetime. */
#define CloseHandle storage_image_close
#define GetFileSizeEx storage_image_file_size
