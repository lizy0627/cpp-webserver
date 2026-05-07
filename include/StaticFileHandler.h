#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

struct StaticFileResult {
    enum class Status {
        Ok,
        Forbidden,
        NotFound,
        BadRequest,
        RangeNotSatisfiable,
        Error
    };

    Status status;
    std::string contentType;
    std::string body;
    std::filesystem::path filePath;
    std::uintmax_t fileSize = 0;
    bool partialContent = false;
    std::uintmax_t contentOffset = 0;
    std::uintmax_t contentLength = 0;
};

class StaticFileHandler {
public:
    explicit StaticFileHandler(std::string rootDirectory = "www");

    StaticFileResult handle(
        const std::string& requestPath,
        const std::optional<std::string>& rangeHeader = std::nullopt) const;

private:
    struct ParsedRange {
        std::uintmax_t start = 0;
        std::uintmax_t end = 0;
    };

    static std::string contentTypeForPath(const std::filesystem::path& path);
    static bool parseRangeHeader(
        const std::string& rangeHeader,
        std::uintmax_t fileSize,
        ParsedRange& range);
    StaticFileResult::Status resolveRequestPath(
        const std::string& requestPath,
        std::filesystem::path& filePath) const;

    std::filesystem::path rootDirectory_;
    bool rootDirectoryReady_;
};
