/* Only source-hashed, statically linked originals are callable. No dynamic
 * guest patching, executable packages or unknown-address dispatch. */
static void title_gpr_get(uint32_t v[8])
{v[0]=g_eax;v[1]=g_ecx;v[2]=g_edx;v[3]=g_ebx;v[4]=g_esp;v[5]=g_ebp;v[6]=g_esi;v[7]=g_edi;}
static void title_gpr_set(const uint32_t v[8])
{g_eax=v[0];g_ecx=v[1];g_edx=v[2];g_ebx=v[3];g_esp=v[4];g_ebp=v[5];g_esi=v[6];g_edi=v[7];}
static int title_stack_copy(uint32_t address,void *data,unsigned bytes,int write)
{
    if(!data || !bytes || bytes>68 || address>UINT32_MAX-bytes)return 0;
    uintptr_t at=(uintptr_t)(memory+address),end=at+bytes;
    while(at<end) {
        MEMORY_BASIC_INFORMATION info;
        if(!VirtualQuery((void*)at,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
           info.Type!=MEM_PRIVATE || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
           ((info.Protect&255)!=PAGE_READWRITE && (info.Protect&255)!=PAGE_WRITECOPY))return 0;
        uintptr_t next=(uintptr_t)info.BaseAddress+info.RegionSize;
        if(next<=at)return 0;at=next;
    }
    if(write)memcpy(memory+address,data,bytes);else memcpy(data,memory+address,bytes);return 1;
}
void pilot_title_function(unsigned id,recomp_func_t original,unsigned cleanup)
{
    uint32_t entry[8];title_gpr_get(entry);
#ifdef PILOT_TITLE_MODS
    if(title_mod && title_mod->function) {
        struct PilotTitleModCall call={0};call.id=id;call.entry=entry;
        if(!title_stack_copy(entry[4],&call.caller,4,0))stop("invalid title function caller",id);
        if(cleanup>sizeof(call.arguments) || (cleanup && !title_stack_copy(entry[4]+4,call.arguments,cleanup,0)))
            stop("invalid title function arguments",id);
        memcpy(call.gpr,entry,sizeof(entry));
        if(title_mod->function(&call)) {
            if(call.gpr[4]!=entry[4]+4+cleanup)stop("title completed call stack contract",id);
            title_gpr_set(call.gpr);return;
        }
        original();call.phase=1;
        title_gpr_get(call.gpr);
        if(call.gpr[4]!=entry[4]+4+cleanup)stop("title original stack contract",id);
        /* Setup acceleration invokes separately qualified originals. Repeating
         * an arbitrary function could replay gameplay side effects. */
        if(title_mod->function(&call))stop("unsupported title function repeat",id);
        if(call.gpr[4]!=entry[4]+4+cleanup)stop("title callback stack contract",id);
        title_gpr_set(call.gpr);
    }
    else original();
#else
    original();
#endif
}
#ifdef PILOT_TITLE_FUNCTIONS
extern recomp_func_t pilot_title_original_lookup(unsigned,unsigned*);
/* Preserve the complete recompiled CPU state, including vector/x87 registers
 * and host floating environment. A small borrowed stack region is restored
 * even when the called original uses cdecl arguments. */
static int title_mod_invoke(uint32_t id,uint32_t object,const uint32_t *args,uint32_t count,uint32_t *result)
{
    unsigned cleanup=0;recomp_func_t fn=pilot_title_original_lookup(id,&cleanup);
    if(!fn || count>16 || (count && !args) || cleanup>count*4 || !result || g_esp<512)return 0;
    struct NativeCpuContext cpu;native_cpu_save(&cpu);
    uint32_t stack=g_esp-256,saved[17],frame[17]={0};
    unsigned bytes=(count+1)*4;
    if(!title_stack_copy(stack,saved,bytes,0))return 0;
    if(count)memcpy(frame+1,args,count*4);
    if(!title_stack_copy(stack,frame,bytes,1))return 0;
    int force=g_force_return;
    g_ecx=object;g_esp=stack;fn();
    *result=g_eax;int valid=g_esp==stack+4+cleanup;
    native_cpu_restore(&cpu);g_force_return=force;
    if(!title_stack_copy(stack,saved,bytes,1))stop("title nested stack restore",id);
    if(!valid)stop("title nested original stack contract",id);
    return 1;
}
#else
static int title_mod_invoke(uint32_t id,uint32_t object,const uint32_t *args,uint32_t count,uint32_t *result)
{(void)id;(void)object;(void)args;(void)count;(void)result;return 0;}
#endif
