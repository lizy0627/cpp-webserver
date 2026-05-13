#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "Router.h"
#include "StaticFileHandler.h"
#include "ThreadPool.h"

class HttpRequest;

struct HttpResponseResult {
    std::string response;
    int statusCode = 0;
    std::size_t bodySize = 0;
    bool keepAlive = false;
    bool closeAfterWrite = false;
    bool sendFile = false;
    std::string filePath;
    std::uintmax_t fileSize = 0;
    std::uintmax_t fileOffset = 0;
    std::uintmax_t fileTransferSize = 0;
    std::string contentType;
};

class Server {
public:
    Server(
        unsigned short port,
        std::size_t threadCount,
        std::string rootDirectory,
        bool enableDirectoryListing,
        std::chrono::seconds connectionIdleTimeout = std::chrono::seconds(30),
        std::size_t maxRequestBodySize = 1024 * 1024,
        bool enableTls = false,
        std::string certFile = "cert.pem",
        std::string keyFile = "key.pem");
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start();
    void stop();

private:
    struct Impl;

    bool createSocket();
    bool initializeTlsContext();
    bool createEventFd();
    bool createShutdownEventFd();
    bool installSignalHandlers();
    void restoreSignalHandlers();
    bool bindSocket();
    bool listenSocket();
    void acceptLoop();
    void acceptReadyClients(int epollFd);
    void handleConnectionEvent(int epollFd, int fd, unsigned int events);
    void handleCompletionEvent(int epollFd);
    void handleShutdownEvent();
    void readFromConnection(int epollFd, int fd);
    void writeToConnection(int epollFd, int fd);
    bool driveTlsHandshake(int epollFd, int fd);
    void processReadBuffer(int epollFd, int fd);
    void queueResponse(
        int fd,
        unsigned long long connectionId,
        HttpResponseResult response,
        bool closeAfterWrite,
        std::string method,
        std::string path,
        std::chrono::steady_clock::time_point requestStartTime);
    void closeIdleConnections(int epollFd, std::chrono::steady_clock::time_point now);
    void closeAllConnections(int epollFd);
    void closeConnection(int epollFd, int fd);
    HttpResponseResult buildResponse(const HttpRequest& request) const;
    void notifyCompletionEvent();
    void notifyShutdownEvent();

    unsigned short port_;
    std::string rootDirectory_;
    Router router_;
    StaticFileHandler staticFileHandler_;
    std::chrono::seconds connectionIdleTimeout_;
    std::size_t maxRequestBodySize_;
    bool enableTls_;
    std::string certFile_;
    std::string keyFile_;
    std::unique_ptr<Impl> impl_;
    ThreadPool threadPool_;
};
