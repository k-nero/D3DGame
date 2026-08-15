//
// Created by thanh.nguyen on 13/8/26.
//
#pragma once
#ifndef ENGINE_OBJECT_H
#define ENGINE_OBJECT_H
#include "refcount.h"

namespace engine::object {
    struct ClassId {
        const char *name; // string literal — static storage, nobody frees it
        const ClassId *parent; // another function-local static — same deal
    };

    class Object : public refcount::RefCounted {
    public:
        static const ClassId *static_class() {
            // Function-local static: lazily initialized, thread-safe, and immune
            // to the cross-TU static-initialization-order fiasco — a parent's ID
            // always exists by the time a child's initializer asks for it.
            static constexpr ClassId id{.name = "Object", .parent = nullptr};
            return &id;
        }

        virtual const ClassId *get_class() const { return static_class(); }

        [[nodiscard]] const char *class_name() const { return get_class()->name; }

    protected:
        Object() = default;
    };

    // Put this at the top of every Object-derived class body:
    //
    //   class Actor : public Object { ENG_CLASS(Actor, Object)
    //   public: ...
    //
    // Provides: SuperClass alias, static_class(), get_class() override.
    // REMINDER: Super must be the ONLY Object-derived base (see rule above).
#define ENGINE_CLASS(Type, Super)                                          \
public:                                                                    \
    using SuperClass = Super;                                              \
    static const ::engine::object::ClassId* static_class() {                       \
        static const ::engine::object::ClassId id{#Type, Super::static_class()};   \
        return &id;                                                        \
    }                                                                      \
    const ::engine::object::ClassId* get_class() const override {                  \
        return static_class();                                             \
    }                                                                      \
private:

    // "Is this OBJECT (dynamic type) a T?" — the runtime question type traits
    // can't answer once you've erased to Object*. Pointer-compare loop over a
    // chain that's typically 2-4 deep.
    template<std::derived_from<Object> T>
    [[nodiscard]] bool is_a(const Object *o) {
        if (!o) return false;
        for (const ClassId *c = o->get_class(); c; c = c->parent)
            if (c == T::static_class())
                return true;
        return false;
    }

    // Checked downcast: nullptr when the dynamic type doesn't match.
    // The engine's dynamic_cast replacement (built with /GR- in mind).
    template<std::derived_from<Object> T>
    [[nodiscard]] T *cast(Object *o) {
        return is_a<T>(o) ? static_cast<T *>(o) : nullptr;
    }

    template<std::derived_from<Object> T>
    [[nodiscard]] const T *cast(const Object *o) {
        return is_a<T>(o) ? static_cast<const T *>(o) : nullptr;
    }
}

#endif //ENGINE_OBJECT_H
