#pragma once

#include "HttpRequest.h"

#include <string>

class HttpParser {
public:
    static bool parse(const std::string& rawRequest, HttpRequest& request);

private:
    static std::string trim(const std::string& value);
};
