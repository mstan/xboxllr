/* Exercise the runtime's shared original kernel/RAM/PIC transport, rather
 * than the separate Python fixture. Owned media stays private. */
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "io_device_lle.h"
#include "files_lle_config.h"
static unsigned char *guest;
#define SLOT (PILOT_IO_BRIDGE_BASE+0x3000)
#define W(at) (*(uint32_t*)(guest+(at)))
static void check(int okay,const char *why)
{if(!okay){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
static uint32_t call(unsigned ordinal,const uint32_t *args)
{
    uint32_t result=0;const char *error=pilot_io_device_lle(ordinal,guest,args,&result);
    if(error){fprintf(stderr,"ordinal %u: %s\n",ordinal,error);exit(1);}return result;
}
int main(int argc,char **argv)
{
    check(argc==2 || argc==3,"comparison file and optional write/verify-delete phase required");
    if(argc==3)check(!strcmp(argv[2],"write") || !strcmp(argv[2],"verify-delete"),"known persistence phase");
    guest=VirtualAlloc(NULL,(SIZE_T)0x100000000ull,MEM_RESERVE,PAGE_NOACCESS);
    check(guest!=NULL,"guest reserve");
    const char *error=pilot_io_device_files(guest);
    if(error){fprintf(stderr,"bootstrap: %s\n",error);return 1;}
    char path[2048];snprintf(path,sizeof(path),"%s\\default.xbe",PILOT_FILES_TITLE_ROOT);
    uint32_t attr=SLOT,name=SLOT+32,text=SLOT+64,output=SLOT+0x900,iosb=output+8;
    W(attr)=0;W(attr+4)=name;W(attr+8)=0x40;
    *(uint16_t*)(guest+name)=(uint16_t)strlen(path);
    *(uint16_t*)(guest+name+2)=(uint16_t)strlen(path)+1;W(name+4)=text;
    memcpy(guest+text,path,strlen(path)+1);
    uint32_t open[]={output,0x100001,attr,iosb,1,0x20};
    uint32_t status=call(202,open);
    fprintf(stderr,"original installed open: %08x handle=%08x\n",status,W(output));
    check(status==0,"original FATX installed file open");uint32_t file=W(output);
    uint32_t in[]={0,65536,0x3000,4,0},out[7],vm_status;
    error=pilot_io_device_vm(184,guest,in,out,&vm_status);
    check(!error && !vm_status,"original read-buffer allocation");uint32_t buffer=out[0];
    FILE *expected=fopen(argv[1],"rb");check(expected!=NULL,"comparison input");
    unsigned char bytes[65536];uint64_t total=0;
    for(;;) {
        size_t count=fread(bytes,1,sizeof(bytes),expected);
        uint32_t read[]={file,0,0,0,iosb,buffer,sizeof(bytes),0};
        status=call(219,read);
        if(!count){check(feof(expected) && status==0xc0000011u,"matching original EOF");break;}
        check(status==0 && W(iosb)==0 && W(iosb+4)==count,"original read status/count");
        check(!memcmp(bytes,guest+buffer,count),"CPU sees exact IDE DMA bytes");total+=count;
    }
    fclose(expected);check(call(187,&file)==0,"original file close");
    if(argc==3) {
        const char *fixture="\\Device\\Harddisk0\\Partition1\\xlle-file-lifetime.bin";
        memcpy(guest+text,fixture,strlen(fixture)+1);
        *(uint16_t*)(guest+name)=(uint16_t)strlen(fixture);
        *(uint16_t*)(guest+name+2)=(uint16_t)strlen(fixture)+1;
        unsigned char expected_bytes[9001];
        for(unsigned i=0;i<sizeof(expected_bytes);++i)expected_bytes[i]=(unsigned char)(i*37+(i>>7));
        if(!strcmp(argv[2],"write")) {
            uint32_t create[]={output,0xc0110080u,attr,iosb,0,0x80,3,2,0x60};
            check(call(190,create)==0,"exclusive persistence fixture create");file=W(output);
            memcpy(guest+buffer,expected_bytes,sizeof(expected_bytes));
            uint32_t write[]={file,0,0,0,iosb,buffer,sizeof(expected_bytes),0};
            check(call(236,write)==0 && W(iosb+4)==sizeof(expected_bytes),"actual original FATX write");
            uint32_t flush[]={file,iosb};check(call(198,flush)==0,"original flush");
            check(call(187,&file)==0,"persistent file close");
            fprintf(stderr,"PASS: 9001 bytes written/flushed/closed for separate-process verification\n");
        } else {
            open[1]=0x110001;check(call(202,open)==0,"separate-process persisted open");file=W(output);
            uint32_t read[]={file,0,0,0,iosb,buffer,sizeof(expected_bytes)+1,0};
            check(call(219,read)==0 && W(iosb+4)==sizeof(expected_bytes),"persisted short read");
            check(!memcmp(guest+buffer,expected_bytes,sizeof(expected_bytes)),"exact persisted bytes");
            guest[buffer]=1;uint32_t remove[]={file,iosb,buffer,1,13};
            check(call(226,remove)==0,"original delete disposition");
            check(call(187,&file)==0,"original close applies delete");
            check(call(202,open)==0xc0000034u,"actual deleted file missing");
            fprintf(stderr,"PASS: separate-process exact readback and real original deletion\n");
        }
    }
    uint32_t release[]={buffer,0,MEM_RELEASE,0,0};
    error=pilot_io_device_vm(199,guest,release,out,&vm_status);
    check(!error && !vm_status,"original read-buffer release");
    fprintf(stderr,"PASS: %llu exact installed XBE bytes through original FATX/cache/IDE and runtime RAM/PIC\n",(unsigned long long)total);
    return 0;
}
