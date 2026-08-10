#include "engine/core/assert.h"
#include "engine/core/log.h"

namespace engine {
	void on_check_failed(const char* expr, const char* file, int line) {
		log_error("Check failed: {} at {}:{}", expr, file, line);
	}

	void on_ensure_failed(const char* expr, const char* file, int line) {
		log_error("Ensure failed: {} at {}:{}", expr, file, line);
	}
}