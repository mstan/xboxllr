// Optional Windows pilot diagnostic; the portable APU only lends observations.
#pragma once
#include "machine/apu_observation.hpp"
#include <cstdint>
#include <memory>

class PilotAudioStageTrace {
public:
    static const char* create(std::unique_ptr<PilotAudioStageTrace>& out,const char* path,
                             uint64_t lifetime,uint64_t epoch,bool retained=false);
    ~PilotAudioStageTrace();
    void record(uint64_t ticks,const xlle::ApuObservation& event) noexcept;
    // External one-shot diagnostic trigger. No samples/time are generated.
    const char* trigger(uint64_t ticks);
    const char* finish();
private:
    struct Storage;
    explicit PilotAudioStageTrace(std::unique_ptr<Storage> storage);
    std::unique_ptr<Storage> storage_;
};
