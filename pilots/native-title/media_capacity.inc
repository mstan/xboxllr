/* Capacity of the immutable directory-backed content provider, not a claim
 * about an original DVD's unprovided padding/physical extent. Count actual
 * named files in 2 KiB logical units plus one metadata unit per directory.
 * Cache only after the complete bounded traversal succeeds. Never follow a
 * reparse point or open file contents. Image providers can supply exact extent
 * through this same native volume interface later. */
static uint64_t media_content_sectors;
static uint32_t media_capacity_walk(HANDLE directory,unsigned depth,uint64_t *sectors)
{
    enum {capacity=16384};
    if(depth>=64 || *sectors==UINT64_MAX)return 0xc00000bbu;
    uint8_t *buffer=malloc(capacity);if(!buffer)return 0xc000009au;
    ++*sectors;uint32_t status=0;BOOLEAN restart=TRUE;
    for(;;) {
        IO_STATUS_BLOCK io={0};
        status=(uint32_t)media_nt_directory(directory,NULL,NULL,NULL,&io,buffer,capacity,1,FALSE,NULL,restart);
        if(status==0x103u)stop("content capacity requires synchronous metadata owner",depth);
        restart=FALSE;if(status==0x80000006u){status=0;break;}
        if(status)break;
        if(io.Information<64 || io.Information>capacity)stop("invalid content metadata completion",(uint32_t)io.Information);
        unsigned offset=0;
        for(;;) {
            if(offset>io.Information-64)stop("content metadata record outside completion",offset);
            uint32_t next,attributes,name_bytes;uint64_t bytes;
            memcpy(&next,buffer+offset,4);memcpy(&bytes,buffer+offset+40,8);
            memcpy(&attributes,buffer+offset+56,4);memcpy(&name_bytes,buffer+offset+60,4);
            if((name_bytes&1) || name_bytes>io.Information-offset-64 || name_bytes>=520)
                {status=0xc0000106u;break;}
            WCHAR name[260];memcpy(name,buffer+offset+64,name_bytes);name[name_bytes/2]=0;
            if(wcscmp(name,L".") && wcscmp(name,L"..")) {
                if(!name_bytes || wcschr(name,'\\') || wcschr(name,'/') || wcslen(name)*2!=name_bytes)
                    {status=MEDIA_INVALID;break;}
                if(attributes&FILE_ATTRIBUTE_REPARSE_POINT){status=0xc00000bbu;break;}
                if(attributes&FILE_ATTRIBUTE_DIRECTORY) {
                    HANDLE child=NULL;IO_STATUS_BLOCK opened={0};
                    /* Root-relative OBJ_DONT_REPARSE also closes the race
                     * between enumeration and opening the child directory. */
                    status=media_open_host(directory,name,FILE_LIST_DIRECTORY|SYNCHRONIZE,FILE_SHARE_READ,0x21,&child,&opened);
                    if(!status) {
                        status=media_capacity_walk(child,depth+1,sectors);
                        if(!CloseHandle(child))stop("content capacity child close failed",0);
                    }
                } else {
                    uint64_t units=bytes/2048+(bytes%2048!=0);
                    if((int64_t)bytes<0 || units>UINT64_MAX-*sectors)status=MEDIA_INVALID;else *sectors+=units;
                }
            }
            if(status || !next)break;
            if((next&7) || next<64+name_bytes || next>io.Information-offset)
                stop("invalid content metadata record chain",next);
            offset+=next;
        }
        if(status)break;
    }
    free(buffer);return status;
}
static uint32_t media_capacity_query(uint64_t *sectors)
{
    if(media_content_sectors){*sectors=media_content_sectors;return 0;}
    uint64_t total=0;uint32_t status=media_capacity_walk(media_root,0,&total);
    if(!status) {
        media_content_sectors=total;*sectors=total;
        fprintf(stderr,"[CONTENT VOLUME] source=mounted-metadata sectors=%llu bytes_per_sector=2048 writable=0\n",(unsigned long long)total);
    }
    return status;
}
