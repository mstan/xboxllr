// Fixed hardware DSP instances. This selects banks, never an HLE fallback.
#pragma once
#include <cstdint>
namespace xlle {
enum class ApuDspKind { Gp,Ep };
constexpr uint32_t apu_dsp_bank_end(ApuDspKind kind,uint32_t address) {
    if(address<(kind==ApuDspKind::Gp?0x1000u:0xc00u))return kind==ApuDspKind::Gp?0x1000u:0xc00u;
    if(kind==ApuDspKind::Gp && address>=0x1400 && address<0x1800)return 0x1800;
    if(address>=0x1800 && address<(kind==ApuDspKind::Gp?0x2000u:0x1900u))return kind==ApuDspKind::Gp?0x2000u:0x1900u;
    if(address>=0x2800 && address<0x3800)return 0x3800;
    return 0;
}
} // namespace xlle
