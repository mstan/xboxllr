// xlle — heap poisoning, the decidable test for "uninitialised host memory reaches the guest"
// (beads-xbox.1.14.11).
//
// WHY THIS EXISTS. A run-to-run trajectory difference with an identical binary, identical argv
// and a zero-filled guest RAM has only a few possible sources, and one of them -- a host buffer
// that is allocated but not fully written before its bytes are handed to the guest -- is
// invisible to every digest until the guest branches on it, potentially billions of
// instructions later. Whatever the allocator happened to leave in that buffer is a function of
// the process's allocation history, so it changes when anything else about the build changes,
// and it can change between runs of the SAME build (freed-block contents, address-space layout,
// OS page recycling).
//
// HOW IT DECIDES. Replace the global operator new so every allocation starts as a known byte
// pattern, chosen per run. Run the same binary and argv twice with two DIFFERENT patterns:
//
//   * if the two runs produce the same guest trajectory, no uninitialised heap byte reached the
//     guest on that path -- the pattern is the only thing that changed and it changed nothing;
//   * if they diverge, some host allocation is read before it is written and its content is
//     guest-visible. The divergence point localises it.
//
// A pattern of 0x00 also gives a "zeroed heap" arm, which is what a fix must make
// indistinguishable from every other pattern.
//
// COST. Off by default and off in every normal run: with no pattern selected this is malloc()
// plus one relaxed bool test, so the replacement costs nothing measurable. It is selected by
// the environment variable XLLE_HEAP_POISON (a byte, e.g. 0xA5 or 165), read once, because
// allocations happen during static initialisation, long before argv is parsed.
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

// -1 = not yet resolved, -2 = disabled, 0..255 = the poison byte.
int g_poison_state = -1;

int poison_byte() {
    if (g_poison_state != -1) return g_poison_state;
    const char* env = std::getenv("XLLE_HEAP_POISON");
    if (!env || !env[0]) {
        g_poison_state = -2;
        return g_poison_state;
    }
    char* end = nullptr;
    const long v = std::strtol(env, &end, 0);
    if (end == env || v < 0 || v > 255) {
        g_poison_state = -2;
        return g_poison_state;
    }
    g_poison_state = static_cast<int>(v);
    return g_poison_state;
}

inline void* allocate(std::size_t n) {
    if (n == 0) n = 1;
    void* p = std::malloc(n);
    if (!p) return nullptr;
    const int b = poison_byte();
    if (b >= 0) std::memset(p, b, n);
    return p;
}

}  // namespace

bool xlle_heap_poison_enabled() { return poison_byte() >= 0; }
int xlle_heap_poison_byte() { return poison_byte(); }

void* operator new(std::size_t n) {
    void* p = allocate(n);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n) {
    void* p = allocate(n);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return allocate(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return allocate(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
