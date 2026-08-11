#include "engine/core/arena.h"
#include "engine/core/log.h"
#include "engine/core/pool.h"

int main() {
    engine::log_trace("{} {}!", "Hello", "world");
    engine::log_info("{} {}!", "Hello", "world");
    engine::log_warn("{} {}!", "Hello", "world");
    engine::log_error("{} {}!", "Hello", "world");
	return 0;
}
