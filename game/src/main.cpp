#include "engine/core/log.h"

int main() {
	engine::log::init(true);
    engine::log::trace("{} {}!", "Hello", "world");
    engine::log::debug("{} {}!", "Hello", "world");
    engine::log::info("{} {}!", "Hello", "world");
    engine::log::warn("{} {}!", "Hello", "world");
    engine::log::error("{} {}!", "Hello", "world");
    engine::log::fatal("{} {}!", "Hello", "world");
	return 0;
}
