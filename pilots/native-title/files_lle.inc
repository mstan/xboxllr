/* Private original Nt/FATX/cache selection. Native events are ABI objects in
 * the separately selected scheduling family: transfer their observed state
 * to/from real original events at completed I/O boundaries. The controller
 * currently completes submitted DMA during the original call; an unfinished
 * request stops rather than inventing an IOSB or notification. */
#include "files_lle_config.h"
static SRWLOCK files_lle_lock=SRWLOCK_INIT;
#define FILES_SLOT (PILOT_IO_BRIDGE_BASE+0x3000u)
static uint32_t files_call(unsigned ordinal,const uint32_t *args)
{
    uint32_t result=0;const char *error=pilot_io_device_thread_component_active()?
        pilot_io_device_thread_component_call(ordinal,args,&result):pilot_io_device_lle(ordinal,memory,args,&result);
    if(error)stop(error,ordinal);return result;
}
static uint32_t files_string(uint32_t slot,const char *text)
{
    size_t length=strlen(text);if(length>1023)stop("original file transport path too long",0);
    MEM16(slot)=(uint16_t)length;MEM16(slot+2)=(uint16_t)length+1;MEM32(slot+4)=slot+8;
    memcpy(memory+slot+8,text,length+1);return slot;
}
static void files_lle_start(void)
{
    const char *error=pilot_io_device_files(memory);if(error)stop(error,0);
    /* Phobos kernel_hal/kernel_thunks independently identifies ordinal 40 as
     * data. Here its value comes from the original initialized kernel rather
     * than the donor's virtual-HDD constant. */
    for(size_t i=0;i<sizeof(pilot_imports)/sizeof(pilot_imports[0]);++i)if(pilot_imports[i].ordinal==40) {
        const uint32_t data=PILOT_KERNEL_DATA_BASE+0x1000u;uint32_t count;
        error=pilot_io_device_file_data(40,&count);if(error)stop(error,40);
        commit(data,4096);MEM32(data)=count;MEM32(pilot_imports[i].address)=data;
        fprintf(stderr,"[FILES LLE DATA] original cache partitions=%u\n",count);
    }
    uint32_t args[2];
    args[0]=files_string(FILES_SLOT,"\\??\\D:");
    uint32_t status=files_call(69,args);
    if(status && status!=0xc0000034u)stop("original installed-drive unlink failed",status);
    args[1]=files_string(FILES_SLOT+0x500,PILOT_FILES_TITLE_ROOT);
    status=files_call(67,args);if(status)stop("original installed-drive mount failed",status);
    fprintf(stderr,"[FILES LLE MOUNT] D: -> %s\n",PILOT_FILES_TITLE_ROOT);
}
static uint32_t files_verify(const char *expected)
{
    uint32_t slot=FILES_SLOT;int component=pilot_io_device_thread_component_active();
    if(component) {
        const char *error=pilot_io_device_thread_component_workspace(4096,&slot);
        if(error)stop(error,0);
    }
    char path[1200];snprintf(path,sizeof(path),"%s\\default.xbe",PILOT_FILES_TITLE_ROOT);
    const uint32_t attr=slot+0x800,output=attr+16,iosb=attr+24;
    MEM32(attr)=0;MEM32(attr+4)=files_string(slot,path);MEM32(attr+8)=0x40;
    uint32_t open[]={output,0x100001,attr,iosb,1,0x20},status=files_call(202,open);
    if(status)return status;uint32_t handle=MEM32(output),buffer=0;
    uint32_t in[]={0,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE,0},out[7];
    const uint32_t address=slot+0x840,size=slot+0x844;
    const char *error=NULL;
    if(component) {
        MEM32(address)=0;MEM32(size)=65536;
        uint32_t allocate[]={address,0,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE};
        status=files_call(184,allocate);out[0]=MEM32(address);
    } else error=pilot_io_device_vm(184,memory,in,out,&status);
    if(error)stop(error,184);
    if(status){files_call(187,&handle);return status;}buffer=out[0];
    BCRYPT_HASH_HANDLE hash=NULL;
    if(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&hash,NULL,0,NULL,0,0)<0)stop("original media hash setup",0);
    for(;;) {
        uint32_t args[]={handle,0,0,0,iosb,buffer,65536,0};status=files_call(219,args);
        if(status==0xc0000011u){status=0;break;}if(status)break;
        uint32_t bytes=MEM32(iosb+4);
        if(MEM32(iosb) || bytes>65536)stop("original installed media read completion",MEM32(iosb));
        if(!bytes)break;
        if(BCryptHashData(hash,memory+buffer,bytes,0)<0)stop("original media hashing",0);
    }
    unsigned char digest[32];char hex[65];
    if(!status) {
        if(BCryptFinishHash(hash,digest,32,0)<0)stop("original media hash finish",0);
        for(unsigned i=0;i<32;++i)sprintf(hex+i*2,"%02x",digest[i]);
        if(strcmp(hex,expected))status=0xc0000012u;
    }
    BCryptDestroyHash(hash);
    uint32_t release[]={buffer,0,MEM_RELEASE,0,0},released;
    if(component) {
        MEM32(address)=buffer;MEM32(size)=0;
        uint32_t free_args[]={address,size,MEM_RELEASE};released=files_call(199,free_args);
    } else error=pilot_io_device_vm(199,memory,release,out,&released);
    if(error)stop(error,199);if(released)stop("original media buffer release",released);
    if(files_call(187,&handle))stop("original media identity close",handle);
    return status;
}
static void files_verify_abi(const char *expected)
{
    AcquireSRWLockExclusive(&files_lle_lock);g_eax=files_verify(expected);
    ReleaseSRWLockExclusive(&files_lle_lock);
    fprintf(stderr,"[MEDIA VERIFY LLE FILES] actual installed HDD XBE status=%08x\n",g_eax);
    g_esp+=4;
}
static int files_lle_kernel(unsigned ordinal)
{
    unsigned count;
    switch(ordinal) {
    case 35:case 36:count=0;break;
    case 37:case 69:count=1;break;
    case 67:case 198:case 203:count=2;break;
    case 215:count=3;break;
    case 211:case 218:case 226:count=5;break;
    case 202:count=6;break;
    case 219:case 236:count=8;break;
    case 190:count=9;break;
    case 196:case 200:case 207:count=10;break;
    case 187:case 233:case 234:
        /* Native event/thread identities occupy disjoint handle ranges. File
         * and namespace handles stay the actual original kernel identities. */
        if(MEM32(g_esp+4)>=0x30000000u)return 0;
        count=ordinal==187?1:ordinal==233?3:4;break;
    default:return 0;
    }
    uint32_t args[10]={0};for(unsigned i=0;i<count;++i)args[i]=MEM32(g_esp+4+i*4);
    AcquireSRWLockExclusive(&files_lle_lock);
    HANDLE host_event=NULL;uint32_t event=0,event_status=0;
    int io=ordinal==219 || ordinal==236 || ordinal==196 || ordinal==200 || ordinal==207;
    if(io && args[2])stop("original file APC requires guest callback interop",ordinal);
    if(io && args[1]) {
        if(!InitOnceExecuteOnce(&event_once,event_start,NULL,NULL))stop("file event initialization",0);
        host_event=event_duplicate(args[1],&event_status);
        if(host_event) {
            struct {ULONG type;LONG state;} info;
            if(event_nt_query(host_event,0,&info,sizeof(info),NULL))stop("file event state query",args[1]);
            uint32_t create[]={FILES_SLOT+0xa00,0,info.type,info.state!=0};
            uint32_t status=files_call(189,create);
            if(status)stop("original file event allocation",status);
            args[1]=event=MEM32(create[0]);
        }
        /* An unowned native handle remains an invalid original handle, so the
         * original service determines validation order and error effects. */
    }
    if(ordinal==67) {
        char target[1024];uint32_t status=namespace_string(args[1],target,sizeof(target));
        if(!status && !_stricmp(target,"\\Device\\CdRom0"))
            args[1]=files_string(FILES_SLOT+0x500,PILOT_FILES_TITLE_ROOT);
    }
    unsigned invoked=ordinal;
    if(ordinal==234) {
        if(args[1]>1)stop("original file wait mode",args[1]);
        args[1]=args[2];args[2]=args[3];invoked=233;
    }
    uint32_t result=files_call(invoked,args);
    if(event) {
        uint32_t query[]={event,FILES_SLOT+0xa10};
        if(files_call(209,query))stop("original file completion event query",event);
        LONG previous;
        if((MEM32(query[1]+4)?event_nt_set:event_nt_reset)(host_event,&previous))
            stop("file completion event transport",event);
        if(result==0x103 && !MEM32(query[1]+4))stop("original file I/O still pending",ordinal);
        if(files_call(187,&event))stop("original file completion event close",event);
        if(!CloseHandle(host_event))stop("file event reference release",event);
    } else if(io && result==0x103) {
        MEM64(FILES_SLOT+0xa20)=0;
        uint32_t wait[]={args[0],0,FILES_SLOT+0xa20};
        if(files_call(233,wait))stop("original file I/O still pending",ordinal);
    }
    if(pilot_verbose || ((int32_t)result<0 && result!=0xc0000034u && result!=0xc0000011u))
        fprintf(stderr,"[FILES LLE] ordinal=%u status=%08x first=%08x\n",ordinal,result,args[0]);
    if(ordinal!=36)g_eax=result;
    g_esp+=4+count*4;ReleaseSRWLockExclusive(&files_lle_lock);return 1;
}
