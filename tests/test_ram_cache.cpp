// Exercise real FATX bytes and the public file ABI without a disk or game.
#include "../src/storage/storage_api.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"RAM cache check %u: %s\n",__LINE__,#x);std::exit(1);}} while(0)
int main() {
    constexpr uint32_t access=0x1f01ff,not_found=0xc0000034u,denied=0xc0000022u;
    char error[256]={};
    CHECK(!xllr_storage_open_ram_cache(1,262144,error,sizeof(error)));
    CHECK(!xllr_storage_open_ram_cache(3,751ull*1024*1024,error,sizeof(error)));
    void* s=xllr_storage_open_ram_cache(3,262144,error,sizeof(error));CHECK(s);
    uint64_t root=0,file=0,other=0;uint32_t action=0,done=0,unit=0;
    CHECK(xllr_storage_file_open(s,1,0,"save",access,7,0x60,3,0,0,&file,&action)==denied);
    CHECK(!xllr_storage_file_open(s,3,0,"",access,7,0x21,1,0,0,&root,&action));
    CHECK(!xllr_storage_file_open(s,3,root,"ring.dat",access,0,0x60,2,0,0,&file,&action) && action==2);
    CHECK(xllr_storage_file_open(s,3,root,"RING.DAT",access,7,0x60,1,0,0,&other,&action)==0xc0000043u);
    std::array<unsigned char,19001> payload{};for(size_t i=0;i<payload.size();++i)payload[i]=(unsigned char)(i*13+i/19);
    CHECK(!xllr_storage_file_write(s,file,29,payload.data(),(uint32_t)payload.size(),&done) && done==payload.size());
    std::array<unsigned char,19030> loaded{};
    CHECK(!xllr_storage_file_read(s,file,0,loaded.data(),(uint32_t)loaded.size(),&done) && done==loaded.size());
    CHECK(!std::memcmp(loaded.data()+29,payload.data(),payload.size()));
    for(unsigned i=0;i<29;++i)CHECK(!loaded[i]);
    CHECK(!xllr_storage_file_resize(s,file,100,0));
    CHECK(!xllr_storage_file_resize(s,file,19030,0));
    CHECK(!xllr_storage_file_read(s,file,100,loaded.data(),18930,&done));
    for(unsigned i=0;i<done;++i)CHECK(!loaded[i]);
    CHECK(!xllr_storage_file_position(s,file,17));
    CHECK(!xllr_storage_file_write(s,file,-2,payload.data(),7,&done));
    XllrStorageInfo info{};CHECK(!xllr_storage_file_info(s,file,&info) && info.position==24);
    CHECK(!xllr_storage_file_write(s,file,-1,payload.data(),7,&done));
    CHECK(!xllr_storage_file_info(s,file,&info) && info.length==19037);
    uint64_t total=0,available=0;
    CHECK(!xllr_storage_capacity(s,3,&total,&available,&unit) && unit==16384 && available<total);
    CHECK(xllr_storage_file_resize(s,file,262144,0)==0xc000007fu);
    CHECK(!xllr_storage_file_info(s,file,&info) && info.length==19037);
    CHECK(xllr_storage_clear_cache(s,3)==0xc0000043u);
    CHECK(!xllr_storage_file_rename(s,file,root,"renamed.dat",0));
    XllrStorageDirectoryEntry entry{};
    CHECK(!xllr_storage_directory(s,root,"*.dat",1,&entry) && !std::strcmp(entry.name,"renamed.dat"));
    CHECK(!xllr_storage_file_delete(s,file,1));CHECK(!xllr_storage_file_close(s,file));
    CHECK(xllr_storage_file_open(s,3,root,"renamed.dat",access,7,0x60,1,0,0,&file,&action)==not_found);
    CHECK(!xllr_storage_file_open(s,3,root,"again.dat",access,7,0x60,2,0,0,&file,&action));
    CHECK(!xllr_storage_file_write(s,file,0,payload.data(),(uint32_t)payload.size(),&done));
    CHECK(!xllr_storage_file_close(s,file));CHECK(!xllr_storage_clear_cache(s,3));
    CHECK(xllr_storage_clear_cache(s,1)==denied);
    uint64_t resident=0,read=0,written=0;
    CHECK(!xllr_storage_ram_stats(s,&resident,&read,&written) && resident<=262144 && read && written);
    CHECK(!xllr_storage_file_close(s,root));CHECK(!xllr_storage_destroy(s));
    s=xllr_storage_open_ram_cache(5,750ull*1024*1024,error,sizeof(error));CHECK(s);
    CHECK(!xllr_storage_ram_stats(s,&resident,&read,&written) && resident<1024*1024);
    CHECK(xllr_storage_file_open(s,5,0,"ring.dat",access,7,0x60,1,0,0,&file,&action)==not_found);
    CHECK(!xllr_storage_destroy(s));std::puts("RAM utility cache: bytes, EOF, sharing, deletion, bounds, reset and save isolation pass");
}
