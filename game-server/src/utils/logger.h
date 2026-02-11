#pragma once

#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <mutex>

namespace logger {

enum class Level { DEBUG, INFO, WARN, ERROR };

inline Level current_level = Level::INFO;
inline std::mutex log_mutex;

inline void set_level(const std::string& level) {
    if (level == "debug") current_level = Level::DEBUG;
    else if (level == "info") current_level = Level::INFO;
    else if (level == "warn") current_level = Level::WARN;
    else if (level == "error") current_level = Level::ERROR;
}

inline std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return ss.str();
}

inline const char* level_str(Level l) {
    switch (l) {
        case Level::DEBUG: return "DBG";
        case Level::INFO:  return "INF";
        case Level::WARN:  return "WRN";
        case Level::ERROR: return "ERR";
    }
    return "???";
}

inline void log(Level level, const std::string& msg) {
    if (level < current_level) return;
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cerr << timestamp() << " " << level_str(level) << " " << msg << "\n";
}

inline void debug(const std::string& msg) { log(Level::DEBUG, msg); }
inline void info(const std::string& msg)  { log(Level::INFO, msg); }
inline void warn(const std::string& msg)  { log(Level::WARN, msg); }
inline void error(const std::string& msg) { log(Level::ERROR, msg); }

} // namespace logger
