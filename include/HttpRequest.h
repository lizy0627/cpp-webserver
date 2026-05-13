#pragma once

#include <cstddef>
#include <map>
#include <string>

class HttpRequest {
public:
    std::string method;
    std::string path;
    std::string version;
    std::map<std::string, std::string> headers;
    bool hasContentLength = false;
    std::size_t contentLength = 0;
    std::string body;
    bool keepAlive = false;
};
