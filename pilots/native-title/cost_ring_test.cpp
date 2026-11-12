#include "cost_ring.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"cost ring check failed at %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main() {
    CHECK(pilot_cost_query("arm").find("\"ok\":false")!=std::string::npos);
    CHECK(pilot_cost_query("0 4097").find("\"ok\":false")!=std::string::npos);
    CHECK(pilot_cost_query("0 1 extra").find("\"ok\":false")!=std::string::npos);
#if PILOT_COST_RING
    auto outer=pilot_cost_wait_begin(),inner=pilot_cost_wait_begin();
    pilot_cost_wait_end(COST_FLUSH,inner);pilot_cost_wait_end(COST_COMPLETION,outer);
    CHECK(pilot_cost_local.wait_depth==0);
    CHECK(pilot_cost_local.total.waits[COST_COMPLETION]>0 && pilot_cost_local.total.waits[COST_FLUSH]==0);
    std::atomic<bool> done{false};
    std::thread reader([&]{while(!done.load())CHECK(pilot_cost_query("0 2").find("\"ok\":true")!=std::string::npos);});
    for(unsigned i=1;i<=4200;++i){pilot_cost_add(COST_KERNEL_CALL,7);pilot_cost_swap(i,i*2);}
    done.store(true);reader.join();
    auto a=pilot_cost_query("0 2"),b=pilot_cost_query("0 2");CHECK(a==b);
    CHECK(a.find("\"first\":105")!=std::string::npos);
    CHECK(a.find("\"dropped\":104")!=std::string::npos);
    CHECK(a.find("\"sequence\":105,\"swap\":105")!=std::string::npos);
    CHECK(a.find("\"kernel_calls\":7")!=std::string::npos);
    CHECK(a.find("\"write_watch_faults\":2")!=std::string::npos);
    CHECK(pilot_cost_query("4200").find("\"rows\":[]")!=std::string::npos);
    CHECK(pilot_cost_query("18446744073709551615").find("\"rows\":[]")!=std::string::npos);
#else
    pilot_cost_add(COST_KERNEL_CALL,7);pilot_cost_swap(1,0);
    CHECK(pilot_cost_query("").find("\"enabled\":false")!=std::string::npos);
#endif
    std::puts("PASS cost-ring overwrite, concurrent query, read-only pagination, bounds and compiled-out control");
}
