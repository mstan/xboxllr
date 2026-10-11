/* Original XeLoadSection/XeUnloadSection own the loader lock, file reads,
 * reference/boundary counters, VM mutations and byte/zero-fill policy. The
 * bridge supplies current-launch identity and exposes completed original RAM. */
static uint32_t original_section_table,original_section_count;
static uint32_t *original_section_descriptors;
static uint32_t original_section_file_value[2];
static char original_section_file_text[4096];
static uint32_t original_section_file_address;
static uint32_t original_section_launch_worker;
static int original_section_launch_ready;
#define SECTION_BOOT_PARK (SCRATCH+0xa000u)
static uint32_t original_section_launch_event,original_section_launch_event_handle;
static unsigned char original_section_boot_code[1193],original_section_park_code[25];
static int original_section_boot_seen;
static int section_boot_admit(uc_engine *engine,uint32_t address)
{
    if(!original_section_launch_worker || kernel_word(0x8003da64u)!=original_section_launch_worker)return 0;
    if(address==0x800333e5u) {
        if(original_section_boot_seen || original_section_launch_ready)failure="captured launch resumed during installed-title ownership";
        else pending_section_boot=1;
        uc_emu_stop(engine);return 1;
    }
    if(address>=SECTION_BOOT_PARK && address<SECTION_BOOT_PARK+sizeof(original_section_park_code)) {
        if(!original_section_boot_seen || original_section_launch_ready ||
           memcmp(guest_memory+SECTION_BOOT_PARK,original_section_park_code,sizeof(original_section_park_code)))
            failure="original launch handoff adapter ownership changed";
        if(failure)uc_emu_stop(engine);
        return 1;
    }
    return 0;
}
static void section_boot_prepare(uint32_t *entry)
{
    uint32_t pc=0,sp=0,flags=0,empty[2],frame[4];unsigned char code[1193];
    static const char captured_path[]="\\Device\\CdRom0\\default.xbe";
    char path[sizeof(captured_path)];
    if(uc_reg_read(cpu,UC_X86_REG_EIP,&pc)!=UC_ERR_OK || uc_reg_read(cpu,UC_X86_REG_ESP,&sp)!=UC_ERR_OK ||
       uc_reg_read(cpu,UC_X86_REG_EFLAGS,&flags)!=UC_ERR_OK || pc!=0x800333e5u ||
       kernel_word(0x8003da64u)!=original_section_launch_worker || (kernel_word(0x8003da60u)&255) ||
       (flags&0x600)!=0x200 || sp<kernel_word(original_section_launch_worker+0x20u)+52 ||
       (uint64_t)sp+16>kernel_word(original_section_launch_worker+0x1cu) ||
       vm_caller_read(sp,frame,sizeof(frame))!=UC_ERR_OK || frame[0]!=0x80033c30u ||
       frame[1]!=0x8003e400u || frame[2] || frame[3] ||
       vm_caller_read(frame[1],path,sizeof(path))!=UC_ERR_OK || memcmp(path,captured_path,sizeof(path)) ||
       vm_caller_read(pc,code,sizeof(code))!=UC_ERR_OK || memcmp(code,original_section_boot_code,sizeof(code)) ||
       vm_caller_read(original_section_file_address,empty,sizeof(empty))!=UC_ERR_OK || empty[0] || empty[1]) {
        failure="original launch handoff outside reviewed pre-image boundary";return;
    }
    original_section_boot_seen=1;
    fprintf(stderr,"[ORIGINAL BOOT IMAGE BOUNDARY] pc=%08x thread=%08x stack=%08x event=%08x\n",
        pc,original_section_launch_worker,sp,original_section_launch_event);
    /* Complete captured I/O initialization, then notify the original higher-
     * priority loader caller before this first whole image-load body executes.
     * Its original suspension follows the real event wake, outside the hook. */
    *entry=SECTION_BOOT_PARK;
}
static struct SectionPageLease {unsigned count,unloads;DWORD protection;} *original_section_leases;
static void section_pin_prepare(uint32_t base,uint32_t bytes)
{
    if(!original_section_leases)return;
    for(uint64_t page=base&~4095u;page<(uint64_t)base+bytes;page+=4096)
        if(page>=image_start && page<(uint64_t)image_start+image_bytes &&
           original_section_leases[(page-image_start)/4096].unloads) {
            failure="original section pin while unload owns image pages";return;
        }
}
static int section_original_page(uint32_t page)
{
    if(!original_section_descriptors)return 0;
    struct OriginalThread *thread=thread_object(kernel_word(0x8003da64u));
    return thread && thread->in_kernel && thread->call_ordinal==327 && thread->section_access.active &&
        page>=thread->section_access.base && (uint64_t)page<(uint64_t)thread->section_access.base+thread->section_access.bytes;
}
static void section_caller_prepare(unsigned ordinal,const uint32_t *args,struct SectionCallerAccess *access)
{
    if(ordinal!=327 && ordinal!=328)return;
    uint32_t descriptor=args[0];
    if(!original_section_descriptors || !original_section_launch_ready || access->active || descriptor<original_section_table ||
       (descriptor-original_section_table)%56 || (descriptor-original_section_table)/56>=original_section_count) {
        failure="original section call outside current image ownership";return;
    }
    uint32_t filename[2];
    uint32_t filename_address=original_export(326);
    if(filename_address!=original_section_file_address) {
        fprintf(stderr,"[SECTION FILENAME EXPORT] actual=%08x expected=%08x\n",filename_address,original_section_file_address);
        failure="original section filename export identity changed";return;
    }
    if(vm_caller_read(filename_address,filename,sizeof(filename))!=UC_ERR_OK) {
        failure="original section filename owner is not resident";return;
    }
    unsigned length=original_section_file_value[0]&65535u;
    char text[4096];
    if(vm_caller_read(original_section_file_value[1],text,length+1)!=UC_ERR_OK) {
        failure="original section filename allocation is not resident";return;
    }
    if(memcmp(filename,original_section_file_value,sizeof(filename)) ||
       memcmp(text,original_section_file_text,length+1) ||
       ((kernel_word(original_section_launch_worker+0x74u)>>8)&255)!=1) {
        fprintf(stderr,"[SECTION FILENAME GUARD] address=%08x actual=%08x/%08x expected=%08x/%08x\n",
            filename_address,filename[0],filename[1],original_section_file_value[0],original_section_file_value[1]);
        fprintf(stderr,"[SECTION FILENAME BYTES]");
        for(unsigned i=0;i<length+1 && i<128;++i)fprintf(stderr,"%02x",(unsigned char)text[i]);
        fprintf(stderr,"\n");
        failure="original section filename lifetime changed";return;
    }
    unsigned index=(descriptor-original_section_table)/56;
    const uint32_t *expected=original_section_descriptors+index*14;
    uint32_t current[14];
    if(vm_caller_read(descriptor,current,sizeof(current))!=UC_ERR_OK) {
        failure="original section descriptor is not resident";return;
    }
    for(unsigned i=0;i<14;++i)if(i!=6 && current[i]!=expected[i]) {
        failure="original section descriptor differs from current immutable image";return;
    }
    uint32_t base=current[1]&~4095u,end=(current[1]+current[2]+4095)&~4095u;
    for(uint32_t page=base;page<end;page+=4096) {
        unsigned slot=publication_slot(page);
        if(slot<published_count && published[slot].pins && ordinal==328) {
            failure="original section unload while DMA pinned";return;
        }
    }
    access->base=base;access->bytes=end-base;access->active=1;
    for(uint32_t page=base;page<end;page+=4096) {
        struct SectionPageLease *lease=original_section_leases+(page-image_start)/4096;
        if(!lease->count && publication_slot(page)<published_count) {
            if(!VirtualProtect(guest_memory+page,4096,PAGE_READWRITE,&lease->protection)) {
                failure="original section kernel access lease failed";return;
            }
        }
        ++lease->count;
        if(ordinal==328)++lease->unloads;
    }
}
static void section_caller_complete(unsigned ordinal,const uint32_t *args,uint32_t status,struct SectionCallerAccess *access)
{
    if(ordinal!=327 && ordinal!=328)return;
    if(!access->active){failure="original section returned without image lease";return;}
    uint32_t base=access->base,end=base+access->bytes;
    /* Retire original decommits first, including all stale CPU/DMA aliases. */
    check_publications(0);if(failure)return;
    for(uint32_t page=base;page<end;) {
        uint32_t pa;
        if(publication_slot(page)<published_count || !physical(page,&pa)){page+=4096;continue;}
        uint32_t first=page;
        do {page+=4096;} while(page<end && publication_slot(page)==published_count && physical(page,&pa));
        section_publication_base=first;section_publication_bytes=page-first;
        publish_range(first,page-first);section_publication_base=section_publication_bytes=0;
        if(failure)return;
    }
    for(uint32_t page=base;page<end;page+=4096) {
        struct SectionPageLease *lease=original_section_leases+(page-image_start)/4096;
        if(!lease->count){failure="original section access lease underflow";return;}
        --lease->count;
        if(ordinal==328) {
            if(!lease->unloads){failure="original section unload lease underflow";return;}
            --lease->unloads;
        }
    }
    memset(access,0,sizeof(*access));
    /* Query policy in original VAD runs, avoiding one kernel query per page.
     * An overlapping original loader keeps its host access until its own RET. */
    for(uint32_t page=base;page<end;) {
        uint32_t info[7],result=0;vm_original_query(page,info,&result);if(failure)return;
        if(result || !info[3] || (uint64_t)info[0]+info[3]<=page) {
            failure="original section completion policy query failed";return;
        }
        uint32_t limit=(uint32_t)((uint64_t)info[0]+info[3]<end?(uint64_t)info[0]+info[3]:end);
        if(info[4]==MEM_COMMIT)while(page<limit) {
            struct SectionPageLease *lease=original_section_leases+(page-image_start)/4096;
            if(lease->count){page+=4096;continue;}
            uint32_t first=page;
            do {page+=4096;} while(page<limit && !original_section_leases[(page-image_start)/4096].count);
            DWORD previous;
            if(!VirtualProtect(guest_memory+first,page-first,info[5],&previous)) {
                failure="original section completion protection publication failed";return;
            }
        } else page=limit;
    }
    fprintf(stderr,"[ORIGINAL SECTION] ordinal=%u descriptor=%08x status=%08x refs=%u range=%08x/%u\n",
        ordinal,args[0],status,kernel_word(args[0]+24),base,end-base);
    if((int32_t)status<0)fprintf(stderr,"[SECTION OPEN CONTEXT] filename=%s pointer=%08x length=%u thread=%08x\n",
        original_section_file_text,original_section_file_value[1],original_section_file_value[0]&65535u,kernel_word(0x8003da64u));
}
const char *pilot_io_device_sections(unsigned char *guest,const char *image_file)
{
    if(!guest || !image_file || !*image_file)return "original sections require current installed image path";
    size_t length=strlen(image_file);
    if(length>=4096)return "original section image path exceeds retained range";
    AcquireSRWLockExclusive(&lock);
    if(!failure && (guest_memory!=guest || execution_depth || clock_provider || image_start!=0x10000 ||
       !image_bytes || original_section_descriptors))failure="original section bootstrap outside current image context";
    uint32_t header[6]={0};
    if(!failure && vm_caller_read(image_start+0x108,header,sizeof(header))!=UC_ERR_OK)
        failure="original section current image header is not resident";
    uint32_t count=header[5],table=0;
    if(!failure && vm_caller_read(image_start+0x120,&table,4)!=UC_ERR_OK)
        failure="original section table header is not resident";
    if(!failure && (!header[0] || header[0]>image_bytes || header[1]>image_bytes || !count || count>4096 ||
       table<image_start || (uint64_t)table+count*56>(uint64_t)image_start+header[0]))
        failure="original section bootstrap table outside current image";
    if(!failure) {
        original_section_descriptors=HeapAlloc(GetProcessHeap(),0,count*56);
        original_section_leases=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(image_bytes/4096)*sizeof(*original_section_leases));
        if(!original_section_descriptors || !original_section_leases)failure="original section bootstrap ledger allocation failed";
    }
    if(!failure && vm_caller_read(table,original_section_descriptors,count*56)!=UC_ERR_OK)
        failure="original section bootstrap descriptor read failed";
    for(unsigned i=0;!failure && i<count;++i) {
        uint32_t *s=original_section_descriptors+i*14;
        if(!s[2] || s[1]<image_start+header[0] || (uint64_t)s[1]+s[2]>(uint64_t)image_start+image_bytes || s[4]>s[2] ||
           s[7]<image_start || (uint64_t)s[7]+2>(uint64_t)image_start+header[0] || (s[7]&1) ||
           s[8]<image_start || (uint64_t)s[8]+2>(uint64_t)image_start+header[0] || (s[8]&1))
            failure="original section bootstrap descriptor extent invalid";
    }
    if(!failure) {
        uint32_t name=original_export(326),empty[2];
        if(vm_caller_read(name,empty,sizeof(empty))!=UC_ERR_OK || empty[0] || empty[1])
            failure="original section filename already has an unowned lifetime";
        else {
            /* Retain the exact captured launch job. Its real I/O initialization
             * must complete before parking at the first whole image-load body. */
            const uint32_t out=SCRATCH+0xe000u;
            uint32_t ref[]={4,original_export(259),out},status=0,worker=0;
            ++execution_depth;execute(246,ref,&status);--execution_depth;
            if(!failure && (status || vm_caller_read(out,&worker,4)!=UC_ERR_OK || worker!=0xd0008bf8u ||
               kernel_word(worker+0x12cu)!=4 || kernel_word(worker+0x130u)!=0x80018abdu ||
               (kernel_word(worker+0x2cu)&255)!=1 || ((kernel_word(worker+0x74u)>>8)&255) ||
               kernel_word(0x8003da64u)==worker))failure="original captured launch worker identity changed";
            if(!failure) {
                original_section_launch_worker=worker;
                memcpy(original_section_file_text,image_file,length+1);
                original_section_file_value[0]=(uint32_t)length|((uint32_t)(length+1)<<16);
                original_section_table=table;original_section_count=count;original_section_file_address=name;
                unsigned char hash[32];char hex[65];
                if(vm_caller_read(0x800333e5u,original_section_boot_code,sizeof(original_section_boot_code))!=UC_ERR_OK ||
                   BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,original_section_boot_code,sizeof(original_section_boot_code),hash,32)<0)
                    failure="original launch helper binding failed";
                if(!failure) {
                    for(unsigned i=0;i<32;++i)sprintf(hex+i*2,"%02x",hash[i]);
                    if(strcmp(hex,"8c10cdd5597389046c06073043ccf9e872229d56b57ab8cead6759d04ce5fa3c"))
                        failure="original launch helper differs from reviewed body";
                }
                uint32_t create[]={out,0,0,0};
                if(!failure){++execution_depth;execute(189,create,&status);--execution_depth;}
                if(!failure && (status || vm_caller_read(out,&original_section_launch_event_handle,4)!=UC_ERR_OK))
                    failure="original launch handoff event creation failed";
                uint32_t event_ref[]={original_section_launch_event_handle,0,out};
                if(!failure){++execution_depth;execute(246,event_ref,&status);--execution_depth;}
                if(!failure && (status || vm_caller_read(out,&original_section_launch_event,4)!=UC_ERR_OK ||
                   !original_section_launch_event))failure="original launch handoff event reference failed";
                if(!failure) {
                    uint32_t set=original_export(145);
                    unsigned char glue[25]={0x9c,0x60,0x6a,0,0x6a,0,0x68,0,0,0,0,0xb8,0,0,0,0,0xff,0xd0,0x61,0x9d,0xe9,0,0,0,0};
                    uint32_t jump=0x800333e5u-(SECTION_BOOT_PARK+sizeof(glue));
                    memcpy(glue+7,&original_section_launch_event,4);memcpy(glue+12,&set,4);memcpy(glue+21,&jump,4);
                    memcpy(original_section_park_code,glue,sizeof(glue));
                    if(uc_mem_write(cpu,SECTION_BOOT_PARK,glue,sizeof(glue))!=UC_ERR_OK)failure="original launch handoff adapter publication failed";
                }
            }
        }
    }
    const char *error=failure;ReleaseSRWLockExclusive(&lock);return error;
}
const char *pilot_io_device_sections_start(void)
{
    AcquireSRWLockExclusive(&lock);
    if(!failure && (!original_section_launch_worker || original_section_launch_ready ||
       execution_depth || !clock_provider || native_thread_worker))
        failure="original section launch activation outside initialized loader";
    if(!failure) {
        /* Finish the real I/O initialization first. A genuine event wake at the
         * reviewed image-load boundary returns this priority31 caller before
         * the captured lower-priority job can enter the full image loader. */
        int64_t delay=-1000000;const uint32_t slot=SCRATCH+0xe100u;
        uint32_t args[]={0,0,slot},status=0;
        int64_t deadline=-50000000;
        if(((kernel_word(kernel_word(0x8003da64u)+0x30u)>>16)&255)!=31)
            failure="original launch handoff requires initialized priority31 loader caller";
        if(!failure && uc_mem_write(cpu,slot,&deadline,sizeof(deadline))!=UC_ERR_OK)failure="original launch event deadline publication failed";
        uint32_t join[]={original_section_launch_event_handle,0,slot};
        if(!failure){++execution_depth;execute(233,join,&status);--execution_depth;}
        if(!failure && (status || !original_section_boot_seen))failure="original I/O initialization did not reach image handoff";
        uint32_t suspend[]={4,slot+8},previous=~0u;
        if(!failure){++execution_depth;execute(231,suspend,&status);--execution_depth;}
        if(!failure && (status || vm_caller_read(slot+8,&previous,4)!=UC_ERR_OK || previous))
            failure="original captured launch suspension failed";
        /* Its actual suspend wait, after queued APC delivery, owns the handoff. */
        if(uc_mem_write(cpu,slot,&delay,sizeof(delay))!=UC_ERR_OK)failure="original launch delay frame failed";
        if(!failure){++execution_depth;execute(99,args,&status);--execution_depth;}
        uint32_t worker=original_section_launch_worker;
        uint32_t wait=kernel_word(worker+0x58u),stack=kernel_word(worker+0x20u),top=kernel_word(worker+0x1cu);
        if(!failure && (status || (kernel_word(worker+0x2cu)&255)!=5 ||
           (kernel_word(worker+0x54u)>>24)!=5 || ((kernel_word(worker+0x74u)>>8)&255)!=1 ||
           wait<stack || (uint64_t)wait+24>top || kernel_word(wait+8)!=worker ||
           kernel_word(wait+12)!=worker+0xf0u || kernel_word(wait+16)!=wait ||
           kernel_word(wait+20)!=0x00010000u || (kernel_word(worker+0xf0u)&255)!=5 ||
           kernel_word(worker+0xf4u)))failure="original captured launch worker did not enter suspend wait";
        if(!failure) {
            fprintf(stderr,"[ORIGINAL LAUNCH WAIT] state=5 reason=5 suspend=1 block=%08x thread=%08x object=%08x next=%08x key/type=00010000 semaphore=5/0\n",
                wait,worker,worker+0xf0u,wait);
            uint32_t length=original_section_file_value[0]&65535u;
            uint32_t allocate[]={length+1,0x6e466558u},buffer=0,empty[2];
            if(vm_caller_read(original_section_file_address,empty,sizeof(empty))!=UC_ERR_OK || empty[0] || empty[1])
                failure="original captured launch changed filename before handoff";
            if(!failure){++execution_depth;execute(15,allocate,&buffer);--execution_depth;}
            if(!failure && !buffer)failure="original launch filename pool allocation failed";
            original_section_file_value[1]=buffer;
            /* Original15 already publishes this pool allocation on the shared
             * RAM owner. A direct engine API write cannot create a lazy alias. */
            if(!failure)memcpy(guest_memory+buffer,original_section_file_text,length+1);
            if(!failure && uc_mem_write(cpu,original_section_file_address,original_section_file_value,8)!=UC_ERR_OK)
                failure="original launch filename publication failed";
            if(!failure) {
                original_section_launch_ready=1;
                fprintf(stderr,"[ORIGINAL LAUNCH HANDOFF] captured=%08x suspend=1 state=5 filename=%08x pool=%08x bytes=%u\n",
                    worker,original_section_file_address,buffer,length+1);
            }
        }
    }
    const char *error=failure;ReleaseSRWLockExclusive(&lock);return error;
}
