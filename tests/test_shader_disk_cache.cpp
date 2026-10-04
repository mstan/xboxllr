#include "host/shader_disk_cache.hpp"
#include <cstdio>
#include <cstdlib>
static void check(bool ok, const char* why) {
  if (!ok) { std::fprintf(stderr, "%s\n", why); std::exit(1); }
}
int main() {
  using namespace xlle;
  auto dir = std::filesystem::temp_directory_path() /
      ("xlle-shader-test-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
  ShaderDiskCache cache(dir);
  std::string key("driver\0vertex\0fragment", 22);
  std::vector<uint8_t> bytes{1, 2, 3, 4}, result;
  uint32_t format = 0;
  check(!cache.load(key, format, result), "absent cache must miss");
  check(cache.save(key, 7, bytes) && cache.load(key, format, result) && format == 7 && result == bytes, "binary round trip");
  check(!cache.load(key + "changed", format, result), "source/driver change must miss");
  auto file = std::filesystem::directory_iterator(dir)->path();
  // An unrelated full key at the same hashed filename must never load.
  std::fstream edit(file, std::ios::binary | std::ios::in | std::ios::out);
  edit.seekp(40); edit.put('X'); edit.close();
  check(!cache.load(key, format, result), "full key mismatch must miss");
  check(cache.save(key, 7, bytes), "replace corrupt cache atomically");
  edit.open(file, std::ios::binary | std::ios::in | std::ios::out);
  edit.seekp(-1, std::ios::end); edit.put('X'); edit.close();
  check(!cache.load(key, format, result), "corrupt binary must miss");
  std::filesystem::resize_file(file, 5);
  check(!cache.load(key, format, result), "truncated cache must miss");
  check(cache.save(key, 8, bytes) && cache.load(key, format, result) && format == 8, "repair truncated cache");
  std::filesystem::remove(file);
  std::filesystem::remove(dir);
  std::puts("shader disk cache identity/corruption/replacement passed");
}
