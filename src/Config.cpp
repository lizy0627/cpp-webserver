#include "Config.h"

#include "Logger.h"

#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace {
constexpr unsigned short defaultPort = 8080;
constexpr std::size_t defaultThreadNum = 4;
constexpr std::size_t defaultMaxRequestBodySize = 1024 * 1024;
constexpr unsigned long long defaultConnectionIdleTimeoutSeconds = 30;
constexpr bool defaultEnableDirectoryListing = true;
constexpr bool defaultEnableTls = false;
const char* defaultRoot = "www";
const char* defaultCertFile = "cert.pem";
const char* defaultKeyFile = "key.pem";

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

bool parseBool(const std::string& text, bool& value) {
    if (text == "true" || text == "1" || text == "yes" || text == "on") {
        value = true;
        return true;
    }

    if (text == "false" || text == "0" || text == "no" || text == "off") {
        value = false;
        return true;
    }

    return false;
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

std::size_t Config::maxRequestBodySize() const {
    return maxRequestBodySize_;
}

bool Config::enableDirectoryListing() const {
    return enableDirectoryListing_;
}

bool Config::enableTls() const {
    return enableTls_;
}

const std::string& Config::certFile() const {
    return certFile_;
}

const std::string& Config::keyFile() const {
    return keyFile_;
}

void Config::setPort(unsigned short port) {
    port_ = port;
}

void Config::setThreadNum(std::size_t threadNum) {
    threadNum_ = threadNum;
}

void Config::setRoot(std::string root) {
    root_ = std::move(root);
}

void Config::setConnectionIdleTimeout(std::chrono::seconds timeout) {
    connectionIdleTimeout_ = timeout;
}

void Config::setMaxRequestBodySize(std::size_t maxRequestBodySize) {
    maxRequestBodySize_ = maxRequestBodySize;
}

void Config::setEnableDirectoryListing(bool enableDirectoryListing) {
    enableDirectoryListing_ = enableDirectoryListing;
}

void Config::setEnableTls(bool enableTls) {
    enableTls_ = enableTls;
}

void Config::setCertFile(std::string certFile) {
    certFile_ = std::move(certFile);
}

void Config::setKeyFile(std::string keyFile) {
    keyFile_ = std::move(keyFile);
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
    std::size_t parsedMaxRequestBodySize = defaultMaxRequestBodySize;
    bool parsedEnableDirectoryListing = defaultEnableDirectoryListing;
    bool parsedEnableTls = defaultEnableTls;
    std::string parsedCertFile = defaultCertFile;
    std::string parsedKeyFile = defaultKeyFile;

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
        } else if (key == "max_request_body_size") {
            unsigned long long number = 0;
            if (!parseUnsigned(value, number) ||
                number == 0 ||
                number > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
                Logger::warn("invalid config value for max_request_body_size, using defaults");
                resetToDefaults();
                return;
            }

            parsedMaxRequestBodySize = static_cast<std::size_t>(number);
        } else if (key == "enable_directory_listing") {
            bool enabled = false;
            if (!parseBool(value, enabled)) {
                Logger::warn("invalid config value for enable_directory_listing, using defaults");
                resetToDefaults();
                return;
            }

            parsedEnableDirectoryListing = enabled;
        } else if (key == "enable_tls") {
            bool enabled = false;
            if (!parseBool(value, enabled)) {
                Logger::warn("invalid config value for enable_tls, using defaults");
                resetToDefaults();
                return;
            }

            parsedEnableTls = enabled;
        } else if (key == "cert_file") {
            if (value.empty()) {
                Logger::warn("invalid config value for cert_file, using defaults");
                resetToDefaults();
                return;
            }

            parsedCertFile = value;
        } else if (key == "key_file") {
            if (value.empty()) {
                Logger::warn("invalid config value for key_file, using defaults");
                resetToDefaults();
                return;
            }

            parsedKeyFile = value;
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
    maxRequestBodySize_ = parsedMaxRequestBodySize;
    enableDirectoryListing_ = parsedEnableDirectoryListing;
    enableTls_ = parsedEnableTls;
    certFile_ = parsedCertFile;
    keyFile_ = parsedKeyFile;
}

void Config::resetToDefaults() {
    port_ = defaultPort;
    threadNum_ = defaultThreadNum;
    root_ = defaultRoot;
    connectionIdleTimeout_ = std::chrono::seconds(defaultConnectionIdleTimeoutSeconds);
    maxRequestBodySize_ = defaultMaxRequestBodySize;
    enableDirectoryListing_ = defaultEnableDirectoryListing;
    enableTls_ = defaultEnableTls;
    certFile_ = defaultCertFile;
    keyFile_ = defaultKeyFile;
}
