/* Authored diagnostic host for donor-lifted synthetic x86, never a title host. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"
#include "continuation_diagnostic.h"
#ifdef FIXTURE_CONTINUATIONS
#include "pilot_continuation_schema.h"
#endif

ptrdiff_t g_xbox_mem_offset;
uint32_t g_xbox_code_lo=0x10000,g_xbox_code_hi=0x10400;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp;
RECOMP_TLS uint32_t g_fs_base,g_seh_ebp,g_icall_saved_esp,g_icall_dispatch_form;
RECOMP_TLS volatile uint32_t g_icall_trace[ICALL_TRACE_SIZE],g_icall_trace_idx;
RECOMP_TLS volatile uint64_t g_icall_count;
RECOMP_TLS volatile uint32_t *g_pilot_suspend_count;
void pilot_thread_checkpoint(void) { pilot_cont_fault("unexpected scheduler request"); }
static unsigned char ram[0x10000];
static uint32_t trace[32][4],trace_count;
static uint32_t *registers[10];
static const char *capture_path;

void pilot_cont_fault(const char *reason)
{ fprintf(stderr,"continuation fault: %s\n",reason);exit(90); }
void recomp_icall_fail_log(uint32_t va) { (void)va;pilot_cont_fault("unresolved call"); }
void recomp_icall_not_code_log(uint32_t va,uint32_t sp)
{ (void)va;(void)sp;pilot_cont_fault("non-code call"); }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va;return NULL; }
recomp_func_t recomp_lookup_kernel(uint32_t va) { (void)va;return NULL; }
recomp_func_t recomp_lookup(uint32_t va)
{
    switch(va) {
    case 0x10000:return sub_00010000;
    case 0x10100:return sub_00010100;
    case 0x10200:return sub_00010200;
    case 0x10300:return sub_00010300;
    default:return NULL;
    }
}
void recomp_trace_enter(const char *name,uint32_t va)
{
    (void)name;
    if(trace_count==32)pilot_cont_fault("trace overflow");
    uint32_t *r=trace[trace_count++];r[0]=va;r[1]=g_eax;r[2]=g_esp;r[3]=g_ebp;
}
static void put32(unsigned char *p,uint32_t value)
{ for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(value>>(i*8)); }
static uint32_t get32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint32_t hash(const unsigned char *p,size_t n)
{ uint32_t h=2166136261u;for(size_t i=0;i<n;++i)h=(h^p[i])*16777619u;return h; }

#ifdef FIXTURE_CONTINUATIONS
/* Entire fixture state: 10 CPU fields, guest RAM and observed service/entry
 * trace, followed by the separately versioned execution wire. No C structs. */
#define EXEC_OFFSET (8u+40u+sizeof(ram)+4u+sizeof(trace))
static void save_fixture(const char *path)
{
    unsigned char wire[EXEC_OFFSET+PILOT_CONT_WIRE_BOUND+4];
    put32(wire,0x31584643);put32(wire+4,1);
    for(unsigned i=0;i<10;++i)put32(wire+8+i*4,*registers[i]);
    memcpy(wire+48,ram,sizeof(ram));put32(wire+48+sizeof(ram),trace_count);
    for(unsigned i=0;i<32;++i)for(unsigned j=0;j<4;++j)
        put32(wire+52+sizeof(ram)+(i*4+j)*4,trace[i][j]);
    size_t n=pilot_cont_save(wire+EXEC_OFFSET,sizeof(wire)-EXEC_OFFSET-4);
    if(!n || pilot_cont_depth()!=3)pilot_cont_fault("capture did not retain nested callee");
    n+=EXEC_OFFSET;put32(wire+n,hash(wire,n));n+=4;
    FILE *f=fopen(path,"wb");
    if(!f || fwrite(wire,1,n,f)!=n || fclose(f))pilot_cont_fault("snapshot write");
}
static int load_fixture(const char *path)
{
    unsigned char wire[EXEC_OFFSET+PILOT_CONT_WIRE_BOUND+5];
    FILE *f=fopen(path,"rb");if(!f)return 0;
    size_t n=fread(wire,1,sizeof(wire),f);int io=ferror(f);fclose(f);
    if(io || n<EXEC_OFFSET+52 || n>sizeof(wire)-1 ||
       get32(wire)!=0x31584643 || get32(wire+4)!=1 ||
       get32(wire+n-4)!=hash(wire,n-4) ||
       get32(wire+48+sizeof(ram))>32)return 0;
    /* All checks finish before applying CPU/RAM. Execution load is itself
     * transactional and cannot fail after committing its staged records. */
    if(!pilot_cont_load(wire+EXEC_OFFSET,n-EXEC_OFFSET-4))return 0;
    for(unsigned i=0;i<10;++i)*registers[i]=get32(wire+8+i*4);
    memcpy(ram,wire+48,sizeof(ram));trace_count=get32(wire+48+sizeof(ram));
    for(unsigned i=0;i<32;++i)for(unsigned j=0;j<4;++j)
        trace[i][j]=get32(wire+52+sizeof(ram)+(i*4+j)*4);
    return 1;
}
void pilot_cont_checkpoint(uint32_t function,uint32_t address)
{
    if(capture_path && function==0x10200 && address==FIXTURE_CAPTURE_PC) {
        save_fixture(capture_path);
        /* Exit this disposable fixture process; no attempt to restore or
         * unwind its live C frames. A different process performs the load. */
        exit(0);
    }
}
#endif

int main(int argc,char **argv)
{
    uint32_t *current[]={&g_eax,&g_ecx,&g_edx,&g_esp,&g_ebx,&g_esi,
        &g_edi,&g_ebp,&g_fs_base,&g_seh_ebp};
    for(unsigned i=0;i<10;++i)registers[i]=current[i];
    g_xbox_mem_offset=(ptrdiff_t)(uintptr_t)ram-0x10000;
    g_esp=0x1f000;g_ebp=0x1e000;MEM32(g_esp)=0;
#ifdef FIXTURE_CONTINUATIONS
    pilot_cont_bind(&pilot_cont_schema);
    if(argc==3 && !strcmp(argv[1],"capture"))capture_path=argv[2];
    else if(argc>=3 && !strcmp(argv[1],"resume")) {
        if(!load_fixture(argv[2]))return 20;
        if(argc==4) {
            unsigned char before[sizeof(ram)];memcpy(before,ram,sizeof(ram));
            uint32_t regs[10],saved_count=trace_count;
            for(unsigned i=0;i<10;++i)regs[i]=*registers[i];
            if(load_fixture(argv[3]))return 21;
            if(memcmp(before,ram,sizeof(ram)) || saved_count!=trace_count || pilot_cont_depth())return 22;
            for(unsigned i=0;i<10;++i)if(regs[i]!=*registers[i])return 23;
        }
    } else if(argc!=1)return 24;
#else
    (void)argc;(void)argv;(void)capture_path;
#endif
    sub_00010000();
#ifdef FIXTURE_CONTINUATIONS
    if(pilot_cont_depth())return 25;
    {
        unsigned char disabled[PILOT_CONT_WIRE_BOUND];
        if(pilot_cont_save(disabled,sizeof(disabled)))return 26;
    }
#endif
    /* Full observable fixture state, rather than only an opaque master hash. */
    for(unsigned i=0;i<10;++i)printf("r%u=%08x\n",i,*registers[i]);
    printf("ram=%08x data=%u,%u,%u trace=%u\n",hash(ram,sizeof(ram)),
        MEM32(0x11000),MEM32(0x11004),MEM32(0x11008),trace_count);
    for(unsigned i=0;i<trace_count;++i)
        printf("entry=%08x,%08x,%08x,%08x\n",trace[i][0],trace[i][1],trace[i][2],trace[i][3]);
    return 0;
}
