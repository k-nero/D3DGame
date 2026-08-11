//
// Created by thanh.nguyen on 11/8/26.
//
#pragma once
#ifndef ENGINE_POOL_H
#define ENGINE_POOL_H
#include <cstdint>

namespace engine {
    template<class T>
    struct Handle {
        uint32_t index: 24 = 0;
        uint32_t gen: 8 = 0;
        [[nodiscard]] bool is_null() const { return gen == 0; } // gen 0 reserved = null handle
    };

    template<class T>
    class Pool {
    public:
        Handle<T> create(T &&value);

        void destroy(Handle<T>); // bumps generation, pushes to free list
        T *get(Handle<T>); // nullptr if stale/null — the SAFE accessor
        T &get_checked(Handle<T>); // check()s validity — the ASSERTING accessor

        template<class Fn>
        void for_each(Fn &&); // live items only
    };
}
#endif //ENGINE_POOL_H
