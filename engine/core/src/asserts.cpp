#include "engine/core/asserts.h"
#include "engine/core/log.h"

namespace engine::assert {
    void on_check_failed(const char *expr, const char *file, int line) {
        log::error("Check failed: {} at {}:{}", expr, file, line);
    }

    void on_ensure_failed(const char *expr, const char *file, int line) {
        log::error("Ensure failed: {} at {}:{}", expr, file, line);
    }
}