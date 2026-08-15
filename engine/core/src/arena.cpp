//
// Created by thanh.nguyen on 11/8/26.
//

#include "engine/core/arena.h"

#include <bit>

#include "engine/core/asserts.h"

namespace engine::arena {
    void Arena::pop_to(const Marker m) {
        engine_check(m.offset <= offset_); // popping "forward" = stale marker
        offset_ = m.offset;
    }

    Arena::Marker Arena::mark() const { return {offset_}; }

    void Arena::reset() { offset_ = 0; }

    void *Arena::push(const size_t size, const size_t align) {
        engine_check(std::has_single_bit(align)); // alignment must be a power of two
        // TRAP (caught by the alignment test): align the ADDRESS, not the offset.
        // operator new[] only guarantees __STDCPP_DEFAULT_NEW_ALIGNMENT__ (16) for
        // base_, so an aligned *offset* from an unaligned *base* is still unaligned
        // for align > 16.
        const auto base = reinterpret_cast<uintptr_t>(base_.get());
        const uintptr_t aligned = (base + offset_ + align - 1) & ~(align - 1);

        const size_t new_offset = (aligned - base) + size;
        engine_check(new_offset <= capacity_); // out of frame memory is a BUG,
        // not a grow event — see design notes
        offset_ = new_offset;
        high_water_ = std::max(high_water_, offset_);
        return reinterpret_cast<void *>(aligned);
    }
}
