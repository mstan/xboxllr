// Shared byte-addressed image owner. Native file services and LLE devices use
// the same storage bytes; filesystem policy is a separate layer.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace xllr::storage {
class DiskImage {
public:
    DiskImage(const std::string& path, bool writable);
    // Sparse process-local bytes for a disposable utility volume; no host file.
    explicit DiskImage(uint64_t memory_bytes);
    ~DiskImage();
    DiskImage(const DiskImage&) = delete;
    DiskImage& operator=(const DiskImage&) = delete;
    uint64_t size() const { return virtual_size_; }
    bool qcow2() const { return qcow_; }
    bool writable() const { return writable_; }
    bool memory() const { return memory_; }
    uint64_t resident_bytes() const { return ram_pages_*16384ull; }
    uint64_t read_bytes() const { return ram_read_; }
    uint64_t written_bytes() const { return ram_written_; }
    void read(uint64_t offset, void* output, size_t count);
    void write(uint64_t offset, const void* input, size_t count);
    void flush();
private:
    FILE* file_ = nullptr;
    bool memory_ = false;
    uint64_t ram_read_ = 0, ram_written_ = 0, ram_pages_ = 0;
    std::vector<std::unique_ptr<uint8_t[]>> ram_;
    bool writable_ = false, qcow_ = false, dirty_ = false, failed_ = false;
    uint32_t version_ = 0;
    uint64_t physical_size_ = 0, virtual_size_ = 0, cluster_ = 0;
    uint64_t l1_offset_ = 0, ref_offset_ = 0;
    std::vector<uint64_t> l1_, refs_;
    std::map<uint64_t, std::vector<uint64_t>> l2_;
    std::map<uint64_t, std::vector<uint8_t>> refblocks_;
    std::recursive_mutex mutex_;
    void host_read(uint64_t, void*, size_t);
    void host_write(uint64_t, const void*, size_t);
    void sync();
    void mark_dirty();
    void admit_qcow();
    void set_refcount(uint64_t physical, uint16_t count);
    uint64_t allocate();
    uint64_t mapping(uint64_t offset, bool allocate);
};
}
