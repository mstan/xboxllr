#include <windows.h>
#include "audio_device_trace.hpp"
#include <cstring>
#include <new>
#include <string>

namespace {
#ifdef PILOT_AUDIO_TRACE_TEST
constexpr uint64_t capacity=1024;
#else
constexpr uint64_t capacity=64u*1024*1024;
#endif
// The reached Stop-to-restart interval plus two seconds of output is2.832s.
// Capture all concurrent voices/DSP boundaries, never an assumed solo source.
#ifdef PILOT_AUDIO_TRACE_TEST
constexpr uint64_t retained_capacity=capacity;
#else
constexpr uint64_t retained_capacity=256u*1024*1024;
#endif
constexpr uint64_t window_ticks=4u*1125000;
// Windows pilot targets are little-endian IEEE binary32/binary64 hosts.
// status=1 always: a diagnostic prefix never establishes guest retirement.
struct Header {
    char magic[8];uint32_t version,header_bytes,status,flags;
    uint64_t lifetime,epoch,first_tick,bytes,records;
};
struct Record {
    uint32_t kind,id,metadata_words,bytes;
    uint64_t ticks,sequence;
};
struct Footer {char magic[8];uint64_t bytes,records;};
static_assert(sizeof(Header)==64 && sizeof(Record)==32 && sizeof(Footer)==24,"stage trace ABI");
static_assert(sizeof(float)==4 && sizeof(double)==8,"stage trace sample encoding");
bool write_all(HANDLE file,const void* data,uint64_t bytes) {
    auto* next=static_cast<const uint8_t*>(data);
    while(bytes) {
        DWORD part=DWORD(bytes>0x100000?0x100000:bytes),done=0;
        if(!WriteFile(file,next,part,&done,nullptr) || done!=part)return false;
        bytes-=part;next+=part;
    }
    return true;
}
}
struct PilotAudioStageTrace::Storage {
    HANDLE file=INVALID_HANDLE_VALUE;
    uint8_t* buffer=nullptr;
    uint64_t lifetime=0,epoch=0,first=0,last=0,bytes=0,records=0;
    uint64_t limit=capacity,trigger_tick=0;
    // 1=triggered, 2=capacity truncated, 4=invalid event, 8=window elapsed.
    uint32_t flags=0;
    bool finished=false;
    bool external=false,armed=false;
    const char* error=nullptr;
    ~Storage(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);if(buffer)VirtualFree(buffer,0,MEM_RELEASE);}
};
PilotAudioStageTrace::PilotAudioStageTrace(std::unique_ptr<Storage> storage):storage_(std::move(storage)){}
PilotAudioStageTrace::~PilotAudioStageTrace()=default;
const char* PilotAudioStageTrace::create(std::unique_ptr<PilotAudioStageTrace>& out,const char* path,
                                      uint64_t lifetime,uint64_t epoch,bool retained) {
    if(out || !path || !*path || !lifetime)return "invalid original audio stage trace destination/lifetime";
    try {
        auto storage=std::make_unique<Storage>();
        storage->external=retained;storage->limit=retained?retained_capacity:capacity;
        // Never replace an existing final path or partial observation.
        if(GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES || GetLastError()!=ERROR_FILE_NOT_FOUND)
            return "original audio stage trace destination exists or cannot be inspected";
        const std::string partial=std::string(path)+".partial";
        storage->file=CreateFileA(partial.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(storage->file==INVALID_HANDLE_VALUE)return "original audio stage trace exclusive create failed";
        storage->buffer=static_cast<uint8_t*>(VirtualAlloc(nullptr,size_t(storage->limit),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if(!storage->buffer)return "original audio stage trace bounded allocation failed";
        storage->lifetime=lifetime;storage->epoch=epoch;
        out.reset(new PilotAudioStageTrace(std::move(storage)));
        return nullptr;
    } catch(...) {return "original audio stage trace allocation failed";}
}
const char* PilotAudioStageTrace::trigger(uint64_t ticks) {
    auto& s=*storage_;
    if(!s.external || s.armed || s.finished || ticks<s.epoch)
        return "original retained audio stage trace invalid/repeated trigger";
    s.armed=true;s.trigger_tick=ticks;return nullptr;
}
void PilotAudioStageTrace::record(uint64_t ticks,const xlle::ApuObservation& event) noexcept {
    auto& s=*storage_;
    if(s.finished || (s.flags&14))return;
    if(s.external && !s.armed)return;
    if(s.external && ticks>=s.trigger_tick && ticks-s.trigger_tick>=window_ticks) {
        s.flags|=8;finish();return;
    }
    if(!(s.flags&1)) {
        // The original title keeps four silent looping sources active during
        // startup. Begin at the first block containing a nonzero sample, with
        // its real cursor; recording those loops would exhaust the diagnostic
        // bound before movie input. This never suppresses device processing.
        if(!s.external) {
            if(event.kind!=xlle::ApuObservationKind::Source || !event.bytes)return;
            if(!event.data || event.bytes>65536 || event.bytes%sizeof(float)) {
                s.flags|=4;s.error="original audio stage trace invalid source trigger";return;
            }
            bool nonzero=false;
            for(uint32_t at=0;at<event.bytes;at+=sizeof(float)) {
                float sample;std::memcpy(&sample,static_cast<const uint8_t*>(event.data)+at,sizeof(sample));
                if(sample!=0.0f){nonzero=true;break;}
            }
            if(!nonzero)return;
        }
        s.flags|=1;s.first=s.last=ticks;
    }
    if(ticks<s.epoch || (s.external && ticks<s.trigger_tick) || ticks<s.last || event.metadata_words>64 || event.bytes>65536 ||
       (event.metadata_words && !event.metadata) || (event.bytes && !event.data) ||
       uint32_t(event.kind)<1 || uint32_t(event.kind)>8) {
        s.flags|=4;s.error="original audio stage trace invalid borrowed event";return;
    }
    // Publish the immutable diagnostic prefix as soon as its bound is met.
    // A reviewer can inspect it while the same title continues toward a real
    // lifecycle event; finishing observation never stops device processing.
    if(ticks-s.first>=window_ticks){s.flags|=8;finish();return;}
    const uint64_t size=sizeof(Record)+uint64_t(event.metadata_words)*4+event.bytes;
    if(size>s.limit-s.bytes){s.flags|=2;finish();return;}
    const Record record{uint32_t(event.kind),event.id,event.metadata_words,event.bytes,ticks,s.records+1};
    auto* next=s.buffer+s.bytes;
    std::memcpy(next,&record,sizeof(record));next+=sizeof(record);
    if(event.metadata_words)std::memcpy(next,event.metadata,size_t(event.metadata_words)*4);
    next+=size_t(event.metadata_words)*4;
    if(event.bytes)std::memcpy(next,event.data,event.bytes);
    s.bytes+=size;++s.records;s.last=ticks;
}
const char* PilotAudioStageTrace::finish() {
    auto& s=*storage_;if(s.finished)return s.error;s.finished=true;
    const Header header{{'X','L','L','E','S','T','G','1'},1,sizeof(Header),1,s.flags,
                        s.lifetime,s.epoch,s.first,s.bytes,s.records};
    const Footer footer{{'X','L','L','E','E','N','D','1'},s.bytes,s.records};
    const bool written=write_all(s.file,&header,sizeof(header)) && write_all(s.file,s.buffer,s.bytes) &&
                       write_all(s.file,&footer,sizeof(footer));
    const bool flushed=FlushFileBuffers(s.file)!=0;
    const bool closed=CloseHandle(s.file)!=0;if(closed)s.file=INVALID_HANDLE_VALUE;
    if(!s.error && (!written || !flushed || !closed))s.error="original audio stage trace persistence failed";
    return s.error;
}
