#include "Server.h"

#include "Logger.h"

#include <utility>

struct Server::Impl {};

Server::Server(
    unsigned short port,
    std::size_t threadCount,
    std::string rootDirectory,
    std::chrono::seconds connectionIdleTimeout)
    : port_(port),
      rootDirectory_(std::move(rootDirectory)),
      staticFileHandler_(rootDirectory_),
      connectionIdleTimeout_(connectionIdleTimeout),
      impl_(nullptr),
      threadPool_(threadCount) {}

Server::~Server() = default;

bool Server::start() {
    Logger::error("WebServer requires Linux epoll and POSIX sockets.");
    return false;
}

void Server::stop() {}
