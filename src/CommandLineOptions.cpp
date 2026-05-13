#include "CommandLineOptions.h"

#include "Config.h"

#include <limits>
#include <sstream>

namespace {
constexpr unsigned long long maxPort = 65535;

bool parseUnsigned(const std::string& text, unsigned long long& value) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }

    std::istringstream stream(text);
    stream >> value;
    return !stream.fail() && stream.eof();
}

bool hasValue(int argc, int index) {
    return index + 1 < argc;
}

std::string argumentValue(char* value) {
    return value == nullptr ? "" : std::string(value);
}

bool parsePortValue(const std::string& text, unsigned short& port) {
    unsigned long long number = 0;
    if (!parseUnsigned(text, number) || number == 0 || number > maxPort) {
        return false;
    }

    port = static_cast<unsigned short>(number);
    return true;
}

bool parseSizeValue(const std::string& text, std::size_t& value) {
    unsigned long long number = 0;
    if (!parseUnsigned(text, number) ||
        number == 0 ||
        number > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }

    value = static_cast<std::size_t>(number);
    return true;
}

bool parseTimeoutValue(const std::string& text, std::chrono::seconds& timeout) {
    unsigned long long number = 0;
    if (!parseUnsigned(text, number) ||
        number == 0 ||
        number > static_cast<unsigned long long>(std::chrono::seconds::max().count())) {
        return false;
    }

    timeout = std::chrono::seconds(number);
    return true;
}

void setMissingValueError(const std::string& optionName, std::string& errorMessage) {
    errorMessage = "missing value for " + optionName;
}
}

bool parseCommandLineOptions(
    int argc,
    char* argv[],
    CommandLineOptions& options,
    std::string& errorMessage) {
    options = CommandLineOptions{};
    errorMessage.clear();

    for (int i = 1; i < argc; ++i) {
        const std::string optionName = argumentValue(argv[i]);

        if (optionName == "--help") {
            options.showHelp = true;
            return true;
        }

        if (optionName == "--config") {
            if (!hasValue(argc, i)) {
                setMissingValueError(optionName, errorMessage);
                return false;
            }

            const std::string value = argumentValue(argv[++i]);
            if (value.empty()) {
                errorMessage = "invalid value for --config: path cannot be empty";
                return false;
            }

            options.configPath = value;
        } else if (optionName == "--port") {
            if (!hasValue(argc, i)) {
                setMissingValueError(optionName, errorMessage);
                return false;
            }

            const std::string value = argumentValue(argv[++i]);
            unsigned short port = 0;
            if (!parsePortValue(value, port)) {
                errorMessage = "invalid value for --port: expected integer in range 1-65535, got '" + value + "'";
                return false;
            }

            options.port = port;
        } else if (optionName == "--root") {
            if (!hasValue(argc, i)) {
                setMissingValueError(optionName, errorMessage);
                return false;
            }

            const std::string value = argumentValue(argv[++i]);
            if (value.empty()) {
                errorMessage = "invalid value for --root: path cannot be empty";
                return false;
            }

            options.root = value;
        } else if (optionName == "--threads") {
            if (!hasValue(argc, i)) {
                setMissingValueError(optionName, errorMessage);
                return false;
            }

            const std::string value = argumentValue(argv[++i]);
            std::size_t threadCount = 0;
            if (!parseSizeValue(value, threadCount)) {
                errorMessage = "invalid value for --threads: expected positive integer, got '" + value + "'";
                return false;
            }

            options.threadCount = threadCount;
        } else if (optionName == "--timeout") {
            if (!hasValue(argc, i)) {
                setMissingValueError(optionName, errorMessage);
                return false;
            }

            const std::string value = argumentValue(argv[++i]);
            std::chrono::seconds timeout(0);
            if (!parseTimeoutValue(value, timeout)) {
                errorMessage = "invalid value for --timeout: expected positive integer seconds, got '" + value + "'";
                return false;
            }

            options.connectionIdleTimeout = timeout;
        } else if (optionName == "--max-body-size") {
            if (!hasValue(argc, i)) {
                setMissingValueError(optionName, errorMessage);
                return false;
            }

            const std::string value = argumentValue(argv[++i]);
            std::size_t maxBodySize = 0;
            if (!parseSizeValue(value, maxBodySize)) {
                errorMessage = "invalid value for --max-body-size: expected positive integer bytes, got '" + value + "'";
                return false;
            }

            options.maxRequestBodySize = maxBodySize;
        } else {
            errorMessage = "unknown option: " + optionName;
            return false;
        }
    }

    return true;
}

std::string commandLineHelp(const std::string& programName) {
    const std::string executable = programName.empty() ? "WebServer" : programName;

    return "Usage: " + executable + " [options]\n"
        "\n"
        "Options:\n"
        "  --config <path>   Specify config file path (default: config/server.conf)\n"
        "  --port <port>     Override listen port (1-65535)\n"
        "  --root <path>     Override static file root directory\n"
        "  --threads <num>   Override worker thread count\n"
        "  --timeout <sec>   Override idle connection timeout in seconds\n"
        "  --max-body-size <bytes>\n"
        "                    Override maximum POST request body size in bytes\n"
        "  --help            Print this help message and exit\n";
}

void applyCommandLineOverridesToConfig(const CommandLineOptions& options, Config& config) {
    if (options.port.has_value()) {
        config.setPort(*options.port);
    }

    if (options.root.has_value()) {
        config.setRoot(*options.root);
    }

    if (options.threadCount.has_value()) {
        config.setThreadNum(*options.threadCount);
    }

    if (options.connectionIdleTimeout.has_value()) {
        config.setConnectionIdleTimeout(*options.connectionIdleTimeout);
    }

    if (options.maxRequestBodySize.has_value()) {
        config.setMaxRequestBodySize(*options.maxRequestBodySize);
    }
}
