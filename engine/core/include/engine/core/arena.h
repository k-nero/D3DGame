//
// Created by thanh.nguyen on 11/8/26.
//
#pragma once

#ifndef ENGINE_ARENA_H
#define ENGINE_ARENA_H
#include <span>
#include <type_traits>

namespace engine {
    class Arena {
    public:
        explicit Arena(size_t capacity);      // one big malloc up front
        void*  push(size_t size, size_t align);
        void   reset();                       // ptr = base; nothing is destroyed
        [[nodiscard]] size_t used() const;

        template <class T, class... Args>
        T* create(Args&&... args) {           // placement-new convenience
            static_assert(std::is_trivially_destructible_v<T>,
                "arena types must not need destructors");
            return new (push(sizeof(T), alignof(T))) T(std::forward<Args>(args)...);
        }

        template <class T>
        std::span<T> push_array(size_t count);
    };
}
#endif //ENGINE_ARENA_H
