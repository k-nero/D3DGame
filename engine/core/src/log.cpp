#include "engine/core/log.h"
#include "engine/core/color.h"

#include <iostream>
#include <ostream>
#include <chrono>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace engine {
    void log_msg(const LogLevel lv, const std::string_view str) {
        auto sec_precision = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        const std::string time = std::format("{:%FT%TZ}", sec_precision);
        switch (lv) {
            case LogLevel::Trace:
                std::cout << GRAY << time << RESET << WHITE << " TRC " << RESET << str << std::endl;
                break;
            case LogLevel::Info:
                std::cout << GRAY << time << RESET << GREEN << " INF " << RESET << str << std::endl;
                break;
            case LogLevel::Warn:
                std::cout << GRAY << time << RESET << YELLOW << " WRN " << RESET << str << std::endl;
                break;
            case LogLevel::Error:
                std::cout << GRAY << time << RESET << RED << " ERR " << RESET << str << std::endl;
                break;
            default:
                std::cout << GRAY << time << RESET << " UKN " << str << std::endl;
        }
    }

    inline bool stdout_is_terminal() {
#ifdef _WIN32
        return _isatty(_fileno(stdout)) != 0; // <io.h>
#else
        return isatty(fileno(stdout)) != 0; // <unistd.h>
#endif
    }
}
