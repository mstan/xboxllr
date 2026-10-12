// Guarded reference transport: one explicit caller and actual maintained APU RAM.
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <memory>
#include <string>
#include <vector>
#include "audio_reference.h"
#include "machine/apu.hpp"
#include "audio_dsp.h"
#ifdef PILOT_AUDIO_REFERENCE_TEST
extern "C" uint32_t reference_dsp_state_bytes(void);
extern "C" void reference_dsp_state(const PilotDsp*,void*,uint32_t);
#endif
namespace {
constexpr int32_t OK=0,INVALID=(int32_t)0x80070057,UNSUPPORTED=(int32_t)0x80004001,FAIL=(int32_t)0x80004005,OOM=(int32_t)0x8007000e;
constexpr uint32_t table=0x1000,sge=0x10000,data_base=0x20000,max_pages=4096;
struct Buffer {uint64_t id=0,fetched=0;unsigned voice=0,bytes=0,channels=0,bits=0,align=0,frequency=0,rate=0,start=0,pages=0;uint32_t saved_byte=0,loop_start=0,loop_bytes=0,mix_count=2,mix_bins[8]={0,1};int32_t volume=0,mix_volumes[8]{};bool live=true,playing=false,loop=false,drained=false,binding_clean=true;};
#include "audio_reference_capture.hpp"
uint64_t next_engine_id=0;
struct Engine {
 std::string fault; xlle::McpxApu apu{[this](const std::string&s){fault=s;}};
 std::vector<uint8_t> ram=std::vector<uint8_t>(data_base+max_pages*4096);
 std::array<Buffer*,max_pages> page_owner{};std::array<bool,max_pages> used{};std::array<Buffer*,256> voices{};
 std::vector<std::unique_ptr<Buffer>> handles;
 uint64_t frames=0;unsigned carry=0;uint32_t bins[1024]{},speakers[192]{};
 PilotDsp *graph=nullptr;float*tap=nullptr;unsigned tap_capacity=0,tap_frames=0;bool post=false,busy=false,source_work=false;
 ReferenceAudioReadTap read_tap=nullptr;void*read_context=nullptr;
 ReferenceCapture capture;
 Engine(){
  if(!capture.open(++next_engine_id))fault="reference capture create failed";
  apu.set_mem_access([this](uint32_t a,void*p,size_t n){
   if(uint64_t(a)+n>ram.size())return false;
   if(a>=data_base){unsigned page=(a-data_base)/4096;Buffer*b=page<max_pages?page_owner[page]:nullptr;if(!b||!b->live||n>4096-(a&4095))return false;
    unsigned logical=page^1u;uint32_t off=(logical-b->start)*4096+(a&4095);if(off>b->bytes||n>b->bytes-off)return false;
    memcpy(p,ram.data()+a,n);
    if(!capture.read(b,off,p,(uint32_t)n,frames,word(object(b)+0x58)&0xffffff)){fault="reference capture read failed";return false;}
    if(read_tap){ReferenceAudioRead r{b->id,frames,off,a,(uint32_t)n};read_tap(read_context,&r,p);}return true;
   }memcpy(p,ram.data()+a,n);return true;
  },[this](uint32_t a,const void*p,size_t n){
   if(uint64_t(a)+n>ram.size())return false;memcpy(ram.data()+a,p,n);
   if(source_work&&n==4&&a>=table&&a<table+256*128&&(a-table)%128==0x58){auto*b=voices[(a-table)/128];if(b&&b->live)++b->fetched;}return true;
  });reg(0x202c,table);reg(0x2030,sge);
 }
 void put(uint32_t a,uint32_t v){memcpy(ram.data()+a,&v,4);}uint32_t word(uint32_t a)const{uint32_t v;memcpy(&v,ram.data()+a,4);return v;}
 void reg(unsigned o,uint32_t v){apu.mmio_write(xlle::McpxApu::kBase+o,4,v);}void select(Buffer*b){reg(0x202f8,b->voice);}
 uint32_t object(Buffer*b)const{return table+b->voice*128;}
 bool event(Buffer*b,uint32_t kind,uint32_t offset=0,const void*data=nullptr,uint32_t n=0){
  if(capture.event(b,kind,offset,data,n,frames,word(object(b)+0x58)&0xffffff))return true;
  fault="reference capture event failed";return false;
 }
 void active(Buffer*b,bool enabled){put(object(b)+0x54,enabled?1u<<21:0);}
 void mix_config(Buffer*b){
  // Complete original1789A0 fills unused ordered slots with lowest unused bins.
  unsigned ordered[8]{},count=b->mix_count;for(unsigned i=0;i<count;++i)ordered[i]=b->mix_bins[i];
  for(unsigned bin=0;count<8&&bin<32;++bin)if(std::find(ordered,ordered+count,bin)==ordered+count)ordered[count++]=bin;
  constexpr unsigned shift[]={0,5,10,16,21,26};uint32_t route=0;for(unsigned i=0;i<6;++i)route|=ordered[i]<<shift[i];
  select(b);uint32_t format=word(object(b)+4);reg(0x20300,route);reg(0x20304,(format&~1023u)|ordered[6]|(ordered[7]<<5));
  unsigned attenuation[8];for(unsigned i=0;i<8;++i)attenuation[i]=i<b->mix_count?unsigned(std::min<int64_t>(4095,-(int64_t(b->volume)+b->mix_volumes[i])*64/100)):4095;
  // Complete original178A7B: six12-bit fields and two interleaved nibble fields.
  for(unsigned part=0;part<3;++part){uint32_t packed=attenuation[part*2]<<4|attenuation[part*2+1]<<20;
   packed|=(attenuation[6]>>(part*4)&15)|(attenuation[7]>>(part*4)&15)<<16;reg(0x20360+part*4,packed);}
 }
 void region_config(Buffer*b){select(b);uint32_t first=b->loop?b->loop_start/b->align:0,last=b->loop?(b->loop_start+b->loop_bytes)/b->align-1:b->bytes/b->align-1;
  // These actual methods reset maintained prefetch even for an unchanged word.
  // A plain resume must retain its current history rather than replay/drop it.
  if((word(object(b)+0x24)&0xffffff)!=first)reg(0x203a4,first);
  if((word(object(b)+0x5c)&0xffffff)!=last)reg(0x203dc,last);
 }
 void reset_carry(){carry=0;memset(bins,0,sizeof(bins));memset(speakers,0,sizeof(speakers));}
 Buffer* find(void*p){for(auto&b:handles)if(b.get()==p&&b->live)return b.get();return nullptr;}
 uint32_t physical(Buffer*b,uint32_t offset)const{return word(sge+(b->start+offset/4096)*8)+(offset&4095);}
 int32_t copy(Buffer*b,uint32_t offset,void*p,uint32_t n,bool read){auto*q=(uint8_t*)p;while(n){uint32_t step=std::min({n,4096-(offset&4095),b->bytes-offset});uint32_t a=physical(b,offset);if(read)memcpy(q,ram.data()+a,step);else memcpy(ram.data()+a,q,step);n-=step;q+=step;offset=(offset+step)%b->bytes;}return OK;}
 bool emit(unsigned n){float mix[1024]{};
  for(auto*b:voices)if(b&&b->live&&b->playing){float source[64]{};uint32_t count=0;bool ended=false;
   source_work=true;bool ok=b->frequency==48000?apu.source_frames((uint16_t)b->voice,source,n,count,ended):apu.resample_source((uint16_t)b->voice,source,n,count,ended);
   source_work=false;b->binding_clean=false;if(!ok||!apu.mix_2d_samples((uint16_t)b->voice,source,count,mix))return false;
   if(!capture.flush_read()){fault="reference capture source flush failed";return false;}
   if(ended){b->drained=true;b->playing=false;active(b,false);if(!event(b,8))return false;}
  }
  for(unsigned f=0;f<n;++f){float output[6];for(unsigned c=0;c<6;++c){int32_t v=(int32_t)(speakers[c*32+carry]<<8)>>8;output[c]=v*(1.f/8388608.f);}
   if(!capture.emit(output)){fault="reference capture output failed";return false;}
   if(tap&&tap_frames<tap_capacity){if(post)memcpy(tap+tap_frames*6,output,sizeof(output));else for(unsigned c=0;c<32;++c)tap[tap_frames*32+c]=mix[c*32+f];++tap_frames;}
   for(unsigned c=0;c<32;++c){double v=mix[c*32+f]*8388608.;bins[c*32+carry]=(uint32_t)(int32_t)std::max(-8388608.,std::min(8388607.,v))&0xffffff;}
   if(++carry==32){if(graph)pilot_dsp_process(graph,bins,speakers);else memcpy(speakers,bins,sizeof(speakers));carry=0;}++frames;
  }return true;
 }
};
std::unique_ptr<Engine> engine;uint64_t next_id=0;
int32_t ready(){return engine&&!engine->busy&&engine->fault.empty()?OK:FAIL;}
Buffer* buffer(void*p){return engine?engine->find(p):nullptr;}
bool pitch(unsigned f,int&out){if(f<100||f>200000)return false;out=(int)std::lround(4096*std::log2(double(f)/48000));return out>=-8192&&out<=8191;}
uint64_t hash(const void*p,size_t n,uint64_t h=1469598103934665603ull){auto*b=(const uint8_t*)p;while(n--){h^=*b++;h*=1099511628211ull;}return h;}
}
extern "C" {
int32_t pilot_audio_open(void){if(engine)return INVALID;engine=std::make_unique<Engine>();return engine->fault.empty()?OK:FAIL;}
void pilot_audio_close(void){if(!engine||engine->busy)return;if(!engine->capture.finish()){fprintf(stderr,"[REFERENCE CAPTURE FAILURE] finalization\n");ExitProcess(2);}engine->graph=nullptr;engine->reset_carry();engine.reset();}
int32_t pilot_audio_buffer_create(uint32_t bytes,unsigned channels,unsigned rate,unsigned bits,void**out){
 if(ready())return FAIL;int p;if(!out||!bytes||bytes>1024*1024||(channels!=1&&channels!=2)||(bits!=8&&bits!=16)||bytes%(channels*bits/8))return INVALID;if(!pitch(rate,p))return UNSUPPORTED;
 unsigned voice=64;while(voice<256&&engine->voices[voice])++voice;if(voice==256)return OOM;unsigned pages=(bytes+4095)/4096,start=0;
 for(;start+pages<=max_pages;++start){unsigned i=0;while(i<pages&&!engine->used[start+i])++i;if(i==pages)break;start+=i;}if(start+pages>max_pages)return OOM;
 auto owned=std::make_unique<Buffer>();auto*b=owned.get();b->id=++next_id;b->voice=voice;b->bytes=b->loop_bytes=bytes;b->channels=channels;b->bits=bits;b->align=channels*bits/8;b->rate=b->frequency=rate;b->start=start;b->pages=pages;
 engine->voices[voice]=b;for(unsigned i=0;i<pages;++i){engine->used[start+i]=true;unsigned physical=(start+i)^1u;engine->page_owner[physical]=b;engine->put(sge+(start+i)*8,data_base+physical*4096);}
 engine->select(b);uint32_t format=(bits==16?0x50000000u:0)|((channels-1)<<27)|((channels-1)<<16);
 engine->reg(0x20300,1u<<5);engine->reg(0x20304,format);engine->reg(0x20360,0x000f000f);engine->reg(0x20364,0xffffffff);engine->reg(0x20368,0xffffffff);
 engine->reg(0x203a0,start*4096);engine->reg(0x203a4,0);engine->reg(0x203dc,bytes/b->align-1);engine->reg(0x203d8,0);engine->reg(0x2037c,uint32_t(uint16_t(p))<<16);engine->active(b,false);
 std::vector<uint8_t> initial(bytes,bits==8?128:0);engine->copy(b,0,initial.data(),bytes,false);engine->handles.push_back(std::move(owned));if(!engine->event(b,3,0,initial.data(),bytes)||!engine->fault.empty())return FAIL;*out=b;return OK;
}
void pilot_audio_buffer_destroy(void*p){if(ready())return;auto*b=buffer(p);if(!b)return;engine->active(b,false);engine->select(b);engine->reg(0x203d8,0);b->playing=false;if(!engine->event(b,6))return;engine->voices[b->voice]=nullptr;for(unsigned i=0;i<b->pages;++i){engine->used[b->start+i]=false;engine->page_owner[(b->start+i)^1u]=nullptr;engine->put(sge+(b->start+i)*8,0);}b->live=false;}
int32_t pilot_audio_buffer_copy(void*p,uint32_t offset,void*data,uint32_t bytes,int readback){if(ready())return FAIL;auto*b=buffer(p);if(!b||!data||!bytes||offset>=b->bytes||bytes>b->bytes)return INVALID;int32_t result=engine->copy(b,offset,data,bytes,!!readback);if(result>=0&&!readback&&!engine->event(b,1,offset,data,bytes))return FAIL;return result;}
int32_t pilot_audio_buffer_play(void*p,int looping){if(ready())return FAIL;auto*b=buffer(p);if(!b)return INVALID;if(b->drained)return UNSUPPORTED;if(!b->binding_clean&&b->loop!=!!looping)return UNSUPPORTED;
 if(looping&&(engine->word(engine->object(b)+0x58)&0xffffff)>=(b->loop_start+b->loop_bytes)/b->align)return UNSUPPORTED;
 b->loop=!!looping;b->playing=true;engine->select(b);uint32_t f=engine->word(engine->object(b)+4);engine->reg(0x20304,(f&~(1u<<25))|(b->loop?1u<<25:0));engine->region_config(b);engine->active(b,true);return engine->event(b,4)&&engine->fault.empty()?OK:FAIL;}
int32_t pilot_audio_buffer_stop(void*p){if(ready())return FAIL;auto*b=buffer(p);if(!b)return INVALID;if(b->drained)return UNSUPPORTED;b->saved_byte=(engine->word(engine->object(b)+0x58)&0xffffff)*b->align;engine->active(b,false);b->playing=false;return engine->event(b,5)?OK:FAIL;}
int32_t pilot_audio_buffer_status(void*p,uint32_t*out){if(ready())return FAIL;auto*b=buffer(p);if(!b||!out)return INVALID;*out=b->playing?(b->loop?5:1):0;return OK;}
int32_t pilot_audio_buffer_position(void*p,uint32_t*play,uint32_t*write){if(ready())return FAIL;auto*b=buffer(p);if(!b)return INVALID;if(b->drained)return UNSUPPORTED;uint32_t read=b->playing?(engine->word(engine->object(b)+0x58)&0xffffff)*b->align:b->saved_byte;uint32_t next=b->saved_byte;if(b->playing){uint32_t safe=read+32*b->align;next=b->loop&&read>=b->loop_start&&read<b->loop_start+b->loop_bytes?b->loop_start+safe%b->loop_bytes:safe%b->bytes;}if(play)*play=read;if(write)*write=next;return OK;}
int32_t pilot_audio_buffer_seek(void*p,uint32_t at){if(ready())return FAIL;auto*b=buffer(p);if(!b||at>=b->bytes||at%b->align)return INVALID;if(b->loop&&at>=b->loop_start+b->loop_bytes)return UNSUPPORTED;engine->select(b);engine->reg(0x203d8,at/b->align);b->drained=false;b->binding_clean=true;if(!b->playing)b->saved_byte=at;return engine->event(b,7)&&engine->fault.empty()?OK:FAIL;}
int32_t pilot_audio_buffer_loop_region(void*p,uint32_t start,uint32_t bytes){if(ready())return FAIL;auto*b=buffer(p);if(!b||start>=b->bytes||start%b->align)return INVALID;if(!bytes)bytes=b->bytes-start;if(bytes>b->bytes-start||bytes%b->align)return INVALID;
 if(start==b->loop_start&&bytes==b->loop_bytes)return OK;
 // A live converter must not discard already fetched source history. The
 // qualified certain route is clean setup or explicit Seek, then real LBO/EBO.
 if(!b->binding_clean||(b->loop&&(engine->word(engine->object(b)+0x58)&0xffffff)>=(start+bytes)/b->align))return UNSUPPORTED;
 b->loop_start=start;b->loop_bytes=bytes;engine->region_config(b);return engine->fault.empty()?OK:FAIL;}
int32_t pilot_audio_buffer_frequency(void*p,uint32_t f){if(ready())return FAIL;auto*b=buffer(p);if(!b)return INVALID;if(!f)f=b->rate;int delta;if(!pitch(f,delta))return UNSUPPORTED;
 // Switching bypass/converter would abandon already fetched converter input.
 if((b->frequency==48000)!=(f==48000)&&!b->binding_clean)return UNSUPPORTED;
 // The maintained SRC's live ratio interpolation depends on output request
 // extent. Its transition shape is unqualified for exact caller partitions;
 // require explicit Seek for a changed pitch, preserve same-pitch controls.
 if(!b->binding_clean&&uint16_t(delta)!=uint16_t(engine->word(engine->object(b)+0x7c)>>16))return UNSUPPORTED;
 engine->select(b);engine->reg(0x2037c,uint32_t(uint16_t(delta))<<16);b->frequency=f;return OK;
}
int32_t pilot_audio_buffer_volume(void*p,int32_t volume){if(ready())return FAIL;auto*b=buffer(p);if(!b||volume>0||volume< -10000)return INVALID;b->volume=volume;engine->mix_config(b);return engine->fault.empty()?OK:FAIL;}
int32_t pilot_audio_buffer_mixbins(void*p,unsigned count,const uint32_t*bins,const int32_t*volumes){if(ready())return FAIL;auto*b=buffer(p);if(!b||count>8||(count&&(!bins||!volumes)))return INVALID;
 for(unsigned i=0;i<count;++i)if(bins[i]>=32||volumes[i]>0||volumes[i]< -10000)return INVALID;
 b->mix_count=count;for(unsigned i=0;i<count;++i){b->mix_bins[i]=bins[i];b->mix_volumes[i]=volumes[i];}engine->mix_config(b);return engine->fault.empty()?OK:FAIL;
}
int32_t pilot_audio_effects_install(PilotDsp*graph){if(ready())return FAIL;if(!graph)return INVALID;engine->graph=graph;engine->reset_carry();return OK;}
int32_t reference_effects_detach(PilotDsp*expected){if(ready())return FAIL;if(!expected||engine->graph!=expected)return INVALID;engine->graph=nullptr;engine->reset_carry();return OK;}
int32_t pilot_audio_capture_begin(float*samples,uint32_t frames,int after){if(ready())return FAIL;if(!samples||!frames||engine->tap)return INVALID;engine->tap=samples;engine->tap_capacity=frames;engine->tap_frames=0;engine->post=!!after;return OK;}
uint32_t pilot_audio_capture_end(void){if(ready())return 0;uint32_t n=engine->tap_frames;engine->tap=nullptr;engine->tap_capacity=0;return n;}
int32_t reference_advance_frames(uint32_t frames){if(ready())return FAIL;if(frames>65536)return INVALID;engine->busy=true;while(frames){unsigned n=std::min(frames,32u);if(!engine->emit(n)){engine->busy=false;return FAIL;}frames-=n;}engine->busy=false;return OK;}
uint64_t reference_output_frames(void){return engine?engine->frames:0;}
int32_t reference_source_state(void*p,ReferenceAudioSourceState*out){if(ready())return FAIL;auto*b=buffer(p);if(!b||!out)return INVALID;uint32_t cbo=engine->word(engine->object(b)+0x58)&0xffffff;*out={b->id,b->fetched,cbo,cbo*b->align,b->frequency,b->align,b->playing,b->loop,b->drained};return OK;}
#ifdef PILOT_AUDIO_REFERENCE_TEST
int32_t reference_voice_configuration(void*p,uint32_t words[8]){if(ready())return FAIL;auto*b=buffer(p);if(!b||!words)return INVALID;constexpr uint32_t offsets[]={0,4,0x24,0x5c,0x7c,0x60,0x64,0x68};for(unsigned i=0;i<8;++i)words[i]=engine->word(engine->object(b)+offsets[i]);return OK;}
#endif
int32_t reference_set_read_tap(ReferenceAudioReadTap tap,void*context){if(ready())return FAIL;engine->read_tap=tap;engine->read_context=context;return OK;}
int32_t reference_capture_epoch(uint64_t ticks){if(ready()||engine->frames)return FAIL;if(!engine->capture.set_epoch(ticks)){engine->fault="reference capture epoch invalid";return FAIL;}return OK;}
int32_t reference_capture_finish(void){return !engine||engine->capture.finish()?OK:FAIL;}
#ifdef PILOT_AUDIO_REFERENCE_TEST
uint64_t reference_state_hash(void){if(!engine)return 0;uint64_t h=hash(engine->ram.data(),engine->ram.size());h=hash(&engine->frames,sizeof(engine->frames),h);h=hash(&engine->carry,sizeof(engine->carry),h);h=hash(engine->bins,sizeof(engine->bins),h);h=hash(engine->speakers,sizeof(engine->speakers),h);for(auto*b:engine->voices)if(b){h=hash(&b->fetched,sizeof(b->fetched),h);}
 if(engine->graph){std::vector<uint8_t>s(reference_dsp_state_bytes());reference_dsp_state(engine->graph,s.data(),(uint32_t)s.size());h=hash(s.data(),s.size(),h);}return h;
}
#endif
}
