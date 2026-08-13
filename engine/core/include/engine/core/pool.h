//
// Created by thanh.nguyen on 11/8/26.
//
#pragma once
#ifndef ENGINE_POOL_H
#define ENGINE_POOL_H
#include <concepts>
#include <vector>

#include "asserts.h"


namespace engine {
    template<class T>
    concept Poolable = std::movable<T>; // vector growth relocates slots by move

    // Typed handle: Handle<Mesh> is not convertible to Handle<Texture>.
    // 24-bit index (16.7M slots) + 8-bit generation. gen 0 is reserved as NULL,
    // so a default-constructed Handle is invalid forever.
    template<Poolable T>
    struct Handle {
        uint32_t index: 24 = 0; // C++20: default member init on bitfields
        uint32_t gen: 8 = 0;

        [[nodiscard]] bool is_null() const { return gen == 0; }

        bool operator==(const Handle &) const = default; // C++20: defaulted comparisons
    };

    template<Poolable T>
    class Pool {
    public:
        Pool() = default;

        Pool(const Pool &) = delete;

        Pool &operator=(const Pool &) = delete;

        template<class... Args>
        [[nodiscard]] Handle<T> emplace(Args &&... args) {
            uint32_t index;
            if (!free_.empty()) {
                index = free_.back();
                free_.pop_back();
            } else {
                engine_check(slots_.size() < (1u << 24)); // 24-bit index space
                index = static_cast<uint32_t>(slots_.size());
                slots_.emplace_back(); // gen=1, alive=false
            }

            Slot &s = slots_[index];
            ::new(&s.value) T(std::forward<Args>(args)...);
            s.alive = true;
            ++live_;
            return Handle<T>{.index = index, .gen = s.gen}; // C++20: designated init
        }

        [[nodiscard]] Handle<T> create(T &&v) { return emplace(std::move(v)); }

        void destroy(Handle<T> h) {
            Slot *s = resolve(*this, h);
            if (!engine_ensure(s)) // double-destroy / stale handle:
                return; // loud in debug, harmless in release
            s->value.~T();
            s->alive = false;
            s->gen = (s->gen == 255) ? 1 : static_cast<uint8_t>(s->gen + 1); // wrap SKIPS 0 (= null)
            --live_;
            free_.push_back(h.index);
        }

        // The SAFE accessor: nullptr on null/stale/destroyed. The everyday one.
        [[nodiscard]] T *get(Handle<T> h) {
            Slot *s = resolve(*this, h);
            return s ? &s->value : nullptr;
        }

        [[nodiscard]] const T *get(Handle<T> h) const {
            const Slot *s = resolve(*this, h);
            return s ? &s->value : nullptr;
        }

        // The ASSERTING accessor: for call sites where a stale handle is a bug.
        [[nodiscard]] T &get_checked(Handle<T> h) {
            T *p = get(h);
            engine_check(p);
            return *p;
        }

#ifdef __cpp_explicit_this_parameter
        // C++23: deducing this — one for_each serves Pool& and const Pool&,
        // deducing const-ness through `self`. Pre-23 this was two overloads
        // (or a CRTP dance). Feature-tested because compiler support is the
        // newest thing this codebase uses: MSVC 17.2+, Clang 18+, GCC 14+.
        template<class Self, class Fn>
        void for_each(this Self &&self, Fn &&fn) {
            for (auto &s: self.slots_)
                if (s.alive)
                    fn(s.value);
        }
#else
        // Fallback: the classic pre-23 overload pair — same behavior.
        template<class Fn>
        void for_each(Fn &&fn) {
            for (auto &s: slots_)
                if (s.alive) fn(s.value);
        }
        template<class Fn>
        void for_each(Fn &&fn) const {
            for (const auto &s: slots_)
                if (s.alive) fn(s.value);
        }
#endif

        [[nodiscard]] size_t live_count() const { return live_; }
        [[nodiscard]] size_t slot_count() const { return slots_.size(); }

    private:
        struct Slot {
            union {
                T value;
            }; // manual lifetime: constructed by emplace,
            // destroyed by destroy() or ~Slot below
            uint8_t gen = 1; // slots are BORN at gen 1 — gen 0 = null handle
            bool alive = false;

            Slot() noexcept {
            } // does NOT construct value
            Slot(Slot &&o) noexcept : gen(o.gen), alive(o.alive) {
                if (alive) ::new(&value) T(std::move(o.value)); // vector reallocation path
            }

            Slot &operator=(Slot &&) = delete;

            ~Slot() { if (alive) value.~T(); } // pool teardown
        };

        // One resolve serving const and non-const via a deduced Self —
        // same trick as for_each, just spelled as a static helper.
        template<class Self>
        static auto resolve(Self &self, Handle<T> h) {
            using SlotPtr = std::conditional_t<std::is_const_v<Self>, const Slot *, Slot *>;
            if (h.is_null() || h.index >= self.slots_.size())
                return SlotPtr{nullptr};
            auto &s = self.slots_[h.index];
            return (s.alive && s.gen == h.gen) ? SlotPtr{&s} : SlotPtr{nullptr};
        }

        std::vector<Slot> slots_;
        std::vector<uint32_t> free_;
        size_t live_ = 0;
    };
}
#endif //ENGINE_POOL_H
