#include "cost_ring.h"
#include <array>
#include <mutex>
#include <sstream>
#include <charconv>
#include <vector>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <pthread.h>
#endif
static constexpr unsigned capacity=4096;
#if PILOT_COST_RING
extern "C" {PILOT_COST_TLS PilotCostLocal pilot_cost_local;}
namespace {
struct Thread {
#ifdef _WIN32
    HANDLE handle=nullptr;
#else
    clockid_t clock{};
#endif
    uint64_t id=0,cpu=0,cycles=0;bool baseline=false;
};
struct Row {uint64_t sequence,swap,wall_ns,thread_cpu_ns[COST_ROLES],thread_cycles[COST_ROLES],thread_ids[COST_ROLES],write_faults,probe_cycles;unsigned flags;PilotCostTotals totals;};
std::array<Row,capacity> ring{};
std::array<Thread,COST_ROLES> threads{};
std::mutex ring_mutex;
uint64_t sequence=0,previous_wall=0,previous_faults=0;
PilotCostTotals previous{};
uint64_t read_cpu(Thread& t,uint64_t& cycles) {
    cycles=0;
#ifdef _WIN32
    FILETIME a,b,k,u;ULONG64 c=0;
    if(!t.handle || !GetThreadTimes(t.handle,&a,&b,&k,&u) || !QueryThreadCycleTime(t.handle,&c))return 0;
    cycles=c;return ((uint64_t(k.dwHighDateTime)<<32|k.dwLowDateTime)+(uint64_t(u.dwHighDateTime)<<32|u.dwLowDateTime))*100;
#else
    timespec s{};if(!t.id || clock_gettime(t.clock,&s))return 0;
    cycles=uint64_t(s.tv_sec)*1000000000+s.tv_nsec;return cycles;
#endif
}
const char *roles[]={"title","xaudio2","dispatcher","interpolation","feeder"};
const char *waits[]={"pacing","poll","guest_sleep","present_retire","completion","flush","map_read","graphics_lock","audio_lock","callback","i3dl2"};
const char *works[]={"d3d","other","companion","external_packet"};
const char *counters[]={"guest_draws","host_draws","companion_draws","packets","group_exhaustion","vertex_upload_bytes","texture_upload_bytes","visibility_queries","readbacks","render_target_locks","critical_section_enters","kernel_calls","i3dl2_commits"};
const char *epochs[]={"present","clear","target","upload","readback","copy","visibility","capacity","admission","other"};
template<size_t N> void object(std::ostream& o,const char *key,const char *const (&names)[N],const uint64_t *values) {
    o<<",\""<<key<<"\":{";for(size_t i=0;i<N;++i){if(i)o<<',';o<<'"'<<names[i]<<"\":"<<values[i];}o<<'}';
}
}
extern "C" uint64_t pilot_cost_now(void) {
#ifdef _WIN32
    static const uint64_t frequency=[](){LARGE_INTEGER q;QueryPerformanceFrequency(&q);return uint64_t(q.QuadPart);}();
    LARGE_INTEGER q;QueryPerformanceCounter(&q);return uint64_t(q.QuadPart)/frequency*1000000000+uint64_t(q.QuadPart)%frequency*1000000000/frequency;
#else
    timespec s{};clock_gettime(CLOCK_MONOTONIC,&s);return uint64_t(s.tv_sec)*1000000000+s.tv_nsec;
#endif
}
extern "C" uint64_t pilot_cost_cycles(void) {
#ifdef _WIN32
    ULONG64 c=0;QueryThreadCycleTime(GetCurrentThread(),&c);return c;
#else
    timespec s{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&s);return uint64_t(s.tv_sec)*1000000000+s.tv_nsec;
#endif
}
extern "C" void pilot_cost_register(unsigned role) {
    if(role>=COST_ROLES)return;
    static PILOT_COST_TLS unsigned registered=0;
    if(registered&(1u<<role))return;
    if(role==COST_TITLE && !pilot_cost_local.probe_cycles) {
        /* Subtract the cycle read itself before weighting a sample by 64.
         * Otherwise a cheap service could inherit a full syscall per call.
         * Calibrate once, before the first complete source-swap interval. */
        uint64_t before=pilot_cost_cycles(),best=UINT64_MAX;
        for(unsigned i=0;i<32;++i){uint64_t now=pilot_cost_cycles(),delta=now-before;if(delta<best)best=delta;before=now;}
        pilot_cost_local.probe_cycles=best;
    }
    std::lock_guard<std::mutex> guard(ring_mutex);auto& t=threads[role];
#ifdef _WIN32
    const uint64_t id=GetCurrentThreadId();
    if(t.id!=id){if(t.handle)CloseHandle(t.handle);t=Thread{};
        t.handle=OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,DWORD(id));t.id=id;}
#else
    t=Thread{};t.id=uint64_t(pthread_self());pthread_getcpuclockid(pthread_self(),&t.clock);
#endif
    registered|=1u<<role;
}
extern "C" void pilot_cost_swap(uint64_t swap,uint64_t faults) {
    pilot_cost_register(COST_TITLE);
    const uint64_t wall=pilot_cost_now();
    std::lock_guard<std::mutex> guard(ring_mutex);
    Row row{};row.sequence=sequence+1;row.swap=swap;row.wall_ns=previous_wall?wall-previous_wall:0;
    row.probe_cycles=pilot_cost_local.probe_cycles;
    row.flags=(!previous_wall?1u:0u)|(pilot_cost_local.wait_depth?2u:0u)|(pilot_cost_local.service_depth?4u:0u);
    for(unsigned i=0;i<COST_ROLES;++i){auto& t=threads[i];uint64_t cycles=0,cpu=read_cpu(t,cycles);
        row.thread_ids[i]=t.id;
        if(t.baseline && cpu>=t.cpu && cycles>=t.cycles){row.thread_cpu_ns[i]=cpu-t.cpu;row.thread_cycles[i]=cycles-t.cycles;}
        t.baseline=cpu!=0;t.cpu=cpu;t.cycles=cycles;}
    /* All totals are arrays of uint64_t; copy/difference without racing TLS. */
    uint64_t now[sizeof(previous)/sizeof(uint64_t)],before[sizeof(previous)/sizeof(uint64_t)];
    static_assert(sizeof(now)==sizeof(previous));
    std::memcpy(now,&pilot_cost_local.total,sizeof(now));std::memcpy(before,&previous,sizeof(before));
    previous=pilot_cost_local.total;
    for(unsigned i=0;i<sizeof(now)/sizeof(uint64_t);++i)now[i]-=before[i];
    std::memcpy(&row.totals,now,sizeof(now));
    row.write_faults=faults-previous_faults;previous_faults=faults;previous_wall=wall;
    ring[sequence%capacity]=row;++sequence;
}
#endif
std::string pilot_cost_query(const std::string& args) {
    /* One read-only command: cost_ring [after_sequence [limit]]. Pagination
     * keeps serialization off the producer and reports overwritten history. */
    uint64_t after=0;unsigned limit=capacity;std::string a,b,extra;std::istringstream input(args);input>>a>>b;
    auto parse=[](const std::string& s,auto& n){auto r=std::from_chars(s.data(),s.data()+s.size(),n);return !s.empty() && r.ec==std::errc{} && r.ptr==s.data()+s.size();};
    if((!a.empty() && !parse(a,after)) || (!b.empty() && (!parse(b,limit)||!limit||limit>capacity)) || input>>extra)
        return "{\"ok\":false,\"error\":\"cost_ring [after_sequence [limit in 1..4096]]\"}\n";
#if PILOT_COST_RING
    std::vector<Row> copy(capacity);uint64_t end,first;unsigned count=0;
    {std::lock_guard<std::mutex> guard(ring_mutex);end=sequence;first=end>=capacity?end-capacity+1:1;
        uint64_t begin=after>=end?end+1:(after+1<first?first:after+1);
        for(uint64_t s=begin;s<=end && count<limit;++s)copy[count++]=ring[(s-1)%capacity];}
    std::ostringstream o;o<<"{\"ok\":true,\"enabled\":true,\"capacity\":4096,\"first\":"<<first<<",\"latest\":"<<end
        <<",\"dropped\":"<<(after<first-1?first-1-after:0)<<",\"sample_weight\":64,\"flags\":\"1=no prior interval;2=unbalanced wait;4=unbalanced service\",\"work_scope\":\"D3D/other inclusive CPU cycles sampled at 1/64; companion + external_packet are disjoint subsets of D3D; residual includes uninstrumented services; lock waits exclude uncontended acquisitions\",\"unavailable\":[\"driver_thread_cpu\",\"gpu_timestamps\",\"readback_kinds\",\"session_lock_waits\"],\"rows\":[";
    for(unsigned i=0;i<count;++i){const auto& r=copy[i];if(i)o<<',';
        o<<"{\"sequence\":"<<r.sequence<<",\"swap\":"<<r.swap<<",\"wall_ns\":"<<r.wall_ns<<",\"flags\":"<<r.flags<<",\"probe_cycles\":"<<r.probe_cycles;
        object(o,"thread_cpu_ns",roles,r.thread_cpu_ns);object(o,"thread_cycles",roles,r.thread_cycles);object(o,"thread_ids",roles,r.thread_ids);
        object(o,"wait_ns",waits,r.totals.waits);object(o,"wait_cycles",waits,r.totals.wait_cycles);
        object(o,"sampled_cycles",works,r.totals.work);object(o,"samples",works,r.totals.samples);
        object(o,"counters",counters,r.totals.counters);object(o,"epoch_end",epochs,r.totals.epochs);
        o<<",\"write_watch_faults\":"<<r.write_faults<<",\"strip_capacity\":"<<r.totals.strip_capacity<<",\"strip_boundary\":"<<r.totals.strip_boundary<<'}';}
    o<<"]}\n";return o.str();
#else
    return "{\"ok\":true,\"enabled\":false,\"capacity\":4096,\"rows\":[]}\n";
#endif
}
