#include "engine/core/log.h"
#include "engine/core/color.h"

#include <iostream>
#include <ostream>
#include <chrono>

namespace engine {
    void log_msg(const LogLevel lv, const std::string_view str) {
        auto sec_precision = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
		std::string time = std::format("[{}]", std::chrono::zoned_time{ std::chrono::current_zone(), sec_precision });
        switch (lv) {
            case LogLevel::Trace:
                std::cout << CYAN << time << " [TRACE]: " << RESET << str << std::endl;
                break;
            case LogLevel::Info:
                std::cout << GREEN << time << " [INFO]: " << RESET << str << std::endl;
                break;
            case LogLevel::Warn:
                std::cout << YELLOW << time << " [WARN]: " << RESET << str << std::endl;
                break;
            case LogLevel::Error:
                std::cout << RED << time << " [ERROR]: " << RESET << str << std::endl;
                break;
            default:
                std::cout << time << " [UNKNOWN]: " << str << std::endl;
        }
    }
}
