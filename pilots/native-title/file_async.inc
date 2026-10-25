/* Real native asynchronous transfers, IOSB and optional event completion. The private host
 * IOSB/event survive the syscall; publish guest bytes/count/status before
 * signaling its retained event. Phobos's host-file/event approach is reused,
 * but its pending/APC omission is not. No donor implementation body copied. */
struct MediaAsyncTransfer {
    IO_STATUS_BLOCK io;
    HANDLE done,event;
    PTP_WAIT waiter;
    struct MediaFile *owner;
    uint32_t iosb,length,write,data,writer_lease;
    struct NativeFileApc *apc;
    uint32_t session_identity;
    unsigned char *session_buffer;
};
static uint32_t media_async_session_sequence;
static void media_async_publish(uint32_t iosb,uint32_t status,uint32_t bytes)
{
    MEM32(iosb+4)=bytes;MemoryBarrier();
    InterlockedExchange((volatile LONG*)(memory+iosb),(LONG)status);
}
/* Release private transfer ownership for either completion or rejection. Only
 * completed requests publish IOSB/signal guest notification below. */
static void media_async_release(struct MediaAsyncTransfer *r)
{
    struct MediaFile *owner=r->owner;
#ifdef PILOT_MEMORY_WRITE_WATCH
    if(r->writer_lease)pilot_write_watch_native_end(memory+r->data,r->length);
#endif
    native_apc_discard(r->apc);
    if((r->event && !CloseHandle(r->event)) || !CloseHandle(r->done))stop("asynchronous IO handle cleanup failed",owner->id);
    CloseThreadpoolWait(r->waiter);free(r->session_buffer);free(r);
    if(!owner->pending)stop("asynchronous file ownership underflow",owner->id);
    if(!--owner->pending && !SetEvent(owner->async_idle))stop("file drain notification failed",owner->id);
}
/* Caller holds the file's IO lock; completion callback takes it separately. */
static void media_async_finish(struct MediaAsyncTransfer *r)
{
    struct MediaFile *owner=r->owner;uint32_t status=(uint32_t)r->io.Status;
    if(status==0x103u || r->io.Information>r->length)stop("invalid native asynchronous completion",owner->id);
    if(g_pilot_session_enabled) {
        uint64_t evidence=((uint64_t)status<<32)|(uint32_t)r->io.Information;
        pilot_session_verify(PILOT_SESSION_FILE_RESULT,r->session_identity,&evidence,sizeof(evidence));
        if(r->session_buffer && r->io.Information) {
            unsigned char hash[32];
            if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,r->session_buffer,(ULONG)r->io.Information,hash,32)<0)
                stop("asynchronous replay read hash failed",owner->id);
            pilot_session_verify(PILOT_SESSION_FILE_RESULT,(uint64_t)r->session_identity|UINT64_C(0x100000000),hash,sizeof(hash));
            memcpy(memory+r->data,r->session_buffer,(size_t)r->io.Information);
        }
    }
    media_async_publish(r->iosb,status,(uint32_t)r->io.Information);
    native_apc_complete(r->apc);r->apc=NULL;
    if(r->event && event_nt_set(r->event,NULL)<0)stop("file event completion delivery failed",owner->id);
    /* Original async IO signals its explicit event, or the file when no event
     * is supplied. It does not notify both objects for the same request. */
    if(!r->event && !SetEvent(owner->async_signal))stop("file completion signal failed",owner->id);
    if(r->write || pilot_event_trace_enabled())fprintf(stderr,"[ASYNC FILE COMPLETE] handle=%08x requested=%u completed=%llu status=%08x operation=%s\n",owner->id,r->length,(unsigned long long)r->io.Information,status,r->write?"write":"read");
    media_async_release(r);
}
static void CALLBACK media_async_ready(PTP_CALLBACK_INSTANCE instance,void *context,PTP_WAIT waiter,TP_WAIT_RESULT result)
{
    (void)instance;(void)waiter;
    if(result!=WAIT_OBJECT_0)stop("asynchronous IO wait failed",result);
    struct MediaAsyncTransfer *r=context;struct MediaFile *owner=r->owner;
    int session_token=pilot_session_device_enter(0x10000u+r->session_identity);
    AcquireSRWLockExclusive(&owner->io_lock);media_async_finish(r);ReleaseSRWLockExclusive(&owner->io_lock);
    pilot_session_device_leave(session_token);
}
static uint32_t media_async_transfer(struct MediaFile *owner,const uint32_t *a,int write)
{
    if(!a[7])stop("asynchronous transfer requires explicit offset",owner->id);
    LARGE_INTEGER offset;offset.QuadPart=(int64_t)MEM64(a[7]);
    uint32_t status=0;HANDLE event=a[1]?event_duplicate(a[1],&status):NULL;
    if(status)return status;
    struct NativeFileApc *apc=a[2]?native_apc_prepare(a[2],a[3],a[4],&status):NULL;
    if(status){if(event)CloseHandle(event);return status;}
    struct MediaAsyncTransfer *r=calloc(1,sizeof(*r));
    if(r){r->event=event;r->done=CreateEventW(NULL,FALSE,FALSE,NULL);r->waiter=CreateThreadpoolWait(media_async_ready,r,NULL);}
    if(!r || !r->done || !r->waiter) {
        if(r){if(r->done)CloseHandle(r->done);if(r->waiter)CloseThreadpoolWait(r->waiter);free(r);}if(event)CloseHandle(event);
        native_apc_discard(apc);
        return 0xc000009au;
    }
    r->owner=owner;r->iosb=a[4];r->length=a[6];r->write=write;r->io.Status=(LONG)0x103;
    r->apc=apc;
    r->data=a[5];
    if(g_pilot_session_enabled) {
        if(media_async_session_sequence==0xfffeffffu)stop("asynchronous replay identity exhausted",owner->id);
        r->session_identity=++media_async_session_sequence;
        if(!write && r->length) {
            r->session_buffer=malloc(r->length);
            if(!r->session_buffer) {
                native_apc_discard(r->apc);if(r->event)CloseHandle(r->event);
                CloseHandle(r->done);CloseThreadpoolWait(r->waiter);free(r);return 0xc000009au;
            }
        }
    }
#ifdef PILOT_MEMORY_WRITE_WATCH
    r->data=a[5];
    if(!write) {
        if(!pilot_write_watch_native_begin(memory+r->data,r->length))stop("cannot relinquish write watch for async read",a[5]);
        r->writer_lease=1;
    }
#endif
    if((event && event_nt_reset(event,NULL)<0) || !ResetEvent(owner->async_idle) || !ResetEvent(owner->async_signal))stop("file IO event reset failed",owner->id);
    ++owner->pending;
    if(write)fprintf(stderr,"[ASYNC FILE WRITE] handle=%08x event=%08x bytes=%u offset=%lld\n",owner->id,a[1],a[6],offset.QuadPart);
    status=offset.QuadPart<0?MEDIA_INVALID:
        (uint32_t)(write?media_nt_write:media_nt_read)(owner->host,r->done,NULL,NULL,&r->io,
            r->session_buffer?r->session_buffer:memory+a[5],a[6],&offset,NULL);
    if(g_pilot_session_enabled) {
        uint32_t actual=status;
        status=(uint32_t)pilot_session_value64(PILOT_SESSION_FILE_RESULT,r->session_identity,status);
        if((int32_t)actual<0 || (int32_t)status<0) {
            if(actual!=status)pilot_session_diverge("asynchronous submission result differs",r->session_identity);
        } else if(status!=0x103u && actual==0x103u) {
            if(WaitForSingleObject(r->done,30000)!=WAIT_OBJECT_0)
                pilot_session_diverge("recorded immediate IO did not complete",r->session_identity);
        }
    }
    if(status==0x103u) {
        media_async_publish(a[4],0x103,0);
        SetThreadpoolWait(r->waiter,r->done,NULL); /* may complete before return */
    } else if((int32_t)status<0) {
        /* Qualified original FATX EOF/invalid-offset behavior: a rejected
         * request leaves IOSB untouched and notification reset. A later error
         * on an accepted PENDING request still completes through finish(). */
        media_async_release(r);
    } else {
        media_async_finish(r);
    }
    return status;
}
