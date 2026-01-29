
#include "logging.hpp"
#include <chrono>
#include <ctime>

namespace scatter {

Logger &Logger::instance() {
  static Logger inst;
  return inst;
}
void Logger::set_level(LogLevel lvl) { level_ = lvl; }
const char *Logger::level_str(LogLevel lvl) {
  switch (lvl) {
  case LogLevel::TRACE:
    return "TRACE";
  case LogLevel::DEBUG:
    return "DEBUG";
  case LogLevel::INFO:
    return "INFO";
  case LogLevel::WARN:
    return "WARN";
  default:
    return "ERROR";
  }
}

void Logger::log(LogLevel lvl, const char *fmt, ...) {
  if (lvl < level_)
    return;
  std::lock_guard<std::mutex> lk(mtx_);
  using namespace std::chrono;
  auto now = system_clock::now();
  auto t = system_clock::to_time_t(now);
  char ts[32];
  std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
  std::fprintf(stderr, "%s [%s] ", ts, level_str(lvl));
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);
  std::fprintf(stderr, "\n");
}

} // namespace scatter
