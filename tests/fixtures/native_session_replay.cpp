#include "session_replay.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

static void require(bool value) {if(!value){std::fprintf(stderr,"fixture assertion failed\n");std::exit(10);}}
int main(int argc,char **argv) {
    if(argc!=3)return 10;
    std::array<uint8_t,32> identity{};for(unsigned i=0;i<32;++i)identity[i]=(uint8_t)(i*7);
    bool capture=!std::strcmp(argv[1],"record") || !std::strcmp(argv[1],"overflow") || !std::strcmp(argv[1],"terminal-cut");
    if(!std::strcmp(argv[1],"identity"))identity[0]^=1;
    require(pilot_session_open(capture?argv[2]:nullptr,capture?nullptr:argv[2],identity.data())==
        (capture?PILOT_SESSION_RECORD:PILOT_SESSION_REPLAY));
    if(!std::strcmp(argv[1],"overflow")) {
        pilot_session_actor(0);
        std::vector<uint8_t> data(1024*1024+1,1);
        pilot_session_value(1,0,data.data(),data.size());
        require(!pilot_session_healthy());require(!pilot_session_finish());return 0;
    }
    if(!std::strcmp(argv[1],"terminal-cut")) {
        pilot_session_actor(0);
        std::atomic<bool> release{false},entered{false},returned{false};
        std::thread late([&] {
            pilot_session_actor(1);
            while(!release.load())std::this_thread::yield();
            entered=true;pilot_session_value64(PILOT_SESSION_MONOTONIC,0,17);returned=true;
        });
        pilot_session_terminal_begin();release=true;
        while(!entered.load())std::this_thread::yield();
        pilot_session_value64(PILOT_SESSION_MONOTONIC,0,42);
        require(pilot_session_finish());require(!returned.load());
        late.detach();std::_Exit(0); // The real terminal owner exits with suspended native threads.
    }
    std::array<std::thread,4> threads;
    for(unsigned seat=0;seat<4;++seat)threads[seat]=std::thread([seat,capture,argv] {
        pilot_session_actor(seat);
        for(unsigned i=0;i<1200;++i) {
            auto original=uint64_t(seat)*1000000+i;
            auto value=pilot_session_value64(PILOT_SESSION_MONOTONIC,i,capture?original:0);
            require(value==original);
            std::array<uint32_t,5> evidence{{seat,i,0x12345678u,0,0xffffffffu}};
            if(!std::strcmp(argv[1],"evidence") && seat==0 && i==71)evidence[3]=1;
            pilot_session_verify(PILOT_SESSION_FRAME,i,evidence.data(),sizeof(evidence));
        }
    });
    for(auto& thread:threads)thread.join();
    std::mutex resource,admission;
    std::condition_variable order_changed;
    unsigned seat_next=0,shared=0;
    for(unsigned seat=0;seat<4;++seat)threads[seat]=std::thread([&,seat] {
        pilot_session_actor(seat);
        for(unsigned i=0;i<100;++i) {
            std::unique_lock<std::mutex> ordered(admission,std::defer_lock);
            if(capture) {ordered.lock();order_changed.wait(ordered,[&]{return seat_next==seat;});}
            pilot_session_lock_enter(7);
            {std::lock_guard<std::mutex> owner(resource);pilot_session_lock_acquired(7);
             uint32_t evidence[]={++shared,seat,i};
             pilot_session_verify(PILOT_SESSION_SERVICE,7,evidence,sizeof(evidence));}
            if(capture) {seat_next=(seat+1)%4;ordered.unlock();order_changed.notify_all();}
        }
    });
    for(auto& thread:threads)thread.join();
    require(shared==400);
    // Empty observations retain the running CRC, and thousands of distinct
    // async transfers share a bounded stream without losing logical identity.
    for(unsigned id=0x10000;id<0x10400;++id) {
        pilot_session_actor(id);
        pilot_session_verify(PILOT_SESSION_SERVICE,id,nullptr,0);
        require(pilot_session_value64(PILOT_SESSION_SERVICE,id,capture?id:0)==id);
    }
    pilot_session_actor(0);
    require(pilot_session_progress()==0);
    for(unsigned i=0;i<30;++i) {
        PilotSessionTurn turn{i%4,PILOT_SESSION_PREEMPT,i*17ull,0x987654321000ull+i};
        if(!std::strcmp(argv[1],"schedule") && i==14)++turn.checkpoint;
        if(!capture) {PilotSessionTurn next{};require(pilot_session_next_turn(&next));require(next.actor==i%4);}
        pilot_session_turn(&turn);
        require(pilot_session_progress()==i+1);
    }
    if(!capture){PilotSessionTurn next{};require(!pilot_session_next_turn(&next));}
    require(pilot_session_healthy());require(pilot_session_finish());return 0;
}
