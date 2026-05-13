#include "HttpParser.h"

#include <cctype>
#include <cstddef>
#include <limits>
#include <map>
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

std::string toLower(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    return value;
}

std::string trimLinearWhitespace(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }

    const std::size_t last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

bool hasConnectionToken(const std::map<std::string, std::string>& headers, const std::string& token) {
    const auto connection = headers.find("connection");
    if (connection == headers.end()) {
        return false;
    }

    std::size_t tokenStart = 0;
    while (tokenStart <= connection->second.size()) {
        const std::size_t tokenEnd = connection->second.find(',', tokenStart);
        const std::string currentToken = trimLinearWhitespace(connection->second.substr(
            tokenStart,
            tokenEnd == std::string::npos ? std::string::npos : tokenEnd - tokenStart));

        if (toLower(currentToken) == token) {
            return true;
        }

        if (tokenEnd == std::string::npos) {
            break;
        }

        tokenStart = tokenEnd + 1;
    }

    return false;
}

bool shouldKeepAlive(const std::string& version, const std::map<std::string, std::string>& headers) {
    const bool requestedClose = hasConnectionToken(headers, "close");
    const bool requestedKeepAlive = hasConnectionToken(headers, "keep-alive");

    if (requestedClose) {
        return false;
    }

    if (version == "HTTP/1.1") {
        return true;
    }

    return requestedKeepAlive;
}

bool parseContentLengthValue(const std::string& value, std::size_t& contentLength) {
    if (value.empty()) {
        return false;
    }

    std::size_t parsed = 0;
    for (char character : value) {
        const unsigned char unsignedCharacter = static_cast<unsigned char>(character);
        if (!std::isdigit(unsignedCharacter)) {
            return false;
        }

        const std::size_t digit = static_cast<std::size_t>(character - '0');
        if (parsed > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
            return false;
        }

        parsed = parsed * 10 + digit;
    }

    contentLength = parsed;
    return true;
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

    HttpRequest parsedRequest;
    parsedRequest.method = method;
    parsedRequest.path = encodedPath;
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

        const std::string lowerName = toLower(name);
        const std::string value = trim(line.substr(separator + 1));
        auto existingHeader = parsedRequest.headers.find(lowerName);
        if (existingHeader == parsedRequest.headers.end()) {
            parsedRequest.headers[lowerName] = value;
        } else {
            existingHeader->second += ", " + value;
        }
    }

    const auto contentLengthHeader = parsedRequest.headers.find("content-length");
    if (contentLengthHeader != parsedRequest.headers.end()) {
        std::size_t contentLength = 0;
        if (!parseContentLengthValue(contentLengthHeader->second, contentLength)) {
            return false;
        }

        parsedRequest.hasContentLength = true;
        parsedRequest.contentLength = contentLength;
    }

    parsedRequest.keepAlive = shouldKeepAlive(parsedRequest.version, parsedRequest.headers);
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
