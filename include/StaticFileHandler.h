#pragma once

#include <filesystem>
#include <string>

struct StaticFileResult {
    enum class Status {
        Ok,
        Forbidden,
        NotFound,
        BadRequest,
        Error
    };

    Status status;
    std::string contentType;
    std::string body;
};

class StaticFileHandler {
public:
    explicit StaticFileHandler(std::string rootDirectory = "www");

    StaticFileResult handle(const std::string& requestPath) const;

private:
    static std::string contentTypeForPath(const std::filesystem::path& path);
    StaticFileResult::Status resolveRequestPath(
        const std::string& requestPath,
        std::filesystem::path& filePath) const;

    std::filesystem::path rootDirectory_;
    bool rootDirectoryReady_;
};
