/* Native jump-slot bridge adapted from sp00nznet/xboxrecomp, MIT,
 * src/kernel/xbox_memory_layout.c at 1409a7d7801d3e931fb1074be6104209ddd9a33e.
 * Copyright (c) 2026 sp00nz. See LICENSES/sp00nznet-MIT.txt.
 *
 * The guest CRT still saves/restores registers and executes unwind/notification
 * work. This bridge transfers the native C stack only after that work finishes.
 * Changes: explicit failures, frame lifetime/sequence tracking, guest-state
 * validation and no slot overwrite or incomplete guest-only fallback.
 */
#define PILOT_JUMP_SLOTS 32
struct PilotJumpSlot {
    uint32_t buffer, frame, argument_sp, return_ip, saved_bp;
    uint64_t order;
    jmp_buf native;
};
static RECOMP_TLS struct PilotJumpSlot jump_slots[PILOT_JUMP_SLOTS];
static RECOMP_TLS uint64_t jump_order;
static RECOMP_TLS struct PilotJumpSlot *jump_pending;

static struct PilotJumpSlot *jump_find(uint32_t buffer)
{
    for (unsigned i=0;i<PILOT_JUMP_SLOTS;++i)
        if (jump_slots[i].order && jump_slots[i].buffer==buffer) return &jump_slots[i];
    stop("native jump buffer is not armed on this thread",buffer);
}

jmp_buf *pilot_setjmp_slot(uint32_t buffer,uint32_t return_ip,uint32_t frame)
{
    struct PilotJumpSlot *slot=NULL;
    for (unsigned i=0;i<PILOT_JUMP_SLOTS;++i) {
        if (jump_slots[i].order && jump_slots[i].buffer==buffer) { slot=&jump_slots[i]; break; }
        if (!jump_slots[i].order && !slot) slot=&jump_slots[i];
    }
    if (!slot || jump_order==UINT64_MAX) stop("native jump slots exhausted",buffer);
    slot->buffer=buffer; slot->frame=frame; slot->argument_sp=g_esp;
    slot->return_ip=return_ip; slot->saved_bp=g_seh_ebp; slot->order=++jump_order;
    return &slot->native;
}

/* Unqualified donor lowering must never arm an unbounded jump. */
jmp_buf *recomp_setjmp_slot(uint32_t buffer) { stop("unbound guest setjmp",buffer); }

void pilot_jump_frame_leave(uint32_t frame)
{
    for (unsigned i=0;i<PILOT_JUMP_SLOTS;++i)
        if (jump_slots[i].order && jump_slots[i].frame==frame) jump_slots[i].order=0;
}

void pilot_longjmp_begin(uint32_t buffer)
{
    struct PilotJumpSlot *slot=jump_find(buffer);
    if (jump_pending) stop("recursive jump during guest unwinding",buffer);
    if (MEM32(buffer)!=slot->saved_bp || MEM32(buffer+0x10)+4!=slot->argument_sp ||
        MEM32(buffer+0x14)!=slot->return_ip)
        stop("guest jump buffer no longer matches native frame",buffer);
    jump_pending=slot;
}

int recomp_guest_longjmp(uint32_t buffer,uint32_t value)
{
    struct PilotJumpSlot *slot=jump_find(buffer);
    if (jump_pending!=slot) stop("native jump requires original CRT completion",buffer);
    if (g_ebx!=MEM32(buffer+4) || g_edi!=MEM32(buffer+8) || g_esi!=MEM32(buffer+12) ||
        g_esp!=slot->argument_sp || g_seh_ebp!=slot->saved_bp ||
        MEM32(g_fs_base)!=MEM32(buffer+0x18) || MEM32(buffer+0x14)!=slot->return_ip)
        stop("guest CRT did not restore the jump contract",buffer);
    /* ebp is local in translated C; its resumed frame reloads g_seh_ebp. */
    g_ebp=g_seh_ebp;
    for (unsigned i=0;i<PILOT_JUMP_SLOTS;++i)
        if (jump_slots[i].order>slot->order) jump_slots[i].order=0;
    jump_pending=NULL;
    fprintf(stderr,"[NONLOCAL JUMP] buffer=%08x return=%08x value=%08x\n",buffer,slot->return_ip,value?value:1);
    longjmp(slot->native,value?(int)value:1);
}
