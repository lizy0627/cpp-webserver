#include "Config.h"
#include "Server.h"

int main() {
    Config config;
    Server server(config.port(), config.threadNum(), config.root(), config.connectionIdleTimeout());
    return server.start() ? 0 : 1;
}
