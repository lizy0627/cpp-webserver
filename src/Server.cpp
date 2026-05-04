#include "Server.h"

#include <cstdio>   // 提供 perror，用于打印 Linux 系统调用错误。
#include <iostream> // 提供 cout，用于输出服务器启动信息。
#include <string>   // 提供 string，用于构造 HTTP 响应内容。

// 构造函数只保存端口配置，并把 socket 初始化为无效文件描述符。
Server::Server(unsigned short port)
    : port_(port),       // 保存外部传入的监听端口。
      serverSocket_(-1)  // Linux 中 socket 失败时返回 -1，这里用 -1 表示尚未创建。
{}

// 析构函数负责关闭服务器 socket，避免文件描述符泄漏。
Server::~Server() {
    if (serverSocket_ != -1) { // 只有 socket 已经创建成功时才需要关闭。
        close(serverSocket_);  // 使用 Linux close 关闭 socket 文件描述符。
    }
}

// 启动服务器，按 socket、bind、listen、accept 的顺序执行。
bool Server::start() {
    if (!createSocket()) { // 创建监听 socket，如果失败则启动失败。
        return false;      // 返回 false 通知 main 程序异常退出。
    }

    if (!bindSocket()) { // 将 socket 绑定到指定端口，如果失败则启动失败。
        return false;    // 返回 false 通知 main 程序异常退出。
    }

    if (!listenSocket()) { // 让 socket 进入监听状态，如果失败则启动失败。
        return false;      // 返回 false 通知 main 程序异常退出。
    }

    std::cout << "Web server started at http://localhost:" << port_ << std::endl; // 输出服务器访问地址。
    acceptLoop();                                                                  // 进入主循环，持续处理客户端连接。
    return true;                                                                   // 正常情况下 acceptLoop 不会返回。
}

// 创建 IPv4 TCP socket，用作服务器监听入口。
bool Server::createSocket() {
    serverSocket_ = socket(AF_INET, SOCK_STREAM, 0); // 创建 IPv4 + TCP socket，协议参数 0 表示使用默认 TCP。
    if (serverSocket_ == -1) {                       // Linux socket 创建失败会返回 -1。
        perror("socket failed");                     // 打印系统错误原因。
        return false;                                // 通知调用方创建失败。
    }

    return true; // socket 创建成功。
}

// 将服务器 socket 绑定到本机所有网卡的指定端口。
bool Server::bindSocket() {
    sockaddr_in serverAddress{};                   // 创建 IPv4 地址结构，并初始化为 0。
    serverAddress.sin_family = AF_INET;            // 指定地址族为 IPv4。
    serverAddress.sin_addr.s_addr = INADDR_ANY;    // 绑定到本机所有网卡地址。
    serverAddress.sin_port = htons(port_);         // 将主机字节序端口转换为网络字节序。

    const int result = bind(                       // 调用 bind 将 socket 和地址绑定。
        serverSocket_,                             // 服务器监听 socket 文件描述符。
        reinterpret_cast<sockaddr*>(&serverAddress), // 将 IPv4 地址结构转换为通用 sockaddr 指针。
        sizeof(serverAddress));                    // 传入地址结构大小。

    if (result == -1) {        // Linux bind 失败会返回 -1。
        perror("bind failed"); // 打印系统错误原因。
        return false;          // 通知调用方绑定失败。
    }

    return true; // bind 成功。
}

// 让 socket 进入监听状态，等待浏览器或其他客户端连接。
bool Server::listenSocket() {
    const int result = listen(serverSocket_, SOMAXCONN); // 使用系统最大等待队列长度开始监听。
    if (result == -1) {                                  // Linux listen 失败会返回 -1。
        perror("listen failed");                         // 打印系统错误原因。
        return false;                                    // 通知调用方监听失败。
    }

    return true; // listen 成功。
}

// 主循环持续接收客户端连接，每次连接处理完成后关闭客户端 socket。
void Server::acceptLoop() {
    while (true) {                                                 // 服务器持续运行，循环等待新连接。
        const int clientSocket = accept(serverSocket_, nullptr, nullptr); // 接受一个客户端连接。
        if (clientSocket == -1) {                                  // Linux accept 失败会返回 -1。
            perror("accept failed");                              // 打印系统错误原因。
            continue;                                             // 跳过本次失败，继续等待下一个连接。
        }

        handleClient(clientSocket); // 处理当前客户端请求并返回响应。
    }
}

// 读取客户端请求并返回一个最小可用的 HTTP Hello World 响应。
void Server::handleClient(int clientSocket) {
    char buffer[1024]{};                                     // 创建接收缓冲区，用于读取 HTTP 请求。
    recv(clientSocket, buffer, sizeof(buffer) - 1, 0);       // 接收客户端请求，当前简单服务器不解析请求内容。

    const std::string body = "Hello World";                  // 定义 HTTP 响应体内容。
    const std::string response =                             // 构造完整 HTTP 响应报文。
        "HTTP/1.1 200 OK\r\n"                                // 设置 HTTP 状态行为 200 OK。
        "Content-Type: text/plain; charset=utf-8\r\n"        // 设置响应内容类型为 UTF-8 文本。
        "Content-Length: " + std::to_string(body.size()) + "\r\n" // 设置响应体长度。
        "Connection: close\r\n"                              // 告诉客户端响应后关闭连接。
        "\r\n"                                               // 空行用于分隔响应头和响应体。
        + body;                                              // 拼接响应体。

    send(clientSocket, response.c_str(), response.size(), 0); // 发送 HTTP 响应给客户端。
    close(clientSocket);                                      // 使用 Linux close 关闭当前客户端 socket。
}
