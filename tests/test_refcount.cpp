#include <doctest/doctest.h>

#include <engine/core/refcount.h>

#include <utility>

using engine::Ref;
using engine::RefCounted;
using engine::make_ref;

namespace {
    int destroyed = 0;

    struct Counter : RefCounted {
        ~Counter() override { ++destroyed; }
        int v = 7;
    };
} // namespace

TEST_CASE("make_ref adopts the constructor's reference; scope exit destroys once") {
    destroyed = 0;
    {
        const auto r = make_ref<Counter>();
        CHECK(r->ref_count() == 1); // NOT 2 — Ref(T*) adopts, never add_refs
    }
    CHECK(destroyed == 1);
}

TEST_CASE("copy add_refs, move steals, destruction happens exactly once") {
    destroyed = 0;
    {
        auto a = make_ref<Counter>();
        Ref<Counter> b = a; // copy
        CHECK(a->ref_count() == 2);

        Ref<Counter> c = std::move(b); // move: no count change
        CHECK_FALSE(static_cast<bool>(b));
        CHECK(c->ref_count() == 2);

        a = Ref<Counter>{}; // drop one
        CHECK(c->ref_count() == 1);
        CHECK(destroyed == 0);
    }
    CHECK(destroyed == 1); // last Ref out
}

TEST_CASE("self-assignment is safe (copy-and-swap)") {
    destroyed = 0;
    auto r = make_ref<Counter>();
    r = r; // must not release-then-use
    CHECK(r->ref_count() == 1);
    CHECK(r->v == 7);
    r.reset();
    CHECK(destroyed == 1);
}

TEST_CASE("Ref<const T> counts through const") {
    destroyed = 0;
    {
        Ref<const Counter> rc{new Counter}; // explicit adoption
        CHECK(rc->v == 7);
        Ref<const Counter> rc2 = rc;
        CHECK(rc->ref_count() == 2);
    }
    CHECK(destroyed == 1);
}

TEST_CASE("reset releases immediately") {
    destroyed = 0;
    auto r = make_ref<Counter>();
    r.reset();
    CHECK_FALSE(static_cast<bool>(r));
    CHECK(destroyed == 1);
}

// Concept contract — must NOT compile if uncommented:
// TEST_CASE("Ref rejects non-RefCounted types") {
//     struct Plain { int x; };
//     eng::Ref<Plain> bad;                   // constraint 'RefCountable' not satisfied
// }
