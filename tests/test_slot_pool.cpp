// Generational handles: a stale handle never resolves, even after its slot is reused.
#include "raw/core/slot_pool.hpp"
#include "check.hpp"
#include <string>
using namespace raw;
int main(){
    SlotPool<std::string> pool;
    auto a = pool.insert("a"), b = pool.insert("b");
    CHECK(a.gen != 0 && b.gen != 0 && a.index != b.index);
    CHECK(pool.get(a.index, a.gen) && *pool.get(a.index, a.gen) == "a");
    CHECK(pool.get(a.index, 0) == nullptr);
    auto gone = pool.erase(a.index, a.gen);
    CHECK(gone && *gone == "a");
    CHECK(pool.get(a.index, a.gen) == nullptr);
    CHECK(!pool.erase(a.index, a.gen));
    auto c = pool.insert("c");                  // reuses a's slot with a new generation
    CHECK(c.index == a.index && c.gen != a.gen);
    CHECK(pool.get(a.index, a.gen) == nullptr);
    CHECK(*pool.get(c.index, c.gen) == "c");
    CHECK(pool.get(99, 1) == nullptr);
    CHECK(pool.live() == 2);
    return raw_test_summary();
}
