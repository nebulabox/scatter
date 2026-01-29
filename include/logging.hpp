
#pragma once
#include <cstdio>
#include <cstdarg>
#include <mutex>

namespace scatter {

enum class LogLevel { TRACE=0, DEBUG, INFO, WARN, ERROR };

class Logger {
public:
    static Logger& instance();
    void set_level(LogLevel lvl);
    void log(LogLevel lvl, const char* fmt, ...);
private:
    Logger() = default;
    std::mutex mtx_;
    LogLevel level_ = LogLevel::INFO;
    const char* level_str(LogLevel lvl);
};

} // namespace scatter
