//
// Created by thanh.nguyen on 11/8/26.
//
#pragma once

#ifndef ENGINE_ARENA_H
#define ENGINE_ARENA_H
#include <algorithm>
#include <memory>
#include <span>
#include <type_traits>

#include "api.h"

namespace engine::arena {
    class ENGINE_API Arena {
    public:
        // Opaque bookmark for scoped scratch allocations within a frame.
        struct Marker {
            size_t offset;
        };

        // C++20: make_unique_for_overwrite — allocates WITHOUT value-initializing,
        // i.e. no memset over the whole capacity. Plain make_unique<std::byte[]>
        // would zero every byte of a multi-MB arena for nothing.
        explicit Arena(const size_t capacity)
            : base_(std::make_unique_for_overwrite<std::byte[]>(capacity)),
              capacity_(capacity) {
        }

        // An allocator with copy semantics is a bug factory — forbid it.
        Arena(const Arena &) = delete;

        Arena &operator=(const Arena &) = delete;

        [[nodiscard]] void *push(size_t size, size_t align);

        template<class T, class... Args>
        [[nodiscard]] T *create(Args &&... args) {
            static_assert(std::is_trivially_destructible_v<T>,
                          "arena memory never runs destructors — this type needs one. "
                          "If it owns resources (string, vector, Ref), it does not belong in the frame arena.");
            return ::new(push(sizeof(T), alignof(T))) T(std::forward<Args>(args)...);
        }

        // Value-initialized array ({} per element: zeros for arithmetic/pointer types).
        // For hot paths that will overwrite every element anyway, see push_array_uninit.
        template<class T>
        [[nodiscard]] std::span<T> push_array(const size_t count) {
            std::span<T> s = push_array_uninit<T>(count);
            for (T &e: s) ::new(&e) T{};
            return s;
        }

        template<class T>
        [[nodiscard]] std::span<T> push_array_uninit(size_t count) {
            static_assert(std::is_trivially_destructible_v<T>,
                          "arena memory never runs destructors");
            static_assert(std::is_trivially_default_constructible_v<T>,
                          "uninit arrays are only safe for trivially-constructible types");
            T *p = static_cast<T *>(push(sizeof(T) * count, alignof(T)));
            return {p, count};
        }

        void reset(); // frees EVERYTHING. one instruction.
        [[nodiscard]] Marker mark() const;

        void pop_to(Marker m);

        [[nodiscard]] size_t used() const { return offset_; }
        [[nodiscard]] size_t high_water() const { return high_water_; } // size the arena from this
        [[nodiscard]] size_t capacity() const { return capacity_; }

    private:
        std::unique_ptr<std::byte[]> base_;
        size_t capacity_ = 0;
        size_t offset_ = 0;
        size_t high_water_ = 0;
    };
}
#endif //ENGINE_ARENA_H
