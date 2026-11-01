// Original Xbox FATX on-disk structures; no formatter or guest code.
// Format facts: xboxdevwiki FATX and The FATX filesystem specification.
// Differential qualification uses mborgerson/fatx as an external process only.
#include "fatx_volume.hpp"
#include <algorithm>
#include <cstring>
#include <set>
#include <unordered_set>
#include <stdexcept>

namespace xllr::storage {
namespace {
uint16_t le16(const uint8_t* p) { return uint16_t(p[0])|uint16_t(p[1])<<8; }
uint32_t le32(const uint8_t* p) { return uint32_t(le16(p))|uint32_t(le16(p+2))<<16; }
void put16(uint8_t* p,uint16_t v) {p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
void put32(uint8_t* p,uint32_t v) {put16(p,uint16_t(v));put16(p+2,uint16_t(v>>16));}
[[noreturn]] void fail(uint32_t code,const char* text) { throw FileError(code,text); }
constexpr uint32_t corrupt=0xc0000032u,invalid=0xc0000033u;
std::string fold(std::string s) { for(auto& c:s)if(c>='A' && c<='Z')c=char(c-'A'+'a');return s; }
std::vector<std::string> components(const std::string& path) {
    std::vector<std::string> result;
    for(size_t at=0;at<path.size();) {
        size_t end=path.find('\\',at);if(end==std::string::npos)end=path.size();
        std::string part=path.substr(at,end-at);
        if(part.empty() || part=="." || part==".." || part.size()>42 || part.back()=='.' || part.back()==' ')
            fail(invalid,"Invalid FATX path component");
        for(unsigned char c:part)if(c<32 || c>=128 || std::strchr("/\\:*?\"<>|",c))fail(invalid,"Unsupported FATX filename");
        result.push_back(part);at=end+1;
    }
    return result;
}
std::pair<std::string,std::string> parent_name(const std::string& path) {
    components(path);
    size_t split=path.find_last_of('\\');
    return split==std::string::npos?std::make_pair(std::string(),path):std::make_pair(path.substr(0,split),path.substr(split+1));
}
}
std::string FatxEntry::name() const { return std::string(reinterpret_cast<const char*>(raw.data()+2),raw[0]); }
uint32_t FatxEntry::cluster() const { return le32(raw.data()+44); }
uint32_t FatxEntry::size() const { return le32(raw.data()+48); }
FatxVolume::FatxVolume(DiskImage& disk,uint64_t offset,uint64_t bytes) : disk_(disk),base_(offset),bytes_(bytes) {
    if(offset>disk.size() || bytes>disk.size()-offset || bytes<8192)fail(corrupt,"FATX partition outside disk");
    uint8_t sb[4096];disk_.read(base_,sb,sizeof(sb));
    if(std::memcmp(sb,"FATX",4))fail(corrupt,"Missing original Xbox FATX partition");
    uint32_t sectors=le32(sb+8);root_cluster_=le32(sb+12);
    if(!sectors || sectors>128 || (sectors&(sectors-1)))fail(corrupt,"Invalid FATX cluster size");
    cluster_bytes_=sectors*512;
    uint64_t entries=bytes/cluster_bytes_+1;
    entry_bytes_=entries<0xfff0?2:4;
    uint64_t fat_bytes=(entries*entry_bytes_+4095)&~UINT64_C(4095);
    if(fat_bytes>(64u<<20) || fat_bytes+4096>=bytes)fail(corrupt,"Oversized FATX allocation table");
    data_=base_+4096+fat_bytes;
    count_=uint32_t((bytes-4096-fat_bytes)/cluster_bytes_);
    if(!root_cluster_ || root_cluster_>count_ || uint64_t(count_+1)*entry_bytes_>fat_bytes)
        fail(corrupt,"Invalid FATX root or allocation capacity");
    fat_.resize(size_t(fat_bytes));disk_.read(base_+4096,fat_.data(),fat_.size());
    admit();
}
uint32_t FatxVolume::fat(uint32_t index) const {
    if(index>count_)fail(corrupt,"FATX cluster index outside partition");
    const uint8_t* p=fat_.data()+size_t(index)*entry_bytes_;
    uint32_t value=entry_bytes_==2?le16(p):le32(p);
    if(entry_bytes_==2 && value>=0xfff0)value|=0xffff0000u;
    return value;
}
void FatxVolume::fat(uint32_t index,uint32_t value) {
    if(!index || index>count_)fail(corrupt,"Invalid FATX allocation update");
    uint8_t* p=fat_.data()+size_t(index)*entry_bytes_;
    if(entry_bytes_==2)put16(p,uint16_t(value));else put32(p,value);
    disk_.write(base_+4096+uint64_t(index)*entry_bytes_,p,entry_bytes_);
}
uint64_t FatxVolume::cluster_offset(uint32_t cluster) const {
    if(!cluster || cluster>count_)fail(corrupt,"Invalid FATX data cluster");
    return data_+uint64_t(cluster-1)*cluster_bytes_;
}
std::vector<uint32_t> FatxVolume::chain(uint32_t first) const {
    std::vector<uint32_t> result;std::unordered_set<uint32_t> seen;
    while(first) {
        if(first>count_ || !seen.insert(first).second)fail(corrupt,"Invalid/cyclic FATX chain");
        result.push_back(first);uint32_t next=fat(first);
        if(next>=0xfffffff8u)break;
        if(!next || next>=0xfffffff0u)fail(corrupt,"Incomplete FATX cluster chain");
        first=next;
    }
    return result;
}
FatxEntry FatxVolume::root() const {
    FatxEntry e;e.raw[1]=16;put32(e.raw.data()+44,root_cluster_);return e;
}
std::vector<FatxEntry> FatxVolume::list(const FatxEntry& directory) {
    if(!directory.directory())fail(0xc0000103u,"FATX handle is not a directory");
    std::vector<FatxEntry> entries;std::set<std::string> names;
    std::vector<uint8_t> buffer(cluster_bytes_);
    for(uint32_t c:chain(directory.cluster())) {
        uint64_t offset=cluster_offset(c);disk_.read(offset,buffer.data(),buffer.size());
        for(size_t i=0;i<buffer.size();i+=64) {
            unsigned n=buffer[i];if(!n || n==255)return entries;if(n==0xe5)continue;
            if(n>42)fail(corrupt,"Invalid FATX directory name length");
            FatxEntry entry;entry.location=offset+i;std::memcpy(entry.raw.data(),buffer.data()+i,64);
            std::string name=entry.name();
            if(name.find('\0')!=std::string::npos || name.find_first_of("\\/")!=std::string::npos ||
               name=="." || name==".." || !names.insert(fold(name)).second)
                fail(corrupt,"Invalid or duplicate FATX directory entry");
            entries.push_back(entry);
            if(entries.size()>1000000)fail(corrupt,"FATX directory exceeds admission limit");
        }
    }
    return entries;
}
FatxEntry FatxVolume::lookup(const std::string& path) {
    FatxEntry e=root();auto parts=components(path);
    for(size_t i=0;i<parts.size();++i) {
        auto children=list(e);auto found=std::find_if(children.begin(),children.end(),[&](const FatxEntry& child){return fold(child.name())==fold(parts[i]);});
        if(found==children.end())fail(i+1==parts.size()?0xc0000034u:0xc000003au,"FATX file/path not found");
        e=*found;
    }
    return e;
}
void FatxVolume::admit() {
    // Inspect allocation and reachable metadata, not gigabytes of file data.
    // Cross-linked live files are unsafe even if all offsets are in bounds.
    std::vector<std::pair<FatxEntry,unsigned>> pending{{root(),0}};
    std::unordered_set<uint32_t> owned;size_t budget=1000000;
    while(!pending.empty()) {
        auto item=pending.back();pending.pop_back();
        if(!budget-- || item.second>128)fail(corrupt,"FATX tree exceeds admission limit");
        auto clusters=chain(item.first.cluster());
        if((item.first.directory() && clusters.empty()) || uint64_t(clusters.size())*cluster_bytes_<item.first.size())
            fail(corrupt,"FATX entry length exceeds its allocation");
        for(uint32_t c:clusters)if(!owned.insert(c).second)fail(corrupt,"Cross-linked FATX files/directories");
        if(item.first.directory())for(const auto& e:list(item.first))pending.emplace_back(e,item.second+1);
    }
}
uint32_t FatxVolume::free_clusters() const {
    uint32_t n=0;for(uint32_t c=1;c<=count_;++c)if(!fat(c))++n;return n;
}
uint32_t FatxVolume::allocate(bool directory) {
    // A newly populated RAM cache can contain tens of thousands of clusters.
    // Retain the disk allocator's policy; RAM allocation resumes its free scan.
    for(uint32_t n=0;n<count_;++n) {
      uint32_t c=disk_.memory()?1+(memory_next_-1+n)%count_:n+1;
      if(!fat(c)) {
        std::vector<uint8_t> bytes(cluster_bytes_,directory?255:0);
        disk_.write(cluster_offset(c),bytes.data(),bytes.size());fat(c,0xffffffffu);
        memory_next_=c==count_?1:c+1;return c;
      }
    }
    fail(0xc000007fu,"HDD partition is full");
}
void FatxVolume::store(FatxEntry& e) {
    if(e.location==UINT64_MAX)fail(0xc0000022u,"Cannot change a FATX root entry");
    disk_.write(e.location,e.raw.data(),e.raw.size());
}
void FatxVolume::refresh(FatxEntry& e) {
    if(e.location==UINT64_MAX)return;
    disk_.read(e.location,e.raw.data(),e.raw.size());
    if(!e.raw[0] || e.raw[0]>42)fail(0xc0000056u,"FATX file was deleted");
}
uint64_t FatxVolume::empty_slot(FatxEntry& dir) {
    auto clusters=chain(dir.cluster());std::vector<uint8_t> bytes(cluster_bytes_);
    for(uint32_t c:clusters) {
        uint64_t at=cluster_offset(c);disk_.read(at,bytes.data(),bytes.size());
        for(size_t i=0;i<bytes.size();i+=64)if(!bytes[i] || bytes[i]==255 || bytes[i]==0xe5) {
            if(bytes[i]!=0xe5 && i+64<bytes.size()) {
                uint8_t end=255;disk_.write(at+i+64,&end,1);
            }
            return at+i;
        }
    }
    if(clusters.empty())fail(corrupt,"Empty FATX directory chain");
    uint32_t c=allocate(true);fat(clusters.back(),c);return cluster_offset(c);
}
FatxEntry FatxVolume::create(const std::string& path,bool directory,uint8_t attrs) {
    auto pn=parent_name(path);if(pn.second.empty())fail(invalid,"Cannot create FATX root");
    FatxEntry parent=lookup(pn.first);
    for(const auto& e:list(parent))if(fold(e.name())==fold(pn.second))fail(0xc0000035u,"FATX name already exists");
    FatxEntry e;e.raw.fill(255);e.raw[0]=uint8_t(pn.second.size());e.raw[1]=uint8_t((attrs&0x27)|(directory?16:0));
    std::copy(pn.second.begin(),pn.second.end(),e.raw.begin()+2);std::fill(e.raw.begin()+44,e.raw.end(),uint8_t(0));
    e.location=empty_slot(parent);
    if(directory)put32(e.raw.data()+44,allocate(true));
    store(e);disk_.flush();return e;
}
size_t FatxVolume::read(const FatxEntry& e,uint64_t at,void* output,size_t length) {
    if(e.directory())fail(0xc00000bau,"FATX entry is a directory");
    if(at>=e.size())return 0;
    length=size_t(std::min<uint64_t>(length,uint64_t(e.size())-at));auto clusters=chain(e.cluster());
    auto* bytes=static_cast<uint8_t*>(output);size_t done=0;
    while(done<length) {
        size_t n=std::min(length-done,size_t(cluster_bytes_-at%cluster_bytes_));
        disk_.read(cluster_offset(clusters.at(size_t(at/cluster_bytes_)))+at%cluster_bytes_,bytes+done,n);done+=n;at+=n;
    }
    return done;
}
void FatxVolume::resize(FatxEntry& e,uint32_t bytes) {
    if(e.directory())fail(0xc00000bau,"Cannot resize a directory");
    auto clusters=chain(e.cluster());size_t needed=(uint64_t(bytes)+cluster_bytes_-1)/cluster_bytes_;
    if(needed>clusters.size() && needed-clusters.size()>free_clusters())fail(0xc000007fu,"HDD partition is full");
    // Initialise new allocations before making them visible through EOF.
    while(clusters.size()<needed) {
        uint32_t next=allocate(false);
        if(!clusters.empty())fat(clusters.back(),next);else put32(e.raw.data()+44,next);
        clusters.push_back(next);
    }
    if(bytes>e.size() && e.size()%cluster_bytes_ && !clusters.empty()) {
        uint64_t at=e.size();uint64_t end=std::min<uint64_t>(bytes,((at/cluster_bytes_)+1)*cluster_bytes_);
        std::vector<uint8_t> zero(size_t(end-at),0);disk_.write(cluster_offset(clusters[size_t(at/cluster_bytes_)])+at%cluster_bytes_,zero.data(),zero.size());
    }
    put32(e.raw.data()+48,bytes);
    if(!needed)put32(e.raw.data()+44,0);
    store(e);disk_.flush(); // Publish a shorter EOF before freeing old clusters.
    if(needed<clusters.size()) {
        if(needed)fat(clusters[needed-1],0xffffffffu);
        for(size_t i=needed;i<clusters.size();++i)fat(clusters[i],0);
    }
    disk_.flush();
}
void FatxVolume::reserve(FatxEntry& e,uint32_t bytes) {
    uint32_t eof=e.size();resize(e,bytes);
    if(bytes>eof) {put32(e.raw.data()+48,eof);store(e);disk_.flush();}
}
size_t FatxVolume::write(FatxEntry& e,uint64_t at,const void* input,size_t length) {
    if(e.raw[1]&1)fail(0xc0000022u,"FATX file is read-only");
    if(at>UINT32_MAX || length>UINT32_MAX-at)fail(0xc000007fu,"FATX file is too large");
    if(!length)return 0;
    if(at+length>e.size())resize(e,uint32_t(at+length));
    auto clusters=chain(e.cluster());const auto* bytes=static_cast<const uint8_t*>(input);size_t done=0;
    while(done<length) {
        size_t n=std::min(length-done,size_t(cluster_bytes_-at%cluster_bytes_));
        disk_.write(cluster_offset(clusters.at(size_t(at/cluster_bytes_)))+at%cluster_bytes_,bytes+done,n);done+=n;at+=n;
    }
    disk_.flush();return done;
}
void FatxVolume::remove(FatxEntry& e) {
    if(e.location==UINT64_MAX || (e.raw[1]&1))fail(0xc0000022u,"Cannot delete protected FATX entry");
    if(e.directory() && !list(e).empty())fail(0xc0000101u,"FATX directory is not empty");
    auto clusters=chain(e.cluster());e.raw[0]=0xe5;store(e);disk_.flush();
    for(uint32_t c:clusters)fat(c,0);
    disk_.flush();
}
void FatxVolume::rename(FatxEntry& e,const std::string& target,bool replace) {
    auto pn=parent_name(target);if(pn.second.empty() || e.location==UINT64_MAX)fail(invalid,"Invalid FATX rename");
    FatxEntry parent=lookup(pn.first);auto children=list(parent);
    for(auto& child:children)if(fold(child.name())==fold(pn.second) && child.location!=e.location) {
        if(!replace)fail(0xc0000035u,"FATX destination exists");
        if(child.directory()!=e.directory())fail(0xc0000022u,"Cannot replace a different FATX entry type");
        remove(child);
    }
    // Native callers currently rename within a directory. Cross-directory
    // movement needs separately qualified crash/handle lifetime semantics.
    uint64_t destination=e.location;bool same=false;
    for(const auto& child:children)if(child.location==e.location)same=true;
    if(!same)fail(0xc00000bbu,"Cross-directory FATX rename is unsupported");
    e.raw[0]=uint8_t(pn.second.size());std::fill(e.raw.begin()+2,e.raw.begin()+44,uint8_t(255));
    std::copy(pn.second.begin(),pn.second.end(),e.raw.begin()+2);e.location=destination;store(e);disk_.flush();
}
void FatxVolume::metadata(FatxEntry& e,uint8_t attributes,const uint16_t times[6]) {
    e.raw[1]=uint8_t((attributes&0x27)|(e.directory()?16:0));
    for(unsigned i=0;i<6;++i)if(times[i])put16(e.raw.data()+52+i*2,times[i]);
    store(e);disk_.flush();
}
XboxDisk::XboxDisk(const std::string& path,bool writable) : image(path,writable) {
    // Retail partition numbering is E, C, X, Y, Z. Extended/homebrew tables
    // are deliberately not interpreted or rewritten by the initial importer.
    constexpr uint64_t starts[]={0,0xabe80000,0x8ca80000,0x80000,0x2ee80000,0x5dc80000};
    constexpr uint64_t sizes[]={0,0x1312d6000,0x1f400000,0x2ee00000,0x2ee00000,0x2ee00000};
    for(unsigned i=1;i<=5;++i)partitions[i]=std::make_unique<FatxVolume>(image,starts[i],sizes[i]);
}
XboxDisk::XboxDisk(unsigned partition,uint64_t bytes) : image(bytes) {
    if(partition<3 || partition>5)fail(0xc0000022u,"RAM storage is restricted to utility partitions");
    // Format only newly allocated RAM. This constructor cannot open, format or
    // otherwise alter an owner's disk, image, saved games or existing cache.
    std::vector<uint8_t> header(4096,0);
    std::memcpy(header.data(),"FATX",4);put32(header.data()+8,32);put32(header.data()+12,1);
    image.write(0,header.data(),header.size());
    uint64_t entries=bytes/16384+1,table=(entries*2+4095)&~UINT64_C(4095);
    std::vector<uint8_t> fat(size_t(table),0);put16(fat.data(),0xffff);put16(fat.data()+2,0xffff);
    image.write(4096,fat.data(),fat.size());
    std::vector<uint8_t> root(16384,255);image.write(4096+table,root.data(),root.size());
    partitions[partition]=std::make_unique<FatxVolume>(image,0,bytes);
}
}
