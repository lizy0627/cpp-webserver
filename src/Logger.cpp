#include "Logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

std::mutex Logger::mutex_;

void Logger::info(const std::string& message) {
    log(LogLevel::Info, message);
}

void Logger::warn(const std::string& message) {
    log(LogLevel::Warn, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::Error, message);
}

void Logger::access(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "[ACCESS] " << message << std::endl;
}

void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "[" << timestamp() << "] "
              << "[" << levelToString(level) << "] "
              << message << std::endl;
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }

    return "INFO";
}

std::string Logger::timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}
