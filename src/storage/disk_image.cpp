// First-party implementation of the documented QEMU qcow2 v2/v3 subset.
// Format reference: https://www.qemu.org/docs/master/interop/qcow2.html
// No guest software, formatter, backing-file traversal or donor source.
#include "disk_image.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <set>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <share.h>
#else
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace xllr::storage {
namespace {
constexpr uint64_t copied = UINT64_C(1) << 63;
constexpr uint64_t address_mask = UINT64_C(0x00fffffffffffe00);
[[noreturn]] void bad(const char* text) { throw std::runtime_error(text); }
uint32_t be32(const uint8_t* p) { return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
uint64_t be64(const uint8_t* p) { return uint64_t(be32(p))<<32|be32(p+4); }
void put64(uint8_t* p,uint64_t v) { for(unsigned i=0;i<8;++i) p[7-i]=uint8_t(v>>(i*8)); }
bool fits(uint64_t offset,uint64_t bytes,uint64_t size) { return offset<=size && bytes<=size-offset; }
}

DiskImage::DiskImage(const std::string& path,bool writable) : writable_(writable) {
    try {
#ifdef _WIN32
        int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,nullptr,0);
        if(!n) bad("Invalid disk image path");
        std::vector<wchar_t> wide(size_t(n),0);
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,wide.data(),n);
        // This is an existing regular file, never a physical disk or named pipe.
        DWORD attrs=GetFileAttributesW(wide.data());
        if(attrs==INVALID_FILE_ATTRIBUTES || (attrs&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_DEVICE)))
            bad("Select an existing ordinary HDD image file");
        file_=_wfsopen(wide.data(),writable?L"r+b":L"rb",_SH_DENYWR);
        if(!file_) bad("HDD image is busy or cannot be opened");
        HANDLE handle=(HANDLE)_get_osfhandle(_fileno(file_));
        BY_HANDLE_FILE_INFORMATION info={};
        if(GetFileType(handle)!=FILE_TYPE_DISK || !GetFileInformationByHandle(handle,&info) ||
           (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_DEVICE)))
            bad("HDD image must be an ordinary file");
        LARGE_INTEGER size;
        if(!GetFileSizeEx(handle,&size) || size.QuadPart<512) bad("HDD image is too small");
        physical_size_=uint64_t(size.QuadPart);
#else
        file_=std::fopen(path.c_str(),writable?"r+b":"rb");
        if(!file_) bad("Cannot open HDD image");
        struct stat st={};
        if(fstat(fileno(file_),&st) || !S_ISREG(st.st_mode) || st.st_size<512) bad("HDD image must be an ordinary file");
        if(flock(fileno(file_),(writable?LOCK_EX:LOCK_SH)|LOCK_NB)) bad("HDD image is busy");
        physical_size_=uint64_t(st.st_size);
#endif
        virtual_size_=physical_size_;
        uint8_t header[104]={};host_read(0,header,sizeof(header));
        if(be32(header)==0x514649fbu) {qcow_=true;admit_qcow();}
        else if(physical_size_%512) bad("Raw HDD image length must be a multiple of 512 bytes");
    } catch(...) { if(file_)std::fclose(file_);file_=nullptr;throw; }
}
DiskImage::DiskImage(uint64_t bytes) : memory_(true), writable_(true) {
    if(bytes<65536 || bytes>750ull*1024*1024 || bytes%16384)
        bad("RAM utility volume must be 64 KiB to 750 MiB in 16 KiB units");
    virtual_size_=bytes;
    // The bounded page index costs at most 384 KiB. Data pages remain lazy;
    // ordinary cache traffic does not pay a tree lookup for every byte range.
    ram_.resize(size_t(bytes/16384));
}
DiskImage::~DiskImage() {
    // Every public mutation is flushed explicitly. Never clear a dirty marker
    // after an I/O failure or silently repair a user's image during teardown.
    if(file_) std::fclose(file_);
}
void DiskImage::host_read(uint64_t offset,void* buffer,size_t count) {
    if(!fits(offset,count,physical_size_))bad("Truncated HDD image metadata/data");
#ifdef _WIN32
    if(_fseeki64(file_,static_cast<int64_t>(offset),SEEK_SET))bad("HDD image seek failed");
#else
    if(fseeko(file_,static_cast<off_t>(offset),SEEK_SET))bad("HDD image seek failed");
#endif
    if(count && std::fread(buffer,1,count,file_)!=count)bad("HDD image read failed");
}
void DiskImage::host_write(uint64_t offset,const void* buffer,size_t count) {
    if(!writable_ || failed_)bad("HDD image is not writable");
    if(offset>uint64_t(INT64_MAX) || count>uint64_t(INT64_MAX)-offset)bad("HDD image offset overflow");
#ifdef _WIN32
    if(_fseeki64(file_,static_cast<int64_t>(offset),SEEK_SET)) {failed_=true;bad("HDD image seek failed");}
#else
    if(fseeko(file_,static_cast<off_t>(offset),SEEK_SET)) {failed_=true;bad("HDD image seek failed");}
#endif
    if(count && std::fwrite(buffer,1,count,file_)!=count) {failed_=true;bad("HDD image write failed");}
    physical_size_=std::max(physical_size_,offset+count);
}
void DiskImage::sync() {
    if(std::fflush(file_)) {failed_=true;bad("HDD image flush failed");}
#ifdef _WIN32
    if(_commit(_fileno(file_))) {failed_=true;bad("HDD image durability flush failed");}
#else
    if(fsync(fileno(file_))) {failed_=true;bad("HDD image durability flush failed");}
#endif
}
void DiskImage::admit_qcow() {
    uint8_t h[104]={};host_read(0,h,sizeof(h));version_=be32(h+4);
    if(version_!=2 && version_!=3)bad("Only QCOW2 versions 2 and 3 are supported");
    if(be64(h+8) || be32(h+16) || be32(h+32) || be32(h+60) || be64(h+64))
        bad("QCOW2 backing files, encryption and snapshots are not supported");
    uint32_t bits=be32(h+20);
    if(bits<9 || bits>21)bad("Invalid QCOW2 cluster size");
    cluster_=UINT64_C(1)<<bits;virtual_size_=be64(h+24);
    if(!virtual_size_ || virtual_size_%512 || virtual_size_>(UINT64_C(1)<<41))bad("Unsupported QCOW2 disk capacity");
    if(version_==3 && (be64(h+72) || be64(h+88) || be32(h+96)!=4 ||
        be32(h+100)<104 || be32(h+100)>cluster_ || be32(h+100)%8))
        bad("QCOW2 is dirty, damaged, or uses unsupported features");
    uint64_t count=be32(h+36),ref_clusters=be32(h+56);
    l1_offset_=be64(h+40);ref_offset_=be64(h+48);
    if(!count || count*8>(4u<<20) || !ref_clusters || ref_clusters*cluster_>(8u<<20) ||
       count<(virtual_size_+cluster_*(cluster_/8)-1)/(cluster_*(cluster_/8)))
        bad("Invalid or oversized QCOW2 mapping tables");
    auto extent=[&](uint64_t offset,uint64_t length) {
        if(!offset || offset%cluster_ || !fits(offset,length,physical_size_))bad("QCOW2 metadata points outside its image");
    };
    extent(l1_offset_,count*8);extent(ref_offset_,ref_clusters*cluster_);
    std::map<uint64_t,uint16_t> expected;
    auto own=[&](uint64_t at,uint64_t length) {
        extent(at,length);
        for(uint64_t p=at/cluster_;p<(at+length+cluster_-1)/cluster_;++p)
            if(!expected.emplace(p,uint16_t(1)).second)bad("Overlapping QCOW2 metadata/data clusters");
    };
    expected.emplace(uint64_t(0),uint16_t(1));own(l1_offset_,count*8);own(ref_offset_,ref_clusters*cluster_);
    std::vector<uint8_t> bytes(size_t(ref_clusters*cluster_));host_read(ref_offset_,bytes.data(),bytes.size());
    refs_.resize(bytes.size()/8);
    for(size_t i=0;i<refs_.size();++i) {
        refs_[i]=be64(bytes.data()+i*8);
        if(!refs_[i])continue;
        own(refs_[i],cluster_);
        auto& block=refblocks_[refs_[i]];block.resize(size_t(cluster_));host_read(refs_[i],block.data(),block.size());
    }
    bytes.resize(size_t(count*8));host_read(l1_offset_,bytes.data(),bytes.size());l1_.resize(size_t(count));
    for(size_t i=0;i<l1_.size();++i) {
        uint64_t value=be64(bytes.data()+i*8);l1_[i]=value;
        if(value&~(copied|address_mask))bad("Invalid QCOW2 L1 flags");
        uint64_t offset=value&address_mask;if(!offset) {if(value)bad("Invalid empty QCOW2 L1 entry");continue;}
        own(offset,cluster_);std::vector<uint8_t> raw(size_t(cluster_),0);host_read(offset,raw.data(),raw.size());
        auto& table=l2_[offset];table.resize(size_t(cluster_/8));
        for(size_t j=0;j<table.size();++j) {
            value=be64(raw.data()+j*8);table[j]=value;
            if(value&~(copied|address_mask|(version_==3?UINT64_C(1):0)))bad("Compressed or invalid QCOW2 L2 entries are unsupported");
            uint64_t data=value&address_mask;
            if(data)own(data,cluster_);
            else if(value&copied)bad("Invalid copied QCOW2 zero/unallocated entry");
        }
    }
    // Compare actual reference counts with *all* active mappings before any
    // write. Refuse sharing/orphans rather than corrupt snapshots or reclaim
    // storage whose ownership this backend cannot establish.
    for(size_t i=0;i<refs_.size();++i) if(refs_[i]) {
        const auto& block=refblocks_.at(refs_[i]);
        for(size_t j=0;j<block.size()/2;++j) {
            uint16_t actual=uint16_t(block[j*2])<<8|block[j*2+1];
            uint64_t physical=uint64_t(i)*(cluster_/2)+j;
            auto found=expected.find(physical);uint16_t wanted=found==expected.end()?0:found->second;
            if(actual!=wanted)bad("QCOW2 reference counts need repair by the image's own tools");
        }
    }
    for(const auto& entry:expected) {
        uint64_t index=entry.first/(cluster_/2),slot=entry.first%(cluster_/2);
        if(index>=refs_.size() || !refs_[size_t(index)] ||
            (uint16_t(refblocks_.at(refs_[size_t(index)])[size_t(slot*2)])<<8|
                refblocks_.at(refs_[size_t(index)])[size_t(slot*2+1)])!=1)
            bad("QCOW2 mapping lacks an exclusive reference count");
    }
}
void DiskImage::mark_dirty() {
    if(dirty_)return;
    if(qcow_ && version_==3) {uint8_t b[8];put64(b,1);host_write(72,b,8);sync();}
    dirty_=true;
}
void DiskImage::set_refcount(uint64_t physical,uint16_t count) {
    uint64_t entry=(physical/cluster_)%(cluster_/2),index=(physical/cluster_)/(cluster_/2);
    if(index>=refs_.size() || !refs_[size_t(index)])bad("QCOW2 reference table cannot grow this image");
    auto& block=refblocks_.at(refs_[size_t(index)]);
    block[size_t(entry*2)]=uint8_t(count>>8);block[size_t(entry*2+1)]=uint8_t(count);
    host_write(refs_[size_t(index)]+entry*2,block.data()+entry*2,2);
}
uint64_t DiskImage::allocate() {
    uint64_t offset=(physical_size_+cluster_-1)&~(cluster_-1);
    uint64_t index=(offset/cluster_)/(cluster_/2);
    if(index>=refs_.size())bad("QCOW2 reference table capacity exhausted");
    std::vector<uint8_t> zero(size_t(cluster_),0);
    if(!refs_[size_t(index)]) {
        // Reserve a new refcount block first, including the block's own count.
        host_write(offset,zero.data(),zero.size());refs_[size_t(index)]=offset;refblocks_[offset]=zero;
        uint8_t encoded[8];put64(encoded,offset);host_write(ref_offset_+index*8,encoded,8);
        set_refcount(offset,1);sync();return allocate();
    }
    host_write(offset,zero.data(),zero.size());set_refcount(offset,1);return offset;
}
uint64_t DiskImage::mapping(uint64_t offset,bool create) {
    size_t i=size_t(offset/(cluster_*(cluster_/8))),j=size_t(offset/cluster_%(cluster_/8));
    uint64_t table=l1_.at(i)&address_mask;
    if(!table && create) {
        table=allocate();l2_[table]=std::vector<uint64_t>(size_t(cluster_/8),0);
        l1_[i]=table|copied;uint8_t b[8];put64(b,l1_[i]);host_write(l1_offset_+i*8,b,8);
    }
    if(!table)return 0;
    auto& value=l2_.at(table).at(j);uint64_t physical=value&address_mask;
    if(create && (!physical || (value&1))) {
        // An allocated ZERO cluster is already exclusively owned; initialise
        // it before clearing ZERO, otherwise old host bytes could escape.
        if(!physical)physical=allocate();
        else {std::vector<uint8_t> zero(size_t(cluster_),0);host_write(physical,zero.data(),zero.size());}
        value=physical|copied;uint8_t b[8];put64(b,value);host_write(table+j*8,b,8);
    }
    return (!create && (value&1))?0:physical;
}
void DiskImage::read(uint64_t offset,void* output,size_t count) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if(!fits(offset,count,virtual_size_))bad("HDD image read outside capacity");
    if(memory_) {
        ram_read_+=count;
        auto* bytes=static_cast<uint8_t*>(output);
        while(count) {
            size_t n=std::min(count,size_t(16384-offset%16384));
            const auto& page=ram_[size_t(offset/16384)];
            if(!page)std::memset(bytes,0,n);
            else std::memcpy(bytes,page.get()+offset%16384,n);
            offset+=n;bytes+=n;count-=n;
        }
        return;
    }
    if(!qcow_) {host_read(offset,output,count);return;}
    auto* bytes=static_cast<uint8_t*>(output);
    while(count) {
        size_t n=size_t(std::min<uint64_t>(count,cluster_-offset%cluster_));uint64_t physical=mapping(offset,false);
        if(physical)host_read(physical+offset%cluster_,bytes,n);else std::memset(bytes,0,n);
        offset+=n;bytes+=n;count-=n;
    }
}
void DiskImage::write(uint64_t offset,const void* input,size_t count) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if(!writable_ || failed_ || !fits(offset,count,virtual_size_))bad("HDD image write outside capacity or not writable");
    if(!count)return;
    if(memory_) {
        const auto* bytes=static_cast<const uint8_t*>(input);
        while(count) {
            size_t n=std::min(count,size_t(16384-offset%16384));
            auto& page=ram_[size_t(offset/16384)];
            if(!page){page=std::make_unique<uint8_t[]>(16384);++ram_pages_;}
            std::memcpy(page.get()+offset%16384,bytes,n);
            offset+=n;bytes+=n;count-=n;ram_written_+=n;
        }
        return;
    }
    mark_dirty();
    if(!qcow_) {host_write(offset,input,count);return;}
    const auto* bytes=static_cast<const uint8_t*>(input);
    try {
        while(count) {
            size_t n=size_t(std::min<uint64_t>(count,cluster_-offset%cluster_));uint64_t physical=mapping(offset,true);
            host_write(physical+offset%cluster_,bytes,n);offset+=n;bytes+=n;count-=n;
        }
    } catch(...) {failed_=true;throw;}
}
void DiskImage::flush() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if(memory_)return; // All mutations already belong to these process-local bytes.
    if(failed_)bad("Previous HDD image I/O failed; close and inspect the image");
    if(!writable_ || !dirty_)return;
    sync();
    if(qcow_ && version_==3) {uint8_t b[8]={};host_write(72,b,8);sync();}
    dirty_=false;
}
}
