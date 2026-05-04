#include "Server.h"

#include "HttpParser.h"
#include "HttpResponse.h"
#include "Logger.h"
#include "StaticFileHandler.h"

#include <cerrno>
#include <cstdio>
#include <exception>
#include <fcntl.h>
#include <string>
#include <sys/epoll.h>
#include <vector>

namespace {
class ClientSocketGuard {
public:
    explicit ClientSocketGuard(int socket)
        : socket_(socket) {}

    ~ClientSocketGuard() {
        if (socket_ != -1) {
            close(socket_);
        }
    }

    ClientSocketGuard(const ClientSocketGuard&) = delete;
    ClientSocketGuard& operator=(const ClientSocketGuard&) = delete;

    int get() const {
        return socket_;
    }

    void release() {
        socket_ = -1;
    }

private:
    int socket_;
};

class FileDescriptorGuard {
public:
    explicit FileDescriptorGuard(int fd)
        : fd_(fd) {}

    ~FileDescriptorGuard() {
        if (fd_ != -1) {
            close(fd_);
        }
    }

    FileDescriptorGuard(const FileDescriptorGuard&) = delete;
    FileDescriptorGuard& operator=(const FileDescriptorGuard&) = delete;

    int get() const {
        return fd_;
    }

private:
    int fd_;
};

bool wouldBlock() {
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

bool setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

bool addSocketToEpoll(int epollFd, int socket, uint32_t events) {
    epoll_event event{};
    event.events = events;
    event.data.fd = socket;
    return epoll_ctl(epollFd, EPOLL_CTL_ADD, socket, &event) != -1;
}

HttpResponse makeTextResponse(int statusCode,
                              const std::string& statusText,
                              const std::string& body) {
    HttpResponse response(statusCode, statusText);
    response.setBody(body);
    return response;
}
}

Server::Server(unsigned short port)
    : port_(port),
      serverSocket_(-1) {}

Server::~Server() {
    if (serverSocket_ != -1) {
        close(serverSocket_);
    }
}

bool Server::start() {
    if (!createSocket()) {
        return false;
    }

    if (!bindSocket()) {
        return false;
    }

    if (!listenSocket()) {
        return false;
    }

    Logger::info("Web server started at http://localhost:" + std::to_string(port_));
    acceptLoop();
    return true;
}

bool Server::createSocket() {
    serverSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket_ == -1) {
        Logger::error("socket creation failed");
        perror("socket failed");
        return false;
    }

    if (!setNonBlocking(serverSocket_)) {
        Logger::error("failed to set server socket non-blocking");
        perror("fcntl failed");
        close(serverSocket_);
        serverSocket_ = -1;
        return false;
    }

    return true;
}

bool Server::bindSocket() {
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port_);

    const int result = bind(
        serverSocket_,
        reinterpret_cast<sockaddr*>(&serverAddress),
        sizeof(serverAddress));
    if (result == -1) {
        Logger::error("bind failed");
        perror("bind failed");
        return false;
    }

    return true;
}

bool Server::listenSocket() {
    const int result = listen(serverSocket_, SOMAXCONN);
    if (result == -1) {
        Logger::error("listen failed");
        perror("listen failed");
        return false;
    }

    return true;
}

void Server::acceptLoop() {
    const int epollFd = epoll_create1(0);
    if (epollFd == -1) {
        Logger::error("epoll_create1 failed");
        perror("epoll_create1 failed");
        return;
    }

    FileDescriptorGuard epollGuard(epollFd);
    if (!addSocketToEpoll(epollFd, serverSocket_, EPOLLIN)) {
        Logger::error("failed to add server socket to epoll");
        perror("epoll_ctl failed");
        return;
    }

    constexpr int maxEvents = 64;
    std::vector<epoll_event> events(maxEvents);

    while (true) {
        const int eventCount = epoll_wait(epollFd, events.data(), static_cast<int>(events.size()), -1);
        if (eventCount == -1) {
            if (errno == EINTR) {
                continue;
            }

            Logger::error("epoll_wait failed");
            perror("epoll_wait failed");
            return;
        }

        for (int i = 0; i < eventCount; ++i) {
            const int readySocket = events[i].data.fd;
            const uint32_t readyEvents = events[i].events;

            if (readySocket == serverSocket_) {
                acceptReadyClients(epollFd);
                continue;
            }

            if ((readyEvents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
                Logger::info("client connection closed");
                close(readySocket);
                continue;
            }

            if ((readyEvents & EPOLLIN) != 0) {
                handleClient(readySocket);
            }
        }
    }
}

void Server::acceptReadyClients(int epollFd) {
    while (true) {
        const int clientSocket = accept(serverSocket_, nullptr, nullptr);
        if (clientSocket == -1) {
            if (wouldBlock()) {
                return;
            }

            if (errno == EINTR) {
                continue;
            }

            Logger::error("accept failed");
            perror("accept failed");
            return;
        }

        if (!setNonBlocking(clientSocket)) {
            Logger::error("failed to set client socket non-blocking");
            perror("fcntl failed");
            close(clientSocket);
            continue;
        }

        if (!addSocketToEpoll(epollFd, clientSocket, EPOLLIN | EPOLLRDHUP)) {
            Logger::error("failed to add client socket to epoll");
            perror("epoll_ctl failed");
            close(clientSocket);
            continue;
        }
    }
}

void Server::handleClient(int clientSocket) {
    ClientSocketGuard client(clientSocket);
    char buffer[1024]{};
    const ssize_t bytesRead = recv(client.get(), buffer, sizeof(buffer) - 1, 0);

    if (bytesRead == -1 && wouldBlock()) {
        client.release();
        return;
    }

    if (bytesRead == -1) {
        Logger::error("recv failed");
        perror("recv failed");
        return;
    }

    if (bytesRead == 0) {
        Logger::info("client closed connection");
        return;
    }

    HttpResponse response;
    HttpRequest request;
    StaticFileHandler staticFiles;

    try {
        // Invalid HTTP syntax maps to 400; unsupported methods are handled separately.
        if (bytesRead <= 0 ||
            !HttpParser::parse(std::string(buffer, static_cast<std::size_t>(bytesRead)), request)) {
            Logger::warn("bad request received");
            response = makeTextResponse(400, "Bad Request", "Bad Request");
        } else if (request.method != "GET") {
            Logger::info("request path: " + request.path);
            response = makeTextResponse(405, "Method Not Allowed", "Method Not Allowed");
        } else {
            Logger::info("request path: " + request.path);
            const StaticFileResult file = staticFiles.handle(request.path);
            if (file.status == StaticFileResult::Status::Ok) {
                response = makeTextResponse(200, "OK", file.body);
                response.setContentType(file.contentType);
            } else if (file.status == StaticFileResult::Status::BadRequest) {
                response = makeTextResponse(400, "Bad Request", file.body);
            } else if (file.status == StaticFileResult::Status::NotFound) {
                response = makeTextResponse(404, "Not Found", "Not Found");
            } else {
                response = makeTextResponse(500, "Internal Server Error", "Internal Server Error");
            }
        }
    } catch (const std::exception&) {
        Logger::error("internal server error while handling request");
        response = makeTextResponse(500, "Internal Server Error", "Internal Server Error");
    }

    Logger::info("response status: " + std::to_string(response.statusCode()));
    const std::string rawResponse = response.toString();
    send(client.get(), rawResponse.c_str(), rawResponse.size(), 0);
}
