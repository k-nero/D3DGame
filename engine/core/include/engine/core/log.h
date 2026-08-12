//
// Created by thanh.nguyen on 10/8/26.
//
#pragma once


#ifndef ENGINE_LOG_H
#define ENGINE_LOG_H
#include <format>
#include <boost/log/trivial.hpp>

namespace engine::log {
    void use_console_log_sink(bool enable_colors);
    void use_file_log_sink();
#ifdef _WIN32
    void use_debug_log_sink();
#endif
    void init(boost::log::trivial::severity_level level, bool enable_colors);

    template<class... Args>
    void trace(std::format_string<Args...> fmt, Args &&... args) {
        BOOST_LOG_TRIVIAL(trace) << std::format(fmt, std::forward<Args>(args)...);
    }

    template<class... Args>
    void debug(std::format_string<Args...> fmt, Args &&... args) {
        BOOST_LOG_TRIVIAL(debug) << std::format(fmt, std::forward<Args>(args)...);
    }

    template<class... Args>
    void info(std::format_string<Args...> fmt, Args &&... args) {
        BOOST_LOG_TRIVIAL(info) << std::format(fmt, std::forward<Args>(args)...);
    }

    template<class... Args>
    void warn(std::format_string<Args...> fmt, Args &&... args) {
        BOOST_LOG_TRIVIAL(warning) << std::format(fmt, std::forward<Args>(args)...);
    }

    template<class... Args>
    void error(std::format_string<Args...> fmt, Args &&... args) {
        BOOST_LOG_TRIVIAL(error) << std::format(fmt, std::forward<Args>(args)...);
    }

    template<class... Args>
    void fatal(std::format_string<Args...> fmt, Args &&... args) {
        BOOST_LOG_TRIVIAL(fatal) << std::format(fmt, std::forward<Args>(args)...);
    }
}
#endif //ENGINE_LOG_H
