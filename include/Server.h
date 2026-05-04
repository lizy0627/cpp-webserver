#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

class Server {
public:
    explicit Server(unsigned short port);
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
};
