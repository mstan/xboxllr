// xlle — JsonLine unit test.
#include <cstdio>
#include <string>

#include "xlle/jsonl.hpp"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL: %s (line %d)\n", #c, __LINE__); ++fails; } } while (0)

int main() {
    std::string s = xlle::JsonLine().str("k", "a\"b").i("n", 5).hex("h", 0x1f).b("f", true).line();
    const std::string want = "{\"k\":\"a\\\"b\",\"n\":5,\"h\":\"0x1f\",\"f\":true}\n";
    CHECK(s == want);
    if (s != want) std::printf("got:  %swant: %s", s.c_str(), want.c_str());

    std::printf(fails ? "test_jsonl: %d checks failed\n" : "test_jsonl: ok\n", fails);
    return fails ? 1 : 0;
}
