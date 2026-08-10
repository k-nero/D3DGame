//
// Created by thanh.nguyen on 10/8/26.
//

#ifndef ENGINE_LOG_H
#define ENGINE_LOG_H
#include <format>

namespace engine {
    enum class LogLevel { Trace, Info, Warn, Error };

    void log_msg(LogLevel, std::string_view);

    template<class... Args>
    void log_trace(std::format_string<Args...> fmt, Args &&... args) {
        log_msg(LogLevel::Trace, std::format(fmt, std::forward<Args>(args)...));
    }

    template<class... Args>
    void log_info(std::format_string<Args...> fmt, Args &&... args) {
        log_msg(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
    }

    template<class... Args>
    void log_warn(std::format_string<Args...> fmt, Args &&... args) {
        log_msg(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
    }

    template<class... Args>
    void log_error(std::format_string<Args...> fmt, Args &&... args) {
        log_msg(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
    }
}
#endif //ENGINE_LOG_H
