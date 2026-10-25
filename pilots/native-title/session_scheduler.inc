/* Record/replay CPU admission and serialize guest-visible asynchronous writes.
 * External owners always enter BEFORE taking a device/file/clock lock. Guest
 * preemption remains at generated function/loop boundaries, never inside a
 * host owner. Replaying a turn does not complete any IO or GPU work itself. */
int g_pilot_session_enabled;
RECOMP_TLS uint64_t g_pilot_session_checkpoints;
RECOMP_TLS uint64_t g_pilot_session_deadline=UINT64_MAX;
static RECOMP_TLS uint64_t session_flow=UINT64_C(14695981039346656037);
static RECOMP_TLS unsigned session_expected_boundary;
static RECOMP_TLS unsigned session_device_owned,session_device_suspended;
static volatile LONG session_started;
static struct SessionDeviceWait {uint32_t actor;uint64_t order;int waiting;} session_devices[256];
static uint64_t session_device_order;
static uint64_t session_perf_start,session_perf_owner,session_perf_guest,session_perf_dsp,session_perf_device;
static uint64_t session_perf_now(void){LARGE_INTEGER t;QueryPerformanceCounter(&t);return (uint64_t)t.QuadPart;}
static void session_perf_begin(void){session_perf_owner=session_perf_now();if(!session_perf_start)session_perf_start=session_perf_owner;}
static void session_perf_end(uint32_t actor){
    uint64_t t=session_perf_now()-session_perf_owner;
    if(actor<65)session_perf_guest+=t;else if(actor==68)session_perf_dsp+=t;else session_perf_device+=t;
}

static uint32_t session_owner_actor=PILOT_SESSION_ACTOR_NONE;

static uint64_t session_hash(uint64_t h,const void *value,size_t bytes)
{
    const uint8_t *p=value;while(bytes--)h=(h^*p++)*UINT64_C(1099511628211);return h;
}
/* Calls and all service argument/return bytes contribute to the next CPU
 * turn's execution proof. External observations retain their own exact
 * journals. Folding deterministic evidence avoids a queued allocation and
 * cross-thread wake for every state setter or guest critical-section call. */
static void session_service_evidence(uint64_t service,const void *data,size_t size)
{
    if(!g_pilot_session_enabled)return;
    if(!scheduler_attached){pilot_session_verify(PILOT_SESSION_SERVICE,service,data,size);return;}
    const uint64_t domain=UINT64_C(0x5352564345564944),bytes=size;
    session_flow=session_hash(session_flow,&domain,sizeof(domain));
    session_flow=session_hash(session_flow,&service,sizeof(service));
    session_flow=session_hash(session_flow,&bytes,sizeof(bytes));
    session_flow=session_hash(session_flow,data,size);
}
static uint64_t session_cpu_digest(void)
{
    uint32_t regs[]={g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_fs_base,
        g_seh_ebp,g_icall_saved_esp,g_icall_dispatch_form,(uint32_t)g_fp_top,(uint32_t)g_fp_cmp,(uint32_t)g_df,
        g_fp_control_word,g_fp_cc};
    uint64_t h=session_hash(session_flow,regs,sizeof(regs));
    h=session_hash(h,g_fp_stack,sizeof(g_fp_stack));
    const RecompXmm xmm[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};
    const RecompMmx mm[]={g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7};
    h=session_hash(h,xmm,sizeof(xmm));return session_hash(h,mm,sizeof(mm));
}
static int session_pending_locked(void)
{
    if(!g_pilot_session_enabled)return 0;
    for(unsigned i=0;i<256;++i)if(session_devices[i].waiting)return 1;return 0;
}
static int session_actor_next_locked(uint32_t actor)
{
    if(pilot_session_mode()!=PILOT_SESSION_REPLAY)return 1;
    PilotSessionTurn turn;
    if(!pilot_session_next_turn(&turn))pilot_session_diverge("recorded CPU/device schedule exhausted",g_pilot_session_checkpoints);
    return turn.actor==actor;
}
static void session_admission_failure(const char *reason,uint32_t actor)
{
    PilotSessionTurn turn={0};pilot_session_next_turn(&turn);
    fprintf(stderr,"[SESSION REPLAY ADMISSION] waiting=%u current_owner=%u expected_actor=%u boundary=%u checkpoint=%llu\n",
        actor,scheduler_owner,turn.actor,turn.boundary,(unsigned long long)turn.checkpoint);
    fprintf(stderr,"[SESSION REPLAY DEVICE OWNER] actor=%u\n",session_owner_actor);
    pilot_session_describe_activity();
    for(unsigned i=0;i<65;++i)if(scheduled[i].live)
        fprintf(stderr,"[SESSION REPLAY THREAD] actor=%u blocked=%u suspended=%u priority=%d\n",i,scheduled[i].blocked,scheduled[i].suspended,scheduled[i].priority);
    for(unsigned i=0;i<256;++i)if(session_devices[i].waiting)
        fprintf(stderr,"[SESSION REPLAY DEVICE] actor=%u waiting=1\n",session_devices[i].actor);
    pilot_session_diverge(reason,actor);
}
static void session_guest_begin_locked(void)
{
    if(!g_pilot_session_enabled)return;
    session_perf_begin();
    pilot_session_actor(scheduler_slot);
    if(pilot_session_mode()==PILOT_SESSION_REPLAY) {
        PilotSessionTurn turn;
        if(!pilot_session_next_turn(&turn) || turn.actor!=scheduler_slot || turn.checkpoint<g_pilot_session_checkpoints)
            pilot_session_diverge("guest admission/checkpoint differs",g_pilot_session_checkpoints);
        g_pilot_session_deadline=turn.checkpoint;session_expected_boundary=turn.boundary;
    }
}
static void session_guest_end_locked(unsigned boundary)
{
    if(!g_pilot_session_enabled)return;
    session_perf_end(scheduler_slot);
    PilotSessionTurn turn={scheduler_slot,boundary,g_pilot_session_checkpoints,session_cpu_digest()};
    pilot_session_turn(&turn);g_pilot_session_deadline=UINT64_MAX;
}
static int session_device_enter(uint32_t actor)
{
    if(!g_pilot_session_enabled || !InterlockedCompareExchange(&session_started,0,0))return 0;
    if(session_device_owned)stop("recursive asynchronous session owner",actor);
    if(scheduler_attached && !scheduled[scheduler_slot].blocked)
        stop("device publication entered from guest CPU owner",actor);
    AcquireSRWLockExclusive(&scheduler_lock);
    unsigned slot;
    for(slot=0;slot<256 && session_devices[slot].waiting;++slot){}
    if(slot==256)stop("asynchronous session wait capacity",actor);
    session_devices[slot].actor=actor;session_devices[slot].waiting=1;
    session_devices[slot].order=++session_device_order;scheduler_notify_locked();
    uint64_t began=GetTickCount64(),progress=pilot_session_progress();
    for(;;) {
        int first=1;
        if(pilot_session_mode()!=PILOT_SESSION_REPLAY)
            for(unsigned i=0;i<256;++i)if(session_devices[i].waiting && session_devices[i].order<session_devices[slot].order)first=0;
        if(!scheduler_owner && first && session_actor_next_locked(actor))break;
        if(!SleepConditionVariableSRW(&scheduler_changed,&scheduler_lock,1000,0) && GetLastError()!=ERROR_TIMEOUT)
            stop("asynchronous session admission wait failed",GetLastError());
        uint64_t next_progress=pilot_session_progress();
        if(next_progress!=progress){progress=next_progress;began=GetTickCount64();}
        if(pilot_session_mode()==PILOT_SESSION_REPLAY && GetTickCount64()-began>30000)
            session_admission_failure("asynchronous owner could not reach recorded admission",actor);
    }
    session_devices[slot].waiting=0;scheduler_owner=UINT32_MAX;session_device_owned=1;session_owner_actor=actor;session_perf_begin();
    scheduler_notify_locked();ReleaseSRWLockExclusive(&scheduler_lock);return 1;
}
static void session_device_leave(int token)
{
    if(!token)return;
    AcquireSRWLockExclusive(&scheduler_lock);
    if(!session_device_owned || scheduler_owner!=UINT32_MAX)stop("asynchronous session ownership mismatch",pilot_session_actor_id());
    session_perf_end(pilot_session_actor_id());
    PilotSessionTurn turn={pilot_session_actor_id(),PILOT_SESSION_DEVICE,0,0};pilot_session_turn(&turn);
    session_device_owned=0;scheduler_owner=0;session_owner_actor=PILOT_SESSION_ACTOR_NONE;scheduler_notify_locked();ReleaseSRWLockExclusive(&scheduler_lock);
}
static int session_device_try_enter(uint32_t actor)
{
    if(!g_pilot_session_enabled || !InterlockedCompareExchange(&session_started,0,0))return 0;
    if(session_device_owned || scheduler_attached)stop("recursive presentation session owner",actor);
    AcquireSRWLockExclusive(&scheduler_lock);
    int admitted=!scheduler_owner && session_actor_next_locked(actor);
    if(admitted){scheduler_owner=UINT32_MAX;session_device_owned=1;session_owner_actor=actor;session_perf_begin();scheduler_notify_locked();}
    ReleaseSRWLockExclusive(&scheduler_lock);return admitted;
}
static void session_worker_begin(uint32_t actor)
{
    if(!g_pilot_session_enabled)return;
    while(!InterlockedCompareExchange(&session_started,0,0))Sleep(1);
    (void)pilot_session_device_enter(actor);
}
static void session_worker_end(void)
{if(session_device_owned)session_device_leave(1);}
static int scheduler_block_begin(void);
static void scheduler_block_end(int blocked);
static int session_host_wait_begin(void)
{
    if(!g_pilot_session_enabled)return 0;
    if(session_device_owned){session_device_leave(1);return 2;}
    return scheduler_block_begin();
}
static void session_host_wait_end(int token)
{
    if(token==2)session_device_enter(pilot_session_actor_id());
    else scheduler_block_end(token);
}
static void session_terminal(const char *reason,uint32_t code)
{
    fprintf(stderr,"[NATIVE STOP CONTEXT] reason=%s code=%u host_thread=%u entries=%llu eax=%08x ecx=%08x edx=%08x ebx=%08x esi=%08x edi=%08x ebp=%08x esp=%08x fs=%08x kernel=%u\n",
        reason,code,GetCurrentThreadId(),(unsigned long long)entries,g_eax,g_ecx,g_edx,g_ebx,g_esi,g_edi,g_ebp,g_esp,g_fs_base,next_kernel_ordinal);
    fprintf(stderr,"[NATIVE STOP RECENT]");
    unsigned count=recent_pos<32?recent_pos:32;
    for(unsigned i=recent_pos-count;i<recent_pos;++i)fprintf(stderr," %08x",recent[i&31]);
    fprintf(stderr,"\n");fflush(stderr);
#ifdef PILOT_INPUT_NATIVE
    pilot_input_diagnostic_flush();
#endif
    if(!g_pilot_session_enabled)return;
    // Intentional owner stops only. VEH/SEH calls must not wait on this lock.
    pilot_session_verify(PILOT_SESSION_FAULT,code,reason,strlen(reason));
    AcquireSRWLockExclusive(&scheduler_lock);
    int guest_owner=scheduler_attached && scheduler_owner==scheduler_slot+1;
    ReleaseSRWLockExclusive(&scheduler_lock);
    if(!guest_owner && !session_device_owned && InterlockedCompareExchange(&session_started,0,0)) {
        session_release_terminal_graphics();
        session_device_enter(pilot_session_actor_id());
    }
    pilot_session_terminal_begin();
    AcquireSRWLockExclusive(&scheduler_lock);
    PilotSessionTurn turn={pilot_session_actor_id(),PILOT_SESSION_TERMINAL,
        scheduler_attached?g_pilot_session_checkpoints:0,scheduler_attached?session_cpu_digest():0};
    pilot_session_turn(&turn);ReleaseSRWLockExclusive(&scheduler_lock);
    LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    double scale=1000.0/frequency.QuadPart;
    fprintf(stderr,"[SESSION OWNERSHIP TIMING] wall_ms=%.1f guest_ms=%.1f dsp_ms=%.1f device_ms=%.1f\n",
        (session_perf_now()-session_perf_start)*scale,session_perf_guest*scale,session_perf_dsp*scale,session_perf_device*scale);
    pilot_session_finish();
}
static void session_frame_boundary(uint64_t frame)
{
    if(!g_pilot_session_enabled)return;
    uint64_t h=UINT64_C(14695981039346656037),read_bytes=0;
    const unsigned phase=(unsigned)(frame&255u)*16;
    for(uint64_t offset=0;offset<UINT64_C(0x100000000);) {
        MEMORY_BASIC_INFORMATION info;
        if(!VirtualQuery(memory+(size_t)offset,&info,sizeof(info)) || !info.RegionSize)
            stop("session memory map observation failed",(uint32_t)offset);
        uint64_t end=(uint64_t)((uintptr_t)info.BaseAddress-(uintptr_t)memory)+info.RegionSize;
        if(end>UINT64_C(0x100000000))end=UINT64_C(0x100000000);
        if(end<=offset)stop("session memory map did not advance",(uint32_t)offset);
        uint64_t description[]={offset,end,info.State,info.Protect};
        h=session_hash(h,description,sizeof(description));
        if(info.State==MEM_COMMIT && !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD)) && info.Protect) {
            // Like the PSX recorder, rotate through every readable memory word.
            // Read one cache-line fragment per page, with a fixed per-frame
            // bound and no GPU readback or second simulation execution.
            for(uint64_t page=offset;page+phase+16<=end;page+=4096) {
                h=session_hash(h,memory+(size_t)(page+phase),16);read_bytes+=16;
            }
        }
        offset=end;
    }
    uint64_t evidence[]={frame,h,session_cpu_digest(),read_bytes};
    pilot_session_verify(PILOT_SESSION_FRAME,frame,evidence,sizeof(evidence));
}
