/* Test-only caller of the selected real input implementation. Run separately
 * from production: no duplicate execution or comparison inside a shipped path.
 * Boot/title initialization and driver/device transactions remain real. */
void pilot_input_qualify_open(recomp_func_t capabilities,recomp_func_t feedback)
{
    if(!g_eax)return;
    uint32_t saved[]={g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi,g_seh_ebp,
        next_kernel_ordinal,g_icall_saved_esp,g_icall_dispatch_form};
    uint32_t base=(g_esp-192)&~15u,out=base+32;
    memset(memory+out,0xcc,26);
    g_esp=base;MEM32(base)=0;MEM32(base+4)=saved[0];MEM32(base+8)=out;
    capabilities();
    fprintf(stderr,"[INPUT QUALIFICATION] handle=%08x capabilities_status=%08x bytes=",saved[0],g_eax);
    for(unsigned i=0;i<25;++i)fprintf(stderr,"%02x",MEM8(out+i));fprintf(stderr,"\n");
    if(g_eax || g_esp!=base+12 || MEM8(out+25)!=0xcc)stop("input capabilities qualification failed",saved[0]);
    uint8_t pad[18],motors[4];uint32_t status=pilot_input_host_caps(0,pad,motors);
    if(status || MEM8(out)!=2 || MEM16(out+1) || memcmp(memory+out+3,pad,18) || memcmp(memory+out+21,motors,4))
        stop("input capabilities differ from configured peripheral",saved[0]);
    memset(memory+out,0,70);MEM8(out+70)=0xcc;
    g_esp=base;MEM32(base)=0;MEM32(base+4)=saved[0];MEM32(base+8)=out;
    feedback();uint32_t submitted=g_eax;
    fprintf(stderr,"[INPUT QUALIFICATION] zero-rumble return=%08x status=%08x\n",submitted,MEM32(out));
    if((submitted!=0 && submitted!=ERROR_IO_PENDING) || g_esp!=base+12)
        stop("input feedback submission qualification failed",submitted);
    uint64_t deadline=GetTickCount64()+2000;
    while(MEM32(out)==ERROR_IO_PENDING && GetTickCount64()<deadline) {
        int blocked=scheduler_block_begin();Sleep(1);scheduler_block_end(blocked);
        pilot_thread_checkpoint();
    }
    if(MEM32(out) || MEM8(out+70)!=0xcc)stop("input feedback completion qualification failed",MEM32(out));
    fprintf(stderr,"[INPUT QUALIFICATION] real capabilities and zero-rumble completed\n");
    g_eax=saved[0];g_ecx=saved[1];g_edx=saved[2];g_ebx=saved[3];g_esp=saved[4];
    g_ebp=saved[5];g_esi=saved[6];g_edi=saved[7];g_seh_ebp=saved[8];
    next_kernel_ordinal=saved[9];g_icall_saved_esp=saved[10];g_icall_dispatch_form=saved[11];
}
