/* Private operation census, compiled out of ordinary title builds. This
 * records accepted swap boundaries and returned graphics calls, not a pixel
 * oracle. Missing inputs, resets and incomplete frames remain explicit. */
#ifdef PILOT_GRAPHICS_FRAME_AUDIT
#include <stdarg.h>
static struct {
    int configured,enabled,finished;
    const char *directory,*trigger;
    uint32_t after_swap,calls,draws,captured,clears,target_sets,capture_sequence;
    HANDLE file;
} graphics_frame_audit;
#include "graphics_frame_inputs.inc"

static void graphics_frame_audit_configure(void)
{
    if(graphics_frame_audit.configured)return;
    graphics_frame_audit.configured=1;
    graphics_frame_audit.directory=getenv("XLLE_PILOT_FRAME_AUDIT");
    if(!graphics_frame_audit.directory || !*graphics_frame_audit.directory)return;
    graphics_frame_audit.enabled=1;
    graphics_frame_audit.trigger=getenv("XLLE_PILOT_DRAW_CAPTURE_TRIGGER");
    const char *setting=getenv("XLLE_PILOT_FRAME_AUDIT_AFTER_SWAP");
    if(setting) {
        char *end;unsigned long value=strtoul(setting,&end,10);
        if(!*setting || *end || value>1000000)stop("frame audit swap setting",0);
        graphics_frame_audit.after_swap=(uint32_t)value;
    }
}
static int graphics_frame_audit_capture_active(void)
{
    graphics_frame_audit_configure();
    return !graphics_frame_audit.enabled || graphics_frame_audit.file!=NULL;
}
static void graphics_frame_audit_line(const char *format,...)
{
    char line[4096];va_list args;va_start(args,format);
    int bytes=vsnprintf(line,sizeof(line),format,args);va_end(args);
    DWORD written;
    if(bytes<0 || bytes>=(int)sizeof(line) ||
       !WriteFile(graphics_frame_audit.file,line,(DWORD)bytes,&written,NULL) || written!=(DWORD)bytes)
        stop("frame audit write",GetLastError());
}
static void graphics_frame_audit_call(unsigned entry,const uint32_t *arguments)
{
    graphics_frame_audit_configure();
    if(!graphics_frame_audit.enabled || graphics_frame_audit.finished)return;
    if(entry==PILOT_D3D_D3DDevice_Swap) {
        if(graphics_frame_audit.file) {
            graphics_frame_inputs_close();
            graphics_frame_audit_line("{\"event\":\"end\",\"swap\":%u,\"calls\":%u,\"draws\":%u,\"captured\":%u,\"clears\":%u,\"target_sets\":%u,\"inputs\":%u}\n",
                graphics_swap_submitted,graphics_frame_audit.calls,graphics_frame_audit.draws,
                graphics_frame_audit.captured,graphics_frame_audit.clears,graphics_frame_audit.target_sets,graphics_frame_inputs.draws);
            if(!FlushFileBuffers(graphics_frame_audit.file) || !CloseHandle(graphics_frame_audit.file))
                stop("frame audit close",GetLastError());
            graphics_frame_audit.file=NULL;graphics_frame_audit.finished=1;
            fprintf(stderr,"[FRAME AUDIT] complete swap=%u calls=%u draws=%u captured=%u clears=%u target_sets=%u\n",
                graphics_swap_submitted,graphics_frame_audit.calls,graphics_frame_audit.draws,
                graphics_frame_audit.captured,graphics_frame_audit.clears,graphics_frame_audit.target_sets);
            return;
        }
        if(graphics_swap_submitted<graphics_frame_audit.after_swap ||
           (graphics_frame_audit.trigger && *graphics_frame_audit.trigger &&
            GetFileAttributesA(graphics_frame_audit.trigger)==INVALID_FILE_ATTRIBUTES))return;
        char path[1024];
        if(snprintf(path,sizeof(path),"%s/frame.jsonl",graphics_frame_audit.directory)>=(int)sizeof(path))
            stop("frame audit path",0);
        graphics_frame_audit.file=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        if(graphics_frame_audit.file==INVALID_HANDLE_VALUE)stop("frame audit create",GetLastError());
        graphics_frame_audit_line("{\"event\":\"begin\",\"schema\":1,\"swap\":%u,\"width\":%u,\"height\":%u,\"target\":%u,\"depth\":%u}\n",
            graphics_swap_submitted,graphics_parameters[0],graphics_parameters[1],graphics_target,graphics_depth);
        graphics_frame_inputs_open(graphics_swap_submitted);
        fprintf(stderr,"[FRAME AUDIT] begin after_swap=%u\n",graphics_swap_submitted);
        return;
    }
    if(!graphics_frame_audit.file)return;
    if(graphics_frame_audit.calls==32768)stop("frame audit call limit; incomplete frame",entry);
    int draw=entry==PILOT_D3D_D3DDevice_DrawVertices || entry==PILOT_D3D_D3DDevice_DrawVerticesUP;
    if(draw) {
        ++graphics_frame_audit.draws;
        if(graphics_frame_audit.capture_sequence)++graphics_frame_audit.captured;
    }
    if(entry==PILOT_D3D_D3DDevice_Clear)++graphics_frame_audit.clears;
    if(entry==PILOT_D3D_D3DDevice_SetRenderTarget || entry==PILOT_D3D_D3D_CommonSetRenderTarget)
        ++graphics_frame_audit.target_sets;
    unsigned count=pilot_d3d_calls[entry].stack_bytes/4;
    char args[1024];size_t used=0;
    for(unsigned i=0;i<count;++i) {
        int bytes=snprintf(args+used,sizeof(args)-used,"%s%u",i?",":"",arguments[i]);
        if(bytes<0 || (size_t)bytes>=sizeof(args)-used)stop("frame audit arguments",entry);
        used+=(size_t)bytes;
    }
    args[used]=0;
    ++graphics_frame_audit.calls;graphics_frame_inputs_after(entry);
    graphics_frame_audit_line("{\"event\":\"call\",\"sequence\":%u,\"name\":\"%s\",\"args\":[%s],\"shader\":%u,\"target\":%u,\"depth\":%u,\"capture_sequence\":%u,\"return_eax\":%u,\"input_sequence\":%u}\n",
        graphics_frame_audit.calls,pilot_d3d_calls[entry].name,args,
        graphics_active?MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER):0,
        graphics_target,graphics_depth,draw?graphics_frame_audit.capture_sequence:0,g_eax,draw?graphics_frame_inputs.input_sequence:0);
}
#endif
