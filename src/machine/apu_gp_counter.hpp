// Exact observed GP/EP composite profile over each serialized CPU's retired
// progress. This is not a timer identity map or EP cadence/threshold contract.
#pragma once
#include <cstdint>
namespace xlle {
class ApuDspCounterProfile {
public:
    bool permits(uint32_t address,uint32_t value)const {
        return (stage_==0 && address==0xffffb2 && value==0xffffff) ||
               (stage_==1 && address==0xffffb0 && value==1) ||
               (stage_==2 && address==0xffffb1 && value==1);
    }
    bool write(uint32_t address,uint32_t value) {
        if(!permits(address,value))return false;
        ++stage_;return true;
    }
    void retired(uint32_t cycles) {
        // Stage3 is pending: callback-time progress precedes the B1 instruction.
        if(stage_==3){epoch_=cycles;stage_=4;}
    }
    uint32_t observe(uint32_t retired_before_instruction)const {
        return stage_==4?(retired_before_instruction-epoch_)&0xffffffu:0u;
    }
    bool touched()const{return stage_!=0;}
    bool active()const{return stage_==4;}
private:
    uint32_t stage_=0,epoch_=0;
};
} // namespace xlle
