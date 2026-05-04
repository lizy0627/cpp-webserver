#include "Server.h"

// 程序入口只负责创建服务器对象并启动服务。
int main() {
    Server server(8080);              // 创建监听 8080 端口的服务器对象。
    return server.start() ? 0 : 1;    // 启动服务器，启动成功返回 0，失败返回 1。
}
