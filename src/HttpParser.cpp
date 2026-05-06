#include "HttpParser.h"

#include <cctype>
#include <sstream>

namespace {
constexpr std::size_t maxRequestLineLength = 8192;
constexpr std::size_t maxHeaderLength = 8192;

std::string removeTrailingCarriageReturn(std::string value) {
    if (!value.empty() && value.back() == '\r') {
        value.pop_back();
    }

    return value;
}

bool isTokenCharacter(char value) {
    const unsigned char character = static_cast<unsigned char>(value);
    if (std::isalnum(character)) {
        return true;
    }

    switch (value) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
        return true;
    default:
        return false;
    }
}

bool isValidToken(const std::string& value) {
    if (value.empty()) {
        return false;
    }

    for (char character : value) {
        if (!isTokenCharacter(character)) {
            return false;
        }
    }

    return true;
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }

    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }

    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }

    return -1;
}

bool urlDecode(const std::string& encoded, std::string& decoded) {
    decoded.clear();
    decoded.reserve(encoded.size());

    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] != '%') {
            decoded.push_back(encoded[i]);
            continue;
        }

        if (i + 2 >= encoded.size()) {
            return false;
        }

        const int high = hexValue(encoded[i + 1]);
        const int low = hexValue(encoded[i + 2]);
        if (high == -1 || low == -1) {
            return false;
        }

        decoded.push_back(static_cast<char>((high << 4) | low));
        i += 2;
    }

    return true;
}

std::string toLower(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
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

    if (line.size() > maxRequestLineLength) {
        return false;
    }

    line = removeTrailingCarriageReturn(line);

    const std::size_t firstSpace = line.find(' ');
    if (firstSpace == std::string::npos || firstSpace == 0) {
        return false;
    }

    const std::size_t secondSpace = line.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos || secondSpace == firstSpace + 1) {
        return false;
    }

    if (line.find(' ', secondSpace + 1) != std::string::npos) {
        return false;
    }

    const std::string method = line.substr(0, firstSpace);
    const std::string encodedPath = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    const std::string version = line.substr(secondSpace + 1);

    if (!isValidToken(method)) {
        return false;
    }

    if (encodedPath.empty() || encodedPath.front() != '/') {
        return false;
    }

    if (version != "HTTP/1.0" && version != "HTTP/1.1") {
        return false;
    }

    std::string decodedPath;
    if (!urlDecode(encodedPath, decodedPath)) {
        return false;
    }

    HttpRequest parsedRequest;
    parsedRequest.method = method;
    parsedRequest.path = decodedPath;
    parsedRequest.version = version;

    std::size_t headerLength = 0;
    while (std::getline(stream, line)) {
        headerLength += line.size() + 1;
        if (headerLength > maxHeaderLength) {
            return false;
        }

        line = removeTrailingCarriageReturn(line);
        if (line.empty()) {
            break;
        }

        const std::size_t separator = line.find(':');
        if (separator == std::string::npos || separator == 0) {
            return false;
        }

        const std::string name = line.substr(0, separator);
        if (!isValidToken(name)) {
            return false;
        }

        parsedRequest.headers[toLower(name)] = trim(line.substr(separator + 1));
    }

    request = parsedRequest;
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
