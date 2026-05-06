#include "StaticFileHandler.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace {
std::string stripQueryAndFragment(const std::string& path) {
    const std::size_t end = path.find_first_of("?#");
    return end == std::string::npos ? path : path.substr(0, end);
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

bool containsNullByte(const std::string& value) {
    return value.find('\0') != std::string::npos;
}

bool containsBackslash(const std::string& value) {
    return value.find('\\') != std::string::npos;
}

bool hasParentDirectorySegment(const std::filesystem::path& path) {
    return std::any_of(path.begin(), path.end(), [](const std::filesystem::path& segment) {
        return segment == "..";
    });
}

bool isInsideRoot(const std::filesystem::path& rootDirectory, const std::filesystem::path& filePath) {
    const std::filesystem::path relative = filePath.lexically_relative(rootDirectory);
    if (relative.empty()) {
        return filePath == rootDirectory;
    }

    const auto begin = relative.begin();
    return begin == relative.end() || *begin != "..";
}

std::string extensionLower(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}

bool readFile(const std::filesystem::path& filePath, std::string& body) {
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
    : rootDirectoryReady_(false) {
    std::error_code error;
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(std::move(rootDirectory), error);
    if (error) {
        return;
    }

    rootDirectory_ = std::filesystem::weakly_canonical(absoluteRoot, error);
    rootDirectoryReady_ = !error;
}

StaticFileResult StaticFileHandler::handle(const std::string& requestPath) const {
    std::filesystem::path filePath;
    const StaticFileResult::Status resolvedStatus = resolveRequestPath(requestPath, filePath);
    if (resolvedStatus == StaticFileResult::Status::BadRequest) {
        return {resolvedStatus, "text/plain; charset=utf-8", "Bad Request"};
    }

    if (resolvedStatus == StaticFileResult::Status::Forbidden) {
        return {resolvedStatus, "text/plain; charset=utf-8", "Forbidden"};
    }

    if (resolvedStatus == StaticFileResult::Status::NotFound) {
        return {resolvedStatus, "text/plain; charset=utf-8", "Not Found"};
    }

    if (resolvedStatus == StaticFileResult::Status::Error) {
        return {resolvedStatus, "text/plain; charset=utf-8", "Internal Server Error"};
    }

    std::string body;
    if (!readFile(filePath, body)) {
        return {StaticFileResult::Status::Error, "text/plain; charset=utf-8", "Internal Server Error"};
    }

    return {StaticFileResult::Status::Ok, contentTypeForPath(filePath), body};
}

std::string StaticFileHandler::contentTypeForPath(const std::filesystem::path& path) {
    const std::string extension = extensionLower(path);

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

    if (extension == ".png") {
        return "image/png";
    }

    if (extension == ".jpg" || extension == ".jpeg") {
        return "image/jpeg";
    }

    if (extension == ".gif") {
        return "image/gif";
    }

    if (extension == ".svg") {
        return "image/svg+xml";
    }

    if (extension == ".ico") {
        return "image/x-icon";
    }

    if (extension == ".json") {
        return "application/json";
    }

    if (extension == ".pdf") {
        return "application/pdf";
    }

    if (extension == ".wasm") {
        return "application/wasm";
    }

    return "application/octet-stream";
}

StaticFileResult::Status StaticFileHandler::resolveRequestPath(
    const std::string& requestPath,
    std::filesystem::path& filePath) const {
    if (!rootDirectoryReady_) {
        return StaticFileResult::Status::Error;
    }

    const std::string cleanPath = stripQueryAndFragment(requestPath);
    if (cleanPath.empty() || cleanPath.front() != '/' ||
        containsBackslash(cleanPath) ||
        containsNullByte(cleanPath)) {
        return StaticFileResult::Status::BadRequest;
    }

    std::string decodedPath;
    if (!urlDecode(cleanPath, decodedPath) ||
        containsBackslash(decodedPath) ||
        containsNullByte(decodedPath)) {
        return StaticFileResult::Status::BadRequest;
    }

    std::filesystem::path relativePath = decodedPath == "/"
        ? std::filesystem::path("index.html")
        : std::filesystem::path(decodedPath.substr(1));

    if (relativePath.is_absolute() ||
        relativePath.has_root_name() ||
        relativePath.has_root_directory() ||
        hasParentDirectorySegment(relativePath)) {
        return StaticFileResult::Status::BadRequest;
    }

    std::error_code error;
    std::filesystem::path resolvedPath = std::filesystem::weakly_canonical(rootDirectory_ / relativePath, error);
    if (error) {
        return StaticFileResult::Status::Error;
    }

    if (!isInsideRoot(rootDirectory_, resolvedPath)) {
        return StaticFileResult::Status::Forbidden;
    }

    if (!std::filesystem::exists(resolvedPath, error)) {
        return error ? StaticFileResult::Status::Error : StaticFileResult::Status::NotFound;
    }

    if (std::filesystem::is_directory(resolvedPath, error)) {
        if (error) {
            return StaticFileResult::Status::Error;
        }

        resolvedPath = std::filesystem::weakly_canonical(resolvedPath / "index.html", error);
        if (error) {
            return StaticFileResult::Status::Error;
        }

        if (!isInsideRoot(rootDirectory_, resolvedPath)) {
            return StaticFileResult::Status::Forbidden;
        }

        if (!std::filesystem::exists(resolvedPath, error)) {
            return error ? StaticFileResult::Status::Error : StaticFileResult::Status::Forbidden;
        }
    }

    if (!std::filesystem::is_regular_file(resolvedPath, error)) {
        if (error) {
            return StaticFileResult::Status::Error;
        }
        return StaticFileResult::Status::Forbidden;
    }

    filePath = resolvedPath;
    return StaticFileResult::Status::Ok;
}
