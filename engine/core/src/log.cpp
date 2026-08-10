#include "engine/core/log.h"
#include "engine/core/color.h"

#include <iostream>
#include <ostream>

namespace engine {
    void log_msg(const LogLevel lv, const std::string_view str) {
        switch (lv) {
            case LogLevel::Trace:
                std::cout << CYAN << "[TRACE]: " << str << RESET << std::endl;
                break;
            case LogLevel::Info:
                std::cout << GREEN << "[INFO]: " << str << RESET << std::endl;
                break;
            case LogLevel::Warn:
                std::cout << YELLOW << "[WARN]: " << str << RESET << std::endl;
                break;
            case LogLevel::Error:
                std::cout << RED << "[ERROR]: " << str << RESET << std::endl;
                break;
            default:
                std::cout << "[UNKNOWN]: " << str << std::endl;
        }
    }
}
