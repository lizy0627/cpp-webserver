#include "Config.h"

#include "Logger.h"

#include <fstream>
#include <limits>
#include <sstream>
#include <string>

namespace {
constexpr unsigned short defaultPort = 8080;
constexpr std::size_t defaultThreadNum = 4;
constexpr unsigned long long defaultConnectionIdleTimeoutSeconds = 30;
const char* defaultRoot = "www";

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool parseUnsigned(const std::string& text, unsigned long long& value) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }

    std::istringstream stream(text);
    stream >> value;
    return !stream.fail() && stream.eof();
}
}

Config::Config(const std::string& filePath) {
    resetToDefaults();
    load(filePath);
}

unsigned short Config::port() const {
    return port_;
}

std::size_t Config::threadNum() const {
    return threadNum_;
}

const std::string& Config::root() const {
    return root_;
}

std::chrono::seconds Config::connectionIdleTimeout() const {
    return connectionIdleTimeout_;
}

void Config::load(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file) {
        Logger::warn("config file not found, using defaults: " + filePath);
        return;
    }

    unsigned short parsedPort = defaultPort;
    std::size_t parsedThreadNum = defaultThreadNum;
    std::string parsedRoot = defaultRoot;
    std::chrono::seconds parsedConnectionIdleTimeout(defaultConnectionIdleTimeoutSeconds);

    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;

        const std::string cleanLine = trim(line);
        if (cleanLine.empty() || cleanLine.front() == '#') {
            continue;
        }

        const std::size_t separator = cleanLine.find('=');
        if (separator == std::string::npos) {
            Logger::warn("failed to parse config line " + std::to_string(lineNumber) + ", using defaults");
            resetToDefaults();
            return;
        }

        const std::string key = trim(cleanLine.substr(0, separator));
        const std::string value = trim(cleanLine.substr(separator + 1));

        if (key == "port") {
            unsigned long long number = 0;
            if (!parseUnsigned(value, number) ||
                number == 0 ||
                number > std::numeric_limits<unsigned short>::max()) {
                Logger::warn("invalid config value for port, using defaults");
                resetToDefaults();
                return;
            }

            parsedPort = static_cast<unsigned short>(number);
        } else if (key == "thread_num") {
            unsigned long long number = 0;
            if (!parseUnsigned(value, number) ||
                number == 0 ||
                number > std::numeric_limits<std::size_t>::max()) {
                Logger::warn("invalid config value for thread_num, using defaults");
                resetToDefaults();
                return;
            }

            parsedThreadNum = static_cast<std::size_t>(number);
        } else if (key == "root") {
            if (value.empty()) {
                Logger::warn("invalid config value for root, using defaults");
                resetToDefaults();
                return;
            }

            parsedRoot = value;
        } else if (key == "connection_idle_timeout_seconds") {
            unsigned long long number = 0;
            if (!parseUnsigned(value, number) || number == 0) {
                Logger::warn("invalid config value for connection_idle_timeout_seconds, using defaults");
                resetToDefaults();
                return;
            }

            parsedConnectionIdleTimeout = std::chrono::seconds(number);
        } else {
            Logger::warn("unknown config key '" + key + "', using defaults");
            resetToDefaults();
            return;
        }
    }

    port_ = parsedPort;
    threadNum_ = parsedThreadNum;
    root_ = parsedRoot;
    connectionIdleTimeout_ = parsedConnectionIdleTimeout;
}

void Config::resetToDefaults() {
    port_ = defaultPort;
    threadNum_ = defaultThreadNum;
    root_ = defaultRoot;
    connectionIdleTimeout_ = std::chrono::seconds(defaultConnectionIdleTimeoutSeconds);
}
