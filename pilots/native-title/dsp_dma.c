#include "dsp_dma.h"
#include <stddef.h>
#include <string.h>
typedef struct Span {uint32_t *words;uint32_t count;} Span;
static int overlap(const void *a,size_t an,const void *b,size_t bn)
{
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 if(an>UINTPTR_MAX-x || bn>UINTPTR_MAX-y)return 1;
 return x<y+bn && y<x+an;
}
static void mark(const PilotDspDmaView *v,const void *pointer,uint32_t count)
{
 uintptr_t p=(uintptr_t)pointer,b=(uintptr_t)v->owned;
 if(!v->owned || !count || p<b || p-b>v->owned_bytes || count>v->owned_bytes-(p-b))return;
 uint32_t at=(uint32_t)(p-b),first=8-(at&7);if(first>count)first=count;
 v->dirty[at/8]|=(uint8_t)(((1u<<first)-1u)<<(at&7));at+=first;count-=first;
 uint32_t whole=count/8;if(whole){memset(v->dirty+at/8,255,whole);at+=whole*8;count-=whole*8;}
 if(count)v->dirty[at/8]|=(uint8_t)((1u<<count)-1u);
}
static int owner_extent(const PilotDspDmaView *v,const void *pointer,size_t bytes)
{
 if(!v->owned || !overlap(v->owned,v->owned_bytes,pointer,bytes))return 1;
 uintptr_t p=(uintptr_t)pointer,b=(uintptr_t)v->owned;
 return p>=b && p-b<=v->owned_bytes && bytes<=v->owned_bytes-(p-b);
}
static uint32_t load_ring(const uint8_t *ring,uint32_t size,uint32_t *position)
{
 uint32_t at=*position,value=0;
 if(size-at>=4){value=(uint32_t)ring[at]|((uint32_t)ring[at+1]<<8)|((uint32_t)ring[at+2]<<16)|((uint32_t)ring[at+3]<<24);at+=4;if(at==size)at=0;}
 else for(unsigned b=0;b<4;++b){value|=(uint32_t)ring[at]<<(8*b);if(++at==size)at=0;}
 *position=at;return value;
}
static void store_ring(uint8_t *ring,uint32_t size,uint32_t *position,uint32_t value)
{
 uint32_t at=*position;
 if(size-at>=4){ring[at]=(uint8_t)value;ring[at+1]=(uint8_t)(value>>8);ring[at+2]=(uint8_t)(value>>16);ring[at+3]=(uint8_t)(value>>24);at+=4;if(at==size)at=0;}
 else for(unsigned b=0;b<4;++b){ring[at]=(uint8_t)(value>>(8*b));if(++at==size)at=0;}
 *position=at;
}
int pilot_dsp_dma_payload(const PilotDspDmaView *v,unsigned to_scratch,
 uint32_t address,uint32_t count,uint32_t base,uint32_t size,uint32_t offset,uint32_t *next_offset)
{
 if(!v || !v->x || !v->y || !v->mix || !v->scratch || !next_offset || to_scratch>1 || !count || count>4096 || !size || offset>=size || base>v->scratch_bytes || size>v->scratch_bytes-base)return 0;
 if(v->owned && (!v->dirty || !v->owned_bytes || v->owned_bytes>UINTPTR_MAX-(uintptr_t)v->owned || (uint64_t)v->dirty_bytes<((uint64_t)v->owned_bytes+7)/8))return 0;
 Span spans[2]={{0}};unsigned span_count=1;
 if(address<0x1000){if(count>0x1000-address)return 0;spans[0]=(Span){v->x+address,count};}
 else if(address>=0x1400 && address<0x2000){
  if(count>0x2000-address)return 0;
  if(address<0x1800){uint32_t first=0x1800-address;if(first>count)first=count;spans[0]=(Span){v->mix+address-0x1400,first};if(count>first){spans[1]=(Span){v->y,count-first};span_count=2;}}
  else spans[0]=(Span){v->y+address-0x1800,count};
 }else return 0;
 uint8_t *ring=v->scratch+base;
 if(!owner_extent(v,ring,size))return 0;
 for(unsigned s=0;s<span_count;++s){
  if(!owner_extent(v,spans[s].words,(size_t)spans[s].count*4))return 0;
  if(overlap(spans[s].words,(size_t)spans[s].count*4,ring,size))return 0;
  if(v->owned && (overlap(v->dirty,v->dirty_bytes,spans[s].words,(size_t)spans[s].count*4) || overlap(v->dirty,v->dirty_bytes,ring,size)))return 0;
 }
 uint32_t position=offset;
 if(to_scratch){
  for(unsigned s=0;s<span_count;++s)for(uint32_t i=0;i<spans[s].count;++i)store_ring(ring,size,&position,(spans[s].words[i]&0xffffffu)<<8);
  uint32_t bytes=count*4;
  if(bytes>=size)mark(v,ring,size);
  else {uint32_t first=size-offset;if(first>bytes)first=bytes;mark(v,ring+offset,first);if(bytes>first)mark(v,ring,bytes-first);}
 }else{
  for(unsigned s=0;s<span_count;++s){for(uint32_t i=0;i<spans[s].count;++i)spans[s].words[i]=load_ring(ring,size,&position)>>8;mark(v,spans[s].words,spans[s].count*4);}
 }
 *next_offset=position;return 1;
}
