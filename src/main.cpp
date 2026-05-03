#include "Server.h"

// 程序入口只负责创建服务器对象并启动服务。
int main() {
    Server server(8080);
    return server.start() ? 0 : 1;
}
