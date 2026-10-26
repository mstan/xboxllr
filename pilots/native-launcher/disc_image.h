#ifndef XBOXLLR_DISC_IMAGE_H
#define XBOXLLR_DISC_IMAGE_H
/* Bounded, read-only default.xbe view. Format: XboxDev/extract-xiso's public
 * XDVDFS descriptor/directory layout. No extraction or executable patching. */
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace xboxllr_disc {
inline uint32_t u32(const unsigned char *p) {
    return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
inline uint32_t u16(const unsigned char *p) { return p[0] | uint32_t(p[1])<<8; }
inline std::vector<unsigned char> image(const char *path,uint64_t *source_size=nullptr) {
    const auto name=std::filesystem::u8path(path?path:"");
    std::ifstream file(name,std::ios::binary|std::ios::ate);
    if(!file || file.tellg()<0)throw std::runtime_error("Cannot read selected game file");
    const uint64_t length=uint64_t(file.tellg());if(source_size)*source_size=length;
    auto read=[&](uint64_t at,uint64_t size) {
        if(at>length || size>length-at || size>256u*1024*1024)throw std::runtime_error("Xbox image extent is truncated or invalid");
        std::vector<unsigned char> data(static_cast<size_t>(size));file.clear();file.seekg(std::streamoff(at));
        if(size && !file.read(reinterpret_cast<char*>(data.data()),std::streamsize(size)))throw std::runtime_error("Xbox image is truncated");
        return data;
    };
    auto extension=name.extension().u8string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(extension!=".iso" && extension!=".xiso")return read(0,length);
    const uint64_t partitions[]={0,0x18300000,0x0fd90000,0x02080000};
    uint64_t base=0,root=0;uint32_t root_size=0;bool found=false;
    for(auto candidate:partitions) {
        if(candidate+33*2048>length)continue;
        const auto descriptor=read(candidate+32*2048,2048);
        if(!std::memcmp(descriptor.data(),"MICROSOFT*XBOX*MEDIA",20) &&
           !std::memcmp(descriptor.data()+2048-20,"MICROSOFT*XBOX*MEDIA",20)) {
            base=candidate;root=base+uint64_t(u32(descriptor.data()+20))*2048;
            root_size=u32(descriptor.data()+24);found=true;break;
        }
    }
    if(!found)throw std::runtime_error("Not an Xbox ISO/XISO image");
    if(root_size<14 || root_size>64u*1024*1024)throw std::runtime_error("Xbox disc directory is invalid");
    const auto table=read(root,root_size);
    std::vector<uint32_t> pending={0};std::set<uint32_t> seen;
    std::vector<std::pair<uint32_t,uint32_t>> intervals;
    std::set<std::string> names;uint64_t executable=0;uint32_t executable_size=0;unsigned matches=0;
    while(!pending.empty()) {
        const auto at=pending.back();pending.pop_back();
        if(at>root_size-14 || seen.size()>=100000 || !seen.insert(at).second)throw std::runtime_error("Xbox disc directory has an invalid node or cycle");
        const auto p=table.data()+at;const unsigned n=p[13];
        if(!n || n>root_size-at-14)throw std::runtime_error("Xbox disc filename is truncated");
        std::string entry(reinterpret_cast<const char*>(p+14),n);
        std::transform(entry.begin(),entry.end(),entry.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(!names.insert(entry).second)throw std::runtime_error("Xbox disc contains colliding filenames");
        intervals.emplace_back(at,at+14+n);
        const auto start=base+uint64_t(u32(p+4))*2048;const auto size=u32(p+8);
        if(start>length || size>length-start)throw std::runtime_error("Xbox disc file is truncated");
        if(entry=="default.xbe" && !(p[12]&0x10)) {executable=start;executable_size=size;++matches;}
        for(unsigned child:{u16(p),u16(p+2)})if(child)pending.push_back(child*4);
    }
    std::sort(intervals.begin(),intervals.end());
    for(size_t i=1;i<intervals.size();++i)if(intervals[i].first<intervals[i-1].second)throw std::runtime_error("Xbox disc directory records overlap");
    if(matches!=1 || executable_size<0x178)throw std::runtime_error("Xbox disc has no valid root default.xbe");
    return read(executable,executable_size);
}
}
#endif
