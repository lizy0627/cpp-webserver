#include "Server.h"

#include <iostream>
#include <string>

// 构造函数只保存配置，不立即占用系统资源。
Server::Server(unsigned short port)
    : port_(port),
      serverSocket_(INVALID_SOCKET),
      winsockInitialized_(false) {}

// 析构函数负责统一清理资源，防止 socket 泄漏。
Server::~Server() {
    if (serverSocket_ != INVALID_SOCKET) {
        closesocket(serverSocket_);
    }

    if (winsockInitialized_) {
        WSACleanup();
    }
}

// 启动服务器，按网络编程的标准顺序完成初始化、绑定、监听和接收连接。
bool Server::start() {
    if (!initializeWinsock()) {
        return false;
    }

    if (!createSocket()) {
        return false;
    }

    if (!bindSocket()) {
        return false;
    }

    if (!listenSocket()) {
        return false;
    }

    std::cout << "Web server started at http://localhost:" << port_ << std::endl;
    acceptLoop();
    return true;
}

// 初始化 Winsock 2.2，这是 Windows 使用 socket API 的前置条件。
bool Server::initializeWinsock() {
    WSADATA wsaData{};
    const int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return false;
    }

    winsockInitialized_ = true;
    return true;
}

// 创建 IPv4 TCP socket，用作服务器监听入口。
bool Server::createSocket() {
    serverSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket_ == INVALID_SOCKET) {
        std::cerr << "socket failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    return true;
}

// 将服务器 socket 绑定到本机所有网卡的指定端口。
bool Server::bindSocket() {
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port_);

    const int result = bind(
        serverSocket_,
        reinterpret_cast<sockaddr*>(&serverAddress),
        sizeof(serverAddress));

    if (result == SOCKET_ERROR) {
        std::cerr << "bind failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    return true;
}

// 让 socket 进入监听状态，等待浏览器或其他客户端连接。
bool Server::listenSocket() {
    const int result = listen(serverSocket_, SOMAXCONN);
    if (result == SOCKET_ERROR) {
        std::cerr << "listen failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    return true;
}

// 主循环持续接收客户端连接，每次连接处理完成后立即关闭客户端 socket。
void Server::acceptLoop() {
    while (true) {
        const SOCKET clientSocket = accept(serverSocket_, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "accept failed: " << WSAGetLastError() << std::endl;
            continue;
        }

        handleClient(clientSocket);
    }
}

// 读取客户端请求并返回一个最小可用的 HTTP Hello World 响应。
void Server::handleClient(SOCKET clientSocket) {
    char buffer[1024]{};
    recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

    const std::string body = "Hello World";
    const std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body;

    send(clientSocket, response.c_str(), static_cast<int>(response.size()), 0);
    closesocket(clientSocket);
}
