#pragma once

#include <arpa/inet.h>   // 提供 inet 相关函数和网络字节序工具。
#include <netinet/in.h>  // 提供 sockaddr_in 等 IPv4 地址结构。
#include <sys/socket.h>  // 提供 socket、bind、listen、accept、send、recv。
#include <unistd.h>      // 提供 close 函数，用于关闭 socket 文件描述符。

// Server 封装 Linux HTTP 服务器的生命周期和核心网络逻辑。
class Server {
public:
    // 构造服务器对象，保存监听端口。
    explicit Server(unsigned short port);

    // 析构服务器对象，关闭服务器 socket 文件描述符。
    ~Server();

    // 禁止拷贝，避免多个对象重复关闭同一个 socket 文件描述符。
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // 启动服务器，依次完成 socket、bind、listen 和 accept。
    bool start();

private:
    // 创建 TCP 监听 socket。
    bool createSocket();

    // 将监听 socket 绑定到指定端口。
    bool bindSocket();

    // 开始监听客户端连接。
    bool listenSocket();

    // 循环接收客户端连接并处理 HTTP 请求。
    void acceptLoop();

    // 处理单个客户端连接并返回 HTTP 响应。
    void handleClient(int clientSocket);

    unsigned short port_;  // 保存服务器监听端口。
    int serverSocket_;     // 保存服务器监听 socket 文件描述符。
};
