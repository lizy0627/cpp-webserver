# CppWebServer

CppWebServer 是一个基于 C++17 实现的 Linux 高性能静态 HTTP 服务器。项目围绕 `epoll` 事件驱动模型、非阻塞 I/O、线程池任务处理和 `eventfd` 跨线程通知展开，支持 Keep-Alive、HEAD 请求、HTTP Range 请求、空闲连接超时和静态文件服务，并包含核心模块的单元测试。

该项目适合作为简历中的 C++ 网络编程实践项目，重点展示 Linux I/O 多路复用、HTTP 协议处理、并发模型设计、资源管理和基础工程化能力。

## 技术栈

- 语言标准：C++17
- 构建系统：CMake 3.10+
- 网络模型：Linux `epoll` + 非阻塞 socket
- 并发模型：固定线程池 + 有界任务队列
- 线程通知：`eventfd`
- 测试工具：CTest + 自定义单元测试
- 调试工具：AddressSanitizer
- 压测工具：wrk

## 核心功能

- 支持 HTTP/1.0、HTTP/1.1 请求解析
- 支持 `GET` 和 `HEAD` 方法
- 支持 HTTP Keep-Alive，并根据 `Connection` 头决定连接复用策略
- 支持空闲连接超时，默认 30 秒
- 基于 `epoll` 管理监听 socket、客户端 socket 和 `eventfd`
- 使用非阻塞 I/O 处理 accept、read、write
- 使用线程池处理静态文件响应构建，避免阻塞事件循环
- Linux 下使用 `sendfile()` 分块发送静态文件内容，响应头仍通过普通 socket 写入
- 支持单段 HTTP Range 请求：`bytes=0-1023`、`bytes=100-`、`bytes=-500`
- 使用 `eventfd` 将工作线程完成事件通知回主事件循环
- 支持捕获 SIGINT/SIGTERM，触发 epoll 主循环优雅退出
- 提供静态文件服务，默认根目录为 `www`
- `GET /` 映射到 `www/index.html`
- 支持目录索引：访问目录且不存在 `index.html` 时可生成 HTML 文件列表，开关由 `enable_directory_listing` 控制
- 支持常见 MIME 类型：HTML、CSS、JavaScript、JSON、PNG、JPEG、GIF、SVG、ICO、PDF、WASM 等
- 路径安全检查：拒绝 `..`、反斜杠、空字节、非法 URL 编码和越界访问
- 对异常请求返回 400、403、404、405、413、416、500、503 等响应
- 每个请求完成响应构建后输出访问日志，记录方法、路径、状态码、响应长度、耗时和 Keep-Alive 状态
- 覆盖 HTTP 解析、响应生成、静态文件处理、配置解析和线程池行为的单元测试

## 架构设计

整体采用 Reactor + 线程池的结构：

```text
Client
  |
  v
Non-blocking Socket
  |
  v
epoll Event Loop
  |-- accept new connections
  |-- read request headers
  |-- detect idle connections
  |-- write responses
  |
  v
ThreadPool
  |
  v
StaticFileHandler / HttpResponse
  |
  v
eventfd notification
  |
  v
epoll Event Loop writes response
```

关键模块说明：

- `Server`：负责监听 socket 创建、`epoll` 事件循环、连接状态管理、读写缓冲区管理、空闲连接清理、线程池任务派发和访问日志记录。
- `ThreadPool`：固定数量 worker 线程，使用条件变量调度任务，并通过有界队列限制任务堆积。
- `HttpParser`：解析请求行和请求头，保留原始 request-target，支持 Header 名大小写归一化、HTTP 版本校验和 Keep-Alive 判断。
- `HttpResponse`：生成 HTTP 响应报文，支持自定义 Header，支持 HEAD 场景下仅输出响应头但保留正确的 `Content-Length`。
- `StaticFileHandler`：解析静态文件路径、执行 URL decode、路径安全检查和单段 Range 解析，返回文件路径、大小、MIME 类型、文件偏移和传输长度等元信息。
- `Config`：读取 `config/server.conf`，提供端口、线程数、静态文件根目录和空闲连接超时配置。

## 目录结构

```text
.
|-- CMakeLists.txt              # 顶层 CMake 构建配置
|-- CMakeSettings.json          # Visual Studio CMake 配置
|-- cmake/
|   `-- CTestDefaultConfig.cmake
|-- config/
|   `-- server.conf             # 服务端运行配置
|-- include/                    # 头文件
|   |-- Config.h
|   |-- CommandLineOptions.h
|   |-- HttpParser.h
|   |-- HttpRequest.h
|   |-- HttpResponse.h
|   |-- Logger.h
|   |-- Server.h
|   |-- StaticFileHandler.h
|   `-- ThreadPool.h
|-- src/                        # 核心源码
|   |-- CommandLineOptions.cpp
|   |-- Config.cpp
|   |-- HttpParser.cpp
|   |-- HttpResponse.cpp
|   |-- Logger.cpp
|   |-- Server.cpp              # Linux epoll 实现
|   |-- ServerUnsupported.cpp   # 非 Linux 平台提示实现
|   |-- StaticFileHandler.cpp
|   |-- ThreadPool.cpp
|   `-- main.cpp
|-- tests/
|   |-- CMakeLists.txt
|   `-- test_core.cpp           # 核心模块单元测试
`-- www/
    `-- index.html              # 默认静态页面
```

## 构建方式

推荐在 Linux 环境构建和运行服务端，因为主服务实现依赖 Linux `epoll`、`eventfd` 和 POSIX socket。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Debug 构建：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

说明：

- 非 Linux 平台可以编译核心代码和测试，但服务端入口会使用 `ServerUnsupported.cpp`，启动时提示需要 Linux 环境。
- 多配置生成器，如 Visual Studio，产物可能位于 `build/Debug` 或 `build/Release`。

## 运行方式

在项目根目录运行：

```bash
./build/WebServer
```

查看命令行帮助：

```bash
./build/WebServer --help
```

可以通过命令行覆盖配置文件中的端口、静态资源目录、线程数和空闲连接超时时间：

```bash
./build/WebServer --port 9090 --root www --threads 8 --timeout 60 --max-body-size 1048576
```

也可以指定其他配置文件：

```bash
./build/WebServer --config config/server.conf
```

启动后访问：

```text
http://127.0.0.1:8080/
```

也可以使用 curl 验证：

```bash
curl -i http://127.0.0.1:8080/
curl -I http://127.0.0.1:8080/
```

Range 请求支持单段字节范围。合法 Range 返回 `206 Partial Content`，并带有 `Content-Range: bytes start-end/total` 和范围长度对应的 `Content-Length`；不可满足或格式非法的 Range 返回 `416 Range Not Satisfiable`，并带有 `Content-Range: bytes */total`。`HEAD + Range` 只返回相同的响应头，不发送 body。

```bash
curl -i -H "Range: bytes=0-3" http://127.0.0.1:8080/
curl -i -H "Range: bytes=3-" http://127.0.0.1:8080/
curl -i -H "Range: bytes=-4" http://127.0.0.1:8080/
curl -I -H "Range: bytes=0-3" http://127.0.0.1:8080/
```

访问目录时，服务器会先查找该目录下的 `index.html`；如果不存在且 `enable_directory_listing=true`，会动态生成目录索引页。目录索引包含文件名、是否目录、文件大小和修改时间，目录项链接会进行 URL 编码。`HEAD` 请求目录索引只返回 header，不发送 body；`Range` 请求不会作用于动态生成的目录索引，会按普通 200 响应返回完整索引。

POST request bodies are capped by `max_request_body_size` (default `1048576` bytes). The server rejects a POST whose `Content-Length` is over the limit with `413 Payload Too Large`; while reading a POST body, it also returns 413 if buffered body bytes exceed the limit. `GET` and `HEAD` do not wait for a request body.

```bash
curl -i -X POST http://127.0.0.1:8080/api/echo -d "hello"
python3 - <<'PY' | curl -i -X POST http://127.0.0.1:8080/api/echo --data-binary @-
print("x" * (1024 * 1024 + 1), end="")
PY
```

访问日志会输出到标准输出，格式示例：

```text
[ACCESS] method=GET path=/index.html status=200 bytes=1024 cost=2ms keep_alive=true
```

其中 `bytes` 表示响应体长度，也就是响应头中的 `Content-Length`。对 `HEAD` 请求，实际不会发送 body，但日志仍记录对应 GET 响应的 `Content-Length`。静态文件 200/206 响应还会记录文件路径、文件大小和 `Content-Type`，400、404、405、413、416、500、503 等错误响应也会输出访问日志。

手动验证优雅退出：

```bash
./build/WebServer
curl -i http://127.0.0.1:8080/
# 回到服务器终端按 Ctrl+C
```

预期现象：

- 服务器打印启动日志。
- 页面可以正常访问。
- 按 `Ctrl+C` 后触发 SIGINT，服务器打印退出日志并正常结束进程。
- 也可以通过 `kill -TERM <pid>` 验证 SIGTERM 退出路径。

## 配置说明

默认配置文件为 `config/server.conf`：

```conf
port=8080
thread_num=4
root=www
connection_idle_timeout_seconds=30
max_request_body_size=1048576
enable_directory_listing=true
enable_tls=false
cert_file=cert.pem
key_file=key.pem
```

配置项说明：

| 配置项 | 默认值 | 说明 |
| --- | --- | --- |
| `port` | `8080` | 服务监听端口 |
| `thread_num` | `4` | 线程池 worker 数量 |
| `root` | `www` | 静态文件根目录 |
| `connection_idle_timeout_seconds` | `30` | Keep-Alive 空闲连接超时时间，单位为秒 |
| `max_request_body_size` | `1048576` | Maximum POST request body size in bytes; over-limit requests return 413 |
| `enable_directory_listing` | `true` | 是否在目录缺少 `index.html` 时生成目录索引；设为 `false` 可关闭 |

当配置文件不存在或配置非法时，程序会回退到默认配置。

命令行参数优先级高于配置文件，支持的参数如下：

| 参数 | 说明 |
| --- | --- |
| `--config <path>` | 指定配置文件路径，默认 `config/server.conf` |
| `--port <port>` | 覆盖监听端口，范围为 `1-65535` |
| `--root <path>` | 覆盖静态文件根目录 |
| `--threads <num>` | 覆盖线程池 worker 数量，必须为正整数 |
| `--timeout <sec>` | 覆盖 Keep-Alive 空闲连接超时时间，单位为秒，必须为正整数 |
| `--max-body-size <bytes>` | Override maximum POST request body size in bytes |
| `--help` | 打印帮助信息并退出 |

示例：

```bash
./build/WebServer --config config/server.conf --port 8081 --threads 8 --max-body-size 2097152
```

### HTTPS/TLS

TLS is optional and uses OpenSSL. When `enable_tls=false`, the server keeps the existing plain HTTP `recv/send/sendfile` behavior. When `enable_tls=true`, accepted connections perform `SSL_accept`, then HTTP/1.x requests are read and written with `SSL_read` and `SSL_write`.

Generate a local self-signed certificate for testing:

```bash
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout key.pem \
  -out cert.pem \
  -days 365 \
  -subj "/CN=localhost"
```

Enable TLS in `config/server.conf`:

```conf
enable_tls=true
cert_file=cert.pem
key_file=key.pem
```

Use `curl -k https://127.0.0.1:8080/` with the self-signed certificate. HTTP/2 is not implemented; TLS mode serves the existing HTTP/1.x protocol.

## 测试方式

构建后运行单元测试：

```bash
ctest --test-dir build --output-on-failure
```

多配置生成器可指定配置：

```bash
ctest --test-dir build -C Debug --output-on-failure
```

当前测试覆盖内容：

- HTTP 请求解析：请求行、Header、原始 request-target 保留、HTTP/1.0 与 HTTP/1.1、Keep-Alive、非法报文拒绝
- HTTP 响应生成：状态行、Header、`Content-Length`、`Connection`、自定义 Header、HEAD 无 body 响应
- 静态文件服务：根路径映射、URL decode、query string 忽略、404、MIME 类型、路径穿越防护、文件元信息、目录索引、单段 Range 解析
- 配置解析：端口、线程数、根目录、空闲连接超时、默认值
- 命令行参数：默认配置路径、配置覆盖、非法参数和帮助信息
- 线程池：任务执行、默认 worker 创建、队列满拒绝、析构时等待任务完成

手动验证 `sendfile()` 静态文件发送路径：

```bash
mkdir -p /tmp/cpp-webserver-sendfile
printf small-body > /tmp/cpp-webserver-sendfile/small.txt
dd if=/dev/zero of=/tmp/cpp-webserver-sendfile/large.bin bs=1M count=100
./build/WebServer --port 18080 --root /tmp/cpp-webserver-sendfile
```

在另一个终端验证小文件、大文件、HEAD 和 404：

```bash
curl -i http://127.0.0.1:18080/small.txt
curl -o /tmp/large.out -w "%{http_code} %{size_download}\n" http://127.0.0.1:18080/large.bin
curl -I http://127.0.0.1:18080/large.bin
curl -i -H "Range: bytes=0-3" http://127.0.0.1:18080/small.txt
curl -I -H "Range: bytes=0-3" http://127.0.0.1:18080/small.txt
curl -i http://127.0.0.1:18080/missing.txt
```

## AddressSanitizer 使用方式

项目提供 `WEBSERVER_ENABLE_ASAN` CMake 选项，可在 GCC/Clang 下开启 AddressSanitizer：

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DWEBSERVER_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

也可以在 ASan 构建下启动服务端进行手工访问或压测：

```bash
./build-asan/WebServer
```

## wrk 性能压测方式

先使用 Release 构建并启动服务端：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/WebServer
```

在另一个终端执行 wrk：

```bash
wrk -t4 -c100 -d30s http://127.0.0.1:8080/
wrk -t8 -c1000 -d30s http://127.0.0.1:8080/
```

建议记录以下指标，并结合硬件环境、系统参数和构建类型说明测试条件：

- 测试机器 CPU、内存、操作系统和内核版本
- 构建类型，例如 Release 或 Debug
- 并发连接数、线程数、测试时长
- QPS、平均延迟、P99 延迟和错误率

注意：README 不预置虚构性能数据，实际 QPS 和延迟应以本机或目标服务器测试结果为准。

## 后续优化方向

- 支持请求 body、POST/PUT 等更多 HTTP 方法
- 支持多段 Range 请求和更细致的大文件传输控制
- 引入更完整的日志级别、访问日志和错误日志
- 增加配置热加载或命令行参数覆盖
- 增加连接数、请求数、错误数、队列长度等运行时指标
- 增强 HTTP 协议兼容性，如更多状态码、Header 处理和长连接流水线策略
- 增加集成测试和端到端压测脚本
- 在高并发场景下进一步优化缓冲区管理和文件读取策略

## 简历描述参考

可以根据实际掌握程度选择以下表述：

- 基于 C++17 实现 Linux 静态 HTTP 服务器，采用 `epoll` + 非阻塞 I/O 构建事件驱动网络模型，支持多连接并发处理。
- 设计 Reactor + 线程池架构，主线程负责连接管理和 I/O 事件分发，工作线程负责静态文件响应构建，并通过 `eventfd` 将完成事件通知回事件循环。
- 实现 HTTP/1.0、HTTP/1.1 请求解析，支持 `GET`、`HEAD`、Keep-Alive、单段 Range、空闲连接超时和常见 MIME 类型响应。
- 实现静态文件路径安全检查，防护路径穿越、反斜杠、空字节和非法 URL 编码等异常输入。
- 使用 CMake 管理构建，编写单元测试覆盖 HTTP 解析、响应生成、配置解析、静态文件处理和线程池行为，并支持 AddressSanitizer 辅助定位内存问题。
