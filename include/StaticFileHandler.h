#pragma once

#include <string>

struct StaticFileResult {
    enum class Status {
        Ok,
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
    static std::string contentTypeForPath(const std::string& path);
    static bool buildSafeRelativePath(const std::string& requestPath, std::string& relativePath);

    std::string rootDirectory_;
};
