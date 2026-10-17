// Exercise the real C ABI through a loaded module. No private device mutation.
// The same executable can inspect a preserved older DLL as a failing control.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static int failures, escaped, mode, reads, writes;
static uint32_t backing;
#define CHECK(c) do { if(!(c)) { std::printf("FAIL line %d: %s\n",__LINE__,#c);++failures; } } while(0)
static void inject() {
    if(mode==1)throw std::bad_alloc();
    if(mode==2)throw std::runtime_error("authored bus callback failure");
    if(mode==3)throw 17;
}
static int read_bus(uint32_t,void* p,size_t n) {
    ++reads;inject();if(n!=4)return 0;std::memcpy(p,&backing,4);return 1;
}
static int write_bus(uint32_t,const void* p,size_t n) {
    ++writes;inject();if(n!=4)return 0;std::memcpy(&backing,p,4);return 1;
}
static void irq(int,int) {}

int main(int argc,char** argv) {
    if(argc!=2)return 2;
#ifdef _WIN32
    auto module=LoadLibraryA(argv[1]);
    auto symbol=[&](const char* name) {return reinterpret_cast<void*>(GetProcAddress(module,name));};
#else
    auto module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    auto symbol=[&](const char* name) {return dlsym(module,name);};
#endif
    if(!module){std::fprintf(stderr,"Could not load reference module\n");return 2;}
    auto create=reinterpret_cast<void*(*)()>(symbol("xlle_nv2a_reference_create"));
    auto destroy=reinterpret_cast<void(*)(void*)>(symbol("xlle_nv2a_reference_destroy"));
    auto bus=reinterpret_cast<void(*)(void*,int(*)(uint32_t,void*,size_t),
        int(*)(uint32_t,const void*,size_t),void(*)(int,int))>(symbol("xlle_nv2a_reference_bus"));
    auto aperture=reinterpret_cast<int(*)(void*,uint32_t,unsigned,int,uint64_t*)>(
        symbol("xlle_nv2a_reference_aperture"));
    auto error=reinterpret_cast<const char*(*)(void*)>(symbol("xlle_nv2a_reference_error"));
    auto mmio_write=reinterpret_cast<void(*)(void*,uint32_t,int,uint32_t)>(
        symbol("xlle_nv2a_reference_write"));
    if(!create || !destroy || !bus || !aperture || !error || !mmio_write)return 2;
    {
        void* ref=create();if(!ref)return 2;
        mode=reads=writes=0;bus(ref,read_bus,write_bus,irq);
        uint64_t value=0x12345678;
        CHECK(aperture(ref,0x1000,4,1,&value)==1 && writes==1 && backing==value);
        value=0;CHECK(aperture(ref,0x1000,4,0,&value)==1 && reads==1 && value==backing);
        CHECK(!error(ref));destroy(ref);
    }
    for(int fault=1;fault<=3;++fault)for(int write=0;write<2;++write) {
        void* ref=create();if(!ref)return 2;
        mode=0;bus(ref,read_bus,write_bus,irq);reads=writes=0;
        backing=0xa5a5a5a5;uint64_t value=0x8877665544332211ull;
        mode=fault;bool left_abi=false;int result=-1;
        try { result=aperture(ref,0x1000,4,write,&value); }
        catch(const std::exception& e) {left_abi=true;std::printf("ESCAPED: %s\n",e.what());}
        catch(...) {left_abi=true;std::puts("ESCAPED: non-standard C++ exception");}
        if(left_abi){++escaped;++failures;destroy(ref);continue;}
        CHECK(result==0 && value==0x8877665544332211ull && backing==0xa5a5a5a5);
        CHECK(reads+writes==1);
        const char* message=error(ref);CHECK(message);
        const std::string first=message?message:"";
        CHECK(first.find("xlle_nv2a_reference_aperture")!=std::string::npos);
        CHECK(first.find(fault==1?std::bad_alloc().what():fault==2?"authored bus callback failure":
                         "non-standard C++")!=std::string::npos);
        // Failed processing stays failed even when the callback stops throwing.
        // Neither a read nor a write may restart work or overwrite the cause.
        mode=0;value=0x99aabbcc;
        CHECK(aperture(ref,0x1000,4,0,&value)==0 && value==0x99aabbcc);
        CHECK(aperture(ref,0x1000,4,1,&value)==0 && reads+writes==1);
        CHECK(error(ref) && first==error(ref));destroy(ref);
    }
    {
        // The real MMIO write export is void. A failing PFIFO fetch still
        // has to report failure and prevent the next kick, without unwinding.
        void* ref=create();if(!ref)return 2;
        mode=reads=writes=0;bus(ref,read_bus,write_bus,irq);
        const uint32_t setup[][2]={{0x700180,0x3d},{0x700184,0xffff},{0x700188,0},
            {0x002504,1},{0x003204,0x100},{0x00322c,0x18},{0x003240,0},{0x003244,0},
            {0x003220,1},{0x003250,1},{0x003200,1},{0x400720,1},{0x400880,0x28c3ff}};
        for(const auto& word:setup)mmio_write(ref,0xfd000000u+word[0],4,word[1]);
        CHECK(!error(ref));reads=writes=0;mode=2;bool left_abi=false;
        try {mmio_write(ref,0xfd800040,4,8);}
        catch(...) {left_abi=true;std::puts("ESCAPED: MMIO fetch callback");}
        if(left_abi){++escaped;++failures;}
        else {
            CHECK(reads==1 && writes==0 && error(ref));
            const std::string first=error(ref)?error(ref):"";
            CHECK(first.find("xlle_nv2a_reference_write")!=std::string::npos);
            mode=0;mmio_write(ref,0xfd800040,4,16);
            CHECK(reads==1 && writes==0 && error(ref) && first==error(ref));
        }
        destroy(ref);
    }
#ifdef _WIN32
    FreeLibrary(module);
#else
    dlclose(module);
#endif
    std::printf("reference exception controls: failures=%d escaped=%d\n",failures,escaped);
    return failures?1:0;
}
