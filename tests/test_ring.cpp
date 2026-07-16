// xlle — Ring unit test.
#include <cstdio>
#include <vector>

#include "xlle/ring.hpp"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL: %s (line %d)\n", #c, __LINE__); ++fails; } } while (0)

int main() {
    xlle::Ring<int> r(8);
    for (int i = 0; i < 10; ++i) r.push(i);  // overwrites oldest two

    CHECK(r.total() == 10);
    CHECK(r.capacity() == 8);

    std::vector<int> w;
    CHECK(r.window(3, w) == 3);
    CHECK(w.size() == 3 && w[0] == 7 && w[1] == 8 && w[2] == 9);  // most-recent 3, oldest-first

    CHECK(r.window(100, w) == 8);  // clamps to retained capacity
    CHECK(w.front() == 2 && w.back() == 9);

    std::printf(fails ? "test_ring: %d checks failed\n" : "test_ring: ok\n", fails);
    return fails ? 1 : 0;
}
