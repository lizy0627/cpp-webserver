#include "CommandLineOptions.h"
#include "Config.h"
#include "Server.h"

#include <iostream>

int main(int argc, char* argv[]) {
    CommandLineOptions options;
    std::string errorMessage;
    if (!parseCommandLineOptions(argc, argv, options, errorMessage)) {
        std::cerr << "error: " << errorMessage << '\n';
        std::cerr << "Run with --help to see available options.\n";
        return 1;
    }

    if (options.showHelp) {
        std::cout << commandLineHelp(argc > 0 && argv[0] != nullptr ? argv[0] : "WebServer");
        return 0;
    }

    Config config(options.configPath);
    applyCommandLineOverridesToConfig(options, config);

    Server server(config.port(), config.threadNum(), config.root(), config.connectionIdleTimeout());
    return server.start() ? 0 : 1;
}
