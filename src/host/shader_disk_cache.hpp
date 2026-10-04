#pragma once
// Adapted from phobos665/xboxrecomp rhi_shader_cache.c's versioned full-key
// validation and temporary-file publication design. OpenGL binaries are also
// tied to the driver identity. A cache is optional: every miss compiles source.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace xlle {
class ShaderDiskCache {
  std::filesystem::path root_;
  static uint64_t hash(const void* data, size_t size) {
    uint64_t h = 14695981039346656037ull;
    auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
  }
  std::filesystem::path path(const std::string& key) const {
    return root_ / (std::to_string(hash(key.data(), key.size())) + ".bin");
  }
  struct Header {
    uint32_t magic = 0x534c4c58, version = 1, format = 0, reserved = 0;
    uint64_t key_size = 0, binary_size = 0, checksum = 0;
  };
public:
  explicit ShaderDiskCache(std::filesystem::path root) : root_(std::move(root)) {}
  bool load(const std::string& key, uint32_t& format, std::vector<uint8_t>& binary) const {
    if (root_.empty()) return false;
    std::ifstream f(path(key), std::ios::binary);
    Header h;
    if (!f.read(reinterpret_cast<char*>(&h), sizeof(h)) || h.magic != 0x534c4c58 ||
        h.version != 1 || h.reserved || h.key_size != key.size() ||
        !h.binary_size || h.binary_size > 64 * 1024 * 1024) return false;
    std::string stored(key.size(), '\0');
    if (!f.read(stored.data(), stored.size()) || stored != key) return false;
    std::vector<uint8_t> bytes(static_cast<size_t>(h.binary_size));
    if (!f.read(reinterpret_cast<char*>(bytes.data()), bytes.size()) ||
        f.peek() != std::char_traits<char>::eof() ||
        hash(bytes.data(), bytes.size()) != h.checksum) return false;
    format = h.format;
    binary = std::move(bytes);
    return true;
  }
  bool save(const std::string& key, uint32_t format, const std::vector<uint8_t>& binary) const {
    if (root_.empty() || binary.empty() || binary.size() > 64 * 1024 * 1024) return false;
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error) return false;
    static std::atomic<uint64_t> serial{0};
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    auto temporary = path(key);
    temporary += ".tmp-" + std::to_string(stamp) + "-" + std::to_string(++serial);
    Header h;
    h.format = format; h.key_size = key.size(); h.binary_size = binary.size();
    h.checksum = hash(binary.data(), binary.size());
    {
      std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
      f.write(reinterpret_cast<const char*>(&h), sizeof(h));
      f.write(key.data(), key.size());
      f.write(reinterpret_cast<const char*>(binary.data()), binary.size());
      f.close();
      if (!f) { std::filesystem::remove(temporary, error); return false; }
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path(key).c_str(), MOVEFILE_REPLACE_EXISTING))
      error = std::error_code(GetLastError(), std::system_category());
#else
    std::filesystem::rename(temporary, path(key), error);
#endif
    if (error) { std::filesystem::remove(temporary, error); return false; }
    return true;
  }
};
} // namespace xlle
