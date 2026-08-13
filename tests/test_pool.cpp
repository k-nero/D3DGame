#include <doctest/doctest.h>

#include <engine/core/pool.h>

#include <memory>
#include <string>

using engine::Handle;
using engine::Pool;

namespace {
    struct Enemy {
        int hp = 0;
        std::string name; // non-trivial member: exercises Slot's manual lifetime
    };
} // namespace

TEST_CASE("create / get roundtrip") {
    Pool<Enemy> pool;
    auto h = pool.emplace(100, "grunt");

    Enemy *e = pool.get(h);
    REQUIRE(e != nullptr);
    CHECK(e->hp == 100);
    CHECK(e->name == "grunt");
    CHECK(pool.live_count() == 1);
}

TEST_CASE("default handle is null and never resolves") {
    Pool<Enemy> pool;
    (void) pool.emplace(1, "a"); // slot 0 exists and is alive...

    constexpr Handle<Enemy> null_h{}; // ...but index 0 + gen 0 must NOT find it
    CHECK(null_h.is_null());
    CHECK(pool.get(null_h) == nullptr);
}

TEST_CASE("destroy: old handle goes stale, memory is reused under a new identity") {
    Pool<Enemy> pool;
    auto h1 = pool.emplace(50, "first");
    pool.destroy(h1);

    CHECK(pool.get(h1) == nullptr); // stale handle -> nullptr, not garbage
    CHECK(pool.live_count() == 0);

    auto h2 = pool.emplace(75, "second");
    auto h1_idx = h1.index;
    CHECK(h2.index == h1_idx); // same slot reused...
    CHECK(h2 != h1); // ...different identity (gen bumped)
    CHECK(pool.get(h1) == nullptr); // old handle STILL dead
    CHECK(pool.get(h2)->hp == 75);
}

TEST_CASE("generation wrap skips 0 (the null sentinel)") {
    Pool<int> pool;
    for (int i = 0; i < 300; ++i) {
        // 300 > 255: forces a full gen cycle on slot 0
        auto h = pool.create(std::forward<int>(i));
        CHECK(h.gen != 0); // THE invariant: live handle never gen 0
        CHECK(!h.is_null());
        CHECK(*pool.get(h) == i);
        pool.destroy(h);
        CHECK(pool.get(h) == nullptr); // immediate staleness every cycle
    }
    CHECK(pool.live_count() == 0);
    CHECK(pool.slot_count() == 1); // it really was one slot the whole time
}

TEST_CASE("for_each visits live only; const overload deduces const") {
    Pool<int> pool;
    auto a = pool.create(1);
    auto b = pool.create(2);
    auto c = pool.create(3);
    pool.destroy(b);

    int sum = 0;
    pool.for_each([&](const int &v) { sum += v; });
    CHECK(sum == 4); // 1 + 3, dead slot skipped

    const Pool<int> &cpool = pool;
    sum = 0;
    cpool.for_each([&](const int &v) { sum += v; });
    CHECK(sum == 4);

    pool.destroy(a);
    pool.destroy(c);
}

TEST_CASE("get_checked returns a reference for valid handles") {
    Pool<int> pool;
    const auto h = pool.create(9);
    pool.get_checked(h) = 11; // writable reference
    CHECK(*pool.get(h) == 11);
    // stale get_checked is a check() abort — verified manually, not in ctest
}

TEST_CASE("move-only types satisfy Poolable; growth relocates live slots") {
    Pool<std::unique_ptr<int> > pool;

    // enough creates to force several vector reallocations while slots are alive,
    // exercising Slot's move constructor path
    Handle<std::unique_ptr<int> > handles[64];
    for (int i = 0; i < 64; ++i)
        handles[i] = pool.emplace(std::make_unique<int>(i));

    for (int i = 0; i < 64; ++i) {
        auto *p = pool.get(handles[i]);
        REQUIRE(p != nullptr);
        CHECK(**p == i); // values survived relocation intact
    }

    for (auto &h: handles) pool.destroy(h);
    CHECK(pool.live_count() == 0);
}

TEST_CASE("typed handles: Handle<A> does not cross-resolve pools of A") {
    Pool<int> ints;
    Pool<int> other_ints;
    const auto h = ints.create(5);

    // Different pools of the SAME type share a handle type — the pool is the
    // namespace. This documents that handles are only meaningful with the pool
    // that minted them; gen matching makes the wrong pool return null here,
    // but that's luck, not a guarantee. Rule: one pool per handle domain.
    CHECK(other_ints.get(h) == nullptr); // empty pool: index out of range
    ints.destroy(h);
}
