#include "engine/core/log.h"

int main() {
	engine::log::init(boost::log::trivial::trace, true);
    engine::log::trace("{} {}!", "Hello", "world");
    engine::log::debug("{} {}!", "Hello", "world");
    engine::log::info("{} {}!", "Hello", "world");
    engine::log::warn("{} {}!", "Hello", "world");
    engine::log::error("{} {}!", "Hello", "world");
	return 0;
}
