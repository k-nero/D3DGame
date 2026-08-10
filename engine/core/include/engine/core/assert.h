//
// Created by thanh.nguyen on 10/8/26.
//

#ifndef ENGINE_ASSERT_H
#define ENGINE_ASSERT_H

#define ENG_DEBUG_BREAK() /* __debugbreak() on MSVC, __builtin_debugtrap() on clang */

#define check(expr) \
    do { if (!(expr)) { \
        eng::on_check_failed(#expr, __FILE__, __LINE__); \
        ENG_DEBUG_BREAK(); std::abort(); \
    } } while (0)

#define ensure(expr) \
    ( (expr) ? true : (eng::on_ensure_failed(#expr, __FILE__, __LINE__), false) )
#endif //ENGINE_ASSERT_H
