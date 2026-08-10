#include "engine/core/log.h"

int main() {
    engine::log_trace("{} {}!", "Hello", "world");
    engine::log_info("{} {}!", "Hello", "world");
    engine::log_warn("{} {}!", "Hello", "world");
    engine::log_error("{} {}!", "Hello", "world");
}
