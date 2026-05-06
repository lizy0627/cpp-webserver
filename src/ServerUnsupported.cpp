#include "Server.h"

#include "Logger.h"

#include <utility>

struct Server::Impl {};

Server::Server(unsigned short port, std::size_t threadCount, std::string rootDirectory)
    : port_(port),
      threadPool_(threadCount),
      rootDirectory_(std::move(rootDirectory)),
      staticFileHandler_(rootDirectory_),
      impl_(nullptr) {}

Server::~Server() = default;

bool Server::start() {
    Logger::error("WebServer requires Linux epoll and POSIX sockets.");
    return false;
}
