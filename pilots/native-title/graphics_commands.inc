/* Whole API calls and original inline packet writers share this command owner.
 * The profile supplies the original CDevice publication/allocator ABI. GPU
 * completion continues to belong to the real native query/fence machinery. */
#include "graphics_commands.h"
#ifdef PILOT_D3D_COMMANDS_VERSION
#if PILOT_D3D_COMMANDS_VERSION != 1
#error Unsupported inline command-buffer ABI
#endif
static struct PilotCommandArena graphics_commands;
static int graphics_command_method(void *context,uint32_t method,uint32_t value,int apply)
{
    (void)context;int accepted=pilot_gfx_command_method(method,value,apply);
    if(!accepted)fprintf(stderr,"[D3D COMMAND] method=%04x value=%08x apply=%d\n",method,value,apply);
    return accepted;
}
static void graphics_commands_check(void)
{
    if(!graphics_commands.base || MEM32(PILOT_D3D_DEVICE_GLOBAL)!=PILOT_D3D_DEVICE_OBJECT ||
       (MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_COMMANDS_FLAGS_OFFSET)&PILOT_D3D_COMMANDS_RECORD_MASK) ||
       MEM32(PILOT_D3D_COMMANDS_CAPACITY_GLOBAL)!=graphics_commands.bytes)
        stop("unqualified inline command-buffer owner/recording mode",graphics_commands.base);
}
static uint32_t *graphics_commands_snapshot(uint32_t *current)
{
    graphics_commands_check();*current=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_COMMANDS_CURRENT_OFFSET);
    if(*current<graphics_commands.base || *current-graphics_commands.base<graphics_commands.consumed ||
       *current-graphics_commands.base>graphics_commands.bytes || (*current&3))
        stop("inline command pointer outside owned arena",*current);
    uint32_t bytes=*current-graphics_commands.base-graphics_commands.consumed;
    if(!bytes)return NULL;
    uint32_t *snapshot=malloc(bytes);if(!snapshot)stop("inline command snapshot allocation failed",bytes);
    memcpy(snapshot,graphics_pointer(graphics_commands.base+graphics_commands.consumed,bytes),bytes);
    return snapshot;
}
static void graphics_commands_flush(void)
{
    uint32_t current;uint32_t *snapshot=graphics_commands_snapshot(&current);
    int error=pilot_command_flush(&graphics_commands,snapshot,current,graphics_command_method,NULL);
    free(snapshot);
    if(error)stop("unqualified inline graphics command",graphics_commands.detail);
}
static void graphics_commands_open(void)
{
    uint32_t capacity=MEM32(PILOT_D3D_COMMANDS_CAPACITY_GLOBAL),request=MEM32(PILOT_D3D_COMMANDS_REQUEST_GLOBAL);
    if(!capacity)capacity=PILOT_D3D_COMMANDS_CAPACITY_DEFAULT;
    if(!request)request=PILOT_D3D_COMMANDS_REQUEST_DEFAULT;
    if(capacity>1024*1024 || request>capacity || request<=PILOT_D3D_COMMANDS_MARGIN || (capacity&3) || (request&7))
        stop("unqualified inline command allocation policy",capacity);
    uint32_t base=0,bytes=capacity;
    if(vm_allocate(&base,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))
        stop("inline command arena allocation failed",capacity);
    if(pilot_command_open(&graphics_commands,base,capacity,PILOT_D3D_COMMANDS_MARGIN))
        stop("invalid inline command arena",base);
    MEM32(PILOT_D3D_COMMANDS_CAPACITY_GLOBAL)=capacity;MEM32(PILOT_D3D_COMMANDS_REQUEST_GLOBAL)=request;
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_COMMANDS_CURRENT_OFFSET)=base;
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_COMMANDS_LIMIT_OFFSET)=base;
}
static uint32_t graphics_commands_reserve(uint32_t minimum,uint32_t window)
{
    uint32_t current,next=0,limit=0;uint32_t *snapshot=graphics_commands_snapshot(&current);
    int error=pilot_command_reserve(&graphics_commands,snapshot,current,minimum,window,
                                    graphics_command_method,NULL,&next,&limit);
    free(snapshot);if(error)stop("inline command reservation failed",graphics_commands.detail);
    if(!pilot_gfx_command_submit())stop("inline native command submission failed",0);
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_COMMANDS_CURRENT_OFFSET)=next;
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_COMMANDS_LIMIT_OFFSET)=limit;
    return next;
}
static void graphics_commands_close(void)
{
    uint32_t base=graphics_commands.base,bytes=0;
    if(base && vm_free(&base,&bytes,MEM_RELEASE))stop("inline command arena retirement failed",base);
    memset(&graphics_commands,0,sizeof(graphics_commands));
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_COMMANDS_CURRENT_OFFSET)=0;
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_COMMANDS_LIMIT_OFFSET)=0;
}
#endif
