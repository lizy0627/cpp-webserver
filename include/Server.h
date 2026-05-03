#pragma once

#include <winsock2.h>

// Server 封装 HTTP 服务器的生命周期和核心网络逻辑。
class Server {
public:
    // 构造服务器对象，保存监听端口。
    explicit Server(unsigned short port);

    // 析构服务器对象，释放 socket 和 Winsock 资源。
    ~Server();

    // 禁止拷贝，避免多个对象重复关闭同一个 socket。
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // 启动服务器，依次完成 socket、bind、listen 和 accept。
    bool start();

private:
    // 初始化 Windows Socket 库。
    bool initializeWinsock();

    // 创建 TCP 监听 socket。
    bool createSocket();

    // 将监听 socket 绑定到指定端口。
    bool bindSocket();

    // 开始监听客户端连接。
    bool listenSocket();

    // 循环接收客户端连接并处理 HTTP 请求。
    void acceptLoop();

    // 处理单个客户端连接并返回 HTTP 响应。
    void handleClient(SOCKET clientSocket);

    unsigned short port_;
    SOCKET serverSocket_;
    bool winsockInitialized_;
};
