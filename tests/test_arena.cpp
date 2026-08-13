#include <doctest/doctest.h>

#include <engine/core/arena.h>

using engine::Arena;

TEST_CASE("push respects alignment across mixed sizes") {
    Arena a(4096);
    // deliberately misalign the cursor first
    (void)a.push(1, 1);

    for (const size_t align : {size_t{1}, size_t{2}, size_t{4}, size_t{8}, size_t{16}, size_t{64}}) {
        void* p = a.push(3, align);   // odd size keeps forcing realignment next round
        CHECK(reinterpret_cast<uintptr_t>(p) % align == 0);
    }
}

TEST_CASE("reset reuses the same memory") {
    Arena a(1024);
    void* first = a.push(100, 8);
    a.reset();
    CHECK(a.used() == 0);
    void* again = a.push(100, 8);
    CHECK(first == again);
}

TEST_CASE("marker / pop_to gives scoped scratch space") {
    Arena a(1024);
    (void)a.push(64, 8);
    const size_t before = a.used();

    auto m = a.mark();
    (void)a.push(256, 16);
    CHECK(a.used() > before);

    a.pop_to(m);
    CHECK(a.used() == before);
}

TEST_CASE("high_water survives reset — it's how you size the arena") {
    Arena a(1024);
    (void)a.push(500, 8);
    a.reset();
    (void)a.push(10, 8);
    CHECK(a.high_water() >= 500);
    CHECK(a.used() == 10);
}

TEST_CASE("create constructs in place") {
    struct Vec { float x, y, z; };
    Arena a(256);
    Vec* v = a.create<Vec>(1.f, 2.f, 3.f);
    CHECK(v->y == 2.f);
    CHECK(reinterpret_cast<uintptr_t>(v) % alignof(Vec) == 0);
}

TEST_CASE("push_array: correct extent, value-initialized") {
    Arena a(4096);
    auto s = a.push_array<uint32_t>(128);
    CHECK(s.size() == 128);
    for (uint32_t x : s) CHECK(x == 0);      // T{} zeroed them

    s[0] = 42; s[127] = 7;                   // and it's writable to the edges
    CHECK(s[0] + s[127] == 49);
}

TEST_CASE("push_array_uninit: correct extent and alignment") {
    Arena a(4096);
    auto s = a.push_array_uninit<double>(16);
    CHECK(s.size() == 16);
    CHECK(reinterpret_cast<uintptr_t>(s.data()) % alignof(double) == 0);
}

// Compile-time contract checks: these must NOT compile if uncommented.
// TEST_CASE("arena rejects destructor-owning types") {
//     Arena a(256);
//     a.create<std::string>("boom");            // static_assert fires
//     a.push_array<std::vector<int>>(4);        // static_assert fires
// }
