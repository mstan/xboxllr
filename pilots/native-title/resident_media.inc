/* Immutable content preload, outside the Xbox memory budget. Retained read-only
 * handles deny writers/deletion; opens match volume/file identity, never a path
 * suffix. Native saves, utility-cache writes and streaming exclusions stay on
 * their original I/O path. Incomplete preloads are discarded atomically. */
#include "resident_blob.h"
struct ResidentFile {HANDLE handle;BY_HANDLE_FILE_INFORMATION identity;PilotResidentBlob blob;uint64_t size;WCHAR *relative;};
static struct ResidentFile *resident_files;
static unsigned resident_count,resident_ready,resident_budget,resident_exclusion_count;
static WCHAR resident_exclusions[8][12];
static uint64_t resident_total;
static unsigned resident_file_views;
static size_t resident_root_length;
static volatile LONG64 resident_hits,resident_hit_bytes;
static int media_resident_configure(uint32_t budget,const char *const *extensions,uint32_t count)
{
    if(resident_files || budget>1536 || count>8 || (count && !extensions))return 0;
    for(unsigned i=0;i<count;++i) {
        const char *s=extensions[i];size_t n=s?strlen(s):0;
        if(n<2 || n>=12 || s[0]!='.')return 0;
        for(unsigned j=1;j<n;++j)if(s[j]<'a' || s[j]>'z')return 0;
        for(unsigned j=0;j<=n;++j)resident_exclusions[i][j]=(WCHAR)s[j];
    }
    resident_budget=budget;resident_exclusion_count=count;return 1;
}
static int media_resident_ready(void){return resident_ready!=0;}
static void media_resident_close(void)
{
    if(resident_files)for(unsigned i=0;i<resident_count;++i) {
        pilot_resident_blob_release(&resident_files[i].blob);
        if(resident_files[i].handle)CloseHandle(resident_files[i].handle);
        free(resident_files[i].relative);
    }
    free(resident_files);resident_files=NULL;resident_ready=resident_count=0;resident_total=0;
}
static int resident_same(const BY_HANDLE_FILE_INFORMATION *a,const BY_HANDLE_FILE_INFORMATION *b)
{return a->dwVolumeSerialNumber==b->dwVolumeSerialNumber && a->nFileIndexHigh==b->nFileIndexHigh && a->nFileIndexLow==b->nFileIndexLow;}
static const struct ResidentFile *media_resident_find(HANDLE handle)
{
    if(!resident_ready)return NULL;
    BY_HANDLE_FILE_INFORMATION info;
    if(!GetFileInformationByHandle(handle,&info))return NULL;
    for(unsigned i=0;i<resident_count;++i)if(resident_same(&info,&resident_files[i].identity))return &resident_files[i];
    return NULL;
}
static int resident_scan(WCHAR *path,unsigned depth)
{
    size_t base=wcslen(path);
    if(depth>16 || base>32000)return 0;
    memcpy(path+base,L"\\*",3*sizeof(WCHAR));
    WIN32_FIND_DATAW entry;HANDLE search=FindFirstFileW(path,&entry);path[base]=0;
    if(search==INVALID_HANDLE_VALUE)return 0;
    int ok=1;
    do {
        if(!wcscmp(entry.cFileName,L".") || !wcscmp(entry.cFileName,L".."))continue;
        if(entry.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){ok=0;break;}
        size_t n=wcslen(entry.cFileName);
        if(base+n+2>32767){ok=0;break;}
        path[base]='\\';memcpy(path+base+1,entry.cFileName,(n+1)*sizeof(WCHAR));
        if(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)ok=resident_scan(path,depth+1);
        else {
            const WCHAR *ext=wcsrchr(entry.cFileName,L'.');unsigned excluded=0;
            for(unsigned i=0;ext && i<resident_exclusion_count;++i)if(!_wcsicmp(ext,resident_exclusions[i]))excluded=1;
            if(!excluded) {
                uint64_t size=((uint64_t)entry.nFileSizeHigh<<32)|entry.nFileSizeLow;
                if(resident_count==8192 || size>(uint64_t)resident_budget*1024*1024-resident_total)ok=0;
                else {
                    struct ResidentFile *file=&resident_files[resident_count++];
                    file->handle=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
                    if(file->handle==INVALID_HANDLE_VALUE){file->handle=NULL;ok=0;}
                    else if(!GetFileInformationByHandle(file->handle,&file->identity) ||
                        (file->identity.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) ||
                        (((uint64_t)file->identity.nFileSizeHigh<<32)|file->identity.nFileSizeLow)!=size)ok=0;
                    else if(size) {
                        ok=pilot_resident_blob_load(file->handle,size,resident_file_views,&file->blob);
                    }
                    if(ok) {
                        size_t characters=wcslen(path+resident_root_length+1)+1;
                        file->relative=malloc(characters*sizeof(WCHAR));
                        if(!file->relative)ok=0;
                        else memcpy(file->relative,path+resident_root_length+1,characters*sizeof(WCHAR));
                    }
                    if(ok){file->size=size;resident_total+=size;}
                }
            }
        }
        path[base]=0;
        if(!ok)break;
    }while(FindNextFileW(search,&entry));
    if(ok && GetLastError()!=ERROR_NO_MORE_FILES)ok=0;
    FindClose(search);path[base]=0;return ok;
}
static void media_resident_prepare(void)
{
    if(!resident_budget || !media_content_path || resident_files)return;
    const char *views=getenv("XLLE_PILOT_RESIDENT_FILE_VIEWS");
    resident_file_views=views?!strcmp(views,"1"):PILOT_RESIDENT_FILE_VIEWS_AVAILABLE;
    uint64_t begin=dispatch_monotonic();MEMORYSTATUSEX memory_status={sizeof(memory_status)};
    WCHAR *path=calloc(32768,sizeof(WCHAR));
    int ok=path && GlobalMemoryStatusEx(&memory_status) &&
        memory_status.ullAvailPhys>(uint64_t)(resident_budget+1024)*1024*1024 &&
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,media_content_path,-1,path,32768);
    if(ok) {
        DWORD attr=GetFileAttributesW(path);
        ok=attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_DIRECTORY) && !(attr&FILE_ATTRIBUTE_REPARSE_POINT);
    }
    if(ok){resident_root_length=wcslen(path);resident_files=calloc(8192,sizeof(*resident_files));ok=resident_files && resident_scan(path,0);}
    free(path);
    if(!ok){media_resident_close();fprintf(stderr,"[RESIDENT MEDIA] preload unavailable; original I/O retained\n");return;}
    resident_ready=1;atexit(media_resident_close);
    fprintf(stderr,"[RESIDENT MEDIA] ready files=%u bytes=%llu elapsed_ms=%.3f streaming_exclusions=%u\n",
        resident_count,(unsigned long long)resident_total,(dispatch_monotonic()-begin)/10000.0,resident_exclusion_count);
    if(resident_file_views){
        unsigned mapped=0;uint64_t mapped_bytes=0;
        for(unsigned i=0;i<resident_count;++i)if(resident_files[i].blob.mapped){++mapped;mapped_bytes+=resident_files[i].size;}
        fprintf(stderr,"[RESIDENT MEDIA OWNER] requested=1 available=%u mapped_files=%u mapped_bytes=%llu\n",
            PILOT_RESIDENT_FILE_VIEWS_AVAILABLE,mapped,(unsigned long long)mapped_bytes);
    }
}
static int media_resident_read(struct MediaFile *file,IO_STATUS_BLOCK *io,void *buffer,unsigned length,
    LARGE_INTEGER *offset,uint32_t *status)
{
    const struct ResidentFile *resident=file->resident;
    if(!resident || file->nonbuffered || !length || !offset || offset->QuadPart<0)return 0;
    uint64_t start=(uint64_t)offset->QuadPart;
    if(start>=resident->size){*status=0xc0000011u;return 1;}
    unsigned count=length;
    if(count>resident->size-start)count=(unsigned)(resident->size-start);
    /* A mapping fault falls back to the original read. Leave the host cursor
     * untouched until the guarded copy succeeds, including NULL/-2 reads. */
    if(!pilot_resident_blob_copy(&resident->blob,start,buffer,count))return 0;
    if(!file->asynchronous) {
        LARGE_INTEGER next;next.QuadPart=start+count;IO_STATUS_BLOCK position_io={0};
        *status=(uint32_t)media_nt_set(file->host,&position_io,&next,sizeof(next),14);
        if(*status)return 1;
    }
    io->Status=0;io->Information=count;*status=0;
    InterlockedIncrement64(&resident_hits);InterlockedAdd64(&resident_hit_bytes,count);return 1;
}
