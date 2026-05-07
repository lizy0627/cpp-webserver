#pragma once

#include <mutex>
#include <string>

enum class LogLevel {
    Info,
    Warn,
    Error
};

class Logger {
public:
    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);
    static void access(const std::string& message);
    static void log(LogLevel level, const std::string& message);

private:
    static std::string levelToString(LogLevel level);
    static std::string timestamp();

    static std::mutex mutex_;
};
