#pragma once

#include <cstddef>
#include <string>

class Config {
public:
    explicit Config(const std::string& filePath = "config/server.conf");

    unsigned short port() const;
    std::size_t threadNum() const;
    const std::string& root() const;

private:
    void load(const std::string& filePath);
    void resetToDefaults();

    unsigned short port_;
    std::size_t threadNum_;
    std::string root_;
};
