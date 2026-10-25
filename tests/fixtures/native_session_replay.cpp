#include "session_replay.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

static void require(bool value) {if(!value){std::fprintf(stderr,"fixture assertion failed\n");std::exit(10);}}
int main(int argc,char **argv) {
    if(argc!=3)return 10;
    std::array<uint8_t,32> identity{};for(unsigned i=0;i<32;++i)identity[i]=(uint8_t)(i*7);
    bool capture=!std::strcmp(argv[1],"record") || !std::strcmp(argv[1],"overflow");
    if(!std::strcmp(argv[1],"identity"))identity[0]^=1;
    require(pilot_session_open(capture?argv[2]:nullptr,capture?nullptr:argv[2],identity.data())==
        (capture?PILOT_SESSION_RECORD:PILOT_SESSION_REPLAY));
    if(!std::strcmp(argv[1],"overflow")) {
        pilot_session_actor(0);
        std::vector<uint8_t> data(1024*1024+1,1);
        pilot_session_value(1,0,data.data(),data.size());
        require(!pilot_session_healthy());require(!pilot_session_finish());return 0;
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
    pilot_session_actor(0);
    for(unsigned i=0;i<30;++i) {
        PilotSessionTurn turn{i%4,PILOT_SESSION_PREEMPT,i*17ull,0x987654321000ull+i};
        if(!std::strcmp(argv[1],"schedule") && i==14)++turn.checkpoint;
        if(!capture) {PilotSessionTurn next{};require(pilot_session_next_turn(&next));require(next.actor==i%4);}
        pilot_session_turn(&turn);
    }
    if(!capture){PilotSessionTurn next{};require(!pilot_session_next_turn(&next));}
    require(pilot_session_healthy());require(pilot_session_finish());return 0;
}
