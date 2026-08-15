//
// Created by thanh.nguyen on 10/8/26.
//
#pragma once

#ifndef ENGINE_ASSERT_H
#define ENGINE_ASSERT_H
#include <cstdlib>
#define ENGINE_DEBUG_BREAK() /* __debugbreak() on MSVC, __builtin_debugtrap() on clang */
#define engine_check(expr) \
    do { if (!(expr)) { \
        engine::assert::on_check_failed(#expr, __FILE__, __LINE__); \
        ENGINE_DEBUG_BREAK(); std::abort(); \
    } } while (0)

#define engine_ensure(expr) \
    ( (expr) ? true : (engine::assert::on_ensure_failed(#expr, __FILE__, __LINE__), false) )

namespace engine::assert {
    void on_check_failed(const char *expr, const char *file, int line);

    void on_ensure_failed(const char *expr, const char *file, int line);
}
#endif //ENGINE_ASSERT_H
