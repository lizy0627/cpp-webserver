#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

#include "StaticFileHandler.h"
#include "ThreadPool.h"

class HttpRequest;

class Server {
public:
    Server(
        unsigned short port,
        std::size_t threadCount,
        std::string rootDirectory,
        std::chrono::seconds connectionIdleTimeout = std::chrono::seconds(30));
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start();

private:
    struct Impl;

    bool createSocket();
    bool createEventFd();
    bool bindSocket();
    bool listenSocket();
    void acceptLoop();
    void acceptReadyClients(int epollFd);
    void handleConnectionEvent(int epollFd, int fd, unsigned int events);
    void handleCompletionEvent(int epollFd);
    void readFromConnection(int epollFd, int fd);
    void writeToConnection(int epollFd, int fd);
    void processReadBuffer(int epollFd, int fd);
    void queueResponse(int fd, unsigned long long connectionId, std::string response, bool closeAfterWrite);
    void closeIdleConnections(int epollFd, std::chrono::steady_clock::time_point now);
    void closeConnection(int epollFd, int fd);
    std::string buildResponse(const HttpRequest& request) const;
    void notifyCompletionEvent();

    unsigned short port_;
    ThreadPool threadPool_;
    std::string rootDirectory_;
    StaticFileHandler staticFileHandler_;
    std::chrono::seconds connectionIdleTimeout_;
    std::unique_ptr<Impl> impl_;
};
