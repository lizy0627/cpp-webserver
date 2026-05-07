#include "Server.h"

#include "HttpParser.h"
#include "HttpResponse.h"
#include "Logger.h"
#include "StaticFileHandler.h"

#include <arpa/inet.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <fcntl.h>
#include <mutex>
#include <netinet/in.h>
#include <optional>
#include <signal.h>
#include <string>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
constexpr std::size_t maxRequestHeaderSize = 8192;
constexpr std::size_t maxSendfileBytesPerEvent = 1024 * 1024;
constexpr int invalidFd = -1;
constexpr int idleScanIntervalMs = 1000;
constexpr std::chrono::seconds defaultConnectionIdleTimeout(30);

volatile std::sig_atomic_t shutdownSignalRequested = 0;
int shutdownSignalEventFd = invalidFd;

void shutdownSignalHandler(int) {
    shutdownSignalRequested = 1;

    if (shutdownSignalEventFd != invalidFd) {
        const uint64_t value = 1;
        const ssize_t ignored = write(shutdownSignalEventFd, &value, sizeof(value));
        (void)ignored;
    }
}

class UniqueFd {
public:
    UniqueFd() = default;

    explicit UniqueFd(int fd)
        : fd_(fd) {}

    ~UniqueFd() {
        reset();
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept
        : fd_(other.release()) {}

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    int get() const {
        return fd_;
    }

    bool valid() const {
        return fd_ != invalidFd;
    }

    int release() {
        const int fd = fd_;
        fd_ = invalidFd;
        return fd;
    }

    void reset(int fd = invalidFd) {
        if (fd_ != invalidFd) {
            close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_ = invalidFd;
};

struct Completion {
    int fd = invalidFd;
    unsigned long long connectionId = 0;
    HttpResponseResult response;
    bool closeAfterWrite = true;
    std::string method;
    std::string path;
    std::chrono::steady_clock::time_point requestStartTime = std::chrono::steady_clock::now();
};

struct SignalHandlers {
    bool installed = false;
    struct sigaction previousSigint {};
    struct sigaction previousSigterm {};
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

bool addSocketToEpoll(int epollFd, int fd, uint32_t events) {
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;
    return epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &event) != -1;
}

bool modifySocketInEpoll(int epollFd, int fd, uint32_t events) {
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;
    return epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &event) != -1;
}

void removeSocketFromEpoll(int epollFd, int fd) {
    if (epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr) == -1 && errno != ENOENT && errno != EBADF) {
        Logger::warn("failed to remove fd from epoll");
    }
}

HttpResponse makeTextResponse(int statusCode,
                              const std::string& statusText,
                              const std::string& body,
                              bool keepAlive = false) {
    HttpResponse response(statusCode, statusText);
    response.setBody(body);
    response.setKeepAlive(keepAlive);
    return response;
}

HttpResponseResult makeResponseResult(HttpResponse response, bool keepAlive, bool includeBody) {
    return {
        response.toString(includeBody),
        response.statusCode(),
        response.bodySize(),
        keepAlive,
        false,
        "",
        0,
        0,
        0,
        ""
    };
}

HttpResponseResult makeTextResponseResult(int statusCode,
                                          const std::string& statusText,
                                          const std::string& body,
                                          bool keepAlive = false,
                                          bool includeBody = true) {
    return makeResponseResult(
        makeTextResponse(statusCode, statusText, body, keepAlive),
        keepAlive,
        includeBody);
}

HttpResponseResult makeStaticFileResponseResult(const StaticFileResult& file, bool keepAlive, bool includeBody) {
    if (file.status == StaticFileResult::Status::Ok) {
        HttpResponse response(
            file.partialContent ? 206 : 200,
            file.partialContent ? "Partial Content" : "OK");
        response.setContentType(file.contentType);
        response.setContentLength(static_cast<std::size_t>(file.contentLength));
        if (file.partialContent) {
            response.setHeader(
                "Content-Range",
                "bytes " + std::to_string(file.contentOffset) + "-" +
                    std::to_string(file.contentOffset + file.contentLength - 1) + "/" +
                    std::to_string(file.fileSize));
        }
        response.setKeepAlive(keepAlive);
        HttpResponseResult result = makeResponseResult(std::move(response), keepAlive, false);
        result.sendFile = includeBody;
        result.filePath = file.filePath.string();
        result.fileSize = file.fileSize;
        result.fileOffset = file.contentOffset;
        result.fileTransferSize = file.contentLength;
        result.contentType = file.contentType;
        return result;
    }

    if (file.status == StaticFileResult::Status::RangeNotSatisfiable) {
        HttpResponse response(416, "Range Not Satisfiable");
        response.setBody(file.body);
        response.setKeepAlive(keepAlive);
        response.setHeader("Content-Range", "bytes */" + std::to_string(file.fileSize));
        return makeResponseResult(std::move(response), keepAlive, includeBody);
    }

    if (file.status == StaticFileResult::Status::BadRequest) {
        return makeTextResponseResult(400, "Bad Request", file.body, keepAlive, includeBody);
    }

    if (file.status == StaticFileResult::Status::Forbidden) {
        return makeTextResponseResult(403, "Forbidden", file.body, keepAlive, includeBody);
    }

    if (file.status == StaticFileResult::Status::NotFound) {
        return makeTextResponseResult(404, "Not Found", "Not Found", keepAlive, includeBody);
    }

    return makeTextResponseResult(500, "Internal Server Error", "Internal Server Error", keepAlive, includeBody);
}

void logAccess(const std::string& method,
               const std::string& path,
               const HttpResponseResult& response,
               std::chrono::steady_clock::time_point requestStartTime) {
    const auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - requestStartTime);

    std::string message =
        "method=" + (method.empty() ? std::string("-") : method) +
        " path=" + (path.empty() ? std::string("-") : path) +
        " status=" + std::to_string(response.statusCode) +
        " bytes=" + std::to_string(response.bodySize) +
        " cost=" + std::to_string(cost.count()) + "ms" +
        " keep_alive=" + (response.keepAlive ? std::string("true") : std::string("false"));

    if (!response.filePath.empty()) {
        message +=
            " file=\"" + response.filePath + "\"" +
            " file_size=" + std::to_string(response.fileSize) +
            " content_type=\"" + response.contentType + "\"";
    }

    Logger::access(message);
}

std::size_t completeHeaderLength(const std::string& buffer) {
    const std::size_t crlfHeaderEnd = buffer.find("\r\n\r\n");
    const std::size_t lfHeaderEnd = buffer.find("\n\n");

    if (crlfHeaderEnd == std::string::npos) {
        return lfHeaderEnd == std::string::npos ? std::string::npos : lfHeaderEnd + 2;
    }

    if (lfHeaderEnd == std::string::npos || crlfHeaderEnd < lfHeaderEnd) {
        return crlfHeaderEnd + 4;
    }

    return lfHeaderEnd + 2;
}

bool hasCompleteHeader(const std::string& buffer) {
    return completeHeaderLength(buffer) != std::string::npos;
}

bool currentRequestHeaderTooLarge(const std::string& buffer) {
    const std::size_t headerLength = completeHeaderLength(buffer);
    if (headerLength != std::string::npos) {
        return headerLength > maxRequestHeaderSize;
    }

    return buffer.size() > maxRequestHeaderSize;
}
}

struct Server::Impl {
    struct FileTransfer {
        UniqueFd file;
        off_t offset = 0;
        std::uintmax_t size = 0;
        std::uintmax_t sent = 0;

        bool active() const {
            return file.valid() && sent < size;
        }

        void reset() {
            file.reset();
            offset = 0;
            size = 0;
            sent = 0;
        }
    };

    struct Connection {
        UniqueFd fd;
        unsigned long long id = 0;
        std::string readBuffer;
        std::string writeBuffer;
        FileTransfer fileTransfer;
        bool closeAfterWrite = true;
        bool responsePending = false;
        std::chrono::steady_clock::time_point lastActiveTime = std::chrono::steady_clock::now();
    };

    UniqueFd serverSocket;
    UniqueFd completionEvent;
    UniqueFd shutdownEvent;
    std::unordered_map<int, Connection> connections;
    std::mutex completionsMutex;
    std::deque<Completion> completions;
    SignalHandlers signalHandlers;
    std::atomic_bool running{false};
    unsigned long long nextConnectionId = 1;
};

Server::Server(
    unsigned short port,
    std::size_t threadCount,
    std::string rootDirectory,
    std::chrono::seconds connectionIdleTimeout)
    : port_(port),
      rootDirectory_(std::move(rootDirectory)),
      staticFileHandler_(rootDirectory_),
      connectionIdleTimeout_(connectionIdleTimeout > std::chrono::seconds::zero()
          ? connectionIdleTimeout
          : defaultConnectionIdleTimeout),
      impl_(std::make_unique<Impl>()),
      threadPool_(threadCount) {}

Server::~Server() {
    restoreSignalHandlers();
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

    if (!createEventFd()) {
        return false;
    }

    if (!createShutdownEventFd()) {
        return false;
    }

    if (!installSignalHandlers()) {
        return false;
    }

    impl_->running.store(true);
    Logger::info("Web server started at http://localhost:" + std::to_string(port_));
    acceptLoop();
    restoreSignalHandlers();
    Logger::info("Web server stopped");
    return true;
}

void Server::stop() {
    if (impl_ == nullptr || !impl_->running.exchange(false)) {
        return;
    }

    notifyShutdownEvent();
}

bool Server::createSocket() {
    UniqueFd socketFd(socket(AF_INET, SOCK_STREAM, 0));
    if (!socketFd.valid()) {
        Logger::error("socket creation failed");
        perror("socket failed");
        return false;
    }

    const int reuseAddress = 1;
    if (setsockopt(socketFd.get(), SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress)) == -1) {
        Logger::warn("failed to set SO_REUSEADDR");
    }

    if (!setNonBlocking(socketFd.get())) {
        Logger::error("failed to set server socket non-blocking");
        perror("fcntl failed");
        return false;
    }

    impl_->serverSocket = std::move(socketFd);
    return true;
}

bool Server::createEventFd() {
    UniqueFd eventFd(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
    if (!eventFd.valid()) {
        Logger::error("eventfd creation failed");
        perror("eventfd failed");
        return false;
    }

    impl_->completionEvent = std::move(eventFd);
    return true;
}

bool Server::createShutdownEventFd() {
    UniqueFd eventFd(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
    if (!eventFd.valid()) {
        Logger::error("shutdown eventfd creation failed");
        perror("eventfd failed");
        return false;
    }

    impl_->shutdownEvent = std::move(eventFd);
    return true;
}

bool Server::installSignalHandlers() {
    shutdownSignalRequested = 0;
    shutdownSignalEventFd = impl_->shutdownEvent.get();

    struct sigaction action {};
    sigemptyset(&action.sa_mask);
    action.sa_handler = shutdownSignalHandler;

    if (sigaction(SIGINT, &action, &impl_->signalHandlers.previousSigint) == -1) {
        Logger::error("failed to install SIGINT handler");
        perror("sigaction failed");
        shutdownSignalEventFd = invalidFd;
        return false;
    }

    if (sigaction(SIGTERM, &action, &impl_->signalHandlers.previousSigterm) == -1) {
        Logger::error("failed to install SIGTERM handler");
        perror("sigaction failed");
        sigaction(SIGINT, &impl_->signalHandlers.previousSigint, nullptr);
        shutdownSignalEventFd = invalidFd;
        return false;
    }

    impl_->signalHandlers.installed = true;
    return true;
}

void Server::restoreSignalHandlers() {
    if (impl_ == nullptr || !impl_->signalHandlers.installed) {
        return;
    }

    sigaction(SIGINT, &impl_->signalHandlers.previousSigint, nullptr);
    sigaction(SIGTERM, &impl_->signalHandlers.previousSigterm, nullptr);

    if (shutdownSignalEventFd == impl_->shutdownEvent.get()) {
        shutdownSignalEventFd = invalidFd;
    }

    impl_->signalHandlers.installed = false;
}

bool Server::bindSocket() {
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port_);

    const int result = bind(
        impl_->serverSocket.get(),
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
    const int result = listen(impl_->serverSocket.get(), SOMAXCONN);
    if (result == -1) {
        Logger::error("listen failed");
        perror("listen failed");
        return false;
    }

    return true;
}

void Server::acceptLoop() {
    UniqueFd epollFd(epoll_create1(EPOLL_CLOEXEC));
    if (!epollFd.valid()) {
        Logger::error("epoll_create1 failed");
        perror("epoll_create1 failed");
        return;
    }

    if (!addSocketToEpoll(epollFd.get(), impl_->serverSocket.get(), EPOLLIN)) {
        Logger::error("failed to add server socket to epoll");
        perror("epoll_ctl failed");
        return;
    }

    if (!addSocketToEpoll(epollFd.get(), impl_->completionEvent.get(), EPOLLIN)) {
        Logger::error("failed to add completion event fd to epoll");
        perror("epoll_ctl failed");
        return;
    }

    if (!addSocketToEpoll(epollFd.get(), impl_->shutdownEvent.get(), EPOLLIN)) {
        Logger::error("failed to add shutdown event fd to epoll");
        perror("epoll_ctl failed");
        return;
    }

    constexpr int maxEvents = 64;
    std::vector<epoll_event> events(maxEvents);
    auto lastIdleScanTime = std::chrono::steady_clock::now();

    while (impl_->running.load()) {
        const int eventCount = epoll_wait(
            epollFd.get(),
            events.data(),
            static_cast<int>(events.size()),
            idleScanIntervalMs);
        if (eventCount == -1) {
            if (errno == EINTR) {
                if (shutdownSignalRequested != 0) {
                    Logger::info("shutdown signal received");
                    stop();
                }
                continue;
            }

            Logger::error("epoll_wait failed");
            perror("epoll_wait failed");
            break;
        }

        if (shutdownSignalRequested != 0) {
            Logger::info("shutdown signal received");
            stop();
        }

        for (int i = 0; i < eventCount && impl_->running.load(); ++i) {
            const int readyFd = events[i].data.fd;
            const uint32_t readyEvents = events[i].events;

            if (readyFd == impl_->serverSocket.get()) {
                if ((readyEvents & EPOLLIN) != 0) {
                    acceptReadyClients(epollFd.get());
                }
                continue;
            }

            if (readyFd == impl_->completionEvent.get()) {
                handleCompletionEvent(epollFd.get());
                continue;
            }

            if (readyFd == impl_->shutdownEvent.get()) {
                handleShutdownEvent();
                continue;
            }

            handleConnectionEvent(epollFd.get(), readyFd, readyEvents);
        }

        if (!impl_->running.load()) {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - lastIdleScanTime >= std::chrono::milliseconds(idleScanIntervalMs)) {
            closeIdleConnections(epollFd.get(), now);
            lastIdleScanTime = now;
        }
    }

    closeAllConnections(epollFd.get());
    removeSocketFromEpoll(epollFd.get(), impl_->serverSocket.get());
    removeSocketFromEpoll(epollFd.get(), impl_->completionEvent.get());
    removeSocketFromEpoll(epollFd.get(), impl_->shutdownEvent.get());
    impl_->serverSocket.reset();
}

void Server::acceptReadyClients(int epollFd) {
    while (impl_->running.load()) {
        UniqueFd clientSocket(accept4(impl_->serverSocket.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC));
        if (!clientSocket.valid()) {
            if (wouldBlock()) {
                return;
            }

            if (errno == EINTR) {
                if (shutdownSignalRequested != 0) {
                    Logger::info("shutdown signal received");
                    stop();
                    return;
                }
                continue;
            }

            Logger::error("accept failed");
            perror("accept failed");
            return;
        }

        const int fd = clientSocket.get();
        Impl::Connection connection;
        connection.fd = std::move(clientSocket);
        connection.id = impl_->nextConnectionId++;
        connection.lastActiveTime = std::chrono::steady_clock::now();

        const auto inserted = impl_->connections.emplace(fd, std::move(connection));
        if (!inserted.second) {
            Logger::error("failed to track accepted client socket");
            continue;
        }

        if (!addSocketToEpoll(epollFd, fd, EPOLLIN | EPOLLRDHUP)) {
            Logger::error("failed to add client socket to epoll");
            perror("epoll_ctl failed");
            impl_->connections.erase(fd);
            continue;
        }
    }
}

void Server::handleConnectionEvent(int epollFd, int fd, unsigned int events) {
    if ((events & (EPOLLERR | EPOLLHUP)) != 0) {
        Logger::info("client connection closed");
        closeConnection(epollFd, fd);
        return;
    }

    if ((events & EPOLLRDHUP) != 0) {
        const auto connection = impl_->connections.find(fd);
        if (connection == impl_->connections.end() ||
            (connection->second.writeBuffer.empty() &&
             !connection->second.fileTransfer.active() &&
             !connection->second.responsePending)) {
            Logger::info("client read side closed");
            closeConnection(epollFd, fd);
            return;
        }
    }

    if ((events & EPOLLIN) != 0) {
        readFromConnection(epollFd, fd);
    }

    if ((events & EPOLLOUT) != 0) {
        writeToConnection(epollFd, fd);
    }
}

void Server::handleCompletionEvent(int epollFd) {
    while (true) {
        uint64_t value = 0;
        const ssize_t result = read(impl_->completionEvent.get(), &value, sizeof(value));
        if (result == sizeof(value)) {
            continue;
        }

        if (result == -1 && errno == EINTR) {
            continue;
        }

        if (result == -1 && wouldBlock()) {
            break;
        }

        if (result == -1) {
            Logger::error("failed to read completion event fd");
            perror("eventfd read failed");
        }
        break;
    }

    std::deque<Completion> completions;
    {
        std::lock_guard<std::mutex> lock(impl_->completionsMutex);
        completions.swap(impl_->completions);
    }

    for (Completion& completion : completions) {
        const auto connection = impl_->connections.find(completion.fd);
        if (connection == impl_->connections.end() ||
            connection->second.id != completion.connectionId) {
            continue;
        }

        HttpResponseResult response = std::move(completion.response);
        connection->second.fileTransfer.reset();

        if (response.sendFile) {
            UniqueFd file(open(response.filePath.c_str(), O_RDONLY | O_CLOEXEC));
            if (!file.valid()) {
                Logger::error("failed to open static file for sendfile: " + response.filePath);
                response = makeTextResponseResult(
                    500,
                    "Internal Server Error",
                    "Internal Server Error",
                    response.keepAlive);
            } else {
                connection->second.fileTransfer.file = std::move(file);
                connection->second.fileTransfer.offset = static_cast<off_t>(response.fileOffset);
                connection->second.fileTransfer.size = response.fileTransferSize;
            }
        }

        logAccess(completion.method, completion.path, response, completion.requestStartTime);

        connection->second.responsePending = false;
        connection->second.writeBuffer = std::move(response.response);
        connection->second.closeAfterWrite = completion.closeAfterWrite;

        if (!modifySocketInEpoll(epollFd, completion.fd, EPOLLOUT | EPOLLRDHUP)) {
            Logger::error("failed to register client socket for write");
            perror("epoll_ctl failed");
            closeConnection(epollFd, completion.fd);
        }
    }
}

void Server::handleShutdownEvent() {
    while (true) {
        uint64_t value = 0;
        const ssize_t result = read(impl_->shutdownEvent.get(), &value, sizeof(value));
        if (result == sizeof(value)) {
            continue;
        }

        if (result == -1 && errno == EINTR) {
            continue;
        }

        if (result == -1 && wouldBlock()) {
            break;
        }

        if (result == -1) {
            Logger::error("failed to read shutdown event fd");
            perror("eventfd read failed");
        }
        break;
    }

    Logger::info("shutdown requested");
    stop();
}

void Server::readFromConnection(int epollFd, int fd) {
    const auto connection = impl_->connections.find(fd);
    if (connection == impl_->connections.end() || connection->second.responsePending) {
        return;
    }

    char buffer[4096]{};
    while (true) {
        const ssize_t bytesRead = recv(fd, buffer, sizeof(buffer), 0);
        if (bytesRead > 0) {
            connection->second.lastActiveTime = std::chrono::steady_clock::now();
            connection->second.readBuffer.append(buffer, static_cast<std::size_t>(bytesRead));
            if (currentRequestHeaderTooLarge(connection->second.readBuffer)) {
                const auto requestStartTime = std::chrono::steady_clock::now();
                const HttpResponseResult response =
                    makeTextResponseResult(413, "Payload Too Large", "Payload Too Large");
                logAccess("-", "-", response, requestStartTime);
                connection->second.writeBuffer = response.response;
                connection->second.closeAfterWrite = true;
                if (!modifySocketInEpoll(epollFd, fd, EPOLLOUT | EPOLLRDHUP)) {
                    closeConnection(epollFd, fd);
                }
                return;
            }

            if (hasCompleteHeader(connection->second.readBuffer)) {
                processReadBuffer(epollFd, fd);
                return;
            }

            continue;
        }

        if (bytesRead == 0) {
            closeConnection(epollFd, fd);
            return;
        }

        if (errno == EINTR) {
            continue;
        }

        if (wouldBlock()) {
            return;
        }

        Logger::error("recv failed");
        perror("recv failed");
        closeConnection(epollFd, fd);
        return;
    }
}

void Server::writeToConnection(int epollFd, int fd) {
    const auto connection = impl_->connections.find(fd);
    if (connection == impl_->connections.end()) {
        return;
    }

    std::string& writeBuffer = connection->second.writeBuffer;
    while (!writeBuffer.empty()) {
        const ssize_t bytesSent = send(fd, writeBuffer.data(), writeBuffer.size(), 0);
        if (bytesSent > 0) {
            connection->second.lastActiveTime = std::chrono::steady_clock::now();
            writeBuffer.erase(0, static_cast<std::size_t>(bytesSent));
            continue;
        }

        if (bytesSent == 0) {
            return;
        }

        if (errno == EINTR) {
            continue;
        }

        if (wouldBlock()) {
            return;
        }

        Logger::error("send failed");
        perror("send failed");
        closeConnection(epollFd, fd);
        return;
    }

    auto& fileTransfer = connection->second.fileTransfer;
    std::size_t sendfileBudget = maxSendfileBytesPerEvent;
    while (fileTransfer.active() && sendfileBudget > 0) {
        const std::uintmax_t remaining = fileTransfer.size - fileTransfer.sent;
        const std::size_t bytesToSend = static_cast<std::size_t>(std::min<std::uintmax_t>(
            remaining,
            sendfileBudget));
        const ssize_t bytesSent = sendfile(
            fd,
            fileTransfer.file.get(),
            &fileTransfer.offset,
            bytesToSend);

        if (bytesSent > 0) {
            const auto sent = static_cast<std::uintmax_t>(bytesSent);
            fileTransfer.sent += sent;
            sendfileBudget -= static_cast<std::size_t>(bytesSent);
            connection->second.lastActiveTime = std::chrono::steady_clock::now();
            continue;
        }

        if (bytesSent == 0) {
            fileTransfer.reset();
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        if (wouldBlock()) {
            return;
        }

        Logger::error("sendfile failed");
        perror("sendfile failed");
        closeConnection(epollFd, fd);
        return;
    }

    if (fileTransfer.active()) {
        return;
    }

    fileTransfer.reset();

    if (connection->second.closeAfterWrite) {
        closeConnection(epollFd, fd);
        return;
    }

    if (hasCompleteHeader(connection->second.readBuffer)) {
        processReadBuffer(epollFd, fd);
        return;
    }

    if (!modifySocketInEpoll(epollFd, fd, EPOLLIN | EPOLLRDHUP)) {
        Logger::error("failed to register client socket for read");
        perror("epoll_ctl failed");
        closeConnection(epollFd, fd);
    }
}

void Server::processReadBuffer(int epollFd, int fd) {
    const auto connection = impl_->connections.find(fd);
    if (connection == impl_->connections.end()) {
        return;
    }

    const std::size_t requestLength = completeHeaderLength(connection->second.readBuffer);
    if (requestLength == std::string::npos) {
        return;
    }

    const std::string rawRequest = connection->second.readBuffer.substr(0, requestLength);
    const auto requestStartTime = std::chrono::steady_clock::now();
    HttpRequest request;
    if (!HttpParser::parse(rawRequest, request)) {
        Logger::warn("bad request received");
        const HttpResponseResult response = makeTextResponseResult(400, "Bad Request", "Bad Request");
        logAccess("-", "-", response, requestStartTime);
        connection->second.writeBuffer = response.response;
        connection->second.closeAfterWrite = true;
        if (!modifySocketInEpoll(epollFd, fd, EPOLLOUT | EPOLLRDHUP)) {
            closeConnection(epollFd, fd);
        }
        return;
    }

    connection->second.lastActiveTime = std::chrono::steady_clock::now();
    connection->second.readBuffer.erase(0, requestLength);
    const bool closeAfterWrite = !request.keepAlive;

    if (request.method != "GET" && request.method != "HEAD") {
        const HttpResponseResult response = makeTextResponseResult(
            405,
            "Method Not Allowed",
            "Method Not Allowed",
            request.keepAlive);
        logAccess(request.method, request.path, response, requestStartTime);
        connection->second.writeBuffer = response.response;
        connection->second.closeAfterWrite = closeAfterWrite;
        if (!modifySocketInEpoll(epollFd, fd, EPOLLOUT | EPOLLRDHUP)) {
            closeConnection(epollFd, fd);
        }
        return;
    }

    connection->second.responsePending = true;
    const unsigned long long connectionId = connection->second.id;

    if (!modifySocketInEpoll(epollFd, fd, EPOLLRDHUP)) {
        closeConnection(epollFd, fd);
        return;
    }

    const bool enqueued = threadPool_.enqueue([this, fd, connectionId, request, requestStartTime]() {
        HttpResponseResult response = buildResponse(request);
        queueResponse(
            fd,
            connectionId,
            std::move(response),
            !request.keepAlive,
            request.method,
            request.path,
            requestStartTime);
    });

    if (!enqueued) {
        connection->second.responsePending = false;
        const HttpResponseResult response =
            makeTextResponseResult(503, "Service Unavailable", "Service Unavailable");
        logAccess(request.method, request.path, response, requestStartTime);
        connection->second.writeBuffer = response.response;
        connection->second.closeAfterWrite = true;
        if (!modifySocketInEpoll(epollFd, fd, EPOLLOUT | EPOLLRDHUP)) {
            closeConnection(epollFd, fd);
        }
    }
}

void Server::queueResponse(
    int fd,
    unsigned long long connectionId,
    HttpResponseResult response,
    bool closeAfterWrite,
    std::string method,
    std::string path,
    std::chrono::steady_clock::time_point requestStartTime) {
    {
        std::lock_guard<std::mutex> lock(impl_->completionsMutex);
        impl_->completions.push_back({
            fd,
            connectionId,
            std::move(response),
            closeAfterWrite,
            std::move(method),
            std::move(path),
            requestStartTime});
    }

    notifyCompletionEvent();
}

void Server::closeIdleConnections(int epollFd, std::chrono::steady_clock::time_point now) {
    std::vector<int> timedOutFds;
    for (const auto& connection : impl_->connections) {
        if (connection.second.responsePending ||
            !connection.second.writeBuffer.empty() ||
            connection.second.fileTransfer.active()) {
            continue;
        }

        if (now - connection.second.lastActiveTime >= connectionIdleTimeout_) {
            timedOutFds.push_back(connection.first);
        }
    }

    for (int fd : timedOutFds) {
        Logger::info("client connection timed out");
        closeConnection(epollFd, fd);
    }
}

void Server::closeAllConnections(int epollFd) {
    std::vector<int> fds;
    fds.reserve(impl_->connections.size());
    for (const auto& connection : impl_->connections) {
        fds.push_back(connection.first);
    }

    for (int fd : fds) {
        closeConnection(epollFd, fd);
    }
}

void Server::closeConnection(int epollFd, int fd) {
    removeSocketFromEpoll(epollFd, fd);
    impl_->connections.erase(fd);
}

HttpResponseResult Server::buildResponse(const HttpRequest& request) const {
    try {
        const auto rangeHeader = request.headers.find("range");
        const StaticFileResult file = staticFileHandler_.handle(
            request.path,
            rangeHeader == request.headers.end()
                ? std::nullopt
                : std::optional<std::string>(rangeHeader->second));
        return makeStaticFileResponseResult(file, request.keepAlive, request.method != "HEAD");
    } catch (const std::exception&) {
        Logger::error("internal server error while handling request");
        return makeTextResponseResult(
            500,
            "Internal Server Error",
            "Internal Server Error",
            request.keepAlive,
            request.method != "HEAD");
    }
}

void Server::notifyCompletionEvent() {
    uint64_t value = 1;
    while (true) {
        const ssize_t result = write(impl_->completionEvent.get(), &value, sizeof(value));
        if (result == sizeof(value)) {
            return;
        }

        if (result == -1 && errno == EINTR) {
            continue;
        }

        if (result == -1 && wouldBlock()) {
            return;
        }

        Logger::error("failed to notify completion event fd");
        perror("eventfd write failed");
        return;
    }
}

void Server::notifyShutdownEvent() {
    if (impl_ == nullptr || !impl_->shutdownEvent.valid()) {
        return;
    }

    uint64_t value = 1;
    while (true) {
        const ssize_t result = write(impl_->shutdownEvent.get(), &value, sizeof(value));
        if (result == sizeof(value)) {
            return;
        }

        if (result == -1 && errno == EINTR) {
            continue;
        }

        if (result == -1 && wouldBlock()) {
            return;
        }

        Logger::error("failed to notify shutdown event fd");
        perror("eventfd write failed");
        return;
    }
}
