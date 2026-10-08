/* Opt-in observation of actual uploaded YUV samples used by a successful
 * draw (including intermediate render targets), associated with the next
 * accepted native present. This is not
 * a second decoder or an assertion about physical display scan-out. */
static struct {
    HANDLE file;
    unsigned pending,records,finished;
    uint32_t metadata[14];
    uint64_t draw_time;
    unsigned char luma[256*256];
} graphics_movie_capture;
static void graphics_movie_capture_draw(void)
{
    const char *path=graphics_diagnostic(GRAPHICS_DIAGNOSTIC_MOVIE);
    if(!path || !*path || graphics_movie_capture.finished)return;
    for(unsigned stage=0;stage<4;++stage) {
        uint32_t header=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES+stage*4);
        if(!header)continue;
        struct GraphicsTextureObject *t=&graphics_textures[graphics_texture_slot(header)];
        if(t->format!=0x24 && t->format!=0x25)continue;
        unsigned columns=(t->width+15)/16,rows=(t->height+15)/16;
        if(columns*rows>sizeof(graphics_movie_capture.luma))stop("movie capture sample bounds",header);
        if(pilot_gfx_movie_luma(t->native,16,graphics_movie_capture.luma,columns*rows)<0)
            stop("movie capture uploaded texture unavailable",header);
        uint32_t metadata[]={0x434d4c58,1,header,t->format,t->width,t->height,
            stage,16,columns,rows,0,columns*rows,graphics_target,t->native};
        memcpy(graphics_movie_capture.metadata,metadata,sizeof(metadata));
        graphics_movie_capture.draw_time=dispatch_monotonic();graphics_movie_capture.pending=1;
        if(!graphics_movie_capture.file) {
            graphics_movie_capture.file=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
            if(graphics_movie_capture.file==INVALID_HANDLE_VALUE)stop("movie capture create",GetLastError());
            fprintf(stderr,"[MOVIE CAPTURE] uploaded luma grid and accepted-present clock; first_target=%08x back=%08x maximum36000records\n",graphics_target,graphics_headers);
        }
        break;
    }
}
static void graphics_movie_capture_present(uint64_t time,uint32_t swap,uint64_t serial)
{
    if(!graphics_movie_capture.pending)return;
    graphics_movie_capture.pending=0;graphics_movie_capture.metadata[10]=swap;
    uint64_t times[]={graphics_movie_capture.draw_time,time,serial};
    DWORD written;
#define MOVIE_WRITE(data,bytes) do {if(!WriteFile(graphics_movie_capture.file,data,bytes,&written,NULL) || written!=(bytes))stop("movie capture write",GetLastError());} while(0)
    MOVIE_WRITE(graphics_movie_capture.metadata,sizeof(graphics_movie_capture.metadata));
    MOVIE_WRITE(times,sizeof(times));
    MOVIE_WRITE(graphics_movie_capture.luma,graphics_movie_capture.metadata[11]);
#undef MOVIE_WRITE
    if(++graphics_movie_capture.records==36000) {
        if(!CloseHandle(graphics_movie_capture.file))stop("movie capture close",GetLastError());
        graphics_movie_capture.file=NULL;graphics_movie_capture.finished=1;
        fprintf(stderr,"[MOVIE CAPTURE] diagnostic record limit reached\n");
    }
}
