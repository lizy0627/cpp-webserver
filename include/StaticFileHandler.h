#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "FileCache.h"

struct StaticFileResult {
    enum class Status {
        Ok,
        Forbidden,
        NotFound,
        BadRequest,
        NotModified,
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
    bool dynamicBody = false;
    std::string etag;
    bool gzipEncoded = false;
};

class StaticFileHandler {
public:
    explicit StaticFileHandler(
        std::string rootDirectory = "www",
        bool enableDirectoryListing = true,
        std::uintmax_t maxCacheableFileSize = 64 * 1024,
        std::uintmax_t maxCacheCapacity = 16 * 1024 * 1024);

    StaticFileResult handle(
        const std::string& requestPath,
        const std::optional<std::string>& rangeHeader = std::nullopt,
        const std::optional<std::string>& ifNoneMatchHeader = std::nullopt,
        const std::optional<std::string>& acceptEncodingHeader = std::nullopt) const;

private:
    struct ParsedRange {
        std::uintmax_t start = 0;
        std::uintmax_t end = 0;
    };

    static std::string contentTypeForPath(const std::filesystem::path& path);
    static std::string etagForFile(
        const std::filesystem::path& path,
        std::uintmax_t fileSize,
        const std::filesystem::file_time_type& lastModified);
    static bool ifNoneMatchMatches(const std::string& ifNoneMatchHeader, const std::string& etag);
    static bool parseRangeHeader(
        const std::string& rangeHeader,
        std::uintmax_t fileSize,
        ParsedRange& range);
    StaticFileResult buildDirectoryListing(
        const std::filesystem::path& directoryPath,
        const std::string& requestPath) const;
    StaticFileResult::Status resolveRequestPath(
        const std::string& requestPath,
        std::filesystem::path& filePath,
        bool& directoryListing) const;

    std::filesystem::path rootDirectory_;
    bool rootDirectoryReady_;
    bool enableDirectoryListing_;
    mutable FileCache fileCache_;
};
