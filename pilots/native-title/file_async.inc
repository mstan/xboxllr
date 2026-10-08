/* Real native asynchronous transfers, IOSB and optional event completion. The private host
 * IOSB/event survive the syscall; publish guest bytes/count/status before
 * signaling its retained event. Phobos's host-file/event approach is reused,
 * but its pending/APC omission is not. No donor implementation body copied. */
struct MediaAsyncTransfer {
    IO_STATUS_BLOCK io;
    HANDLE done,event;
    PTP_WAIT waiter;
    struct MediaFile *owner;
    uint32_t iosb,length,write;
};
static void media_async_publish(uint32_t iosb,uint32_t status,uint32_t bytes)
{
    MEM32(iosb+4)=bytes;MemoryBarrier();
    InterlockedExchange((volatile LONG*)(memory+iosb),(LONG)status);
}
/* Caller holds the file's IO lock; completion callback takes it separately. */
static void media_async_finish(struct MediaAsyncTransfer *r)
{
    struct MediaFile *owner=r->owner;uint32_t status=(uint32_t)r->io.Status;
    if(status==0x103u || r->io.Information>r->length)stop("invalid native asynchronous completion",owner->id);
    media_async_publish(r->iosb,status,(uint32_t)r->io.Information);
    if(r->event && event_nt_set(r->event,NULL)<0)stop("file event completion delivery failed",owner->id);
    if(!SetEvent(owner->async_signal))stop("file completion signal failed",owner->id);
    if(r->write || pilot_event_trace_enabled())fprintf(stderr,"[ASYNC FILE COMPLETE] handle=%08x requested=%u completed=%llu status=%08x operation=%s\n",owner->id,r->length,(unsigned long long)r->io.Information,status,r->write?"write":"read");
    if((r->event && !CloseHandle(r->event)) || !CloseHandle(r->done))stop("asynchronous IO handle cleanup failed",owner->id);
    CloseThreadpoolWait(r->waiter);free(r);
    if(!owner->pending)stop("asynchronous file ownership underflow",owner->id);
    if(!--owner->pending && !SetEvent(owner->async_idle))stop("file drain notification failed",owner->id);
}
static void CALLBACK media_async_ready(PTP_CALLBACK_INSTANCE instance,void *context,PTP_WAIT waiter,TP_WAIT_RESULT result)
{
    (void)instance;(void)waiter;
    if(result!=WAIT_OBJECT_0)stop("asynchronous IO wait failed",result);
    struct MediaAsyncTransfer *r=context;struct MediaFile *owner=r->owner;
    AcquireSRWLockExclusive(&owner->io_lock);media_async_finish(r);ReleaseSRWLockExclusive(&owner->io_lock);
}
static uint32_t media_async_transfer(struct MediaFile *owner,const uint32_t *a,int write)
{
    if(a[2])stop("asynchronous file APC delivery outside qualified scope",a[2]);
    if(!a[7])stop("asynchronous transfer requires explicit offset",owner->id);
    LARGE_INTEGER offset;offset.QuadPart=(int64_t)MEM64(a[7]);
    if(offset.QuadPart<0){media_async_publish(a[4],MEDIA_INVALID,0);return MEDIA_INVALID;}
    uint32_t status=0;HANDLE event=a[1]?event_duplicate(a[1],&status):NULL;
    if(status){media_async_publish(a[4],status,0);return status;}
    struct MediaAsyncTransfer *r=calloc(1,sizeof(*r));
    if(r){r->event=event;r->done=CreateEventW(NULL,FALSE,FALSE,NULL);r->waiter=CreateThreadpoolWait(media_async_ready,r,NULL);}
    if(!r || !r->done || !r->waiter) {
        if(r){if(r->done)CloseHandle(r->done);if(r->waiter)CloseThreadpoolWait(r->waiter);free(r);}if(event)CloseHandle(event);
        media_async_publish(a[4],0xc000009au,0);return 0xc000009au;
    }
    r->owner=owner;r->iosb=a[4];r->length=a[6];r->write=write;r->io.Status=(LONG)0x103;
    if((event && event_nt_reset(event,NULL)<0) || !ResetEvent(owner->async_idle) || !ResetEvent(owner->async_signal))stop("file IO event reset failed",owner->id);
    ++owner->pending;media_async_publish(a[4],0x103,0);
    if(write)fprintf(stderr,"[ASYNC FILE WRITE] handle=%08x event=%08x bytes=%u offset=%lld\n",owner->id,a[1],a[6],offset.QuadPart);
    status=(uint32_t)(write?media_nt_write:media_nt_read)(owner->host,r->done,NULL,NULL,&r->io,memory+a[5],a[6],&offset,NULL);
    if(status==0x103u) {
        SetThreadpoolWait(r->waiter,r->done,NULL); /* may complete before return */
    } else {
        if((uint32_t)r->io.Status==0x103u){r->io.Status=(LONG)status;r->io.Information=0;}
        media_async_finish(r);
    }
    return status;
}
