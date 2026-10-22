#pragma once
#include "disk_image.hpp"
#include <array>
#include <memory>
#include <stdexcept>

namespace xllr::storage {
struct FileError : std::runtime_error {
    uint32_t status;
    FileError(uint32_t code,const char* reason) : std::runtime_error(reason),status(code) {}
};
struct FatxEntry {
    uint64_t location=UINT64_MAX;
    std::array<uint8_t,64> raw{};
    std::string name() const;
    uint32_t cluster() const;
    uint32_t size() const;
    bool directory() const { return (raw[1]&16)!=0; }
};
class FatxVolume {
public:
    FatxVolume(DiskImage&,uint64_t offset,uint64_t bytes);
    FatxEntry root() const;
    FatxEntry lookup(const std::string& path);
    std::vector<FatxEntry> list(const FatxEntry& directory);
    FatxEntry create(const std::string& path,bool directory,uint8_t attributes);
    void resize(FatxEntry&,uint32_t bytes);
    void reserve(FatxEntry&,uint32_t bytes);
    void refresh(FatxEntry&);
    size_t read(const FatxEntry&,uint64_t offset,void*,size_t);
    size_t write(FatxEntry&,uint64_t offset,const void*,size_t);
    void remove(FatxEntry&);
    void rename(FatxEntry&,const std::string& target,bool replace);
    void metadata(FatxEntry&,uint8_t attributes,const uint16_t times[6]);
    uint32_t cluster_size() const { return cluster_bytes_; }
    uint32_t clusters() const { return count_; }
    uint32_t free_clusters() const;
    uint64_t allocated(const FatxEntry& entry) const {return uint64_t(chain(entry.cluster()).size())*cluster_bytes_;}
private:
    DiskImage& disk_;
    uint64_t base_,bytes_,data_;
    uint32_t cluster_bytes_=0,count_=0,entry_bytes_=0,root_cluster_=0;
    std::vector<uint8_t> fat_;
    uint32_t fat(uint32_t) const;
    void fat(uint32_t,uint32_t);
    uint64_t cluster_offset(uint32_t) const;
    std::vector<uint32_t> chain(uint32_t) const;
    void store(FatxEntry&);
    uint32_t allocate(bool directory);
    uint64_t empty_slot(FatxEntry& directory);
    void admit();
};
class XboxDisk {
public:
    DiskImage image;
    std::unique_ptr<FatxVolume> partitions[6];
    XboxDisk(const std::string& path,bool writable);
};
}
