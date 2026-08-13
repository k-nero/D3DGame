#include "engine/core/log.h"

#include <iostream>
#include <ostream>
#include <iomanip>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/core/null_deleter.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/phoenix/bind.hpp>

#include "engine/core/color.h"

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#include <boost/log/sinks/debug_output_backend.hpp>
#else
#include <unistd.h>
#endif

namespace logging = boost::log;
namespace sinks = boost::log::sinks;
namespace expr = boost::log::expressions;

namespace engine::log {
    namespace {
        // ANSI escape codes per severity
        const char *color_for(const boost::log::trivial::severity_level lvl) {
            switch (lvl) {
                case boost::log::trivial::trace: return GRAY; // bright black (gray)
                case boost::log::trivial::debug: return CYAN; // cyan
                case boost::log::trivial::info: return GREEN; // green
                case boost::log::trivial::warning: return YELLOW; // yellow
                case boost::log::trivial::error: return RED; // red
                case boost::log::trivial::fatal: return BRIGHT_RED; // white on red
                default: return "";
            }
        }

        constexpr auto reset = RESET;

        bool enable_vt_mode() {
#ifdef _WIN32
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            if (h == INVALID_HANDLE_VALUE) return false;
            DWORD mode = 0;
            if (!GetConsoleMode(h, &mode)) return false; // fails if redirected to a file/pipe
            return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
            return isatty(fileno(stdout));
#endif
        }

        std::string common_attributes(const logging::record_view &rec) {
            const auto ts = logging::extract<boost::posix_time::ptime>("TimeStamp", rec);
            const auto pid = logging::extract<boost::log::process_id>("ProcessID", rec);
            const auto scope = logging::extract<boost::log::attributes::named_scope>("Scope", rec);
            return boost::posix_time::to_iso_extended_string(*ts) + " PID: " + std::to_string(pid.get().native_id());
        }

        void console_formatter(const logging::record_view &rec,
                               logging::formatting_ostream &strm,
                               const bool colors) {
            const auto sev = rec[logging::trivial::severity];

            if (colors && sev) {
                if (sev == boost::log::trivial::fatal) {
                    strm << color_for(*sev) << common_attributes(rec) << " ";
                } else {
                    strm << GRAY << common_attributes(rec) << RESET << " " << color_for(*sev);
                }
            } else {
                strm << common_attributes(rec) << " ";
            }

            strm << std::setw(7) << std::left << *sev << " ";

            if (colors && sev) {
                if (sev == boost::log::trivial::fatal) {
                    strm << rec[expr::smessage] << reset;
                } else {
                    strm << reset << rec[expr::smessage];
                }
            } else {
                strm << rec[expr::smessage];
            }
        }

#ifdef _WIN32
        void attach_console() {
            // Reuse the parent console if launched from a terminal, else make one
            if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
                if (!AllocConsole()) return;
            }
            FILE *f;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
            freopen_s(&f, "CONIN$", "r", stdin);
            std::ios::sync_with_stdio(true);
            std::clog.clear();
            std::cout.clear();
            std::cerr.clear();
        }
#endif
        using pid_value = boost::log::attributes::current_process_id::value_type; // = process_id
        unsigned long native_pid(logging::value_ref<pid_value> const &ref) {
            return ref ? ref->native_id() : 0ul;
        }
    } // anonymous namespace
    void init(boost::log::trivial::severity_level level, const bool enable_colors) {
        logging::add_common_attributes(); // TimeStamp, ThreadID, etc.
        logging::core::get()->add_global_attribute("Scope", boost::log::attributes::named_scope());
#ifdef _WIN32
        attach_console();
#endif
        const bool colors = enable_colors && enable_vt_mode();
        // Console sink — colored
        using console_sink_t = sinks::synchronous_sink<sinks::text_ostream_backend>;

        const auto console_sink = boost::make_shared<console_sink_t>();
        console_sink->locked_backend()->add_stream(
            boost::shared_ptr<std::ostream>(&std::cout, boost::null_deleter()));
        console_sink->locked_backend()->auto_flush(true);
        console_sink->set_formatter(
            [colors](const logging::record_view &rec, logging::formatting_ostream &strm) {
                console_formatter(rec, strm, colors);
            });
        logging::core::get()->add_sink(console_sink);

        auto file_sink = logging::add_file_log(
            logging::keywords::file_name = "engine_%N.log",
            logging::keywords::rotation_size = 10 * 1024 * 1024,
            logging::keywords::auto_flush = true,
            logging::keywords::format = (
                (
                    expr::stream
                    << expr::attr<unsigned int>("LineID")
                    << " " << expr::format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%dT%H:%M:%S")
                    << " PID: " << boost::phoenix::bind(&native_pid, expr::attr<pid_value>("ProcessID").or_none())
                    << " " << expr::format_named_scope("Scope",
                                                logging::keywords::format = "%n (%f:%l)",
                                                // name, file, line of the scope
                                                logging::keywords::depth = 2, // only innermost 2 entries
                                                logging::keywords::delimiter = " <- ")
                    << " " << std::left << std::setw(7) << logging::trivial::severity
                    << " " << expr::smessage
                )
            ));

#ifdef _WIN32
        using debug_sink_t = sinks::synchronous_sink<sinks::debug_output_backend>;
        auto debug_sink = boost::make_shared<debug_sink_t>();
        // Only emit when a debugger is actually listening
        debug_sink->set_filter(expr::is_debugger_present());
        debug_sink->set_formatter(
            [](const logging::record_view& rec, logging::formatting_ostream& strm) {
                console_formatter(rec, strm, false);
				strm << std::endl; // Add a newline for debugger output
            });
        logging::core::get()->add_sink(debug_sink);
#endif

#ifdef NDEBUG
        logging::core::get()->set_filter(logging::trivial::severity >= level);
#else
        logging::core::get()->set_filter([level](const logging::attribute_value_set &attrs) {
            const auto sev = logging::extract<boost::log::trivial::severity_level>("Severity", attrs);
            return sev && *sev >= level;
        });
#endif
    }
}
