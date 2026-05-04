# C++ Web Server 核心代码整理

本文档根据当前项目源码整理核心结构和核心代码，方便快速理解这个最小 HTTP Web Server 的运行流程。

注意：当前源码文件中的中文注释在终端读取时出现了编码乱码，并且有些代码和注释显示在同一行。下面的代码按项目意图重新整理为更清晰的阅读版本，没有直接保留乱码注释。

## 项目结构

```text
cpp-webserver/
|-- CMakeLists.txt
|-- include/
|   `-- Server.h
`-- src/
    |-- main.cpp
    `-- Server.cpp
```

核心模块只有一个 `Server` 类：

- `src/main.cpp`：程序入口，创建服务器并启动。
- `include/Server.h`：声明服务器类、生命周期函数和 socket 处理函数。
- `src/Server.cpp`：实现 socket 创建、端口绑定、监听、接收连接和 HTTP 响应。
- `CMakeLists.txt`：配置 CMake 构建可执行程序 `WebServer`。

## 核心流程

```text
main()
  `-- 创建 Server(8080)
      `-- server.start()
          |-- createSocket()
          |-- bindSocket()
          |-- listenSocket()
          `-- acceptLoop()
              `-- handleClient(clientSocket)
                  |-- recv()
                  |-- 构造 HTTP 响应
                  |-- send()
                  `-- close()
```

这个项目本质上是一个同步、单线程、阻塞式 HTTP 服务端。它监听 `8080` 端口，每接受一个 TCP 连接，就读取一次请求并返回固定的 `Hello World` 文本响应。

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.10)

project(CppWebServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(WebServer
    src/main.cpp
    src/Server.cpp
)

target_include_directories(WebServer PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
```

说明：

- 使用 C++17 标准。
- 编译入口文件 `src/main.cpp` 和服务器实现文件 `src/Server.cpp`。
- 将 `include` 加入头文件搜索路径，使 `#include "Server.h"` 可以被正确找到。

## include/Server.h

```cpp
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
    void handleClient(int clientSocket);

    unsigned short port_;
    int serverSocket_;
};
```

说明：

- `port_` 保存监听端口。
- `serverSocket_` 保存服务端监听 socket 文件描述符。
- 禁止拷贝，避免多个 `Server` 对象重复关闭同一个 socket。
- `start()` 是对外暴露的启动入口。
- 私有函数拆分了 socket 服务端启动和请求处理过程。

## src/main.cpp

```cpp
#include "Server.h"

int main() {
    Server server(8080);
    return server.start() ? 0 : 1;
}
```

说明：

- 程序入口只负责创建服务器对象。
- `Server server(8080)` 表示监听本机 `8080` 端口。
- `server.start()` 成功返回 `0`，失败返回 `1`。

## src/Server.cpp

```cpp
#include "Server.h"

#include <cstdio>
#include <iostream>
#include <string>

Server::Server(unsigned short port)
    : port_(port),
      serverSocket_(-1) {}

Server::~Server() {
    if (serverSocket_ != -1) {
        close(serverSocket_);
    }
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

    std::cout << "Web server started at http://localhost:" << port_ << std::endl;
    acceptLoop();
    return true;
}

bool Server::createSocket() {
    serverSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket_ == -1) {
        perror("socket failed");
        return false;
    }

    return true;
}

bool Server::bindSocket() {
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port_);

    const int result = bind(
        serverSocket_,
        reinterpret_cast<sockaddr*>(&serverAddress),
        sizeof(serverAddress));

    if (result == -1) {
        perror("bind failed");
        return false;
    }

    return true;
}

bool Server::listenSocket() {
    const int result = listen(serverSocket_, SOMAXCONN);
    if (result == -1) {
        perror("listen failed");
        return false;
    }

    return true;
}

void Server::acceptLoop() {
    while (true) {
        const int clientSocket = accept(serverSocket_, nullptr, nullptr);
        if (clientSocket == -1) {
            perror("accept failed");
            continue;
        }

        handleClient(clientSocket);
    }
}

void Server::handleClient(int clientSocket) {
    char buffer[1024]{};
    recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

    const std::string body = "Hello World";
    const std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n"
        + body;

    send(clientSocket, response.c_str(), response.size(), 0);
    close(clientSocket);
}
```

## 关键函数说明

### `Server::createSocket()`

```cpp
serverSocket_ = socket(AF_INET, SOCK_STREAM, 0);
```

创建一个 IPv4 TCP socket：

- `AF_INET`：IPv4。
- `SOCK_STREAM`：TCP 字节流。
- `0`：使用默认协议，也就是 TCP。

### `Server::bindSocket()`

```cpp
sockaddr_in serverAddress{};
serverAddress.sin_family = AF_INET;
serverAddress.sin_addr.s_addr = INADDR_ANY;
serverAddress.sin_port = htons(port_);
```

准备服务端地址：

- `INADDR_ANY`：绑定本机所有网卡地址。
- `htons(port_)`：将主机字节序的端口转换为网络字节序。

然后调用：

```cpp
bind(serverSocket_, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress));
```

把监听 socket 绑定到指定端口。

### `Server::listenSocket()`

```cpp
listen(serverSocket_, SOMAXCONN);
```

让 socket 进入监听状态。`SOMAXCONN` 使用系统允许的最大等待连接队列长度。

### `Server::acceptLoop()`

```cpp
while (true) {
    const int clientSocket = accept(serverSocket_, nullptr, nullptr);
    if (clientSocket == -1) {
        perror("accept failed");
        continue;
    }

    handleClient(clientSocket);
}
```

这是服务器主循环：

- 阻塞等待客户端连接。
- 每次 `accept()` 成功后得到一个新的客户端 socket。
- 调用 `handleClient()` 处理当前连接。
- 当前实现是同步处理，一个请求处理完才会接受下一个请求。

### `Server::handleClient()`

```cpp
char buffer[1024]{};
recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
```

读取客户端请求。当前版本只是读取请求，不解析 HTTP 方法、路径或请求头。

```cpp
const std::string body = "Hello World";
const std::string response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Content-Length: " + std::to_string(body.size()) + "\r\n"
    "Connection: close\r\n"
    "\r\n"
    + body;
```

构造一个最小可用 HTTP 响应：

- 状态码：`200 OK`
- 内容类型：`text/plain; charset=utf-8`
- 内容长度：根据 `body.size()` 自动计算
- 连接策略：响应后关闭连接
- 响应体：`Hello World`

最后：

```cpp
send(clientSocket, response.c_str(), response.size(), 0);
close(clientSocket);
```

发送响应并关闭客户端连接。

## 当前实现特点

- 简单清晰，适合作为 socket 和 HTTP 服务端入门示例。
- 使用 RAII 思路在析构函数中关闭服务端 socket。
- 每个客户端连接处理完后主动关闭。
- 请求内容没有解析，所以无论访问什么路径都会返回 `Hello World`。
- 单线程阻塞模型，同时只能处理一个连接。
- 没有处理 `recv()` 和 `send()` 的返回值，生产环境需要补充错误处理和短写处理。

## 运行方式

```bash
cmake -S . -B build
cmake --build build
./build/WebServer
```

启动后访问：

```text
http://localhost:8080
```

预期响应：

```text
Hello World
```
