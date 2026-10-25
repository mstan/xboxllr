// Called only under the transport lock. Progress comes from the source voice's
// real SamplesPlayed counter; wall time never manufactures a completion.
static void notifications_close(Buffer *b)
{
    for(unsigned i=0;i<b->notification_count;++i)
        if(!CloseHandle(b->notifications[i].event))fail("notification reference close",HRESULT_FROM_WIN32(GetLastError()));
    b->notification_count=0;
}
static void notifications_progress(Buffer *b)
{
    if(!b->notification_count || !b->playing)return;
    XAUDIO2_VOICE_STATE state;session_voice_state(b,&state);
    UINT64 now=(state.SamplesPlayed-b->baseline)*b->align,previous=b->notification_bytes;
    if(now<previous)fail("notification playback epoch regressed");
    UINT32 end=b->looping && b->origin<b->loop_end?b->loop_end:b->bytes;
    for(unsigned i=0;i<b->notification_count;++i) {
        UINT32 offset=b->notifications[i].offset;bool crossed=false;
        if(offset>=b->origin && offset<end) {
            UINT64 first=(UINT64)offset-b->origin+b->align;
            crossed=first>previous && first<=now;
        }
        if(b->looping && offset>=b->loop_start && offset<b->loop_end) {
            UINT64 first=(UINT64)end-b->origin+offset-b->loop_start+b->align;
            UINT64 span=b->loop_end-b->loop_start,next=first;
            if(next<=previous)next+=((previous-next)/span+1)*span;
            crossed|=next<=now;
        }
        if(crossed && !SetEvent(b->notifications[i].event))
            fail("notification event signal",HRESULT_FROM_WIN32(GetLastError()));
    }
    b->notification_bytes=now;
}
int32_t pilot_audio_buffer_notifications(void *opaque,unsigned count,const PilotAudioNotification *positions)
{
    auto *b=static_cast<Buffer*>(opaque);
    if(!b || count>64 || (count && !positions))return E_INVALIDARG;
    PilotAudioNotification staged[64]{};HRESULT result=S_OK;unsigned retained=0;
    // Own every event before publishing any configuration or replacing old refs.
    for(;retained<count;++retained) {
        if(positions[retained].offset>=b->bytes || positions[retained].offset%b->align || !positions[retained].event) {
            result=E_INVALIDARG;break;
        }
        staged[retained].offset=positions[retained].offset;
        if(!DuplicateHandle(GetCurrentProcess(),positions[retained].event,GetCurrentProcess(),
                            &staged[retained].event,EVENT_MODIFY_STATE,FALSE,0)) {
            result=HRESULT_FROM_WIN32(GetLastError());break;
        }
    }
    if(FAILED(result)) {
        for(unsigned i=0;i<retained;++i)CloseHandle(staged[i].event);
        return result;
    }
    Guard guard;notifications_progress(b);
    XAUDIO2_VOICE_STATE state;session_voice_state(b,&state);
    notifications_close(b);memcpy(b->notifications,staged,count*sizeof(*staged));
    b->notification_count=count;b->notification_bytes=(state.SamplesPlayed-b->baseline)*b->align;
    return S_OK;
}
