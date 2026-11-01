/* The host block-cache extent must not change the simulated machine. */
#include "dsp56300.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error Keep DSP cache contract assertions enabled
#endif
static uint32_t guarded_read(void *opaque,uint32_t address)
{assert(address>=128 && address<256);++((unsigned*)opaque)[0];return 0x123456;}
static void guarded_write(void *opaque,uint32_t address,uint32_t value)
{assert(address>=128 && address<256 && value==0x654321);++((unsigned*)opaque)[1];}
static void equal_state(Dsp56300Jit *a,Dsp56300Jit *b)
{
 Dsp56300State x,y;dsp56300_get_state(a,&x);dsp56300_get_state(b,&y);
#define SAME(field) assert(x.field==y.field)
 SAME(pc);SAME(pc_advance);SAME(pc_on_rep);SAME(cycle_count);SAME(cycle_budget);
 SAME(interrupts.pipeline_stage);SAME(interrupts.vector_addr);SAME(interrupts.saved_pc);
 SAME(interrupts.state);SAME(interrupts.ipl_to_raise);
 assert(!memcmp(x.interrupts.pending_bits,y.interrupts.pending_bits,sizeof(x.interrupts.pending_bits)));
 assert(!memcmp(x.interrupts.ipl,y.interrupts.ipl,sizeof(x.interrupts.ipl)));
 SAME(loop_rep);SAME(halt_requested);SAME(power_state);
 assert(!memcmp(x.registers,y.registers,sizeof(x.registers)));
 assert(!memcmp(x.stack,y.stack,sizeof(x.stack)));
#undef SAME
}
static void enter(Dsp56300Jit *d,uint32_t pc)
{
 Dsp56300State s;dsp56300_get_state(d,&s);s.pc=pc;s.power_state=0;s.halt_requested=false;
 dsp56300_set_state(d,&s);dsp56300_run(d,1000);
 dsp56300_get_state(d,&s);assert(s.power_state==2 && s.cycle_budget==0);
}
int main(void)
{
 uint32_t p[2][128]={{0}};unsigned callbacks[2][2]={{0}};
 Dsp56300MemoryRegion regions[2][2];Dsp56300CreateInfo info[2];Dsp56300Jit *core[2];
 for(unsigned i=0;i<2;++i) {
  p[i][17]=0x0c0040; /* Jump across the cache boundary to valid code. */
  p[i][66]=p[i][70]=0x87; /* Actual STOP, not a manufactured completion. */
  regions[i][0]=(Dsp56300MemoryRegion){0,128,DSP56300_REGION_BUFFER,{.buffer={p[i],0}}};
  regions[i][1]=(Dsp56300MemoryRegion){128,256,DSP56300_REGION_CALLBACK,
      {.callback={callbacks[i],guarded_read,guarded_write}}};
  info[i]=(Dsp56300CreateInfo){.memory_map={NULL,0,NULL,0,regions[i],2}};
 }
 assert(!dsp56300_create_cached(&info[0],0));
 assert(!dsp56300_create_cached(&info[0],257));
 assert(!dsp56300_create_cached(&info[0],0x1000001));
 core[0]=dsp56300_create(&info[0]);core[1]=dsp56300_create_cached(&info[1],16);
 assert(core[0] && core[1]);equal_state(core[0],core[1]);
 for(unsigned round=0;round<4;++round) {
  for(unsigned i=0;i<2;++i) {
   if(round==1)dsp56300_write_memory(core[i],DSP56300_MEM_SPACE_P,65,0x0c0046);
   if(round==2)dsp56300_write_memory(core[i],DSP56300_MEM_SPACE_P,15,0x0c0040);
   if(round==3)dsp56300_write_memory(core[i],DSP56300_MEM_SPACE_P,65,0);
   enter(core[i],round==3?64:14);
  }
  equal_state(core[0],core[1]);assert(!memcmp(p[0],p[1],sizeof(p[0])));
 }
 for(unsigned i=0;i<2;++i) {
  assert(callbacks[i][0]==0 && callbacks[i][1]==0);
  assert(dsp56300_read_memory(core[i],DSP56300_MEM_SPACE_P,128)==0x123456);
  assert(callbacks[i][0]==1 && callbacks[i][1]==0);
  dsp56300_write_memory(core[i],DSP56300_MEM_SPACE_P,128,0x654321);
  /* The retained P-write API reads the old word before marking it dirty. */
  assert(callbacks[i][0]==2 && callbacks[i][1]==1);
  dsp56300_destroy(core[i]);
 }
 assert(!memcmp(callbacks[0],callbacks[1],sizeof(callbacks[0])));
 puts("DSP cache extent: exact machine state, valid code outside cache, cross-boundary invalidation, guards and invalid extents passed");
 return 0;
}
