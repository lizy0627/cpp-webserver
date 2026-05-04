#include "StaticFileHandler.h"

#include <fstream>
#include <sstream>
#include <utility>

namespace {
std::string stripQueryAndFragment(const std::string& path) {
    const std::size_t end = path.find_first_of("?#");
    return end == std::string::npos ? path : path.substr(0, end);
}

bool readFile(const std::string& filePath, std::string& body) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        return false;
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    body = stream.str();
    return true;
}
}

StaticFileHandler::StaticFileHandler(std::string rootDirectory)
    : rootDirectory_(std::move(rootDirectory)) {}

StaticFileResult StaticFileHandler::handle(const std::string& requestPath) const {
    std::string relativePath;
    if (!buildSafeRelativePath(requestPath, relativePath)) {
        return {StaticFileResult::Status::BadRequest, "text/plain; charset=utf-8", "Bad Request"};
    }

    const std::string filePath = rootDirectory_ + "/" + relativePath;
    std::string body;
    if (!readFile(filePath, body)) {
        return {StaticFileResult::Status::NotFound, "text/plain; charset=utf-8", "Not Found"};
    }

    return {StaticFileResult::Status::Ok, contentTypeForPath(relativePath), body};
}

std::string StaticFileHandler::contentTypeForPath(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    const std::string extension = dot == std::string::npos ? "" : path.substr(dot);

    if (extension == ".html") {
        return "text/html; charset=utf-8";
    }

    if (extension == ".css") {
        return "text/css; charset=utf-8";
    }

    if (extension == ".js") {
        return "application/javascript; charset=utf-8";
    }

    if (extension == ".txt") {
        return "text/plain; charset=utf-8";
    }

    return "application/octet-stream";
}

bool StaticFileHandler::buildSafeRelativePath(const std::string& requestPath, std::string& relativePath) {
    const std::string cleanPath = stripQueryAndFragment(requestPath);
    if (cleanPath.empty() || cleanPath.front() != '/' ||
        cleanPath.find('\\') != std::string::npos ||
        cleanPath.find('\0') != std::string::npos) {
        return false;
    }

    if (cleanPath == "/") {
        relativePath = "index.html";
        return true;
    }

    relativePath.clear();
    std::size_t start = 1;
    while (start <= cleanPath.size()) {
        const std::size_t slash = cleanPath.find('/', start);
        const std::string segment = cleanPath.substr(start, slash - start);

        if (segment == "..") {
            return false;
        }

        if (!segment.empty() && segment != ".") {
            if (!relativePath.empty()) {
                relativePath += '/';
            }
            relativePath += segment;
        }

        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }

    return !relativePath.empty();
}
