/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "winternl.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/file.h>
#include <sys/syscall.h>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <sys/xattr.h>
#include <sys/mman.h>

struct HostFile {
    struct HostFile *next;int fd;unsigned references;DWORD access,share;
    int delete_pending;off_t position;DIR *directory;char *pattern;
};
static struct HostFile *files;
static pthread_mutex_t file_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct HostFile *file(HANDLE pointer)
{for(struct HostFile *f=files;f;f=f->next)if(pointer==f)return f;return NULL;}
static DWORD mapped_access(DWORD access)
{
    DWORD mapped=access&~0xf0000000u;
    if(access&GENERIC_READ)mapped|=FILE_GENERIC_READ;
    if(access&GENERIC_WRITE)mapped|=FILE_GENERIC_WRITE;
    if(access&0x20000000u)mapped|=0x1200a0u;
    if(access&GENERIC_ALL)mapped|=0x1f01ffu;
    return mapped;
}
static DWORD dos_error(int value)
{return value==ENOENT?ERROR_FILE_NOT_FOUND:value==ENOTDIR?ERROR_PATH_NOT_FOUND:value==EACCES || value==EPERM || value==ELOOP?ERROR_ACCESS_DENIED:value==EEXIST?ERROR_ALREADY_EXISTS:value==ENOMEM?ERROR_NOT_ENOUGH_MEMORY:ERROR_INVALID_PARAMETER;}
BOOL CreateDirectoryA(LPCSTR path,void *security)
{
    if(!path || security){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    if(mkdir(path,0700)){SetLastError(dos_error(errno));return FALSE;}return TRUE;
}
static uint32_t nt_error(int value)
{return value==ENOENT?0xc0000034u:value==ENOTDIR?0xc0000103u:value==EISDIR?0xc00000bau:value==EEXIST?0xc0000035u:value==ENOSPC?0xc000007fu:value==EACCES || value==EPERM || value==ELOOP?0xc0000022u:0xc000000du;}
static int path_utf8(LPCWSTR path,size_t count,char *out,size_t capacity)
{
    if(count>INT_MAX)return 0;int bytes=WideCharToMultiByte(CP_UTF8,0,path,(int)count,out,(int)capacity-1,NULL,NULL);
    if(!bytes && count)return 0;out[bytes]=0;for(char *p=out;*p;++p)if(*p=='\\')*p='/';return 1;
}
static int file_path(int fd,char *out,size_t capacity)
{
    char link[64];snprintf(link,sizeof(link),"/proc/self/fd/%d",fd);ssize_t bytes=readlink(link,out,capacity-1);
    if(bytes<0 || (size_t)bytes>=capacity-1)return 0;out[bytes]=0;return 1;
}
static uint32_t attributes(const struct stat *s)
{return S_ISLNK(s->st_mode)?FILE_ATTRIBUTE_REPARSE_POINT:S_ISDIR(s->st_mode)?FILE_ATTRIBUTE_DIRECTORY:S_ISREG(s->st_mode)?FILE_ATTRIBUTE_NORMAL:FILE_ATTRIBUTE_DEVICE;}
static FILETIME timestamp(struct timespec time)
{uint64_t ticks=116444736000000000ull+(uint64_t)time.tv_sec*10000000+time.tv_nsec/100;return (FILETIME){(DWORD)ticks,(DWORD)(ticks>>32)};}
/* Walk one component at a time. Never follow a symlink, traverse '..', or
 * choose arbitrarily between two host names differing only in case.
 */
static int component(int parent,const char *name,int flags,mode_t mode,int *created)
{
    if(created)*created=0;
    int lookup_flags=flags&~(O_CREAT|O_EXCL);
    int fd=openat(parent,name,lookup_flags|O_NOFOLLOW|O_CLOEXEC,mode);
    if(fd>=0){if((flags&(O_CREAT|O_EXCL))==(O_CREAT|O_EXCL)){close(fd);errno=EEXIST;return -1;}return fd;}
    if(errno!=ENOENT)return -1;
    int scan=openat(parent,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(scan<0)return -1;
    DIR *directory=fdopendir(scan);if(!directory){close(scan);return -1;}
    char matched[NAME_MAX+1]={0};int ambiguous=0;struct dirent *entry;
    while((entry=readdir(directory)))if(!strcasecmp(entry->d_name,name)) {
        if(matched[0]){ambiguous=1;break;}strcpy(matched,entry->d_name);
    }
    closedir(directory);if(ambiguous){errno=EEXIST;return -1;}
    if(matched[0]) {
        if((flags&(O_CREAT|O_EXCL))==(O_CREAT|O_EXCL)){errno=EEXIST;return -1;}
        return openat(parent,matched,lookup_flags|O_NOFOLLOW|O_CLOEXEC,mode);
    }
    if(flags&O_CREAT) {
        fd=openat(parent,name,flags|O_EXCL|O_NOFOLLOW|O_CLOEXEC,mode);
        if(fd>=0){if(created)*created=1;return fd;}
        if(errno==EEXIST && !(flags&O_EXCL))return openat(parent,name,lookup_flags|O_NOFOLLOW|O_CLOEXEC,mode);
        return -1;
    }
    errno=ENOENT;return -1;
}
static int relative_open(int root,const char *path,int flags,mode_t mode,int make_directory,int *created)
{
    if(created)*created=0;
    if(!path || path[0]=='/' || strlen(path)>=PATH_MAX){errno=EINVAL;return -1;}
    if(!*path)return fcntl(root,F_DUPFD_CLOEXEC,3);
    char copy[PATH_MAX];strcpy(copy,path);
    size_t length=strlen(copy);while(length && copy[length-1]=='/')copy[--length]=0;
    if(!length)return fcntl(root,F_DUPFD_CLOEXEC,3);
    int parent=fcntl(root,F_DUPFD_CLOEXEC,3);if(parent<0)return -1;
    char *cursor=copy,*part;
    while((part=strsep(&cursor,"/"))) {
        if(!*part || !strcmp(part,".") || !strcmp(part,"..") || strchr(part,':')){close(parent);errno=EACCES;return -1;}
        int final=cursor==NULL;
        if(final && make_directory) {
            int existing=component(parent,part,O_RDONLY|O_DIRECTORY,0,NULL);
            if(existing>=0){close(parent);return existing;}
            if(errno!=ENOENT){int saved=errno;close(parent);errno=saved;return -1;}
            if(!mkdirat(parent,part,0700)){if(created)*created=1;}
            else if(errno!=EEXIST){int saved=errno;close(parent);errno=saved;return -1;}
        }
        int next=component(parent,part,final?flags:O_RDONLY|O_DIRECTORY,mode,final && !make_directory?created:NULL);int saved=errno;close(parent);parent=next;errno=saved;
        if(parent<0)return -1;
    }
    return parent;
}
static struct HostFile *publish_file(int fd,DWORD access,DWORD share)
{
    struct stat identity;if(fstat(fd,&identity))return NULL;
    int read=!!(access&(GENERIC_READ|FILE_READ_DATA|FILE_EXECUTE)),write=!!(access&(GENERIC_WRITE|FILE_WRITE_DATA|FILE_APPEND_DATA)),remove=!!(access&DELETE);
    for(struct HostFile *f=files;f;f=f->next) {
        struct stat existing;if(fstat(f->fd,&existing))continue;
        if(existing.st_dev!=identity.st_dev || existing.st_ino!=identity.st_ino)continue;
        int er=!!(f->access&(GENERIC_READ|FILE_READ_DATA|FILE_EXECUTE)),ew=!!(f->access&(GENERIC_WRITE|FILE_WRITE_DATA|FILE_APPEND_DATA)),ed=!!(f->access&DELETE);
        if(f->delete_pending || (read && !(f->share&FILE_SHARE_READ)) || (write && !(f->share&FILE_SHARE_WRITE)) || (remove && !(f->share&FILE_SHARE_DELETE)) || (er && !(share&FILE_SHARE_READ)) || (ew && !(share&FILE_SHARE_WRITE)) || (ed && !(share&FILE_SHARE_DELETE))){errno=EACCES;return NULL;}
    }
    struct HostFile *f=calloc(1,sizeof(*f));if(!f){errno=ENOMEM;return NULL;}
    f->fd=fd;f->references=1;f->access=access;f->share=share;f->next=files;files=f;return f;
}
int pilot_posix_file_close(HANDLE pointer)
{
    pthread_mutex_lock(&file_mutex);struct HostFile *f=file(pointer);if(!f){pthread_mutex_unlock(&file_mutex);return 0;}
    if(--f->references==0) {
        struct HostFile **at=&files;while(*at!=f)at=&(*at)->next;*at=f->next;
        if(f->delete_pending){char path[PATH_MAX];struct stat info;if(file_path(f->fd,path,sizeof(path)) && !fstat(f->fd,&info)){if(S_ISDIR(info.st_mode))rmdir(path);else unlink(path);}}
        if(f->directory)closedir(f->directory);close(f->fd);free(f->pattern);free(f);
    }
    pthread_mutex_unlock(&file_mutex);return 1;
}
int pilot_posix_file_duplicate(HANDLE pointer,HANDLE *out)
{pthread_mutex_lock(&file_mutex);struct HostFile *f=file(pointer);if(f){++f->references;*out=f;}pthread_mutex_unlock(&file_mutex);return f!=NULL;}
HANDLE CreateFileA(LPCSTR path,DWORD access,DWORD share,void *security,DWORD disposition,DWORD attrs,HANDLE template_file)
{
    if(!path || security || template_file || disposition<CREATE_NEW || disposition>TRUNCATE_EXISTING){SetLastError(ERROR_INVALID_PARAMETER);return INVALID_HANDLE_VALUE;}
    access=mapped_access(access);
    int flags=(access&(FILE_WRITE_DATA|FILE_APPEND_DATA))?((access&FILE_READ_DATA)?O_RDWR:O_WRONLY):O_RDONLY;
    struct stat directory_info;
    if((attrs&FILE_FLAG_BACKUP_SEMANTICS) && !lstat(path,&directory_info) && S_ISDIR(directory_info.st_mode)) {
        if(disposition!=OPEN_EXISTING){SetLastError(ERROR_NOT_SUPPORTED);return INVALID_HANDLE_VALUE;}
        flags=O_RDONLY|O_DIRECTORY;
    }
    if(disposition==CREATE_NEW)flags|=O_CREAT|O_EXCL;
    else if(disposition==CREATE_ALWAYS)flags|=O_CREAT;
    else if(disposition==OPEN_ALWAYS)flags|=O_CREAT;
    if(attrs&FILE_FLAG_WRITE_THROUGH)flags|=O_SYNC;
    int fd=open(path,flags|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0){SetLastError(dos_error(errno));return INVALID_HANDLE_VALUE;}
    pthread_mutex_lock(&file_mutex);struct HostFile *f=publish_file(fd,access,share);int saved=errno;pthread_mutex_unlock(&file_mutex);
    if(!f){close(fd);SetLastError(dos_error(saved));return INVALID_HANDLE_VALUE;}
    if((disposition==CREATE_ALWAYS || disposition==TRUNCATE_EXISTING) && ftruncate(fd,0)) {saved=errno;pilot_posix_file_close(f);SetLastError(dos_error(saved));return INVALID_HANDLE_VALUE;}return f;
}
HANDLE CreateFileW(LPCWSTR path,DWORD access,DWORD share,void *security,DWORD disposition,DWORD attrs,HANDLE template_file)
{char utf8[PATH_MAX];if(!path_utf8(path,wcslen(path),utf8,sizeof(utf8)))return INVALID_HANDLE_VALUE;return CreateFileA(utf8,access,share,security,disposition,attrs,template_file);}
BOOL ReadFile(HANDLE pointer,void *out,DWORD bytes,DWORD *transferred,void *overlapped)
{
    if(!transferred || (bytes && !out) || overlapped){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
    pthread_mutex_lock(&file_mutex);struct HostFile *f=file(pointer);ssize_t got=-1;
    if(f && (f->access&(GENERIC_READ|FILE_READ_DATA))){do{got=pread(f->fd,out,bytes,f->position);}while(got<0 && errno==EINTR);if(got>=0)f->position+=got;}
    else errno=EBADF;
    int saved=errno;pthread_mutex_unlock(&file_mutex);*transferred=got<0?0:(DWORD)got;if(got<0){SetLastError(dos_error(saved));return FALSE;}return TRUE;
}
BOOL WriteFile(HANDLE pointer,const void *source,DWORD bytes,DWORD *transferred,void *overlapped)
{
    if(!transferred || (bytes && !source) || overlapped){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
    if(pointer==(HANDLE)(intptr_t)-12){ssize_t written=write(STDERR_FILENO,source,bytes);*transferred=written<0?0:(DWORD)written;return written>=0;}
    pthread_mutex_lock(&file_mutex);struct HostFile *f=file(pointer);ssize_t got=-1;
    if(f && (f->access&(GENERIC_WRITE|FILE_WRITE_DATA|FILE_APPEND_DATA))){do{got=pwrite(f->fd,source,bytes,f->position);}while(got<0 && errno==EINTR);if(got>=0)f->position+=got;}
    else errno=EBADF;
    int saved=errno;pthread_mutex_unlock(&file_mutex);*transferred=got<0?0:(DWORD)got;if(got<0){SetLastError(dos_error(saved));return FALSE;}return TRUE;
}
BOOL FlushFileBuffers(HANDLE pointer)
{pthread_mutex_lock(&file_mutex);struct HostFile *f=file(pointer);int result=f?fsync(f->fd):-1;pthread_mutex_unlock(&file_mutex);if(result){SetLastError(ERROR_INVALID_HANDLE);return FALSE;}return TRUE;}
DWORD GetFileAttributesA(LPCSTR path)
{struct stat info;if(lstat(path,&info)){SetLastError(dos_error(errno));return INVALID_FILE_ATTRIBUTES;}return attributes(&info);}
DWORD GetFileAttributesW(LPCWSTR path)
{char utf8[PATH_MAX];return path_utf8(path,wcslen(path),utf8,sizeof(utf8))?GetFileAttributesA(utf8):INVALID_FILE_ATTRIBUTES;}
BOOL GetFileAttributesExA(LPCSTR path,int level,void *opaque)
{
    struct stat info;if(level || !opaque || lstat(path,&info)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    WIN32_FILE_ATTRIBUTE_DATA *out=opaque;*out=(WIN32_FILE_ATTRIBUTE_DATA){attributes(&info),timestamp(info.st_ctim),timestamp(info.st_atim),timestamp(info.st_mtim),(DWORD)((uint64_t)info.st_size>>32),(DWORD)info.st_size};return TRUE;
}
BOOL GetFileInformationByHandle(HANDLE pointer,BY_HANDLE_FILE_INFORMATION *out)
{
    if(!out)return FALSE;pthread_mutex_lock(&file_mutex);struct HostFile *f=file(pointer);struct stat info;int result=f?fstat(f->fd,&info):-1;
    if(!result)*out=(BY_HANDLE_FILE_INFORMATION){attributes(&info),timestamp(info.st_ctim),timestamp(info.st_atim),timestamp(info.st_mtim),(DWORD)info.st_dev,(DWORD)((uint64_t)info.st_size>>32),(DWORD)info.st_size,(DWORD)info.st_nlink,(DWORD)((uint64_t)info.st_ino>>32),(DWORD)info.st_ino};
    pthread_mutex_unlock(&file_mutex);if(result){SetLastError(ERROR_INVALID_HANDLE);return FALSE;}return TRUE;
}
BOOL GetFileSizeEx(HANDLE pointer,LARGE_INTEGER *out)
{BY_HANDLE_FILE_INFORMATION info;if(!out || !GetFileInformationByHandle(pointer,&info))return FALSE;out->QuadPart=(int64_t)(((uint64_t)info.nFileSizeHigh<<32)|info.nFileSizeLow);return TRUE;}
DWORD GetFinalPathNameByHandleW(HANDLE pointer,LPWSTR out,DWORD capacity,DWORD flags)
{
    char path[PATH_MAX];pthread_mutex_lock(&file_mutex);struct HostFile *f=file(pointer);int result=f && !flags && file_path(f->fd,path,sizeof(path));pthread_mutex_unlock(&file_mutex);
    if(!result){SetLastError(ERROR_INVALID_HANDLE);return 0;}int count=MultiByteToWideChar(CP_UTF8,0,path,-1,NULL,0);if(!out || (DWORD)count>capacity)return (DWORD)count;return MultiByteToWideChar(CP_UTF8,0,path,-1,out,(int)capacity)?(DWORD)count-1:0;
}
BOOL DeleteFileW(LPCWSTR path)
{char utf8[PATH_MAX];if(!path_utf8(path,wcslen(path),utf8,sizeof(utf8)))return FALSE;if(!unlink(utf8))return TRUE;SetLastError(dos_error(errno));return FALSE;}
BOOL MoveFileExW(LPCWSTR from,LPCWSTR to,DWORD flags)
{
    char a[PATH_MAX],b[PATH_MAX];if(flags&~(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH) || !path_utf8(from,wcslen(from),a,sizeof(a)) || !path_utf8(to,wcslen(to),b,sizeof(b)))return FALSE;
    int result=flags&MOVEFILE_REPLACE_EXISTING?rename(a,b):(int)syscall(SYS_renameat2,AT_FDCWD,a,AT_FDCWD,b,1);
    if(result){SetLastError(dos_error(errno));return FALSE;}return TRUE;
}
HANDLE GetStdHandle(DWORD which){return which==STD_ERROR_HANDLE?(HANDLE)(intptr_t)-12:INVALID_HANDLE_VALUE;}

/* The native title transport uses NT information class numbers, with explicit
 * conversion to Xbox records in media.inc. These functions never execute
 * a host NT subsystem or expose an OS descriptor to guest memory.
 */
static LONG complete(IO_STATUS_BLOCK *io,uint32_t status,size_t bytes)
{if(io){io->Status=(LONG)status;io->Information=bytes;}return (LONG)status;}
LONG pilot_posix_nt_create(PHANDLE out,ACCESS_MASK access,POBJECT_ATTRIBUTES attr,PIO_STATUS_BLOCK io,PLARGE_INTEGER allocation,ULONG attrs,ULONG share,ULONG disposition,ULONG options,void *ea,ULONG ea_bytes)
{
    if(!out || !attr || !attr->ObjectName || !attr->RootDirectory || !io || ea || ea_bytes || disposition>5 || attr->ObjectName->Length%2)return complete(io,0xc000000du,0);
    if(access&MAXIMUM_ALLOWED)return complete(io,0xc00000bbu,0);
    access=mapped_access(access);
    char path[PATH_MAX];if(!path_utf8(attr->ObjectName->Buffer,attr->ObjectName->Length/2,path,sizeof(path)))return complete(io,0xc0000033u,0);
    int directory=!!(options&1),write=!!(access&(GENERIC_WRITE|FILE_WRITE_DATA|FILE_APPEND_DATA));
    int flags=write?((access&(GENERIC_READ|FILE_READ_DATA))?O_RDWR:O_WRONLY):O_RDONLY;
    if(directory)flags=O_RDONLY|O_DIRECTORY;
    else if(disposition==0 || disposition==5)flags|=O_CREAT;
    else if(disposition==2)flags|=O_CREAT|O_EXCL;
    else if(disposition==3)flags|=O_CREAT;
    pthread_mutex_lock(&file_mutex);struct HostFile *parent=file(attr->RootDirectory);
    int created=0;
    int fd=parent?relative_open(parent->fd,path,flags,0600,directory && disposition!=1 && disposition!=4,&created):-1;
    if(!parent)errno=EBADF;
    if(fd<0){uint32_t status=nt_error(errno);pthread_mutex_unlock(&file_mutex);return complete(io,status,0);}
    if(directory && disposition==2 && !created){close(fd);pthread_mutex_unlock(&file_mutex);return complete(io,0xc0000035u,0);}
    struct stat info;if(fstat(fd,&info) || (!directory && options&0x40 && S_ISDIR(info.st_mode))){close(fd);pthread_mutex_unlock(&file_mutex);return complete(io,0xc00000bau,0);}
    if(allocation && allocation->QuadPart<0){close(fd);pthread_mutex_unlock(&file_mutex);return complete(io,0xc000000du,0);}
    struct HostFile *f=publish_file(fd,access,share);if(!f){close(fd);pthread_mutex_unlock(&file_mutex);return complete(io,0xc0000043u,0);}
    if(!directory && (disposition==0 || disposition==4 || disposition==5) && ftruncate(fd,allocation?allocation->QuadPart:0)) {
        uint32_t status=nt_error(errno);pthread_mutex_unlock(&file_mutex);pilot_posix_file_close(f);return complete(io,status,0);
    }
    if(options&0x1000)f->delete_pending=1;
    (void)attrs;*out=f;pthread_mutex_unlock(&file_mutex);
    return complete(io,0,created?2:disposition==0?0:(disposition==4 || disposition==5)?3:1);
}
LONG pilot_posix_nt_open(PHANDLE out,ACCESS_MASK access,POBJECT_ATTRIBUTES attr,PIO_STATUS_BLOCK io,ULONG share,ULONG options)
{return pilot_posix_nt_create(out,access,attr,io,NULL,0,share,1,options,NULL,0);}
static LONG transfer(HANDLE pointer,HANDLE event,void *apc,void *context,IO_STATUS_BLOCK *io,void *bytes,ULONG count,LARGE_INTEGER *offset,PULONG key,int writing)
{
    if(!io || (count && !bytes) || apc || context || key)return complete(io,0xc00000bbu,0);
    pthread_mutex_lock(&file_mutex);struct HostFile *f=file(pointer);
    if(!f){pthread_mutex_unlock(&file_mutex);return complete(io,0xc0000008u,0);}
    if(writing?!(f->access&(GENERIC_WRITE|FILE_WRITE_DATA|FILE_APPEND_DATA)):!(f->access&(GENERIC_READ|FILE_READ_DATA))){pthread_mutex_unlock(&file_mutex);return complete(io,0xc0000022u,0);}
    int64_t at=offset?offset->QuadPart:f->position;
    if(at==-2)at=f->position;
    if(at==-1 && writing){struct stat info;if(fstat(f->fd,&info)){pthread_mutex_unlock(&file_mutex);return complete(io,0xc0000001u,0);}at=info.st_size;}
    if(at<0){pthread_mutex_unlock(&file_mutex);return complete(io,0xc000000du,0);}
    ssize_t got;do{got=writing?pwrite(f->fd,bytes,count,at):pread(f->fd,bytes,count,at);}while(got<0 && errno==EINTR);
    uint32_t status=got<0?nt_error(errno):!writing && count && !got?0xc0000011u:0;
    if(got>=0)f->position=at+got;pthread_mutex_unlock(&file_mutex);
    complete(io,status,got<0?0:(size_t)got);if(event && !SetEvent(event))return complete(io,0xc0000008u,0);return (LONG)status;
}
LONG pilot_posix_nt_read(HANDLE f,HANDLE e,void *a,void *c,IO_STATUS_BLOCK *io,void *b,ULONG n,LARGE_INTEGER *o,PULONG k){return transfer(f,e,a,c,io,b,n,o,k,0);}
LONG pilot_posix_nt_write(HANDLE f,HANDLE e,void *a,void *c,IO_STATUS_BLOCK *io,void *b,ULONG n,LARGE_INTEGER *o,PULONG k){return transfer(f,e,a,c,io,b,n,o,k,1);}
LONG pilot_posix_nt_flush(HANDLE f,IO_STATUS_BLOCK *io){return complete(io,FlushFileBuffers(f)?0:0xc0000008u,0);}

static uint64_t nt_time(struct timespec time)
{FILETIME ft=timestamp(time);return ((uint64_t)ft.dwHighDateTime<<32)|ft.dwLowDateTime;}
static DWORD file_attributes(int fd,const struct stat *info)
{
    DWORD attrs=attributes(info),saved=0;
    if(S_ISREG(info->st_mode) || S_ISDIR(info->st_mode)) {
        attrs=S_ISDIR(info->st_mode)?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_ARCHIVE;
        if(!(info->st_mode&S_IWUSR))attrs|=FILE_ATTRIBUTE_READONLY;
        if(fgetxattr(fd,"user.xboxllr.attributes",&saved,sizeof(saved))==(ssize_t)sizeof(saved))attrs|=saved&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_ARCHIVE);
    }
    return attrs;
}
static uint64_t creation_time(int fd,const struct stat *info)
{
    uint64_t saved;
    if(fgetxattr(fd,"user.xboxllr.creation-time",&saved,sizeof(saved))==(ssize_t)sizeof(saved))return saved;
    struct statx extended;
    if(!statx(fd,"",AT_EMPTY_PATH,STATX_BTIME,&extended) && (extended.stx_mask&STATX_BTIME))
        return nt_time((struct timespec){extended.stx_btime.tv_sec,extended.stx_btime.tv_nsec});
    return nt_time(info->st_ctim);
}
static void metadata(int fd,const struct stat *info,DWORD attrs,unsigned char out[56])
{
    memset(out,0,56);uint64_t created=creation_time(fd,info),accessed=nt_time(info->st_atim),written=nt_time(info->st_mtim),changed=nt_time(info->st_ctim),allocated=(uint64_t)info->st_blocks*512,size=info->st_size;
    memcpy(out,&created,8);memcpy(out+8,&accessed,8);memcpy(out+16,&written,8);memcpy(out+24,&changed,8);memcpy(out+32,&allocated,8);memcpy(out+40,&size,8);memcpy(out+48,&attrs,4);
}
LONG pilot_posix_nt_query(HANDLE pointer,IO_STATUS_BLOCK *io,void *out,ULONG bytes,ULONG kind)
{
    if(!io || !out)return complete(io,0xc000000du,0);
    pthread_mutex_lock(&file_mutex);struct HostFile *f=file(pointer);struct stat info;unsigned char stage[56]={0};uint32_t status=0;size_t needed=0;
    if(!f)status=0xc0000008u;else if(fstat(f->fd,&info))status=nt_error(errno);
    if(!status) {
        if(kind==34){metadata(f->fd,&info,file_attributes(f->fd,&info),stage);needed=56;}
        else if(kind==4){metadata(f->fd,&info,file_attributes(f->fd,&info),stage);memcpy(stage+32,stage+48,4);memset(stage+36,0,4);needed=40;}
        else if(kind==5){uint64_t allocated=(uint64_t)info.st_blocks*512,size=info.st_size;uint32_t links=info.st_nlink;memcpy(stage,&allocated,8);memcpy(stage+8,&size,8);memcpy(stage+16,&links,4);stage[20]=f->delete_pending;stage[21]=S_ISDIR(info.st_mode);needed=24;}
        else if(kind==8){memcpy(stage,&f->access,4);needed=4;}
        else if(kind==14){memcpy(stage,&f->position,8);needed=8;}
        else if(kind==71){needed=4;} /* This provider resolves names case-insensitively. */
        else status=0xc0000003u;
        if(!status && bytes<needed)status=0xc0000004u;
        if(!status)memcpy(out,stage,needed);
    }
    pthread_mutex_unlock(&file_mutex);return complete(io,status,status?0:needed);
}
static int rename_file(struct HostFile *f,FILE_RENAME_INFO *rename,size_t bytes)
{
    size_t prefix=offsetof(FILE_RENAME_INFO,FileName);
    if(bytes<prefix || rename->FileNameLength%2 || rename->FileNameLength/2>(bytes-prefix)/sizeof(WCHAR)){errno=EINVAL;return -1;}
    struct HostFile *parent=file(rename->RootDirectory);char source[PATH_MAX],name[PATH_MAX];
    if(!parent || !file_path(f->fd,source,sizeof(source)) || !path_utf8(rename->FileName,rename->FileNameLength/2,name,sizeof(name))){errno=EINVAL;return -1;}
    /* The current title contract renames one basename within a proved root.
     * More general path moves remain unsupported rather than traverse links.
     */
    if(!*name || strchr(name,'/') || !strcmp(name,".") || !strcmp(name,"..")){errno=EACCES;return -1;}
    if(rename->ReplaceIfExists)return renameat(AT_FDCWD,source,parent->fd,name);
    return (int)syscall(SYS_renameat2,AT_FDCWD,source,parent->fd,name,1);
}
LONG pilot_posix_nt_set(HANDLE pointer,IO_STATUS_BLOCK *io,void *input,ULONG bytes,ULONG kind)
{
    if(!io || !input)return complete(io,0xc000000du,0);
    pthread_mutex_lock(&file_mutex);struct HostFile *f=file(pointer);uint32_t status=0;
    size_t needed=kind==4?40:kind==13?1:kind==14 || kind==19 || kind==20?8:kind==10?offsetof(FILE_RENAME_INFO,FileName):0;
    if(!f)status=0xc0000008u;else if(!needed)status=0xc0000003u;else if(bytes<needed)status=0xc0000004u;
    if(!status) {
        int result=0;int64_t value=0;if(bytes>=8)memcpy(&value,input,8);
        if(kind==14){if(value<0)status=0xc000000du;else f->position=value;}
        else if(kind==13){if(!(f->access&DELETE))status=0xc0000022u;else f->delete_pending=*(unsigned char*)input!=0;}
        else if(kind==10){if(!(f->access&DELETE))status=0xc0000022u;else result=rename_file(f,input,bytes);}
        else if(kind==19 || kind==20) {
            if(value<0)status=0xc000000du;
            else if(!(f->access&(GENERIC_WRITE|FILE_WRITE_DATA)))status=0xc0000022u;
            else if(kind==20)result=ftruncate(f->fd,value);
            else {struct stat previous;if(fstat(f->fd,&previous))result=-1;else if(value<previous.st_size)result=ftruncate(f->fd,value);else if(value>previous.st_blocks*512){int allocation=posix_fallocate(f->fd,0,value);if(allocation){errno=allocation;result=-1;}else result=ftruncate(f->fd,previous.st_size);}}
        } else if(kind==4) {
            uint64_t created,access,write;DWORD attrs;memcpy(&created,input,8);memcpy(&access,(char*)input+8,8);memcpy(&write,(char*)input+16,8);memcpy(&attrs,(char*)input+32,4);
            if(!(f->access&(GENERIC_WRITE|FILE_WRITE_ATTRIBUTES)))status=0xc0000022u;
            else {
                struct timespec times[2]={{0,UTIME_OMIT},{0,UTIME_OMIT}};uint64_t values[]={access,write};
                for(unsigned i=0;i<2;++i)if(values[i] && values[i]!=UINT64_MAX) {
                    if(values[i]<116444736000000000ull){status=0xc000000du;break;}
                    uint64_t t=values[i]-116444736000000000ull;times[i]=(struct timespec){(time_t)(t/10000000),(long)(t%10000000)*100};
                }
                if(!status && futimens(f->fd,times))result=-1;
                if(!status && !result && created && created!=UINT64_MAX)
                    result=fsetxattr(f->fd,"user.xboxllr.creation-time",&created,sizeof(created),0);
                if(!status && !result && attrs) {
                    DWORD stored=attrs&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_ARCHIVE);
                    if(fsetxattr(f->fd,"user.xboxllr.attributes",&stored,sizeof(stored),0))result=-1;
                    else {struct stat info;if(fstat(f->fd,&info))result=-1;else result=fchmod(f->fd,(info.st_mode&0777)&~S_IWUSR | (attrs&FILE_ATTRIBUTE_READONLY?0:S_IWUSR));}
                }
            }
        }
        if(result)status=nt_error(errno);
    }
    pthread_mutex_unlock(&file_mutex);return complete(io,status,0);
}
BOOL SetFileInformationByHandle(HANDLE f,FILE_INFO_BY_HANDLE_CLASS kind,void *input,DWORD bytes)
{
    ULONG native=kind==FileBasicInfo?4:kind==FileRenameInfo?10:kind==FileDispositionInfo?13:0;IO_STATUS_BLOCK io;
    LONG status=pilot_posix_nt_set(f,&io,input,bytes,native);if(status<0){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}return TRUE;
}
LONG pilot_posix_nt_volume(HANDLE pointer,IO_STATUS_BLOCK *io,void *out,ULONG bytes,ULONG kind)
{
    if(!io || !out || kind!=3)return complete(io,0xc0000003u,0);
    if(bytes<24)return complete(io,0xc0000004u,0);
    pthread_mutex_lock(&file_mutex);struct HostFile *f=file(pointer);struct statvfs info;int result=f?fstatvfs(f->fd,&info):-1;
    if(!result){uint64_t total=(uint64_t)info.f_blocks*info.f_frsize/16384,free=(uint64_t)info.f_bavail*info.f_frsize/16384;uint32_t sectors=32,sector=512;memcpy(out,&total,8);memcpy((char*)out+8,&free,8);memcpy((char*)out+16,&sectors,4);memcpy((char*)out+20,&sector,4);}
    pthread_mutex_unlock(&file_mutex);return complete(io,result?0xc0000008u:0,result?0:24);
}
LONG pilot_posix_nt_directory(HANDLE pointer,HANDLE event,void *apc,void *context,IO_STATUS_BLOCK *io,void *out,ULONG bytes,ULONG kind,BOOLEAN single,UNICODE_STRING *pattern,BOOLEAN restart)
{
    if(event || apc || context || !io || !out || kind!=1)return complete(io,0xc00000bbu,0);
    if(bytes<64)return complete(io,0xc0000004u,0);
    pthread_mutex_lock(&file_mutex);struct HostFile *f=file(pointer);uint32_t status=0;size_t used=0,previous=0;
    if(!f){status=0xc0000008u;goto done;}
    if(!f->directory) {
        int fd=openat(f->fd,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0){status=nt_error(errno);goto done;}
        f->directory=fdopendir(fd);if(!f->directory){close(fd);status=nt_error(errno);goto done;}
    }
    if(restart)rewinddir(f->directory);
    if(pattern) {
        char mask[PATH_MAX];if(pattern->Length%2 || !path_utf8(pattern->Buffer,pattern->Length/2,mask,sizeof(mask))){status=0xc000000du;goto done;}
        char *copy=strdup(!strcmp(mask,"*.*")?"*":mask);if(!copy){status=0xc000009au;goto done;}free(f->pattern);f->pattern=copy;
    }
    for(;;) {
        long before=telldir(f->directory);errno=0;struct dirent *entry=readdir(f->directory);
        if(!entry){if(errno)status=nt_error(errno);else if(!used)status=0x80000006u;break;}
        if(fnmatch(f->pattern?f->pattern:"*",entry->d_name,FNM_CASEFOLD))continue;
        WCHAR wide[NAME_MAX+1];int characters=MultiByteToWideChar(CP_UTF8,0,entry->d_name,-1,wide,NAME_MAX+1);
        if(!characters){status=0xc0000106u;break;}--characters;
        uint16_t utf16[NAME_MAX*2+2];size_t units=0;
        for(int i=0;i<characters;++i){uint32_t c=(uint32_t)wide[i];if(c>=0x10000){c-=0x10000;utf16[units++]=0xd800+(c>>10);utf16[units++]=0xdc00+(c&1023);}else utf16[units++]=(uint16_t)c;}
        size_t record=(64+units*2+7)&~(size_t)7;
        if(record>bytes-used){seekdir(f->directory,before);if(!used)status=0x80000005u;break;}
        struct stat info;if(fstatat(f->fd,entry->d_name,&info,AT_SYMLINK_NOFOLLOW)){status=nt_error(errno);break;}
        unsigned char *at=(unsigned char*)out+used;memset(at,0,record);uint64_t times[]={nt_time(info.st_ctim),nt_time(info.st_atim),nt_time(info.st_mtim),nt_time(info.st_ctim)},size=info.st_size,allocation=(uint64_t)info.st_blocks*512;
        DWORD attrs=attributes(&info),name_bytes=(DWORD)units*2;
        memcpy(at+8,times,32);memcpy(at+40,&size,8);memcpy(at+48,&allocation,8);memcpy(at+56,&attrs,4);memcpy(at+60,&name_bytes,4);memcpy(at+64,utf16,units*2);
        if(used){uint32_t next=(uint32_t)(used-previous);memcpy((char*)out+previous,&next,4);}previous=used;used+=record;
        if(single)break;
    }
done:
    pthread_mutex_unlock(&file_mutex);return complete(io,status,used);
}
static BOOL find_next(HANDLE pointer,WIN32_FIND_DATAW *out)
{
    unsigned char record[64+1024];IO_STATUS_BLOCK io;
    LONG status=pilot_posix_nt_directory(pointer,NULL,NULL,NULL,&io,record,sizeof(record),1,TRUE,NULL,FALSE);
    if(status){SetLastError((uint32_t)status==0x80000006u?ERROR_NO_MORE_FILES:ERROR_INVALID_DATA);return FALSE;}
    memset(out,0,sizeof(*out));memcpy(&out->ftCreationTime,record+8,8);memcpy(&out->ftLastAccessTime,record+16,8);memcpy(&out->ftLastWriteTime,record+24,8);
    uint64_t size;uint32_t name_bytes;memcpy(&size,record+40,8);memcpy(&out->dwFileAttributes,record+56,4);memcpy(&name_bytes,record+60,4);
    out->nFileSizeLow=(DWORD)size;out->nFileSizeHigh=(DWORD)(size>>32);
    const uint16_t *name=(const uint16_t*)(record+64);unsigned used=0;
    for(unsigned i=0;i<name_bytes/2;++i) {
        uint32_t c=name[i];
        if(c>=0xd800 && c<=0xdbff && i+1<name_bytes/2 && name[i+1]>=0xdc00 && name[i+1]<=0xdfff)c=0x10000+((c-0xd800)<<10)+(name[++i]-0xdc00);
        if(used>=259){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}out->cFileName[used++]=(WCHAR)c;
    }
    return TRUE;
}
HANDLE FindFirstFileW(LPCWSTR pattern,WIN32_FIND_DATAW *out)
{
    char path[PATH_MAX];if(!pattern || !out || !path_utf8(pattern,wcslen(pattern),path,sizeof(path)))return INVALID_HANDLE_VALUE;
    char *slash=strrchr(path,'/'),*mask=slash?slash+1:path;char *saved=strdup(mask);if(!saved)return INVALID_HANDLE_VALUE;
    if(slash){if(slash==path)slash[1]=0;else *slash=0;}
    int fd=open(slash?path:".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0){free(saved);SetLastError(dos_error(errno));return INVALID_HANDLE_VALUE;}
    pthread_mutex_lock(&file_mutex);struct HostFile *f=publish_file(fd,FILE_LIST_DIRECTORY,7);if(f)f->pattern=saved;pthread_mutex_unlock(&file_mutex);
    if(!f){free(saved);close(fd);SetLastError(ERROR_NOT_ENOUGH_MEMORY);return INVALID_HANDLE_VALUE;}
    if(!find_next(f,out)){pilot_posix_file_close(f);return INVALID_HANDLE_VALUE;}return f;
}
BOOL FindNextFileW(HANDLE h,WIN32_FIND_DATAW *out){return out?find_next(h,out):FALSE;}
BOOL FindClose(HANDLE h){return CloseHandle(h);}
struct Mapping {struct Mapping *next;void *base;size_t bytes;};
static struct Mapping *mappings;
void *OpenFileMappingA(DWORD access,BOOL inherit,LPCSTR name)
{
    if(access!=FILE_MAP_READ || inherit || !name || name[0]!='/'){SetLastError(ERROR_NOT_SUPPORTED);return NULL;}
    int fd=shm_open(name,O_RDONLY|O_CLOEXEC,0);if(fd<0){SetLastError(dos_error(errno));return NULL;}
    pthread_mutex_lock(&file_mutex);struct HostFile *f=publish_file(fd,GENERIC_READ,7);pthread_mutex_unlock(&file_mutex);
    if(!f)close(fd);return f;
}
void *MapViewOfFile(HANDLE h,DWORD access,DWORD high,DWORD low,SIZE_T bytes)
{
    if(access!=FILE_MAP_READ || !bytes){SetLastError(ERROR_NOT_SUPPORTED);return NULL;}
    uint64_t offset=((uint64_t)high<<32)|low;pthread_mutex_lock(&file_mutex);struct HostFile *f=file(h);struct stat info;void *base=MAP_FAILED;
    if(f && !fstat(f->fd,&info) && offset<=(uint64_t)info.st_size && bytes<=(uint64_t)info.st_size-offset)base=mmap(NULL,bytes,PROT_READ,MAP_SHARED,f->fd,(off_t)offset);
    if(base==MAP_FAILED){pthread_mutex_unlock(&file_mutex);SetLastError(ERROR_INVALID_PARAMETER);return NULL;}
    struct Mapping *mapping=malloc(sizeof(*mapping));if(!mapping){munmap(base,bytes);pthread_mutex_unlock(&file_mutex);SetLastError(ERROR_NOT_ENOUGH_MEMORY);return NULL;}
    *mapping=(struct Mapping){mappings,base,bytes};mappings=mapping;pthread_mutex_unlock(&file_mutex);return base;
}
BOOL UnmapViewOfFile(const void *base)
{
    pthread_mutex_lock(&file_mutex);struct Mapping **at=&mappings;while(*at && (*at)->base!=base)at=&(*at)->next;
    struct Mapping *mapping=*at;int result=-1;if(mapping){result=munmap(mapping->base,mapping->bytes);if(!result){*at=mapping->next;free(mapping);}}
    pthread_mutex_unlock(&file_mutex);if(result){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}return TRUE;
}
