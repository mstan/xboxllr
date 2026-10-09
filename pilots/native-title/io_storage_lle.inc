/* Original-kernel disk transport. One memory/PIC instance is shared with the
 * existing memory and USB floor. Device DMA follows the original physical
 * pages, including published native aliases; no file payload is substituted. */
#include "files_lle_config.h"
static struct {
    void *instance;
    const char *(*error)(void*);
    int (*port)(void*,uint32_t,int,int,uint32_t*);
    HANDLE exclusion;
    int ready;
} storage;
static int storage_irq_pending;
static int storage_dma(uint32_t address,void *buffer,size_t bytes,int write)
{
    if(address>RAM_BYTES || bytes>RAM_BYTES-address)return 0;
    for(size_t done=0;done<bytes;) {
        uint32_t at=address+(uint32_t)done;size_t count=4096-(at&4095);
        if(count>bytes-done)count=bytes-done;
        if(write)memcpy(physical_backing(at),(unsigned char*)buffer+done,count);
        else memcpy((unsigned char*)buffer+done,physical_backing(at),count);
        done+=count;
    }
    return 1;
}
static int storage_dma_read(uint32_t a,void *b,size_t n){return storage_dma(a,b,n,0);}
static int storage_dma_write(uint32_t a,const void *b,size_t n){return storage_dma(a,(void*)b,n,1);}
static void storage_initialize(void)
{
    if(storage.instance)return;
    /* A separate adjacent lock file excludes another pilot from mutating the
     * same provisioned image. This does not restrict ordinary fixture reads. */
    char lock_path[32768];
    int length=snprintf(lock_path,sizeof(lock_path),"%s.xlle-lock",PILOT_FILES_HDD);
    if(length<0 || (size_t)length>=sizeof(lock_path)) {
        failure="storage LLE lock path";return;
    }
    storage.exclusion=CreateFileA(lock_path,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL,NULL);
    if(storage.exclusion==INVALID_HANDLE_VALUE){failure="storage LLE HDD already in use";return;}
    DLL_DIRECTORY_COOKIE directory=AddDllDirectory(PILOT_FILES_DEPENDENCIES);
    if(!directory){failure="storage LLE dependency directory";return;}
    HMODULE library=LoadLibraryExW(PILOT_FILES_LIBRARY,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    RemoveDllDirectory(directory);
    if(!library){failure="storage LLE reference library unavailable";return;}
    void *(*create)(const char*,unsigned char*,uint32_t,uint16_t,uint16_t)=
        (void*)GetProcAddress(library,"xlle_storage_create_writable");
    void (*bus)(void*,int(*)(uint32_t,void*,size_t),int(*)(uint32_t,const void*,size_t),void(*)(int,int))=
        (void*)GetProcAddress(library,"xlle_storage_bus");
    storage.error=(void*)GetProcAddress(library,"xlle_storage_error");
    storage.port=(void*)GetProcAddress(library,"xlle_storage_port");
    if(!create || !bus || !storage.error || !storage.port){failure="storage LLE reference ABI mismatch";return;}
    storage.instance=create(PILOT_FILES_HDD,ram,RAM_BYTES,0xffff,0);
    if(!storage.instance){failure="storage LLE HDD attach failed";return;}
    bus(storage.instance,storage_dma_read,storage_dma_write,pilot_usb_pic_raise);
    failure=storage.error(storage.instance);
    if(!failure)fprintf(stderr,"[FILES LLE] actual writable HDD; original FATX/IDE/PIC, shared physical RAM\n");
}
static int storage_owns_port(uint32_t port)
{
    return (port>=0x1f0 && port<=0x1f7) || port==0x3f6 || (port>=0xff60 && port<0xff70);
}
