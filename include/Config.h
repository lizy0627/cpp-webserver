#pragma once

#include <chrono>
#include <cstddef>
#include <string>

class Config {
public:
    explicit Config(const std::string& filePath = "config/server.conf");

    unsigned short port() const;
    std::size_t threadNum() const;
    const std::string& root() const;
    std::chrono::seconds connectionIdleTimeout() const;
    std::size_t maxRequestBodySize() const;
    bool enableDirectoryListing() const;
    bool enableTls() const;
    const std::string& certFile() const;
    const std::string& keyFile() const;

    void setPort(unsigned short port);
    void setThreadNum(std::size_t threadNum);
    void setRoot(std::string root);
    void setConnectionIdleTimeout(std::chrono::seconds timeout);
    void setMaxRequestBodySize(std::size_t maxRequestBodySize);
    void setEnableDirectoryListing(bool enableDirectoryListing);
    void setEnableTls(bool enableTls);
    void setCertFile(std::string certFile);
    void setKeyFile(std::string keyFile);

private:
    void load(const std::string& filePath);
    void resetToDefaults();

    unsigned short port_;
    std::size_t threadNum_;
    std::string root_;
    std::chrono::seconds connectionIdleTimeout_;
    std::size_t maxRequestBodySize_;
    bool enableDirectoryListing_;
    bool enableTls_;
    std::string certFile_;
    std::string keyFile_;
};
