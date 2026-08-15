//
// Created by thanh.nguyen on 13/8/26.
//
#pragma once

#ifndef ENGINE_REFCOUNT_H
#define ENGINE_REFCOUNT_H
#include <atomic>
#include <concepts>         // C++20: std::derived_from
#include <cstdint>
#include <type_traits>
#include <utility>

#include "asserts.h"


namespace engine::refcount {
    class RefCounted {
    public:
        RefCounted(const RefCounted &) = delete; // count is identity-bound;
        RefCounted &operator=(const RefCounted &) = delete; // copying it is always a bug

        void add_ref() const {
            refs_.fetch_add(1, std::memory_order_relaxed);
        }

        void release() const {
            // acq_rel: the thread that deletes must observe all writes made by
            // other threads before they released. Single-threaded until
            // milestone 10, but this is the version we'd write then anyway.
            if (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1)
                delete this;
        }

        [[nodiscard]] uint32_t ref_count() const {
            // debugging/tests only
            return refs_.load(std::memory_order_relaxed);
        }

    protected:
        RefCounted() = default;

        virtual ~RefCounted() = default; // delete-through-base needs this

    private:
        // mutable + const methods: a Ref<const T> can still count references.
        mutable std::atomic<uint32_t> refs_{1};
    };

    // C++20 concept: constrain at the declaration, error at the call site.
    // derived_from (not is_base_of_v) also rejects PRIVATE inheritance, which
    // would otherwise fail obscurely inside release().
    template<class T>
    concept RefCountable = std::derived_from<std::remove_const_t<T>, RefCounted>;

    template<RefCountable T>
    class Ref {
    public:
        Ref() = default;

        explicit Ref(std::nullptr_t) {
        }

        // explicit on purpose: adoption must be visible at the call site.
        explicit Ref(T *p) : ptr_(p) {
        } // ADOPTS (see convention above)

        Ref(const Ref &o) : ptr_(o.ptr_) { if (ptr_) ptr_->add_ref(); }

        Ref(Ref &&o) noexcept : ptr_(std::exchange(o.ptr_, nullptr)) {
        }

        // Copy-and-swap: one operator= covers copy-assign, move-assign, and
        // self-assign correctly. `o` arrives as a copy (or move) and carries
        // the old pointer out to be released by its destructor.
        Ref &operator=(Ref o) noexcept {
            std::swap(ptr_, o.ptr_);
            return *this;
        }

        ~Ref() { if (ptr_) ptr_->release(); }

        [[nodiscard]] T *get() const { return ptr_; }

        T *operator->() const {
            engine_check(ptr_);
            return ptr_;
        }

        T &operator*() const {
            engine_check(ptr_);
            return *ptr_;
        }

        explicit operator bool() const { return ptr_ != nullptr; }

        void reset() { Ref{}.swap(*this); }
        void swap(Ref &o) noexcept { std::swap(ptr_, o.ptr_); }

        bool operator==(const Ref &) const = default; // C++20: defaulted comparison

    private:
        T *ptr_ = nullptr;
    };

    template<RefCountable T, class... Args>
    [[nodiscard]] Ref<T> make_ref(Args &&... args) {
        return Ref<T>(new T(std::forward<Args>(args)...)); // ctor's ref = the one we adopt
    }
}
#endif //ENGINE_REFCOUNT_H
