#include "session_replay.h"
#include "session_journal.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
constexpr size_t QueueLimit = 8 * 1024 * 1024;
constexpr size_t BatchTarget = 64 * 1024;
constexpr size_t ValueLimit = 1024 * 1024;
constexpr uint64_t SessionLimit = 64ull * 1024 * 1024 * 1024;
constexpr uint64_t StorageLimit = 8ull * 1024 * 1024 * 1024;
constexpr uint32_t ScheduleActor = 0xfffffffeu;
// Asynchronous publication owns the CPU permit, so its observations have one
// exact order even when individual transfer identities grow throughout play.
// Keep that logical identity in the record, using one bounded journal stream.
constexpr uint32_t AsyncActor = 0xfffe0000u;
uint32_t channel(uint32_t id){return id>=0x10000u && id<AsyncActor?AsyncActor:id;}
constexpr uint32_t TurnTag = 0x5455524eu;
constexpr uint32_t LockActorBase = 0xffff0000u, LockTag = 0x4c4f434bu;
constexpr unsigned HeaderBytes = 48, RecordHeaderBytes = 32;
thread_local uint32_t actor = PILOT_SESSION_ACTOR_NONE;
struct Event { uint32_t actor; std::vector<uint8_t> bytes; };
struct Reader { pilot_session_journal::Input file; uint64_t sequence = 0; std::mutex lock; };
struct LockOrder {uint32_t next=0;bool pending=false,ended=false;};
struct State {
    struct Activity {std::atomic<uint32_t> tag{0},active{0};std::atomic<uint64_t> argument{0},sequence{0};};
    std::array<Activity,69> activity{};
    int mode = 0;
    std::atomic<bool> failed{false};
    std::atomic<bool> sealing{false};
    std::atomic<uint64_t> progress{0};
    std::atomic<uint32_t> terminal_actor{PILOT_SESSION_ACTOR_NONE};
    std::array<uint8_t, 32> identity{};
    std::filesystem::path root;
    std::mutex lock;
    std::mutex reader_lock;
    std::mutex order_lock;
    std::condition_variable order_changed;
    std::map<uint32_t,LockOrder> orders;
    std::condition_variable changed, drained;
    std::deque<Event> queue;
    size_t queued = 0;
    uint64_t accepted = 0, written = 0, reserved = 0;
    bool stopping = false, finished = false;
    std::thread worker;
    std::map<uint32_t, uint64_t> sequences;
    std::map<uint32_t, std::unique_ptr<Reader>> readers;
    PilotSessionTurn next{};
    bool have_next = false, exhausted = false;
    PilotSessionDeviceEnter device_enter = nullptr;
    PilotSessionDeviceEnter device_try_enter = nullptr;
    PilotSessionDeviceLeave device_leave = nullptr;
    PilotSessionHostWaitBegin host_wait_begin = nullptr;
    PilotSessionHostWaitEnd host_wait_end = nullptr;
    PilotSessionTerminal terminal = nullptr;
};
/* Deliberately process-lifetime storage: a failed/timed-out writer must never
 * access a destroyed queue during terminal process shutdown. */
State& state() { static State *s = new State; return *s; }
struct StallDeadline {
    uint64_t progress=state().progress.load();
    std::chrono::steady_clock::time_point began=std::chrono::steady_clock::now();
    bool expired() {
        auto next=state().progress.load();auto now=std::chrono::steady_clock::now();
        if(next!=progress){progress=next;began=now;}
        return now-began>std::chrono::seconds(30);
    }
};
void put32(uint8_t *p, uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i)); }
void put64(uint8_t *p, uint64_t v) { for(unsigned i=0;i<8;++i)p[i]=(uint8_t)(v>>(8*i)); }
uint32_t get32(const uint8_t *p) { uint32_t v=0;for(unsigned i=0;i<4;++i)v|=(uint32_t)p[i]<<(8*i);return v; }
uint64_t get64(const uint8_t *p) { uint64_t v=0;for(unsigned i=0;i<8;++i)v|=(uint64_t)p[i]<<(8*i);return v; }
uint32_t crc(const uint8_t *p, size_t n, uint32_t value=0xffffffffu) {
    if(!n)return value;
    return (uint32_t)crc32_z(value^0xffffffffu,p,n)^0xffffffffu;
}
std::filesystem::path filename(const State& s, uint32_t id) {
    char name[40];std::snprintf(name,sizeof(name),"actor-%08x.xlr",id);return s.root/name;
}
std::array<uint8_t,HeaderBytes> header(const State& s, uint32_t id) {
    std::array<uint8_t,HeaderBytes> h{};std::memcpy(h.data(),"XLRRPLY2",8);
    put32(h.data()+8,2);put32(h.data()+12,id);std::memcpy(h.data()+16,s.identity.data(),32);return h;
}
void fail(const char *why) noexcept {
    auto& s=state();if(!s.failed.exchange(true)) {
        std::fprintf(stderr,"[SESSION REPLAY INCOMPLETE] %s; recording stopped, gameplay continues\n",why);std::fflush(stderr);
    }
    s.changed.notify_all();s.drained.notify_all();
}
void writer() noexcept {
    auto& s=state();
    try {
        std::map<uint32_t,pilot_session_journal::Output> files;
        auto last=std::chrono::steady_clock::now();
        for(;;) {
            std::deque<Event> batch;bool stop;
            {
                std::unique_lock<std::mutex> guard(s.lock);
                s.changed.wait_for(guard,std::chrono::milliseconds(50),[&]{return s.stopping || s.failed || s.queued>=BatchTarget;});
                batch.swap(s.queue);s.queued=0;stop=s.stopping;
            }
            if(s.failed)break;
            uint64_t completed=0;
            for(auto& event:batch) {
                auto& file=files[event.actor];
                if(!file.is_open()) {
                    auto path=filename(s,event.actor);
                    if(std::filesystem::exists(path)) {fail("actor file already exists");break;}
                    if(files.size()>512){fail("actor journal count budget reached");break;}
                    file.open(path);
                    auto h=header(s,event.actor);file.write((const char*)h.data(),h.size());
                }
                file.write((const char*)event.bytes.data(),(std::streamsize)event.bytes.size());
                if(!file) {fail("actor journal write failed");break;}
                ++completed;
            }
            {std::lock_guard<std::mutex> guard(s.lock);s.written+=completed;}
            auto now=std::chrono::steady_clock::now();
            if(stop || now-last>=std::chrono::milliseconds(100)) {
                uint64_t storage=0;
                for(auto& entry:files) {entry.second.flush();storage+=entry.second.offset();if(!entry.second)fail("actor journal flush failed");}
                if(storage>StorageLimit)fail("compressed journal storage budget reached");
                last=now;
            }
            s.drained.notify_all();if(stop || s.failed)break;
        }
        for(auto& entry:files) {entry.second.close();if(!entry.second)fail("actor journal close failed");}
    } catch(...) {fail("journal writer exception");}
    { std::lock_guard<std::mutex> guard(s.lock);s.finished=true; }s.drained.notify_all();
}
void append(uint32_t id,uint32_t tag,uint64_t argument,const void *value,size_t bytes) {
    auto& s=state();
    if(s.failed)return;
    if(bytes>ValueLimit || (bytes && !value)) {fail("observation exceeds record bound");return;}
    std::unique_lock<std::mutex> guard(s.lock);
    while(s.sealing.load(std::memory_order_acquire) && actor!=s.terminal_actor.load()) {
        guard.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Process exit owns these threads now.
        guard.lock();
    }
    size_t length=RecordHeaderBytes+bytes;
    if(s.stopping || length>QueueLimit-s.queued || length>SessionLimit-s.reserved) {
        fail(s.stopping?"observation after final drain":length>QueueLimit-s.queued?"journal queue full":"session storage budget reached");return;
    }
    uint32_t stream=channel(id);
    Event event{stream,std::vector<uint8_t>(length)};auto *p=event.bytes.data();
    put64(p,s.sequences[stream]++);put32(p+8,tag);put32(p+12,(uint32_t)bytes);put64(p+16,argument);
    if(bytes)std::memcpy(p+RecordHeaderBytes,value,bytes);
    put32(p+24,~crc(p+RecordHeaderBytes,bytes,crc(p,24)));
    if(stream!=id)put32(p+28,id);
    bool wake=s.queued<BatchTarget && s.queued+length>=BatchTarget;
    s.queued+=length;s.reserved+=length;++s.accepted;s.queue.push_back(std::move(event));
    if(wake)s.changed.notify_one();
}
bool read(uint32_t id,uint32_t tag,uint64_t argument,void *value,size_t bytes,bool allow_end=false,uint64_t *observed_sequence=nullptr) {
    auto& s=state();
    Reader *reader;uint32_t stream=channel(id);
    {
        std::lock_guard<std::mutex> registry(s.reader_lock);
        auto& owned=s.readers[stream];
        if(!owned) {
            owned=std::make_unique<Reader>();owned->file.open(filename(s,stream));
            std::array<uint8_t,HeaderBytes> actual{};owned->file.read((char*)actual.data(),actual.size());
            if(!owned->file || actual!=header(s,stream))pilot_session_diverge("actor identity/header mismatch",0);
        }
        reader=owned.get();
    }
    // Each logical actor has exactly one producer/consumer. The scheduler
    // serializes schedule reads. Registry insertion alone needs a shared lock.
    auto& r=*reader;std::array<uint8_t,RecordHeaderBytes> h{};
    std::lock_guard<std::mutex> owner(r.lock);
    if(id<s.activity.size())s.activity[id].sequence.store(r.sequence,std::memory_order_relaxed);
    r.file.read((char*)h.data(),h.size());
    if(!r.file.gcount() && r.file.eof() && allow_end)return false;
    if(r.file.gcount()!=(std::streamsize)h.size())pilot_session_diverge("incomplete observation header",r.sequence);
    if(bytes>ValueLimit || get64(h.data())!=r.sequence || get32(h.data()+8)!=tag ||
        get32(h.data()+12)!=bytes || get64(h.data()+16)!=argument || get32(h.data()+28)!=(stream==id?0:id)) {
        std::fprintf(stderr,"[SESSION REPLAY ORDER] actor=%08x sequence=%llu expected_tag=%u actual_tag=%u expected_argument=%llx actual_argument=%llx expected_bytes=%u actual_bytes=%zu\n",
            id,(unsigned long long)r.sequence,get32(h.data()+8),tag,
            (unsigned long long)get64(h.data()+16),(unsigned long long)argument,get32(h.data()+12),bytes);
        pilot_session_diverge("observation sequence/tag/argument/size mismatch",r.sequence);
    }
    if(bytes)r.file.read((char*)value,(std::streamsize)bytes);
    if((bytes && r.file.gcount()!=(std::streamsize)bytes) ||
       get32(h.data()+24)!=~crc((const uint8_t*)value,bytes,crc(h.data(),24)))
        pilot_session_diverge("observation checksum/truncation",r.sequence);
    if(observed_sequence)*observed_sequence=r.sequence;
    ++r.sequence;return true;
}
void observation(uint32_t tag,uint64_t argument,void *value,size_t bytes,bool verify) {
    auto& s=state();if(!s.mode || actor==PILOT_SESSION_ACTOR_NONE)return;
    if(actor<s.activity.size()) {
        s.activity[actor].argument.store(argument,std::memory_order_relaxed);
        s.activity[actor].tag.store(tag,std::memory_order_relaxed);
        s.activity[actor].active.store(1,std::memory_order_release);
    }
    if(s.mode==PILOT_SESSION_RECORD) {append(actor,tag,argument,value,bytes);return;}
    if(bytes>ValueLimit || (bytes && !value))pilot_session_diverge("invalid observation bound",0);
    if(verify) {
        std::vector<uint8_t> expected(bytes);uint64_t sequence=0;
        StallDeadline deadline;
        while(!read(actor,tag,argument,expected.data(),bytes,true,&sequence)) {
            if(actor==s.terminal_actor || deadline.expired())
                pilot_session_diverge("observation stream exhausted before terminal boundary",sequence);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if(bytes && std::memcmp(expected.data(),value,bytes)) {
            const auto *actual=static_cast<const uint8_t*>(value);size_t offset=0;
            while(offset<bytes && expected[offset]==actual[offset])++offset;
            std::fprintf(stderr,"[SESSION REPLAY VALUE] actor=%08x sequence=%llu tag=%u argument=%llx bytes=%zu first_difference=%zu expected=",
                actor,(unsigned long long)sequence,tag,(unsigned long long)argument,bytes,offset);
            for(size_t i=offset;i<bytes && i<offset+16;++i)std::fprintf(stderr,"%02x",expected[i]);
            std::fprintf(stderr," actual=");
            for(size_t i=offset;i<bytes && i<offset+16;++i)std::fprintf(stderr,"%02x",actual[i]);
            std::fprintf(stderr,"\n");pilot_session_diverge("simulation evidence differs",sequence);
        }
    } else {
        StallDeadline deadline;
        while(!read(actor,tag,argument,value,bytes,true)) {
            if(actor==s.terminal_actor || deadline.expired())
                pilot_session_diverge("observation stream exhausted before terminal boundary",0);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}
} // namespace

extern "C" void pilot_session_diverge(const char *reason,uint64_t sequence) {
    std::fprintf(stderr,"[SESSION REPLAY DIVERGENCE] actor=%08x sequence=%llu reason=%s\n",actor,(unsigned long long)sequence,reason);
    std::fflush(stderr);std::_Exit(3);
}
extern "C" void pilot_session_describe_activity(void) {
    auto& s=state();
    for(size_t i=0;i<s.activity.size();++i)if(s.activity[i].tag.load())
        std::fprintf(stderr,"[SESSION REPLAY ACTIVITY] actor=%zu active=%u tag=%u argument=%llx sequence=%llu\n",i,
            s.activity[i].active.load(),s.activity[i].tag.load(),(unsigned long long)s.activity[i].argument.load(),
            (unsigned long long)s.activity[i].sequence.load());
}
extern "C" int pilot_session_open(const char *record,const char *replay,const uint8_t identity[32]) {
    auto& s=state();
    if((!record || !*record) && (!replay || !*replay))return 0;
    try {
        if(s.mode || !identity || (record && *record && replay && *replay))pilot_session_diverge("invalid session configuration",0);
        s.mode=record && *record?PILOT_SESSION_RECORD:PILOT_SESSION_REPLAY;
        s.root=std::filesystem::u8path(s.mode==PILOT_SESSION_RECORD?record:replay);
        std::memcpy(s.identity.data(),identity,32);
        if(!std::filesystem::is_directory(s.root) || std::filesystem::is_symlink(s.root))
            throw std::runtime_error("journal directory is unavailable or linked");
        if(s.mode==PILOT_SESSION_RECORD) {
            if(!std::filesystem::is_empty(s.root))throw std::runtime_error("journal directory is not empty");
            s.worker=std::thread(writer);
        }
        std::fprintf(stderr,"[SESSION REPLAY] mode=%s format=2 compression=gzip queue_bytes=%zu storage_bytes=%llu expanded_bytes=%llu\n",
            s.mode==PILOT_SESSION_RECORD?"record":"replay",QueueLimit,(unsigned long long)StorageLimit,(unsigned long long)SessionLimit);
    } catch(...) {if(s.mode==PILOT_SESSION_REPLAY)pilot_session_diverge("session initialization failed",0);fail("session initialization failed");}
    return s.mode;
}
extern "C" int pilot_session_mode(void) {return state().mode;}
extern "C" int pilot_session_healthy(void) {return !state().failed;}
extern "C" void pilot_session_actor(uint32_t id) {actor=id;}
extern "C" uint32_t pilot_session_actor_id(void) {return actor;}
extern "C" void pilot_session_bind_devices(PilotSessionDeviceEnter enter,PilotSessionDeviceLeave leave) {
    state().device_enter=enter;state().device_leave=leave;
}
extern "C" int pilot_session_device_enter(uint32_t id) {
    pilot_session_actor(id);auto& s=state();return s.mode && s.device_enter?s.device_enter(id):0;
}
extern "C" void pilot_session_bind_device_try(PilotSessionDeviceEnter enter) {state().device_try_enter=enter;}
extern "C" int pilot_session_device_try_enter(uint32_t id) {
    pilot_session_actor(id);auto& s=state();return s.mode && s.device_try_enter?s.device_try_enter(id):0;
}
extern "C" void pilot_session_device_leave(int token) {
    auto& s=state();if(token && s.device_leave)s.device_leave(token);
}
extern "C" void pilot_session_bind_host_wait(PilotSessionHostWaitBegin begin,PilotSessionHostWaitEnd end) {
    state().host_wait_begin=begin;state().host_wait_end=end;
}
extern "C" int pilot_session_host_wait_begin(void) {
    auto& s=state();return s.mode && s.host_wait_begin?s.host_wait_begin():0;
}
extern "C" void pilot_session_host_wait_end(int token) {
    auto& s=state();if(token && s.host_wait_end)s.host_wait_end(token);
}
extern "C" void pilot_session_bind_terminal(PilotSessionTerminal terminal) {state().terminal=terminal;}
extern "C" void pilot_session_report_stop(const char *reason,uint32_t code) {
    auto& s=state();if(s.mode && s.terminal)s.terminal(reason,code);
}
extern "C" void pilot_session_terminal_begin(void) {
    auto& s=state();std::lock_guard<std::mutex> guard(s.lock);
    s.terminal_actor.store(actor);s.sealing.store(true,std::memory_order_release);
}
extern "C" void pilot_session_lock_enter(uint32_t id) {
    auto& s=state();if(s.mode!=PILOT_SESSION_REPLAY || actor==PILOT_SESSION_ACTOR_NONE)return;
    try {
        if(!id || id>=65534)pilot_session_diverge("invalid logical owner lock",id);
        std::unique_lock<std::mutex> guard(s.order_lock);auto& order=s.orders[id];
        StallDeadline deadline;
        for(;;) {
            if(!order.pending && !order.ended) {
                uint8_t wire[4];
                if(read(LockActorBase+id,LockTag,id,wire,sizeof(wire),true)) {order.next=get32(wire);order.pending=true;}
                else order.ended=true;
            }
            if(order.pending && order.next==actor)return;
            // A capture may end while a losing caller is blocked on a lock.
            // Keep it parked; only the recorded terminal owner can end replay.
            if(deadline.expired())
                pilot_session_diverge("owner lock could not reach recorded admission",id);
            s.order_changed.wait_for(guard,std::chrono::milliseconds(100));
        }
    } catch(...) {pilot_session_diverge("owner-lock admission exception",id);}
}
extern "C" void pilot_session_lock_acquired(uint32_t id) {
    auto& s=state();if(!s.mode || actor==PILOT_SESSION_ACTOR_NONE)return;
    try {
        if(!id || id>=65534)pilot_session_diverge("invalid logical owner lock",id);
        if(s.mode==PILOT_SESSION_RECORD) {uint8_t wire[4];put32(wire,actor);append(LockActorBase+id,LockTag,id,wire,sizeof(wire));}
        else {
            std::lock_guard<std::mutex> guard(s.order_lock);auto& order=s.orders[id];
            if(!order.pending || order.next!=actor)pilot_session_diverge("owner lock was not admitted",id);
            order.pending=false;s.order_changed.notify_all();
        }
    } catch(...) {if(s.mode==PILOT_SESSION_REPLAY)pilot_session_diverge("owner-lock publication exception",id);fail("owner-lock publication failed");}
}
extern "C" void pilot_session_incomplete(const char *reason) {fail(reason);}
extern "C" void pilot_session_value(uint32_t tag,uint64_t argument,void *value,size_t bytes) {
    try {observation(tag,argument,value,bytes,false);if(actor<state().activity.size())state().activity[actor].active.store(0,std::memory_order_release);}catch(...) {if(state().mode==PILOT_SESSION_REPLAY)pilot_session_diverge("observation exception",0);fail("observation allocation failed");}
}
extern "C" void pilot_session_verify(uint32_t tag,uint64_t argument,const void *value,size_t bytes) {
    try {observation(tag,argument,const_cast<void*>(value),bytes,true);if(actor<state().activity.size())state().activity[actor].active.store(0,std::memory_order_release);}catch(...) {if(state().mode==PILOT_SESSION_REPLAY)pilot_session_diverge("verification exception",0);fail("verification allocation failed");}
}
extern "C" uint64_t pilot_session_value64(uint32_t tag,uint64_t argument,uint64_t value) {
    pilot_session_value(tag,argument,&value,sizeof(value));return value;
}
extern "C" int pilot_session_next_turn(PilotSessionTurn *turn) {
    auto& s=state();if(s.mode!=PILOT_SESSION_REPLAY || !turn)return 0;
    try {
        if(!s.have_next && !s.exhausted) {
            uint8_t wire[24];
            if(!read(ScheduleActor,TurnTag,0,wire,sizeof(wire),true))s.exhausted=true;
            else {s.next={get32(wire),get32(wire+4),get64(wire+8),get64(wire+16)};s.have_next=true;}
        }
        if(s.have_next)*turn=s.next;
    } catch(...) {pilot_session_diverge("schedule read exception",0);}
    return s.have_next?1:0;
}
extern "C" uint64_t pilot_session_progress(void) {return state().progress.load();}
extern "C" void pilot_session_turn(const PilotSessionTurn *turn) {
    auto& s=state();if(!s.mode || !turn)return;
    try {
        if(s.mode==PILOT_SESSION_RECORD) {
            uint8_t wire[24];put32(wire,turn->actor);put32(wire+4,turn->boundary);
            put64(wire+8,turn->checkpoint);put64(wire+16,turn->digest);append(ScheduleActor,TurnTag,0,wire,sizeof(wire));
        } else {
            PilotSessionTurn expected{};
            if(!pilot_session_next_turn(&expected) || expected.actor!=turn->actor || expected.boundary!=turn->boundary ||
                expected.checkpoint!=turn->checkpoint || expected.digest!=turn->digest)
                pilot_session_diverge("CPU/device turn or state digest differs",0);
            s.have_next=false;
        }
        s.progress.fetch_add(1);
    } catch(...) {if(s.mode==PILOT_SESSION_REPLAY)pilot_session_diverge("schedule write exception",0);fail("schedule allocation failed");}
}
extern "C" int pilot_session_finish(void) {
    auto& s=state();if(!s.mode)return !s.failed;
    if(s.mode==PILOT_SESSION_REPLAY) {
        try {
            const auto began=std::chrono::steady_clock::now();
            for(;;) {
                bool complete=true;
                {std::lock_guard<std::mutex> guard(s.order_lock);
                 for(const auto& entry:s.orders)if(entry.second.pending)complete=false;}
                {std::lock_guard<std::mutex> registry(s.reader_lock);
                for(const auto& entry:std::filesystem::directory_iterator(s.root)) {
                auto name=entry.path().filename().string();uint32_t id=0;
                if(name.size()!=18 || name.compare(0,6,"actor-") || name.compare(14,4,".xlr"))
                    pilot_session_diverge("unexpected actor journal file",0);
                for(unsigned i=6;i<14;++i) {
                    char c=name[i];unsigned digit=c>='0' && c<='9'?(unsigned)(c-'0'):
                        c>='a' && c<='f'?(unsigned)(c-'a'+10):16;
                    if(digit>15)pilot_session_diverge("invalid actor journal name",0);
                    id=(id<<4)|digit;
                }
                auto found=s.readers.find(id);
                    if(found==s.readers.end())complete=false;
                    else {std::lock_guard<std::mutex> owner(found->second->lock);
                          if(found->second->file.peek()!=std::char_traits<char>::eof())complete=false;}
                }
                }
                if(complete)break;
                if(std::chrono::steady_clock::now()-began>std::chrono::seconds(30))
                    pilot_session_diverge("unconsumed observations at terminal boundary",0);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            std::fprintf(stderr,"[SESSION REPLAY VERIFIED] terminal boundary and all observations match\n");std::fflush(stderr);return 1;
        } catch(...) {pilot_session_diverge("terminal verification exception",0);}
    }
    try {
        std::unique_lock<std::mutex> guard(s.lock);s.stopping=true;s.changed.notify_all();
        bool done=s.drained.wait_for(guard,std::chrono::seconds(2),[&]{return s.finished;});
        const auto accepted=s.accepted,written=s.written;
        bool complete=done && !s.failed && written==accepted;
        guard.unlock();
        if(done && s.worker.joinable())s.worker.join();else if(!done)fail("final journal drain timed out");
        std::fprintf(stderr,"[SESSION REPLAY FINAL] records=%llu written=%llu complete=%u\n",
            (unsigned long long)accepted,(unsigned long long)written,complete?1u:0u);std::fflush(stderr);return complete?1:0;
    } catch(...) {fail("final journal drain exception");return 0;}
}
