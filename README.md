# CppWebServer

一个用 C++17 写的简易 HTTP 静态文件服务器。项目主要用于练习网络编程、HTTP 报文解析、线程池和基于 `epoll` 的事件驱动模型。

## 功能

- 支持 `GET` 请求
- 默认监听 `8080` 端口
- 从 `www` 目录读取静态文件
- `GET /` 会返回 `www/index.html`
- 对不存在的文件返回 `404 Not Found`
- 拒绝 `..`、反斜杠、空字节等不安全路径
- 支持常见 MIME 类型，例如 `html`、`css`、`js`、`png`、`jpg`、`json`、`pdf` 等
- 使用线程池处理请求任务
- Linux 下使用非阻塞 socket 和 `epoll`

## 环境要求

项目的服务器主体依赖 Linux 的 `epoll` 和 POSIX socket，建议在 Linux 环境下运行。

Windows 下可以用 CMake 编译核心代码和测试，但实际服务器会走 `ServerUnsupported.cpp`，启动时会提示需要 Linux 环境。

需要的工具：

- CMake 3.10 或更高版本
- 支持 C++17 的编译器
- Linux 环境下建议使用 GCC 或 Clang

## 构建

```bash
cmake -S . -B build
cmake --build build
```

如果想开启 AddressSanitizer，可以这样配置：

```bash
cmake -S . -B build -DWEBSERVER_ENABLE_ASAN=ON
cmake --build build
```

## 运行

在项目根目录运行：

```bash
./build/WebServer
```

如果使用多配置生成器，例如 Visual Studio，生成的程序可能在 `build/Debug/` 或 `build/Release/` 下面。

启动后可以访问：

```text
http://localhost:8080/
```

## 配置

配置文件在 `config/server.conf`：

```conf
port=8080
thread_num=4
root=www
```

字段说明：

- `port`：监听端口
- `thread_num`：线程池线程数量
- `root`：静态文件根目录

如果配置文件不存在，程序会使用默认值：`8080` 端口、`4` 个线程、`www` 目录。

## 测试

项目带了一组基础测试，覆盖 HTTP 解析、响应生成、静态文件处理和线程池行为。

```bash
ctest --test-dir build --output-on-failure
```

Visual Studio 这类多配置生成器可以指定配置：

```bash
ctest --test-dir build -C Debug --output-on-failure
```

## 目录结构

```text
.
├── config/      # 配置文件
├── include/     # 头文件
├── src/         # 源文件
├── tests/       # 测试代码
├── www/         # 静态文件目录
└── CMakeLists.txt
```

## 说明

这个项目目前只实现了静态文件服务，HTTP 支持也比较基础，适合学习和实验使用，不建议直接作为生产环境服务器。
