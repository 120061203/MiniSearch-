#pragma once

#include <string>
#include <iostream>
#include <mutex>
#include <chrono>
#include <iomanip>

namespace MiniSearch {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static Logger& getInstance();
    
    void setLevel(LogLevel level);
    void log(LogLevel level, const std::string& message);
    
    void debug(const std::string& message);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    LogLevel m_level = LogLevel::INFO;
    std::mutex m_mutex;
    
    std::string getCurrentTime() const;
    std::string levelToString(LogLevel level) const;
};

// 便利巨集
#define LOG_DEBUG(msg) Logger::getInstance().debug(msg)
#define LOG_INFO(msg) Logger::getInstance().info(msg)
#define LOG_WARN(msg) Logger::getInstance().warn(msg)
#define LOG_ERROR(msg) Logger::getInstance().error(msg)

} // namespace MiniSearch
