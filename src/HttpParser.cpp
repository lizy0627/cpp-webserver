#include "HttpParser.h"

#include <sstream>

namespace {
std::string removeTrailingCarriageReturn(std::string value) {
    if (!value.empty() && value.back() == '\r') {
        value.pop_back();
    }

    return value;
}
}

bool HttpParser::parse(const std::string& rawRequest, HttpRequest& request) {
    std::istringstream stream(rawRequest);
    std::string line;

    if (!std::getline(stream, line)) {
        return false;
    }

    line = removeTrailingCarriageReturn(line);
    std::istringstream requestLine(line);
    std::string extra;

    if (!(requestLine >> request.method >> request.path >> request.version) || (requestLine >> extra)) {
        return false;
    }

    if (request.method.empty() || request.path.empty() || request.version.empty()) {
        return false;
    }

    if (request.version.rfind("HTTP/", 0) != 0) {
        return false;
    }

    // This basic parser intentionally stops at the empty line before the body.
    request.headers.clear();
    while (std::getline(stream, line)) {
        line = removeTrailingCarriageReturn(line);
        if (line.empty()) {
            break;
        }

        const std::size_t separator = line.find(':');
        if (separator == std::string::npos || separator == 0) {
            return false;
        }

        const std::string name = trim(line.substr(0, separator));
        const std::string value = trim(line.substr(separator + 1));
        if (name.empty()) {
            return false;
        }

        request.headers[name] = value;
    }

    return true;
}

std::string HttpParser::trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }

    const std::size_t last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}
