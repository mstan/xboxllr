/* Elective startup-error diagnosis, using the existing read-only call observer.
 * All entries retain their actual original RET cleanup. In particular A57C0
 * is cdecl/RET0: its argument stays on the caller's stack. At caller 2E710,
 * ENTRY EAX is the unmodified return from D3DX helper1459C6 (stdcall60).
 * We do not register that larger helper under a false <=32-byte ABI. */
static const PilotIoDiagnosticCall gpu_texture_diagnostic_calls[]={
    {0x001349e0u,181,28,"15dde24bc4ba060a05f1286eb183e78ced0ee986e72f560312269ead9eac8795"},
    {0x00134ac0u,74,8,"15def0003e7c463e4d2bcacbe19983ad47346f74591dd0f4acdfd5f6be6ba59f"},
    {0x001456aeu,40,28,"3b9593771a3a92ee0beaa2745470577ca78a739a36136c93feabfd0b2b6ce7f5"},
    {0x0014a998u,346,12,"a0f0f26330c630c1bad74b7c3620359206ec18b60f36ec32ef23562af70ba367"},
    {0x000a57c0u,749,0,"1571f604772bafd367ac3d976b1cdcd3776ad4c9c06949eefdb39baf836a240f"}
};
static struct {HANDLE file;unsigned records;} gpu_texture_diagnostic;
static const char *gpu_texture_diagnostic_record(const PilotIoCallDiagnosticEvent *event)
{
    if(!gpu_texture_diagnostic.file || event->index<2 || event->index>=7 ||
       (event->kind!=PILOT_IO_DIAGNOSTIC_ENTRY && event->kind!=PILOT_IO_DIAGNOSTIC_RETURN))
        return "GPU texture diagnostic has an invalid event";
    if(gpu_texture_diagnostic.records>=65536)return "GPU texture diagnostic record bound exhausted";
    const PilotIoDiagnosticCall *call=gpu_texture_diagnostic_calls+event->index-2;
    const int entering=event->kind==PILOT_IO_DIAGNOSTIC_ENTRY;
    char line[1024];
    int bytes=sprintf_s(line,sizeof(line),
        "{\"phase\":\"%s\",\"id\":%llu,\"thread\":%u,\"entry\":%u,\"caller\":%u,\"entry_sp\":%u,"
        "\"eax\":%u,\"regs\":[%u,%u,%u,%u,%u,%u,%u,%u],\"args\":[%u,%u,%u,%u,%u,%u,%u,%u],"
        "\"stack_bytes\":%u,\"source_sha256\":\"%s\",\"qualified\":false}\n",
        entering?"enter":"return",(unsigned long long)event->id,event->thread,call->entry,event->caller,event->entry_sp,
        event->regs[0],event->regs[0],event->regs[1],event->regs[2],event->regs[3],event->regs[4],event->regs[5],event->regs[6],event->regs[7],
        event->args[0],event->args[1],event->args[2],event->args[3],event->args[4],event->args[5],event->args[6],event->args[7],
        call->stack_bytes,call->sha256);
    DWORD written=0;
    if(bytes<0 || !WriteFile(gpu_texture_diagnostic.file,line,(DWORD)bytes,&written,NULL) || written!=(DWORD)bytes)
        return "GPU texture diagnostic evidence write failed";
    ++gpu_texture_diagnostic.records;
    if(entering && call->entry==0x000a57c0u) {
        /* Preserve real physical RAM once, before the error loop overwrites
         * stack evidence. Plain RAM reads cannot execute device callbacks or
         * guest services. This diagnostic stop is never normal title exit. */
        unsigned char *ram=malloc(0x04000000u);
        int saved=ram && pilot_io_device_diagnostic_read(0,ram,0x04000000u,1) &&
            gpu_framebuffer_file(0,"texture-error.ram.bin",ram,0x04000000u);
        free(ram);
        saved=gpu_framebuffer_file(0,"texture-error.cpu.bin",event,sizeof(*event)) && saved;
        fprintf(stderr,"[GPU TEXTURE ERROR] caller=%08x eax=%08x ram_saved=%d; diagnostic stop, no qualification\n",
            event->caller,event->regs[0],saved);
        return saved?"GPU texture diagnostic stopped at original title error":"GPU texture diagnostic error snapshot failed";
    }
    return NULL;
}
