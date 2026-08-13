#include <doctest/doctest.h>

#include <engine/core/object.h>

#include <string_view>

using namespace engine;

namespace {
// 3-deep chain plus a sibling branch:  Object <- Actor <- Pawn <- Enemy
//                                                    ^--- Decor
class Actor : public Object { ENGINE_CLASS(Actor, Object) public: int hp = 0; };
class Pawn  : public Actor  { ENGINE_CLASS(Pawn, Actor)   };
class Enemy : public Pawn   { ENGINE_CLASS(Enemy, Pawn)  public: int dmg = 5; };
class Decor : public Actor  { ENGINE_CLASS(Decor, Actor)  };
} // namespace

TEST_CASE("is_a walks the whole parent chain") {
    auto e = make_ref<Enemy>();
    Object* base = e.get();                   // erased — dynamic type is the question

    CHECK(is_a<Enemy>(base));
    CHECK(is_a<Pawn>(base));
    CHECK(is_a<Actor>(base));
    CHECK(is_a<Object>(base));
}

TEST_CASE("sibling branches don't match") {
    auto e = make_ref<Enemy>();
    CHECK_FALSE(is_a<Decor>(e.get()));

    auto d = make_ref<Decor>();
    CHECK_FALSE(is_a<Pawn>(d.get()));
    CHECK_FALSE(is_a<Enemy>(d.get()));
    CHECK(is_a<Actor>(d.get()));
}

TEST_CASE("cast: typed pointer on match, nullptr across branches, nullptr-safe") {
    auto e = make_ref<Enemy>();
    Object* base = e.get();

    auto* back = cast<Enemy>(base);
    REQUIRE(back != nullptr);
    CHECK(back->dmg == 5);

    CHECK(cast<Decor>(base) == nullptr);
    CHECK(cast<Actor>(static_cast<Object*>(nullptr)) == nullptr);
    CHECK_FALSE(is_a<Actor>(nullptr));
}

TEST_CASE("one ClassID per class — address identity") {
    auto a = make_ref<Enemy>();
    auto b = make_ref<Enemy>();
    CHECK(a->get_class() == b->get_class());              // same static object
    CHECK(a->get_class() == Enemy::static_class());
    CHECK(a->get_class()->parent == Pawn::static_class()); // chain wiring
}

TEST_CASE("class_name and const cast overload") {
    auto e = make_ref<Enemy>();
    const Object* cbase = e.get();

    CHECK(std::string_view(cbase->class_name()) == "Enemy");

    const auto* ce = cast<Enemy>(cbase);
    CHECK(ce != nullptr);
}

TEST_CASE("SuperClass alias is wired") {
    CHECK(std::is_same_v<Enemy::SuperClass, Pawn>);
    CHECK(std::is_same_v<Actor::SuperClass, Object>);
}
