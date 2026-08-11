//
// Created by thanh.nguyen on 11/8/26.
//

#include "engine/core/arena.h"

namespace engine {
    template<class T>
    std::span<T> Arena::push_array(size_t count) {
        return ;
    }

    void *Arena::push(size_t size, size_t align) {
        return nullptr;
    }

    void Arena::reset() {

    }

    size_t Arena::used() const {
        return 0;
    }

    Arena::Arena(size_t capacity) {

    }
}
