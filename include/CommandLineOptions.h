#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

class Config;

struct CommandLineOptions {
    std::string configPath = "config/server.conf";
    std::optional<unsigned short> port;
    std::optional<std::string> root;
    std::optional<std::size_t> threadCount;
    std::optional<std::chrono::seconds> connectionIdleTimeout;
    bool showHelp = false;
};

bool parseCommandLineOptions(
    int argc,
    char* argv[],
    CommandLineOptions& options,
    std::string& errorMessage);

std::string commandLineHelp(const std::string& programName);

void applyCommandLineOverridesToConfig(const CommandLineOptions& options, Config& config);
