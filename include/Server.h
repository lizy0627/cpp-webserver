#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <string>

#include "StaticFileHandler.h"
#include "ThreadPool.h"

class Server {
public:
    Server(unsigned short port, std::size_t threadCount, std::string rootDirectory);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start();

private:
    bool createSocket();
    bool bindSocket();
    bool listenSocket();
    void acceptLoop();
    void acceptReadyClients(int epollFd);
    void handleClient(int clientSocket);

    unsigned short port_;
    int serverSocket_;
    ThreadPool threadPool_;
    std::string rootDirectory_;
    StaticFileHandler staticFileHandler_;
};
