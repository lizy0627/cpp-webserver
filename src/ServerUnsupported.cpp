#include "Server.h"

#include "Logger.h"

#include <utility>

struct Server::Impl {};

Server::Server(
    unsigned short port,
    std::size_t threadCount,
    std::string rootDirectory,
    bool enableDirectoryListing,
    std::chrono::seconds connectionIdleTimeout,
    std::size_t maxRequestBodySize,
    bool enableTls,
    std::string certFile,
    std::string keyFile)
    : port_(port),
      rootDirectory_(std::move(rootDirectory)),
      staticFileHandler_(rootDirectory_, enableDirectoryListing),
      connectionIdleTimeout_(connectionIdleTimeout),
      maxRequestBodySize_(maxRequestBodySize),
      enableTls_(enableTls),
      certFile_(std::move(certFile)),
      keyFile_(std::move(keyFile)),
      impl_(nullptr),
      threadPool_(threadCount) {}

Server::~Server() = default;

bool Server::start() {
    Logger::error("WebServer requires Linux epoll and POSIX sockets.");
    return false;
}

void Server::stop() {}
