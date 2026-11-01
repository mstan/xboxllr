#include "storage_api.h"
#include "fatx_volume.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <set>

namespace {
using namespace xllr::storage;
constexpr uint32_t invalid=0xc000000du,denied=0xc0000022u,bad_handle=0xc0000008u,sharing=0xc0000043u;
struct OpenFile {
    FatxEntry entry;
    std::string path,mask;
    uint64_t position=0;
    size_t cursor=0;
    uint32_t access=0,share=0,options=0;
    unsigned partition=0;
    bool pending=false,search=false;
};
struct Storage {
    XboxDisk disk;
    std::recursive_mutex mutex;
    std::map<uint64_t,OpenFile> handles;
    uint64_t next=1;
    explicit Storage(const char* path,bool writable):disk(path,writable) {}
    Storage(unsigned partition,uint64_t bytes):disk(partition,bytes) {}
    OpenFile& file(uint64_t id) {
        auto found=handles.find(id);if(found==handles.end())throw FileError(bad_handle,"Unknown storage handle");
        auto& f=found->second;disk.partitions[f.partition]->refresh(f.entry);return f;
    }
    std::string path(unsigned partition,uint64_t parent,const char* name) {
        if(!name || std::strlen(name)>1023)throw FileError(invalid,"Invalid storage path");
        if(!parent)return name;
        auto& f=file(parent);
        if(partition!=f.partition || !f.entry.directory())throw FileError(0xc0000103u,"Invalid storage parent directory");
        return f.path.empty()?name:std::string(name).empty()?f.path:f.path+"\\"+name;
    }
};
uint32_t granted(uint32_t access) {
    if(access&0x10000000u)access|=0x1f01ff;
    if(access&0x80000000u)access|=0x120089;
    if(access&0x40000000u)access|=0x120116;
    if(access&0x20000000u)access|=0x1200a0;
    if(access&0x02000000u)access|=0x1f01ff;
    return access&0x01ffffffu;
}
uint32_t share_need(uint32_t access) {return ((access&1)?1u:0u)|((access&6)?2u:0u)|((access&0x10000)?4u:0u);}
bool match(const std::string& pattern,const std::string& text) {
    // Bounded ASCII case-insensitive * / ? matching, without recursion.
    auto lower=[](char c){return c>='A' && c<='Z'?char(c-'A'+'a'):c;};
    size_t p=0,t=0,star=std::string::npos,retry=0;
    while(t<text.size()) {
        if(p<pattern.size() && (pattern[p]=='?' || lower(pattern[p])==lower(text[t]))) {++p;++t;}
        else if(p<pattern.size() && pattern[p]=='*') {star=p++;retry=t;}
        else if(star!=std::string::npos) {p=star+1;t=++retry;}
        else return false;
    }
    while(p<pattern.size() && pattern[p]=='*')++p;
    return p==pattern.size();
}
void info(const OpenFile& f,XllrStorageInfo* out) {
    *out={};out->length=f.entry.size();out->position=f.position;out->attributes=f.entry.raw[1]?f.entry.raw[1]:0x80;
    out->access=f.access;out->directory=f.entry.directory();out->delete_pending=f.pending;
    for(unsigned i=0;i<6;++i)out->times[i]=uint16_t(f.entry.raw[52+i*2])|uint16_t(f.entry.raw[53+i*2])<<8;
}
template<class F> uint32_t invoke(void* instance,F f) {
    if(!instance)return invalid;
    try {auto& s=*static_cast<Storage*>(instance);std::lock_guard<std::recursive_mutex> lock(s.mutex);return f(s);}
    catch(const FileError& e) {return e.status;}
    catch(const std::bad_alloc&) {return 0xc000009au;}
    catch(...) {return 0xc0000185u;}
}
}
extern "C" {
void* xllr_storage_open(const char* path,int writable,char* error,size_t capacity) {
    try {if(!path)throw std::runtime_error("No HDD image selected");return new Storage(path,writable!=0);}
    catch(const std::exception& e) {if(error && capacity) {size_t n=std::min(capacity-1,std::strlen(e.what()));std::memcpy(error,e.what(),n);error[n]=0;}return nullptr;}
}
uint32_t xllr_storage_destroy(void* instance) {
    uint32_t result=xllr_storage_flush(instance);delete static_cast<Storage*>(instance);return result;
}
void* xllr_storage_open_ram_cache(unsigned partition,uint64_t bytes,char* error,size_t capacity) {
    try {return new Storage(partition,bytes);}
    catch(const std::exception& e) {if(error && capacity) {size_t n=std::min(capacity-1,std::strlen(e.what()));std::memcpy(error,e.what(),n);error[n]=0;}return nullptr;}
}
uint32_t xllr_storage_ram_stats(void* s,uint64_t* resident,uint64_t* read,uint64_t* written) {
    if(!resident || !read || !written)return invalid;
    return invoke(s,[&](Storage& v){if(!v.disk.image.memory())return invalid;
        *resident=v.disk.image.resident_bytes();*read=v.disk.image.read_bytes();*written=v.disk.image.written_bytes();return 0u;});
}
uint64_t xllr_storage_bytes(void* instance) {return instance?static_cast<Storage*>(instance)->disk.image.size():0;}
int xllr_storage_is_qcow2(void* instance) {return instance && static_cast<Storage*>(instance)->disk.image.qcow2();}
uint32_t xllr_storage_read_blocks(void* s,uint64_t at,void* out,uint32_t size) {
    if(!out && size)return invalid;
    return invoke(s,[&](Storage& v){v.disk.image.read(at,out,size);return 0u;});
}
uint32_t xllr_storage_write_blocks(void* s,uint64_t at,const void* in,uint32_t size) {
    if(!in && size)return invalid;
    return invoke(s,[&](Storage& v){v.disk.image.write(at,in,size);return 0u;});
}
uint32_t xllr_storage_flush(void* s) {return invoke(s,[](Storage& v){v.disk.image.flush();return 0u;});}
uint32_t xllr_storage_file_open(void* s,unsigned part,uint64_t parent,const char* name,uint32_t access,uint32_t share,
    uint32_t options,uint32_t disposition,uint32_t attrs,int64_t allocation,uint64_t* handle,uint32_t* result) {
    if(!handle || !result || part<1 || part>5 || share>7 || disposition>5 || allocation<0 || uint64_t(allocation)>UINT32_MAX)return invalid;
    return invoke(s,[&](Storage& v) {
        if(!v.disk.partitions[part])return denied;
        if((options&0x30) && !(granted(access)&0x100000))return invalid;
        auto& fs=*v.disk.partitions[part];std::string path=v.path(part,parent,name);
        FatxEntry entry;bool exists=true;
        try {entry=fs.lookup(path);}catch(const FileError& e) {if(e.status!=0xc0000034u)throw;exists=false;}
        if(!exists && (disposition==1 || disposition==4))return 0xc0000034u;
        if(exists && disposition==2)return 0xc0000035u;
        uint32_t effective=granted(access);
        for(const auto& item:v.handles) {
            const auto& old=item.second;
            if(old.partition!=part || !exists || old.entry.location!=entry.location || old.entry.cluster()!=entry.cluster())continue;
            if(old.pending)return 0xc0000056u;
            if((share_need(effective)&~old.share) || (share_need(old.access)&~share))return sharing;
        }
        bool directory=exists?entry.directory():bool(options&1);
        if((options&1) && !directory)return 0xc0000103u;
        if((options&0x40) && directory)return 0xc00000bau;
        if(exists && (entry.raw[1]&1) && (effective&0x10006u))return denied;
        if(!v.disk.image.writable() && (!exists || disposition==0 || disposition>=4 || (effective&0x10116u)))return denied;
        if((options&0x1000) && !(effective&0x10000))return denied;
        if(v.handles.size()>=1024 || !v.next)return 0xc000009au;
        uint32_t action=1;
        if(!exists) {entry=fs.create(path,directory,uint8_t(attrs));action=2;}
        else if(disposition==0 || disposition>=4) {
            if(directory)return 0xc00000bau;
            if(!(effective&2))return denied;
            fs.resize(entry,0);action=disposition==0?0:3;
        }
        if(allocation && !directory)fs.reserve(entry,uint32_t(allocation));
        OpenFile f;f.entry=entry;f.path=path;f.partition=part;f.access=effective;f.share=share;f.options=options;
        f.pending=bool(options&0x1000);
        uint64_t id=v.next++;v.handles.emplace(id,std::move(f));*handle=id;*result=action;return 0u;
    });
}
uint32_t xllr_storage_file_close(void* s,uint64_t id) {
    return invoke(s,[&](Storage& v){auto& f=v.file(id);bool pending=f.pending;
        for(const auto& item:v.handles)if(item.second.partition==f.partition && item.second.entry.location==f.entry.location && item.second.pending)pending=true;
        bool last=true;
        for(const auto& item:v.handles)if(item.first!=id && item.second.partition==f.partition && item.second.entry.location==f.entry.location)last=false;
        if(pending && last)v.disk.partitions[f.partition]->remove(f.entry);
        else if(pending)for(auto& item:v.handles)if(item.second.partition==f.partition && item.second.entry.location==f.entry.location)item.second.pending=true;
        v.handles.erase(id);v.disk.image.flush();return 0u;});
}
uint32_t xllr_storage_file_info(void* s,uint64_t id,XllrStorageInfo* out) {
    if(!out)return invalid;
    return invoke(s,[&](Storage& v){info(v.file(id),out);return 0u;});
}
uint32_t xllr_storage_file_allocation(void* s,uint64_t id,uint64_t* bytes) {
    if(!bytes)return invalid;
    return invoke(s,[&](Storage& v){auto& f=v.file(id);*bytes=v.disk.partitions[f.partition]->allocated(f.entry);return 0u;});
}
uint32_t xllr_storage_file_read(void* s,uint64_t id,int64_t offset,void* buffer,uint32_t bytes,uint32_t* done) {
    if(!done || (!buffer && bytes))return invalid;
    return invoke(s,[&](Storage& v){auto& f=v.file(id);if(!(f.access&1))return denied;
        if(offset==-2)offset=int64_t(f.position);
        if(offset<0)return invalid;
        if(bytes && uint64_t(offset)>=f.entry.size())return 0xc0000011u;
        size_t n=v.disk.partitions[f.partition]->read(f.entry,uint64_t(offset),buffer,bytes);
        if(f.options&0x30)f.position=uint64_t(offset)+n;
        *done=uint32_t(n);return 0u;});
}
uint32_t xllr_storage_file_write(void* s,uint64_t id,int64_t offset,const void* buffer,uint32_t bytes,uint32_t* done) {
    if(!done || (!buffer && bytes))return invalid;
    return invoke(s,[&](Storage& v){auto& f=v.file(id);if(!(f.access&6))return denied;
        if(offset==-1 || !(f.access&2))offset=f.entry.size();else if(offset==-2)offset=int64_t(f.position);
        if(offset<0)return invalid;
        size_t n=v.disk.partitions[f.partition]->write(f.entry,uint64_t(offset),buffer,bytes);
        if(f.options&0x30)f.position=uint64_t(offset)+n;
        *done=uint32_t(n);return 0u;});
}
uint32_t xllr_storage_file_resize(void* s,uint64_t id,int64_t bytes,int allocation) {
    if(bytes<0 || uint64_t(bytes)>UINT32_MAX)return invalid;
    return invoke(s,[&](Storage& v){auto& f=v.file(id);if(!(f.access&2))return denied;
        if(allocation)v.disk.partitions[f.partition]->reserve(f.entry,uint32_t(bytes));
        else v.disk.partitions[f.partition]->resize(f.entry,uint32_t(bytes));
        return 0u;});
}
uint32_t xllr_storage_file_position(void* s,uint64_t id,int64_t position) {
    if(position<0)return invalid;
    return invoke(s,[&](Storage& v){v.file(id).position=uint64_t(position);return 0u;});
}
uint32_t xllr_storage_file_metadata(void* s,uint64_t id,uint32_t attrs,const uint16_t times[6]) {
    if(!times)return invalid;
    return invoke(s,[&](Storage& v){auto& f=v.file(id);if(!(f.access&0x100))return denied;
        v.disk.partitions[f.partition]->metadata(f.entry,uint8_t(attrs?attrs:f.entry.raw[1]),times);return 0u;});
}
uint32_t xllr_storage_file_delete(void* s,uint64_t id,int pending) {
    return invoke(s,[&](Storage& v){auto& f=v.file(id);if(!(f.access&0x10000) || (f.entry.raw[1]&1) || f.entry.location==UINT64_MAX)return denied;
        if(pending && f.entry.directory() && !v.disk.partitions[f.partition]->list(f.entry).empty())return 0xc0000101u;
        for(const auto& item:v.handles)if(item.first!=id && item.second.partition==f.partition && item.second.entry.location==f.entry.location && !(item.second.share&4))return sharing;
        f.pending=pending!=0;return 0u;});
}
uint32_t xllr_storage_file_rename(void* s,uint64_t id,uint64_t parent,const char* name,int replace) {
    return invoke(s,[&](Storage& v){auto& f=v.file(id);if(!(f.access&0x10000))return denied;
        std::string target=v.path(f.partition,parent,name);
        // A target opened by another caller cannot be replaced behind its back.
        for(const auto& item:v.handles)if(item.first!=id && item.second.partition==f.partition && item.second.path==target)return sharing;
        v.disk.partitions[f.partition]->rename(f.entry,target,replace!=0);
        for(auto& item:v.handles)if(item.second.partition==f.partition && item.second.entry.location==f.entry.location)item.second.path=target;
        return 0u;});
}
uint32_t xllr_storage_directory(void* s,uint64_t id,const char* mask,int restart,XllrStorageDirectoryEntry* out) {
    if(!out)return invalid;
    return invoke(s,[&](Storage& v){auto& f=v.file(id);if(!(f.access&1))return denied;
        bool first=!f.search;if(first){f.mask=mask && *mask?mask:"*";if(f.mask=="*.*")f.mask="*";}
        if(restart || first)f.cursor=0;
        f.search=true;
        auto entries=v.disk.partitions[f.partition]->list(f.entry);
        while(f.cursor<entries.size()) {
            const auto& entry=entries[f.cursor++];if(!match(f.mask,entry.name()))continue;
            OpenFile child;child.entry=entry;*out={};info(child,&out->info);std::memcpy(out->name,entry.name().data(),entry.raw[0]);return 0u;
        }
        return first?0xc000000fu:0x80000006u;});
}
uint32_t xllr_storage_capacity(void* s,unsigned partition,uint64_t* total,uint64_t* available,uint32_t* unit) {
    if(partition<1 || partition>5 || !total || !available || !unit)return invalid;
    return invoke(s,[&](Storage& v){if(!v.disk.partitions[partition])return denied;auto& fs=*v.disk.partitions[partition];*total=fs.clusters();*available=fs.free_clusters();*unit=fs.cluster_size();return 0u;});
}
uint32_t xllr_storage_clear_cache(void* s,unsigned partition) {
    if(partition<3 || partition>5)return denied;
    return invoke(s,[&](Storage& v){
        if(!v.disk.partitions[partition])return denied;
        for(const auto& item:v.handles)if(item.second.partition==partition && !item.second.path.empty())return sharing;
        auto& fs=*v.disk.partitions[partition];
        std::vector<std::pair<FatxEntry,bool>> pending{{fs.root(),false}};
        while(!pending.empty()) {
            auto item=pending.back();pending.pop_back();
            if(item.first.directory() && !item.second) {
                pending.emplace_back(item.first,true);
                for(const auto& child:fs.list(item.first))pending.emplace_back(child,false);
            } else if(item.first.location!=UINT64_MAX) {
                if(item.first.raw[1]&1) {uint16_t times[6]={};fs.metadata(item.first,uint8_t(item.first.raw[1]&~1),times);}
                fs.remove(item.first);
            }
        }
        return 0u;
    });
}
}
