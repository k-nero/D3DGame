#include <boost/log/attributes/named_scope.hpp>

#include "engine/core/log.h"

int main() {
	engine::log::init(boost::log::trivial::trace, true);
	BOOST_LOG_FUNCTION();
    engine::log::trace("{} {}!", "Hello", "world");
    engine::log::debug("{} {}!", "Hello", "world");
    engine::log::info("{} {}!", "Hello", "world");
    engine::log::warn("{} {}!", "Hello", "world");
    engine::log::error("{} {}!", "Hello", "world");
	return 0;
}
